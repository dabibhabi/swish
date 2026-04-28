#include "Renderer.h"

#include "../../core/Window/Window.h"
#include "../../scene/Camera/Camera.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../CommandManager/CommandManager.h"
#include "../Pipeline/Device/Device.h"
#include "../Pipeline/Pipeline.h"
#include "../PostProcessManager/PostProcessManager.h"
#include "../RenderPass/RenderPass.h"
#include "../ResourceManager/ResourceManager.h"
#include "../Swapchain/Swapchain.h"
#include "../SyncObjects/SyncObjects.h"
#include "../TextureManager/TextureManager.h"
#include "../Vertex.h"
#include "../VulkanContext/VulkanContext.h"

#include <array>
#include <cstring>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Material name mapping — MaterialId → texture name in TextureManager
// ══════════════════════════════════════════════════════════════════════

static const char* kMaterialNames[MAT_COUNT] = {
    "asphalt",      // MAT_ASPHALT  = 0
    "grass",        // MAT_GRASS    = 1
    "concrete",     // MAT_CONCRETE = 2
    "metal",        // MAT_METAL    = 3
    "default",      // MAT_DEFAULT  = 4
    "rumble",       // MAT_RUMBLE   = 5
    "dirt",         // MAT_DIRT     = 6
    "tree_leaves",  // MAT_TREE     = 7
    "sign_00",      // MAT_SIGN_0   = 8
    "sign_01",      // MAT_SIGN_1   = 9
    "sign_02",      // MAT_SIGN_2   = 10
    "sign_03",      // MAT_SIGN_3   = 11
    "sign_04",      // MAT_SIGN_4   = 12
    "sign_05",      // MAT_SIGN_5   = 13
    "sign_06",      // MAT_SIGN_6   = 14
    "sign_07",      // MAT_SIGN_7   = 15
};

// ══════════════════════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ══════════════════════════════════════════════════════════════════════

Renderer::Renderer() {
    m_context        = new VulkanContext();
    m_device         = new Device();
    m_swapchain      = new Swapchain();
    m_renderPass     = new RenderPass();
    m_commandManager = new CommandManager();
    m_syncObjects    = new SyncObjects();
}

Renderer::~Renderer() {
    delete m_syncObjects;
    delete m_commandManager;
    delete m_renderPass;
    delete m_swapchain;
    delete m_device;
    delete m_context;
}

// ══════════════════════════════════════════════════════════════════════
// init() — Initialize Vulkan core subsystems.
//
// Stops BEFORE textures, scene geometry, and camera.
// Those are now handled by TextureManager, SceneManager, and the
// Scene lambda respectively. App orchestrates the full sequence.
// ══════════════════════════════════════════════════════════════════════

void Renderer::init(Window& window) {
    m_window = &window;

    // ── Core Vulkan setup ─────────────────────────────────────────
    m_context->init(m_window->getHandle());
    m_device->init(m_context->getInstance(), m_context->getSurface());

    int width, height;
    m_window->getFramebufferSize(&width, &height);
    m_swapchain->init(*m_device, m_context->getSurface(),
                      static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    createDepthResources();

    VkFormat depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    m_renderPass->init(m_device->getDevice(), m_swapchain->getImageFormat(), depthFormat);
    m_renderPass->createFramebuffers(m_device->getDevice(), m_swapchain->getImageViews(),
                                     m_depthImageView, m_swapchain->getExtent());

    createPipeline();

    QueueFamilyIndices queueFamilies =
        m_device->findQueueFamilies(m_device->getPhysicalDevice(), m_context->getSurface());
    m_commandManager->init(m_device->getDevice(), queueFamilies.graphicsFamily.value(),
                           MAX_FRAMES_IN_FLIGHT);

    m_syncObjects->init(m_device->getDevice(), MAX_FRAMES_IN_FLIGHT,
                        m_swapchain->getImageCount());

    // ── Camera UBO resources ──────────────────────────────────────
    createUniformBuffers();
    createCameraDescriptorPool();
    createCameraDescriptorSets();

    // ── Post-processing (HDR → bloom → SSAO → composite) ─────────
    m_postProcess = new PostProcessManager();
    m_postProcess->init(m_device->getDevice(), m_device->getPhysicalDevice(),
                        m_commandManager->getPool(), m_device->getGraphicsQueue(),
                        m_swapchain->getExtent(), m_swapchain->getImageFormat(),
                        m_swapchain->getImageViews());

    // Recreate scene pipeline targeting HDR render pass (not swapchain)
    destroyPipeline();
    createPipeline();
}

// ══════════════════════════════════════════════════════════════════════
// cleanup() — Destroy Renderer-owned resources in reverse order.
//
// TextureManager cleanup is called by App BEFORE this, while VkDevice
// is still valid.
// ══════════════════════════════════════════════════════════════════════

void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());

    delete m_camera;
    m_camera = nullptr;

    destroy_scene_geometry();

    // Post-processing pipeline
    if (m_postProcess) {
        m_postProcess->cleanup();
        delete m_postProcess;
        m_postProcess = nullptr;
    }

    // Material descriptor pool (if created)
    if (m_materialPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->getDevice(), m_materialPool, nullptr);
        m_materialPool = VK_NULL_HANDLE;
    }

    // Camera descriptor pool
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->getDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    destroyUniformBuffers();

    // ── Core Vulkan teardown ──────────────────────────────────────
    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    destroyPipeline();
    m_renderPass->cleanup(m_device->getDevice());
    destroyDepthResources();
    m_swapchain->cleanup(m_device->getDevice());
    m_device->cleanup();
    m_context->cleanup();
}

