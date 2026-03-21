#include "Renderer.h"

// TODO: Include these as you implement each subsystem:
#include "../Device/Device.h"
#include "../VulkanContext/VulkanContext.h"
// #include "../Swapchain/Swapchain.h"
// #include "../RenderPass/RenderPass.h"
// #include "../CommandManager/CommandManager.h"
// #include "../SyncObjects/SyncObjects.h"
// #include "../ResourceManager/ResourceManager.h"
// #include "../../utils/VulkanCheck.h"
// #include "../../utils/Types.h"
// #include "../../core/Window/Window.h"

namespace swish {

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

void Renderer::init(Window& window) {
    m_window = &window;

    // TODO: Implement steps 1-8 from the header comments.
    // Start with VulkanContext, then Device, then Swapchain, etc.
    // Each step depends on the one before it.
}

void Renderer::cleanup() {
    // TODO: vkDeviceWaitIdle() first!
    // Then destroy in reverse order of init.
}

void Renderer::drawFrame() {
    // TODO: Implement the 8-step frame loop from the header comments.
    // Start simple — just clear the screen to a color.
}

void Renderer::recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    // TODO: Begin command buffer → begin render pass → (draw nothing yet) → end
    // render pass → end command buffer The clear color is set in
    // VkRenderPassBeginInfo::pClearValues
}

void Renderer::recreateSwapchain() {
    // TODO: Handle window resize — destroy old resources, create new ones
}

void Renderer::createDepthResources() {
    // TODO: Use ResourceManager::createImage + ResourceManager::findDepthFormat
}

void Renderer::destroyDepthResources() {
    // TODO: vkDestroyImageView, vkDestroyImage, vkFreeMemory
}

}  // namespace swish
