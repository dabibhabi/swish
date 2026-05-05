#include "Renderer.h"

#include "../../core/Window/Window.h"
#include "../../scene/Camera/Camera.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../CameraUniforms/CameraUniforms.h"
#include "../CommandManager/CommandManager.h"
#include "../MaterialDescriptors/MaterialDescriptors.h"
#include "../Pipeline/Device/Device.h"
#include "../PostProcessManager/PostProcessManager.h"
#include "../ResourceManager/ResourceManager.h"
#include "../Swapchain/Swapchain.h"
#include "../SyncObjects/SyncObjects.h"
#include "../TextureManager/TextureManager.h"
#include "../VulkanContext/VulkanContext.h"

#include <array>
#include <optional>

namespace swish {

namespace {

void setViewportAndScissor(VkCommandBuffer cmd, VkExtent2D ext) {
    VkViewport vp{0.0f, 0.0f,
                  static_cast<float>(ext.width), static_cast<float>(ext.height),
                  0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

struct ScopedRenderPass {
    VkCommandBuffer cmd;

    ScopedRenderPass(VkCommandBuffer c, VkRenderPass rp, VkFramebuffer fb,
                     VkExtent2D extent, const VkClearValue* clears, uint32_t clearCount)
        : cmd(c) {
        auto info = vk::makeRenderPassBeginInfo();
        info.renderPass        = rp;
        info.framebuffer       = fb;
        info.renderArea.extent = extent;
        info.clearValueCount   = clearCount;
        info.pClearValues      = clears;
        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Convenience overload for the common single-clear case.
    ScopedRenderPass(VkCommandBuffer c, VkRenderPass rp, VkFramebuffer fb,
                     VkExtent2D extent, const VkClearValue& clear)
        : ScopedRenderPass(c, rp, fb, extent, &clear, 1) {}

    ~ScopedRenderPass() { vkCmdEndRenderPass(cmd); }

    ScopedRenderPass(const ScopedRenderPass&)            = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;
};

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ══════════════════════════════════════════════════════════════════════

Renderer::Renderer()
    : m_cameraUniforms(std::make_unique<CameraUniforms>()),
      m_materialDescriptors(std::make_unique<MaterialDescriptors>()) {
    m_context        = new VulkanContext();
    m_device         = new Device();
    m_swapchain      = new Swapchain();
    m_commandManager = new CommandManager();
    m_syncObjects    = new SyncObjects();
}

Renderer::~Renderer() {
    delete m_syncObjects;
    delete m_commandManager;
    delete m_swapchain;
    delete m_device;
    delete m_context;
}

void Renderer::init(Window& window) {
    m_window = &window;

    // ── Core Vulkan setup ─────────────────────────────────────────
    m_context->init(m_window->getHandle());
    m_device->init(m_context->getInstance(), m_context->getSurface());

    int width, height;
    m_window->getFramebufferSize(&width, &height);
    m_swapchain->init(*m_device, m_context->getSurface(),
                      static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    m_cameraUniforms->init(m_device->getDevice(), m_device->getPhysicalDevice(),
                           MAX_FRAMES_IN_FLIGHT);
    m_materialDescriptors->init(m_device->getDevice());

    QueueFamilyIndices queueFamilies =
        m_device->findQueueFamilies(m_device->getPhysicalDevice(), m_context->getSurface());
    m_commandManager->init(m_device->getDevice(), queueFamilies.graphicsFamily.value(),
                           MAX_FRAMES_IN_FLIGHT);

    m_syncObjects->init(m_device->getDevice(), MAX_FRAMES_IN_FLIGHT,
                        m_swapchain->getImageCount());


    m_postProcess = new PostProcessManager();
    m_postProcess->init(m_device->getDevice(), m_device->getPhysicalDevice(),
                        m_commandManager->getPool(), m_device->getGraphicsQueue(),
                        m_swapchain->getExtent(), m_swapchain->getImageFormat(),
                        m_swapchain->getImageViews());

    m_scenePipeline.init(m_device->getDevice(), {
        m_cameraUniforms->get_layout(),
        m_materialDescriptors->get_layout(),
        m_postProcess->get_gbuffer_render_pass(),
        m_swapchain->getExtent(),
    });
    m_deferredLighting.init(m_device->getDevice(), {
        m_cameraUniforms->get_layout(),
        m_postProcess->get_lighting_tex_layout(),
        m_postProcess->get_lighting_render_pass(),
        m_swapchain->getExtent(),
    });
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());

    delete m_camera;
    m_camera = nullptr;

    destroy_scene_geometry();

    if (has_post_process()) {
        m_postProcess->cleanup();
        delete m_postProcess;
        m_postProcess = nullptr;
    }

    m_materialDescriptors->cleanup(m_device->getDevice());
    m_cameraUniforms->cleanup(m_device->getDevice());

    // ── Core Vulkan teardown ──────────────────────────────────────
    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    m_deferredLighting.cleanup(m_device->getDevice());
    m_scenePipeline.cleanup(m_device->getDevice());
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

RendererServices Renderer::services() const {
    return RendererServices{
        m_device->getDevice(),
        m_device->getPhysicalDevice(),
        m_commandManager->getPool(),
        m_device->getGraphicsQueue(),
        m_swapchain->getExtent(),
    };
}

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

    // Each frame phase is a named lambda — the state machine reads as
    // (acquire) → record → (submit) → (present) → handlePresent.
    auto acquireImage = [&]() -> std::optional<uint32_t> {
        uint32_t idx;
        VkResult r = vkAcquireNextImageKHR(
            m_device->getDevice(), m_swapchain->getSwapchain(), UINT64_MAX,
            m_syncObjects->getImageAvailable(m_currentFrame), VK_NULL_HANDLE, &idx);
        if (vk::is_out_of_date(r)) { recreateSwapchain(); return std::nullopt; }
        if (!vk::is_presentable(r))
            throw std::runtime_error("failed to acquire swap chain image!");
        return idx;
    };

    auto submitFrame = [&](uint32_t imageIndex) {
        VkSemaphore          waitSems[]   = {m_syncObjects->getImageAvailable(m_currentFrame)};
        VkSemaphore          signalSems[] = {m_syncObjects->getRenderFinished(imageIndex)};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkCommandBuffer      cmdBufs[]    = {m_commandManager->getBuffer(m_currentFrame)};

        auto si = vk::makeSubmitInfo();
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = waitSems;
        si.pWaitDstStageMask    = waitStages;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = cmdBufs;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = signalSems;

        VK_CHECK(vkQueueSubmit(m_device->getGraphicsQueue(), 1, &si,
                               m_syncObjects->getInFlightFence(m_currentFrame)));
    };

    auto presentFrame = [&](uint32_t imageIndex) -> VkResult {
        VkSemaphore    waitSems[] = {m_syncObjects->getRenderFinished(imageIndex)};
        VkSwapchainKHR scs[]      = {m_swapchain->getSwapchain()};

        auto pi = vk::makePresentInfo();
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = waitSems;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = scs;
        pi.pImageIndices      = &imageIndex;
        return vkQueuePresentKHR(m_device->getPresentQueue(), &pi);
    };

    auto handlePresent = [&](VkResult r) {
        if (vk::swapchain_needs_recreation(r) || m_window->wasResized()) {
            m_window->resetResizedFlag();
            recreateSwapchain();
        } else if (!vk::is_success(r)) {
            throw std::runtime_error("failed to present swap chain image!");
        }
    };

    auto imageIndex = acquireImage();
    if (!imageIndex) return;

    m_syncObjects->resetFence(m_device->getDevice(), m_currentFrame);
    m_commandManager->resetBuffer(m_currentFrame);
    m_cameraUniforms->update(m_currentFrame, *m_camera);
    recordCommandBuffer(m_currentFrame, *imageIndex);

    submitFrame(*imageIndex);
    handlePresent(presentFrame(*imageIndex));

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// command buffer recording

void Renderer::recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    m_commandManager->beginRecording(frameIndex);
    VkCommandBuffer cmd = m_commandManager->getBuffer(frameIndex);

    VkExtent2D fullExtent  = m_postProcess->get_full_extent();
    VkExtent2D bloomExtent = m_postProcess->get_bloom_extent();

    recordGBufferPass(cmd, frameIndex, fullExtent);
    transitionGBufferForLighting(cmd, frameIndex);

    recordLightingPass(cmd, frameIndex, fullExtent);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomExtract(cmd, bloomExtent);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_extract_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomBlur(cmd, bloomExtent, /*horizontal=*/true);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_h_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomBlur(cmd, bloomExtent, /*horizontal=*/false);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_v_image(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordCompositePass(cmd, frameIndex, imageIndex, fullExtent);

    m_commandManager->endRecording(frameIndex);
}

// ── Pass 1: scene → G-buffer ─────────────────────────────────────────
void Renderer::recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent) {
    std::array<VkClearValue, 4> clear{};
    clear[0].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // albedo
    clear[1].color        = {{0.5f, 0.5f, 1.0f, 0.0f}};  // normal (up = 0,0,1 encoded)
    clear[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // material
    clear[3].depthStencil = {1.0f, 0};

    ScopedRenderPass pass(cmd,
        m_postProcess->get_gbuffer_render_pass(),
        m_postProcess->get_gbuffer_framebuffer(frameIndex),
        extent, clear.data(), static_cast<uint32_t>(clear.size()));

    m_scenePipeline.bind(cmd, extent, m_cameraUniforms->get_set(frameIndex));

    m_sceneGeometry.record_draws(cmd, m_scenePipeline, *m_materialDescriptors);
}

// ── G-buffer attachments → SHADER_READ_ONLY for the lighting pass ───
void Renderer::transitionGBufferForLighting(VkCommandBuffer cmd, uint32_t frameIndex) {
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_albedo_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_normal_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_material_image(frameIndex),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_depth_image(frameIndex),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);
}

// ── Pass 2: deferred lighting (G-buffer → HDR) ───────────────────────
void Renderer::recordLightingPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd,
        m_postProcess->get_lighting_render_pass(),
        m_postProcess->get_lighting_framebuffer(frameIndex),
        extent, clear);

    Mat4 invView = glm::inverse(m_camera->get_view_matrix());
    Mat4 invProj = glm::inverse(m_camera->get_projection_matrix());

    m_deferredLighting.bind_and_record(cmd,
        m_cameraUniforms->get_set(frameIndex),
        m_postProcess->get_lighting_set(frameIndex),
        invView, invProj, extent);
}

// ── Pass 2a: bloom extract (HDR → 1/4 res bright pixels) ─────────────
void Renderer::recordBloomExtract(VkCommandBuffer cmd, VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd,
        m_postProcess->get_bloom_render_pass(),
        m_postProcess->get_bloom_extract_framebuffer(),
        extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_postProcess->get_bloom_extract_pipeline());
    setViewportAndScissor(cmd, extent);

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
}

// ── Pass 2b/2c: separable bloom blur (one direction per call) ────────
void Renderer::recordBloomBlur(VkCommandBuffer cmd, VkExtent2D extent, bool horizontal) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkFramebuffer fb = horizontal ? m_postProcess->get_bloom_blur_h_framebuffer()
                                  : m_postProcess->get_bloom_blur_v_framebuffer();