// ══════════════════════════════════════════════════════════════════════
// Manager registration + getters
// ══════════════════════════════════════════════════════════════════════

void Renderer::register_texture_manager(TextureManager* mgr) { m_textureManager = mgr; }
void Renderer::register_scene_manager(SceneManager* mgr)     { m_sceneManager = mgr; }
void Renderer::register_model_manager(ModelManager* mgr)     { m_modelManager = mgr; }

TextureManager* Renderer::get_texture_manager() const { return m_textureManager; }
SceneManager*   Renderer::get_scene_manager() const   { return m_sceneManager; }
ModelManager*   Renderer::get_model_manager() const   { return m_modelManager; }

// ══════════════════════════════════════════════════════════════════════
// Vulkan handle getters (for managers that need raw handles)
// ══════════════════════════════════════════════════════════════════════

VkDevice         Renderer::get_vk_device() const         { return m_device->getDevice(); }
VkPhysicalDevice Renderer::get_vk_physical_device() const { return m_device->getPhysicalDevice(); }
VkCommandPool    Renderer::get_command_pool() const      { return m_commandManager->getPool(); }
VkQueue          Renderer::get_graphics_queue() const    { return m_device->getGraphicsQueue(); }
VkExtent2D       Renderer::get_swapchain_extent() const  { return m_swapchain->getExtent(); }

// ══════════════════════════════════════════════════════════════════════
// Camera
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_camera(Camera* camera) {
    delete m_camera;
    m_camera = camera;
}

Camera* Renderer::get_camera() const { return m_camera; }

// ══════════════════════════════════════════════════════════════════════
// GPU synchronization
// ══════════════════════════════════════════════════════════════════════

void Renderer::wait_for_idle() { vkDeviceWaitIdle(m_device->getDevice()); }

// ══════════════════════════════════════════════════════════════════════
// drawFrame() — ONE FRAME OF THE RENDER LOOP
// ══════════════════════════════════════════════════════════════════════

