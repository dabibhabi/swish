#include "Renderer.h"

#include "../../core/Window/Window.h"
#include "../../scene/Camera/Camera.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../CommandManager/CommandManager.h"
#include "../Device/Device.h"
#include "../Pipeline/Pipeline.h"
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

    m_syncObjects->init(m_device->getDevice(), MAX_FRAMES_IN_FLIGHT);

    // ── Camera UBO resources ──────────────────────────────────────
    createUniformBuffers();
    createCameraDescriptorPool();
    createCameraDescriptorSets();
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

    VkSemaphore signalSemaphores[]  = {m_syncObjects->getRenderFinished(m_currentFrame)};
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
        m_syncObjects->recreateRenderFinishedSemaphore(m_device->getDevice(), m_currentFrame);
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
    VkCommandBuffer commandBuffer = m_commandManager->getBuffer(frameIndex);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = m_renderPass->getRenderPass();
    renderPassInfo.framebuffer       = m_renderPass->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{0.53f, 0.68f, 0.85f, 1.0f}};  // LIE clear sky blue
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapchain->getExtent().width);
    viewport.height   = static_cast<float>(m_swapchain->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchain->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (m_vertexBuffer != VK_NULL_HANDLE) {
        VkBuffer     vertexBuffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[]       = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_descriptorSets[frameIndex], 0, nullptr);

        for (const auto& dc : m_drawCalls) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    1, 1, &m_materialSets[dc.material], 0, nullptr);

            PushConstantData pushData{};
            pushData.model = dc.model;
            pushData.color = dc.color;

            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstantData), &pushData);

            vkCmdDrawIndexed(commandBuffer, dc.indexCount, 1, dc.indexOffset, 0, 0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);
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
    createDepthResources();

    VkFormat depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    m_renderPass->init(m_device->getDevice(), m_swapchain->getImageFormat(), depthFormat);
    m_renderPass->createFramebuffers(m_device->getDevice(), m_swapchain->getImageViews(),
                                     m_depthImageView, m_swapchain->getExtent());
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
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding            = 0;
    uboBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount    = 1;
    uboBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &uboBinding;

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
    config.vertShaderPath = std::string(SHADER_DIR) + "basic.vert.spv";
    config.fragShaderPath = std::string(SHADER_DIR) + "basic.frag.spv";
    config.vertexBindings.push_back(binding);
    config.vertexAttributes.assign(attributes.begin(), attributes.end());
    config.pipelineLayout = m_pipelineLayout;

    m_pipeline = Pipeline::create(m_device->getDevice(), config,
                                  m_renderPass->getRenderPass(), m_swapchain->getExtent());
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
    VkDeviceSize bufferSize = sizeof(CameraUBO);

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ResourceManager::createBuffer(m_device->getDevice(), m_device->getPhysicalDevice(), bufferSize,
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      m_uniformBuffers[i], m_uniformBuffersMemory[i]);

        vkMapMemory(m_device->getDevice(), m_uniformBuffersMemory[i], 0, bufferSize, 0,
                    &m_uniformBuffersMapped[i]);
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
    }
}

// ══════════════════════════════════════════════════════════════════════
// CAMERA DESCRIPTOR POOL + SETS (set 0 — camera UBOs only)
// ══════════════════════════════════════════════════════════════════════

void Renderer::createCameraDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

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
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range  = sizeof(CameraUBO);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet          = m_descriptorSets[i];
        descriptorWrite.dstBinding      = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo     = &bufferInfo;

        vkUpdateDescriptorSets(m_device->getDevice(), 1, &descriptorWrite, 0, nullptr);
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
    CameraUBO ubo{};
    ubo.view     = m_camera->get_view_matrix();
    ubo.proj     = m_camera->get_projection_matrix();
    ubo.camPos   = Vec4(m_camera->get_position(), 1.0f);
    ubo.sunDir   = Vec4(glm::normalize(Vec3(0.3f, 0.6f, 0.15f)), 1.0f);  // autumn afternoon
    ubo.sunColor = Vec4(1.0f, 0.95f, 0.85f, 0.30f);  // warm white, a = ambient strength
    memcpy(m_uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
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
