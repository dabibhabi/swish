#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace swish {

// Forward declarations — Renderer knows about these but doesn't #include them
class Window;
class VulkanContext;
class Device;
class Swapchain;
class RenderPass;
class CommandManager;
class SyncObjects;

// The Renderer orchestrates the entire Vulkan draw loop.
// It OWNS all the Vulkan subsystems and coordinates them each frame.
//
// Architecture (DownPour-style — App owns Window + Renderer, Renderer owns
// Vulkan):
//   App
//    ├── Window        (GLFW)
//    └── Renderer      (this class)
//         ├── VulkanContext   (instance, debug messenger, surface)
//         ├── Device          (physical device, logical device, queues)
//         ├── Swapchain       (swap images, image views)
//         ├── RenderPass      (render pass, framebuffers)
//         ├── Pipeline        (graphics pipeline — created later)
//         ├── CommandManager  (command pool, command buffers)
//         └── SyncObjects     (fences, semaphores)
//
class Renderer {
public:
    Renderer();
    ~Renderer();

    // TODO: Initialize all Vulkan subsystems in dependency order:
    //   1. VulkanContext::init(window)           — instance, debug, surface
    //   2. Device::init(instance, surface)       — pick GPU, create device
    //   3. Swapchain::init(device, surface, w, h) — swap chain + image views
    //   4. Create depth resources                — depth image + view via
    //   ResourceManager
    //   5. RenderPass::init(device, colorFmt, depthFmt) — render pass
    //   6. RenderPass::createFramebuffers(...)   — one per swap image
    //   7. CommandManager::init(device, queueFamily, MAX_FRAMES_IN_FLIGHT)
    //   8. SyncObjects::init(device, MAX_FRAMES_IN_FLIGHT)
    //   9. (Later) Create pipeline, descriptor sets, etc.
    void init(Window& window);

    // TODO: Destroy everything in REVERSE order of init.
    // Must call vkDeviceWaitIdle() first to ensure GPU is done.
    void cleanup();

    // TODO: Execute one frame of the draw loop:
    //   1. syncObjects.waitForFence(currentFrame)
    //   2. vkAcquireNextImageKHR → imageIndex
    //      - If OUT_OF_DATE: recreateSwapchain(), return
    //   3. syncObjects.resetFence(currentFrame)
    //   4. commandManager.resetBuffer(currentFrame)
    //   5. recordCommandBuffer(currentFrame, imageIndex)
    //   6. vkQueueSubmit (wait=imageAvailable, signal=renderFinished,
    //   fence=inFlight)
    //   7. vkQueuePresentKHR (wait=renderFinished)
    //      - If OUT_OF_DATE or SUBOPTIMAL: recreateSwapchain()
    //   8. currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT
    void drawFrame();

private:
    // ── Owned subsystems ───────────────────────────────────────────
    VulkanContext*  m_context        = nullptr;
    Device*         m_device         = nullptr;
    Swapchain*      m_swapchain      = nullptr;
    RenderPass*     m_renderPass     = nullptr;
    CommandManager* m_commandManager = nullptr;
    SyncObjects*    m_syncObjects    = nullptr;

    // ── Depth resources (shared between render passes) ─────────────
    // TODO: Create via ResourceManager::createImage with DEPTH_STENCIL_ATTACHMENT
    // usage
    VkImage        m_depthImage     = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory    = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    // ── Pipeline (will be set up in Part 14) ──────────────────────
    // TODO: Add VkPipeline, VkPipelineLayout, VkDescriptorSetLayout here later

    // ── Frame tracking ─────────────────────────────────────────────
    uint32_t m_currentFrame = 0;

    // ── Pointer back to window (for resize checks) ────────────────
    Window* m_window = nullptr;

    // TODO: Record the command buffer for a single frame.
    // For now: begin render pass → set clear color → end render pass.
    // Later: bind pipeline, bind vertex buffers, draw geometry.
    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);

    // TODO: Destroy old swap chain resources, recreate with new window size.
    // Called when vkAcquireNextImageKHR or vkQueuePresentKHR returns OUT_OF_DATE.
    // Steps:
    //   1. vkDeviceWaitIdle()
    //   2. Cleanup: framebuffers, depth resources, swapchain
    //   3. Recreate: swapchain, depth resources, framebuffers
    void recreateSwapchain();

    // TODO: Create depth image + view for the current swap chain extent
    void createDepthResources();

    // TODO: Destroy depth image, memory, and view
    void destroyDepthResources();
};

}  // namespace swish