void Renderer::drawFrame() {
    m_syncObjects->waitForFence(m_device->getDevice(), m_currentFrame);

    uint32_t imageIndex;
    VkResult result =
        vkAcquireNextImageKHR(m_device->getDevice(), m_swapchain->getSwapchain(), UINT64_MAX,
                              m_syncObjects->getImageAvailable(m_currentFrame), VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    m_syncObjects->resetFence(m_device->getDevice(), m_currentFrame);
    m_commandManager->resetBuffer(m_currentFrame);

    updateUniformBuffer(m_currentFrame);
    recordCommandBuffer(m_currentFrame, imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore          waitSemaphores[] = {m_syncObjects->getImageAvailable(m_currentFrame)};
    VkPipelineStageFlags waitStages[]     = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount         = 1;
    submitInfo.pWaitSemaphores            = waitSemaphores;
    submitInfo.pWaitDstStageMask          = waitStages;

    VkCommandBuffer commandBuffers[] = {m_commandManager->getBuffer(m_currentFrame)};
    submitInfo.commandBufferCount    = 1;
    submitInfo.pCommandBuffers       = commandBuffers;

    VkSemaphore signalSemaphores[]  = {m_syncObjects->getRenderFinished(imageIndex)};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    VK_CHECK(vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo,
                           m_syncObjects->getInFlightFence(m_currentFrame)));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain->getSwapchain()};
    presentInfo.swapchainCount  = 1;
    presentInfo.pSwapchains     = swapchains;
    presentInfo.pImageIndices   = &imageIndex;

    result = vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window->wasResized()) {
        m_window->resetResizedFlag();
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ══════════════════════════════════════════════════════════════════════
// recordCommandBuffer()
// ══════════════════════════════════════════════════════════════════════

void Renderer::recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    m_commandManager->beginRecording(frameIndex);
    VkCommandBuffer cmd = m_commandManager->getBuffer(frameIndex);

    VkExtent2D fullExtent  = m_postProcess->get_full_extent();
    VkExtent2D bloomExtent = m_postProcess->get_bloom_extent();
    VkExtent2D aoExtent    = m_postProcess->get_ao_extent();

    // Helper: set viewport + scissor for a given extent
    auto setViewport = [&](VkExtent2D ext) {
        VkViewport vp{0.0f, 0.0f, static_cast<float>(ext.width), static_cast<float>(ext.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, ext};
        vkCmdSetScissor(cmd, 0, 1, &sc);
    };

    // ═══════════════════════════════════════════════════════════════
    // Pass 1: Scene → G-Buffer (3 MRT + depth)
    // ═══════════════════════════════════════════════════════════════
    {
        std::array<VkClearValue, 4> clear{};
        clear[0].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // albedo
        clear[1].color        = {{0.5f, 0.5f, 1.0f, 0.0f}};  // normal (up = 0,0,1 encoded)
        clear[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // material
        clear[3].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_gbuffer_render_pass();
        rp.framebuffer       = m_postProcess->get_gbuffer_framebuffer(frameIndex);
        rp.renderArea.extent = fullExtent;
        rp.clearValueCount   = static_cast<uint32_t>(clear.size());
        rp.pClearValues      = clear.data();

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        setViewport(fullExtent);

        if (m_vertexBuffer != VK_NULL_HANDLE) {
            VkBuffer     vbs[] = {m_vertexBuffer};
            VkDeviceSize off[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
            vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    0, 1, &m_descriptorSets[frameIndex], 0, nullptr);

            for (const auto& dc : m_drawCalls) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                        1, 1, &m_materialSets[dc.material], 0, nullptr);

                PushConstantData pushData{};
                pushData.model = dc.model;
                pushData.color = dc.color;
                vkCmdPushConstants(cmd, m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(PushConstantData), &pushData);
                vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
            }
        }
        vkCmdEndRenderPass(cmd);
    }

    // Transition G-Buffer images → SHADER_READ_ONLY for lighting pass
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_albedo_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_normal_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_material_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_depth_image(frameIndex),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    // ═══════════════════════════════════════════════════════════════
    // Pass 2: Deferred Lighting (G-Buffer → HDR)
    // ═══════════════════════════════════════════════════════════════
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_lighting_render_pass();
        rp.framebuffer       = m_postProcess->get_lighting_framebuffer(frameIndex);
        rp.renderArea.extent = fullExtent;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_lighting_pipeline());
        setViewport(fullExtent);

        // Set 0: camera + lights UBOs (from Renderer)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_lighting_layout(),
                                0, 1, &m_descriptorSets[frameIndex], 0, nullptr);

        // Set 1: G-buffer textures (from PostProcessManager)
        VkDescriptorSet lightSet = m_postProcess->get_lighting_set(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_lighting_layout(),
                                1, 1, &lightSet, 0, nullptr);

        // Push constants: inverse view + inverse projection for position reconstruction
        struct { Mat4 invView; Mat4 invProj; } lightPC;
        lightPC.invView = glm::inverse(m_camera->get_view_matrix());
        lightPC.invProj = glm::inverse(m_camera->get_projection_matrix());
        vkCmdPushConstants(cmd, m_postProcess->get_lighting_layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, &lightPC);

        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // Transition HDR → SHADER_READ_ONLY for bloom sampling
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ═══════════════════════════════════════════════════════════════
    // Pass 2a: Bloom Extract (HDR → 1/4 res bright pixels)
    // ═══════════════════════════════════════════════════════════════
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_bloom_render_pass();
        rp.framebuffer       = m_postProcess->get_bloom_extract_framebuffer();
        rp.renderArea.extent = bloomExtent;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_bloom_extract_pipeline());
        setViewport(bloomExtent);

        VkDescriptorSet bloomExtSet = m_postProcess->get_bloom_extract_set();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_postprocess_layout(),
                                0, 1, &bloomExtSet, 0, nullptr);

        PostProcessParams pp{};
        pp.threshold       = 1.0f;
        pp.bloom_intensity = 0.3f;
        pp.exposure        = 1.0f;
        vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_extract_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ═══════════════════════════════════════════════════════════════
    // Pass 2b: Bloom Blur H
    // ═══════════════════════════════════════════════════════════════
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_bloom_render_pass();
        rp.framebuffer       = m_postProcess->get_bloom_blur_h_framebuffer();
        rp.renderArea.extent = bloomExtent;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_bloom_blur_pipeline());
        setViewport(bloomExtent);

        VkDescriptorSet blurHSet = m_postProcess->get_bloom_blur_h_set();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_postprocess_layout(),
                                0, 1, &blurHSet, 0, nullptr);

        PostProcessParams pp{};
        pp.texel_x = 1.0f / static_cast<float>(bloomExtent.width);
        pp.texel_y = 0.0f;
        vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_h_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ═══════════════════════════════════════════════════════════════
    // Pass 2c: Bloom Blur V
    // ═══════════════════════════════════════════════════════════════
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_bloom_render_pass();
        rp.framebuffer       = m_postProcess->get_bloom_blur_v_framebuffer();
        rp.renderArea.extent = bloomExtent;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_bloom_blur_pipeline());
        setViewport(bloomExtent);

        VkDescriptorSet blurVSet = m_postProcess->get_bloom_blur_v_set();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_postprocess_layout(),
                                0, 1, &blurVSet, 0, nullptr);

        PostProcessParams pp{};
        pp.texel_x = 0.0f;
        pp.texel_y = 1.0f / static_cast<float>(bloomExtent.height);
        vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_v_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ═══════════════════════════════════════════════════════════════
    // Pass 3: Composite (HDR + bloom → swapchain with ACES)
    // (SSAO disabled for initial bring-up — AO texture is white/1.0)
    // ════════════════════════════════════════════f═══════════════════
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo rp{};
        rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass        = m_postProcess->get_composite_render_pass();
        rp.framebuffer       = m_postProcess->get_composite_framebuffer(imageIndex);
        rp.renderArea.extent = fullExtent;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_composite_pipeline());
        setViewport(fullExtent);

        VkDescriptorSet compSet = m_postProcess->get_composite_set(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_postProcess->get_composite_layout(),
                                0, 1, &compSet, 0, nullptr);

        PostProcessParams pp{};
        pp.bloom_intensity = 0.3f;
        pp.exposure        = 1.0f;
        vkCmdPushConstants(cmd, m_postProcess->get_composite_layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    m_commandManager->endRecording(frameIndex);
}

