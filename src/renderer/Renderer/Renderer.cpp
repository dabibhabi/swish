#include "Renderer.h"

#include "../../core/Window/Window.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../CommandManager/CommandManager.h"
#include "../Device/Device.h"
#include "../Pipeline/Pipeline.h"
#include "../RenderPass/RenderPass.h"
#include "../ResourceManager/ResourceManager.h"
#include "../Swapchain/Swapchain.h"
#include "../SyncObjects/SyncObjects.h"
#include "../Vertex.h"
#include "../VulkanContext/VulkanContext.h"

#include <array>

// Implemented init sequence initializing all core submodules, cleanup sequence tracking reverse dependencies,
// setup for drawframe looping over fences and presenting KHR images, and gracefully recreating our swapchain viewport
// upon resize.

namespace swish {

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

void Renderer::init(Window& window) {
    m_window = &window;

    m_context->init(m_window->getHandle());
    m_device->init(m_context->getInstance(), m_context->getSurface());

    int width, height;
    m_window->getFramebufferSize(&width, &height);
    m_swapchain->init(*m_device, m_context->getSurface(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    createDepthResources();

    VkFormat depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    m_renderPass->init(m_device->getDevice(), m_swapchain->getImageFormat(), depthFormat);
    m_renderPass->createFramebuffers(m_device->getDevice(), m_swapchain->getImageViews(), m_depthImageView,
                                     m_swapchain->getExtent());

    createPipeline();

    QueueFamilyIndices queueFamilies =
        m_device->findQueueFamilies(m_device->getPhysicalDevice(), m_context->getSurface());
    m_commandManager->init(m_device->getDevice(), queueFamilies.graphicsFamily.value(), MAX_FRAMES_IN_FLIGHT);

    m_syncObjects->init(m_device->getDevice(), MAX_FRAMES_IN_FLIGHT);
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());

    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    destroyPipeline();
    m_renderPass->cleanup(m_device->getDevice());

    destroyDepthResources();
    m_swapchain->cleanup(m_device->getDevice());

    m_device->cleanup();
    m_context->cleanup();
}

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

    VK_CHECK(
        vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, m_syncObjects->getInFlightFence(m_currentFrame)));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

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
    // Using a clear blue visual indication
    clearValues[0].color        = {{0.1f, 0.2f, 0.4f, 1.0f}};
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

    // Draw calls go here in Phase 7

    vkCmdEndRenderPass(commandBuffer);

    m_commandManager->endRecording(frameIndex);
}

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    m_window->getFramebufferSize(&width, &height);
    // Explicitly spin wait inside resize if minimuzed
    while (width == 0 || height == 0) {
        m_window->getFramebufferSize(&width, &height);
        m_window->pollEvents();
    }

    vkDeviceWaitIdle(m_device->getDevice());

    m_renderPass->cleanup(m_device->getDevice());
    destroyDepthResources();
    m_swapchain->cleanup(m_device->getDevice());

    m_swapchain->init(*m_device, m_context->getSurface(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    createDepthResources();

    VkFormat depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    m_renderPass->init(m_device->getDevice(), m_swapchain->getImageFormat(), depthFormat);
    m_renderPass->createFramebuffers(m_device->getDevice(), m_swapchain->getImageViews(), m_depthImageView,
                                     m_swapchain->getExtent());
}

void Renderer::createDepthResources() {
    VkFormat   depthFormat = ResourceManager::findDepthFormat(m_device->getPhysicalDevice());
    VkExtent2D extent      = m_swapchain->getExtent();

    ResourceManager::createImage(m_device->getDevice(), m_device->getPhysicalDevice(), extent.width, extent.height,
                                 depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
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

void Renderer::createPipeline() {
    // Descriptor set layout: one UBO binding for camera matrices (view + proj)
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding            = 0;
    uboBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount    = 1;
    uboBinding.stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &uboBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout));

    // Push constant range: model matrix (mat4 = 64 bytes) for vertex shader
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(float) * 16;  // mat4

    m_pipelineLayout = Pipeline::createLayout(m_device->getDevice(), {m_descriptorSetLayout}, {pushConstantRange});

    // Configure and create the graphics pipeline
    auto binding    = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();

    PipelineConfig config{};
    config.vertShaderPath = std::string(SHADER_DIR) + "basic.vert.spv";
    config.fragShaderPath = std::string(SHADER_DIR) + "basic.frag.spv";
    config.vertexBindings.push_back(binding);
    config.vertexAttributes.assign(attributes.begin(), attributes.end());
    config.pipelineLayout = m_pipelineLayout;

    m_pipeline =
        Pipeline::create(m_device->getDevice(), config, m_renderPass->getRenderPass(), m_swapchain->getExtent());
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
}

}  // namespace swish
