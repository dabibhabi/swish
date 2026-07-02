#pragma once

// ══════════════════════════════════════════════════════════════════════
// DebugUI — Dear ImGui (v1.91.x) integration for the swish renderer.
//
// The ENTIRE header body and its .cpp are wrapped in #ifdef SWISH_DEBUG_UI.
// Built without the define, this translation unit is empty and the class is
// absent — every caller must guard its use with #ifdef SWISH_DEBUG_UI too.
//
// Owns its own descriptor pool, render pass, and per-swapchain-image
// framebuffers. The render pass chains AFTER the composite pass (which
// leaves the swapchain image in PRESENT_SRC_KHR): loadOp = LOAD, both
// initial/final layout = PRESENT_SRC_KHR, so the panel is drawn straight
// onto the already-composited frame with no extra transitions.
// ══════════════════════════════════════════════════════════════════════

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"

#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include <vector>

namespace swish {

// One-shot init payload. All handles/objects are borrowed (not owned by
// DebugUI) except what DebugUI explicitly creates itself.
struct DebugUIInitInfo {
    VkInstance       instance;
    VkPhysicalDevice physicalDevice;
    VkDevice         device;
    VkQueue          graphicsQueue;
    uint32_t         graphicsQueueFamily;

    GLFWwindow* window;

    VkFormat                 swapchainFormat;
    std::vector<VkImageView> swapchainViews;
    VkExtent2D               extent;
    uint32_t                 imageCount;
    uint32_t                 minImageCount;
};

class DebugUI {
public:
    DebugUI()  = default;
    ~DebugUI() = default;

    DebugUI(const DebugUI&)            = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    // Create ImGui context + GLFW/Vulkan backends + our own pool/pass/FBs.
    void init(const DebugUIInitInfo& info);

    // ImGui NewFrame → build the panel (mutates `p`) → ImGui::Render.
    // Call once per frame BEFORE record().
    void begin_frame(DebugParams& p);

    // Begin our render pass on framebuffer[imageIndex] and emit ImGui draws.
    // Call inside the frame's primary command buffer, AFTER the composite pass.
    void record(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent);

    // Rebuild framebuffers for a resized/recreated swapchain. Assumes the
    // swapchain format is unchanged (render pass is format-dependent only).
    void recreate(VkFormat fmt, const std::vector<VkImageView>& views, VkExtent2D extent);

    // Tear everything down. Idempotent (guarded by m_init).
    void cleanup();

private:
    void createRenderPass(VkFormat fmt);
    void createFramebuffers(const std::vector<VkImageView>& views, VkExtent2D extent);
    void destroyFramebuffers();

    VkDevice                   m_device         = VK_NULL_HANDLE;
    VkDescriptorPool           m_descriptorPool = VK_NULL_HANDLE;
    VkRenderPass               m_renderPass     = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    VkExtent2D                 m_extent = {};
    bool                       m_init   = false;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