// ══════════════════════════════════════════════════════════════════════
// recreateSwapchain()
// ══════════════════════════════════════════════════════════════════════

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    m_window->getFramebufferSize(&width, &height);
    while (width == 0 || height == 0) {
        m_window->getFramebufferSize(&width, &height);
        m_window->pollEvents();
    }

    vkDeviceWaitIdle(m_device->getDevice());

    m_renderPass->cleanup(m_device->getDevice());
    destroyDepthResources();
    m_swapchain->cleanup(m_device->getDevice());

    m_swapchain->init(*m_device, m_context->getSurface(),
                      static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    m_syncObjects->recreateRenderFinishedSemaphores(m_device->getDevice(),
                                                    m_swapchain->getImageCount());

    createDepthResources();

    VkFormat depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    m_renderPass->init(m_device->getDevice(), m_swapchain->getImageFormat(), depthFormat);
    m_renderPass->createFramebuffers(m_device->getDevice(), m_swapchain->getImageViews(),
                                     m_depthImageView, m_swapchain->getExtent());

    // The PostProcess chain is sized to the swapchain extent (HDR, g-buffer,
    // bloom targets, composite framebuffers). Compositors often hand back a
    // different extent on swapchain recreate (window-decoration accounting),
    // so rebuild the whole size-dependent set. Render passes are kept (they
    // depend on format only, which doesn't change here).
    if (m_postProcess) {
        m_postProcess->recreate(m_swapchain->getExtent(),
                                m_swapchain->getImageFormat(),
                                m_swapchain->getImageViews());

        // PostProcessManager::recreate destroys m_lightingLayout (via
        // destroyDescriptors) but never recreates it, since the lighting
        // layout is built here in Renderer using m_descriptorSetLayout. The
        // lighting pipeline that references the destroyed layout also
        // becomes unusable for binding new descriptor sets. Rebuild both.
        VkPipeline staleLightingPipeline = m_postProcess->get_lighting_pipeline();
        if (staleLightingPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device->getDevice(), staleLightingPipeline, nullptr);
        }

        VkPushConstantRange lightPC{};
        lightPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightPC.offset     = 0;
        lightPC.size       = 128;

        VkPipelineLayout lightLayout = Pipeline::createLayout(
            m_device->getDevice(),
            {m_descriptorSetLayout, m_postProcess->get_lighting_tex_layout()},
            {lightPC});
        m_postProcess->set_lighting_layout(lightLayout);

        PipelineConfig lightCfg{};
        lightCfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
        lightCfg.fragShaderPath   = std::string(SHADER_DIR) + "lighting.frag.spv";
        lightCfg.noVertexInput    = true;
        lightCfg.enableDepthTest  = false;
        lightCfg.enableDepthWrite = false;
        lightCfg.cullMode         = VK_CULL_MODE_NONE;
        lightCfg.pipelineLayout   = lightLayout;
        m_postProcess->set_lighting_pipeline(
            Pipeline::create(m_device->getDevice(), lightCfg,
                             m_postProcess->get_lighting_render_pass(),
                             m_swapchain->getExtent()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// DEPTH RESOURCES
// ══════════════════════════════════════════════════════════════════════

void Renderer::createDepthResources() {
    VkFormat   depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    VkExtent2D extent      = m_swapchain->getExtent();

    ResourceManager::createImage(m_device->getDevice(), m_device->getPhysicalDevice(),
                                 extent.width, extent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(m_device->getDevice(), &viewInfo, nullptr, &m_depthImageView));
}

void Renderer::destroyDepthResources() {
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device->getDevice(), m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device->getDevice(), m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device->getDevice(), m_depthMemory, nullptr);
        m_depthMemory = VK_NULL_HANDLE;
    }
}

// ══════════════════════════════════════════════════════════════════════
// PIPELINE
// ══════════════════════════════════════════════════════════════════════

void Renderer::createPipeline() {
    // Set 0: Camera UBO (binding 0) + Lights UBO (binding 1)
    std::array<VkDescriptorSetLayoutBinding, 2> set0Bindings{};
    set0Bindings[0].binding            = 0;
    set0Bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    set0Bindings[0].descriptorCount    = 1;
    set0Bindings[0].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    set0Bindings[0].pImmutableSamplers = nullptr;

    set0Bindings[1].binding            = 1;
    set0Bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    set0Bindings[1].descriptorCount    = 1;
    set0Bindings[1].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    set0Bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(set0Bindings.size());
    layoutInfo.pBindings    = set0Bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout));

    // Set 1: PBR material textures (albedo, normal, roughness)
    std::array<VkDescriptorSetLayoutBinding, 3> texBindings{};
    for (uint32_t i = 0; i < 3; i++) {
        texBindings[i].binding            = i;
        texBindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBindings[i].descriptorCount    = 1;
        texBindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
        texBindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo texLayoutInfo{};
    texLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    texLayoutInfo.bindingCount = static_cast<uint32_t>(texBindings.size());
    texLayoutInfo.pBindings    = texBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(m_device->getDevice(), &texLayoutInfo, nullptr, &m_materialSetLayout));

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(float) * 20;  // mat4 + vec4 = 80 bytes

    m_pipelineLayout = Pipeline::createLayout(m_device->getDevice(),
                                              {m_descriptorSetLayout, m_materialSetLayout},
                                              {pushConstantRange});

    auto binding    = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();

    PipelineConfig config{};
    config.cullMode       = VK_CULL_MODE_BACK_BIT;
    config.vertexBindings.push_back(binding);
    config.vertexAttributes.assign(attributes.begin(), attributes.end());
    config.pipelineLayout = m_pipelineLayout;

    if (m_postProcess) {
        // Deferred: scene pipeline targets G-buffer (3 MRT + depth)
        config.vertShaderPath       = std::string(SHADER_DIR) + "basic.vert.spv";
        config.fragShaderPath       = std::string(SHADER_DIR) + "gbuffer.frag.spv";
        config.colorAttachmentCount = 3;
        m_pipeline = Pipeline::create(m_device->getDevice(), config,
                                      m_postProcess->get_gbuffer_render_pass(),
                                      m_swapchain->getExtent());

        // Lighting pipeline: fullscreen quad reads G-buffer, outputs HDR
        VkPushConstantRange lightPC{};
        lightPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightPC.offset     = 0;
        lightPC.size       = 128;  // invView + invProj = 2 × mat4

        VkPipelineLayout lightLayout = Pipeline::createLayout(
            m_device->getDevice(),
            {m_descriptorSetLayout, m_postProcess->get_lighting_tex_layout()},
            {lightPC});
        m_postProcess->set_lighting_layout(lightLayout);

        PipelineConfig lightCfg{};
        lightCfg.vertShaderPath  = std::string(SHADER_DIR) + "fullscreen.vert.spv";
        lightCfg.fragShaderPath  = std::string(SHADER_DIR) + "lighting.frag.spv";
        lightCfg.noVertexInput   = true;
        lightCfg.enableDepthTest = false;
        lightCfg.enableDepthWrite = false;
        lightCfg.cullMode        = VK_CULL_MODE_NONE;
        lightCfg.pipelineLayout  = lightLayout;
        m_postProcess->set_lighting_pipeline(
            Pipeline::create(m_device->getDevice(), lightCfg,
                             m_postProcess->get_lighting_render_pass(),
                             m_swapchain->getExtent()));
    } else {
        // Fallback: forward rendering to swapchain
        config.vertShaderPath = std::string(SHADER_DIR) + "basic.vert.spv";
        config.fragShaderPath = std::string(SHADER_DIR) + "basic.frag.spv";
        m_pipeline = Pipeline::create(m_device->getDevice(), config,
                                      m_renderPass->getRenderPass(),
                                      m_swapchain->getExtent());
    }
}