    ScopedRenderPass pass(cmd,
        m_postProcess->get_bloom_render_pass(),
        fb, extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_postProcess->get_bloom_blur_pipeline());
    setViewportAndScissor(cmd, extent);

    VkDescriptorSet blurSet = horizontal ? m_postProcess->get_bloom_blur_h_set()
                                         : m_postProcess->get_bloom_blur_v_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_postProcess->get_postprocess_layout(),
                            0, 1, &blurSet, 0, nullptr);

    PostProcessParams pp{};
    pp.texel_x = horizontal ? 1.0f / static_cast<float>(extent.width)  : 0.0f;
    pp.texel_y = horizontal ? 0.0f                                     : 1.0f / static_cast<float>(extent.height);
    vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ── Pass 3: composite (HDR + bloom → swapchain, ACES tonemap) ────────
void Renderer::recordCompositePass(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex,
                                   VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd,
        m_postProcess->get_composite_render_pass(),
        m_postProcess->get_composite_framebuffer(imageIndex),
        extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_postProcess->get_composite_pipeline());
    setViewportAndScissor(cmd, extent);

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

    m_swapchain->cleanup(m_device->getDevice());

    m_swapchain->init(*m_device, m_context->getSurface(),
                      static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    m_syncObjects->recreateRenderFinishedSemaphores(m_device->getDevice(),
                                                    m_swapchain->getImageCount());

    if (has_post_process()) {
        m_postProcess->recreate(m_swapchain->getExtent(),
                                m_swapchain->getImageFormat(),
                                m_swapchain->getImageViews());

        m_deferredLighting.recreate(m_device->getDevice(),
                                    m_postProcess->get_lighting_render_pass(),
                                    m_swapchain->getExtent());
    }
}

// ══════════════════════════════════════════════════════════════════════
// Scene lights + material descriptors — thin delegations to subsystems.
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_scene_lights(const std::vector<LightDesc>& lights) {
    m_cameraUniforms->set_lights(lights);
}

void Renderer::rebuild_material_descriptors() {
    m_materialDescriptors->rebuild(m_device->getDevice(), *m_textureManager);
}

// ══════════════════════════════════════════════════════════════════════
// Scene geometry — thin delegations to SceneGeometry. Called by Scene
// lambdas after mesh generation / on scene unload.
// ══════════════════════════════════════════════════════════════════════

void Renderer::upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_sceneGeometry.upload(services(), mesh, draws);
}

void Renderer::destroy_scene_geometry() {
    m_sceneGeometry.cleanup(m_device->getDevice());
    m_cameraUniforms->set_lights({});
}

}  // namespace swish
