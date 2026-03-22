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

    // Initializes all Vulkan subsystems in dependency order.
    void init(Window& window);

    // Destroys everything in REVERSE order of init.
    void cleanup();

    // Executes one frame of the draw loop:
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
    // Created via ResourceManager::createImage with DEPTH_STENCIL_ATTACHMENT
    VkImage        m_depthImage     = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory    = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    // ── Pipeline ─────────────────────────────────────────────────
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_pipeline            = VK_NULL_HANDLE;

    // ── Frame tracking ─────────────────────────────────────────────
    uint32_t m_currentFrame = 0;

    // ── Pointer back to window (for resize checks) ────────────────
    Window* m_window = nullptr;

    // Records the command buffer for a single frame.
    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);

    // Destroys old swap chain resources, recreates with new window size.
    void recreateSwapchain();

    // Creates depth image + view for the current swap chain extent
    void createDepthResources();

    // Destroys depth image, memory, and view
    void destroyDepthResources();

    // Creates descriptor set layout, pipeline layout, and graphics pipeline
    void createPipeline();

    // Destroys pipeline, pipeline layout, and descriptor set layout
    void destroyPipeline();
};

}  // namespace swish