void Renderer::destroyPipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device->getDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device->getDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->getDevice(), m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->getDevice(), m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
    }
}

// ══════════════════════════════════════════════════════════════════════
// UNIFORM BUFFERS
// ══════════════════════════════════════════════════════════════════════

void Renderer::createUniformBuffers() {
    VkDeviceSize cameraSize = sizeof(CameraUBO);
    VkDeviceSize lightsSize = sizeof(LightsUBO);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightsBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightsBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightsBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ResourceManager::createBuffer(m_device->getDevice(), m_device->getPhysicalDevice(), cameraSize,
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      m_uniformBuffers[i], m_uniformBuffersMemory[i]);
        vkMapMemory(m_device->getDevice(), m_uniformBuffersMemory[i], 0, cameraSize, 0,
                    &m_uniformBuffersMapped[i]);

        ResourceManager::createBuffer(m_device->getDevice(), m_device->getPhysicalDevice(), lightsSize,
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      m_lightsBuffers[i], m_lightsBuffersMemory[i]);
        vkMapMemory(m_device->getDevice(), m_lightsBuffersMemory[i], 0, lightsSize, 0,
                    &m_lightsBuffersMapped[i]);
    }
}

void Renderer::destroyUniformBuffers() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_uniformBuffersMapped[i] != nullptr) {
            vkUnmapMemory(m_device->getDevice(), m_uniformBuffersMemory[i]);
            m_uniformBuffersMapped[i] = nullptr;
        }
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device->getDevice(), m_uniformBuffers[i], nullptr);
            m_uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device->getDevice(), m_uniformBuffersMemory[i], nullptr);
            m_uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }

        // Lights buffers
        if (m_lightsBuffersMapped.size() > i && m_lightsBuffersMapped[i] != nullptr) {
            vkUnmapMemory(m_device->getDevice(), m_lightsBuffersMemory[i]);
            m_lightsBuffersMapped[i] = nullptr;
        }
        if (m_lightsBuffers.size() > i && m_lightsBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device->getDevice(), m_lightsBuffers[i], nullptr);
            m_lightsBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_lightsBuffersMemory.size() > i && m_lightsBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device->getDevice(), m_lightsBuffersMemory[i], nullptr);
            m_lightsBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }
}

void Renderer::set_scene_lights(const std::vector<LightDesc>& lights) {
    m_sceneLights = lights;
}

// ══════════════════════════════════════════════════════════════════════
// CAMERA DESCRIPTOR POOL + SETS (set 0 — camera UBOs only)
// ══════════════════════════════════════════════════════════════════════

void Renderer::createCameraDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;  // camera + lights UBOs

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkCreateDescriptorPool(m_device->getDevice(), &poolInfo, nullptr, &m_descriptorPool));
}

void Renderer::createCameraDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();

    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(m_device->getDevice(), &allocInfo, m_descriptorSets.data()));

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Binding 0: CameraUBO
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = m_uniformBuffers[i];
        cameraInfo.offset = 0;
        cameraInfo.range  = sizeof(CameraUBO);

        // Binding 1: LightsUBO
        VkDescriptorBufferInfo lightsInfo{};
        lightsInfo.buffer = m_lightsBuffers[i];
        lightsInfo.offset = 0;
        lightsInfo.range  = sizeof(LightsUBO);

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_descriptorSets[i];
        writes[0].dstBinding      = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &cameraInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_descriptorSets[i];
        writes[1].dstBinding      = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &lightsInfo;

        vkUpdateDescriptorSets(m_device->getDevice(), static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

// ══════════════════════════════════════════════════════════════════════
// rebuild_material_descriptors() — Create material descriptor pool +
// sets using textures from TextureManager (set 1).
//
// Called by App after TextureManager loads, and on scene switch.
// ══════════════════════════════════════════════════════════════════════

void Renderer::rebuild_material_descriptors() {
    VkDevice device = m_device->getDevice();

    // Destroy old pool if it exists
    if (m_materialPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_materialPool, nullptr);
        m_materialPool = VK_NULL_HANDLE;
    }
    m_materialSets.clear();

    // Create pool for MAT_COUNT × 3 sampler descriptors (albedo + normal + roughness)
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAT_COUNT * 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAT_COUNT;

    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_materialPool));

    // Allocate MAT_COUNT descriptor sets
    std::vector<VkDescriptorSetLayout> matLayouts(MAT_COUNT, m_materialSetLayout);

    VkDescriptorSetAllocateInfo matAllocInfo{};
    matAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    matAllocInfo.descriptorPool     = m_materialPool;
    matAllocInfo.descriptorSetCount = MAT_COUNT;
    matAllocInfo.pSetLayouts        = matLayouts.data();

    m_materialSets.resize(MAT_COUNT);
    VK_CHECK(vkAllocateDescriptorSets(device, &matAllocInfo, m_materialSets.data()));

    // Texture name suffixes for PBR: binding 0 = albedo, 1 = normal, 2 = roughness
    static const char* kSuffixes[3] = {"", "_normal", "_roughness"};

    for (uint32_t i = 0; i < MAT_COUNT; i++) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        std::array<VkWriteDescriptorSet, 3>  writes{};

        for (uint32_t b = 0; b < 3; b++) {
            std::string texName = std::string(kMaterialNames[i]) + kSuffixes[b];
            Texture* tex = m_textureManager->get_texture(texName);

            // Fallback: if normal/roughness map missing, use the "default" 1x1 white texture
            if (!tex) {
                tex = m_textureManager->get_texture("default");
            }
            if (!tex) {
                throw std::runtime_error("rebuild_material_descriptors: missing texture '" + texName + "'");
            }

            imageInfos[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[b].imageView   = tex->view;
            imageInfos[b].sampler     = m_textureManager->get_sampler();

            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_materialSets[i];
            writes[b].dstBinding      = b;
            writes[b].dstArrayElement = 0;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &imageInfos[b];
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

// ══════════════════════════════════════════════════════════════════════
// updateUniformBuffer()
// ══════════════════════════════════════════════════════════════════════

void Renderer::updateUniformBuffer(uint32_t frameIndex) {
    // Camera UBO (set 0, binding 0)
    CameraUBO ubo{};
    ubo.view     = m_camera->get_view_matrix();
    ubo.proj     = m_camera->get_projection_matrix();
    ubo.camPos   = Vec4(m_camera->get_position(), 1.0f);
    ubo.sunDir   = Vec4(glm::normalize(Vec3(0.3f, 0.6f, 0.15f)), 1.0f);
    ubo.sunColor = Vec4(1.0f, 0.95f, 0.85f, 0.30f);
    memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));

    // Lights UBO (set 0, binding 1)
    LightsUBO lightsUbo{};
    uint32_t count = std::min(static_cast<uint32_t>(m_sceneLights.size()), MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < count; i++) {
        lightsUbo.pointLights[i].positionRadius = Vec4(m_sceneLights[i].position, m_sceneLights[i].radius);
        lightsUbo.pointLights[i].colorIntensity = Vec4(m_sceneLights[i].color, m_sceneLights[i].intensity);
    }
    lightsUbo.numPointLights = glm::uvec4(count, 0, 0, 0);
    memcpy(m_lightsBuffersMapped[frameIndex], &lightsUbo, sizeof(lightsUbo));
}

// ══════════════════════════════════════════════════════════════════════
// upload_scene_geometry() — Upload mesh data to GPU via staging buffers.
// Called by Scene lambdas after generating geometry.
// ══════════════════════════════════════════════════════════════════════

void Renderer::upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_drawCalls = draws;

    if (mesh.empty()) return;

    VkDevice         device         = m_device->getDevice();
    VkPhysicalDevice physicalDevice = m_device->getPhysicalDevice();
    VkCommandPool    commandPool    = m_commandManager->getPool();
    VkQueue          graphicsQueue  = m_device->getGraphicsQueue();

    // ── Vertex data ───────────────────────────────────────────────
    {
        VkDeviceSize bufferSize = sizeof(Vertex) * mesh.getVertices().size();

        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingMemory;
        ResourceManager::createBuffer(device, physicalDevice, bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mesh.getVertices().data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(device, stagingMemory);

        ResourceManager::createBuffer(device, physicalDevice, bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      m_vertexBuffer, m_vertexBufferMemory);

        ResourceManager::copyBuffer(device, commandPool, graphicsQueue,
                                    stagingBuffer, m_vertexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
    }

    // ── Index data ────────────────────────────────────────────────
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * mesh.getIndices().size();

        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingMemory;
        ResourceManager::createBuffer(device, physicalDevice, bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      stagingBuffer, stagingMemory);

        void* data;
        vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
        memcpy(data, mesh.getIndices().data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(device, stagingMemory);

        ResourceManager::createBuffer(device, physicalDevice, bufferSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      m_indexBuffer, m_indexBufferMemory);

        ResourceManager::copyBuffer(device, commandPool, graphicsQueue,
                                    stagingBuffer, m_indexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
    }
}

// ══════════════════════════════════════════════════════════════════════
// destroy_scene_geometry()
// ══════════════════════════════════════════════════════════════════════

void Renderer::destroy_scene_geometry() {
    m_drawCalls.clear();
    m_sceneLights.clear();

    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device->getDevice(), m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device->getDevice(), m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device->getDevice(), m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device->getDevice(), m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}

}  // namespace swish
