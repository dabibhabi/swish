#pragma once

// C3b: the in-game HUD strip + route minimap, drawn over the game's swapchain image (debug build only).
// A second Dear ImGui context with its own Vulkan backend: the panel's context belongs to the detached
// controls window, so this one never touches it (every call saves and restores the current context).
// Draw-list only: no windows, no input. Fed by DebugParams::hud (App fills it, AppHud.cpp).

#ifdef SWISH_DEBUG_UI

#include <vulkan/vulkan.h>

#include <cstdint>

struct ImGuiContext;
struct ImFont;

namespace swish {

struct DebugParams;

struct HudOverlayInit {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          queue          = VK_NULL_HANDLE;
    uint32_t         queueFamily    = 0;
    VkFormat         format         = VK_FORMAT_UNDEFINED;  // game swapchain format
    uint32_t         imageCount     = 2;
};

class HudOverlay {
public:
    void init(const HudOverlayInit& info);
    // Records a LOAD render pass on `fb` (a framebuffer over the swapchain image, PRESENT_SRC in and out).
    void record(VkCommandBuffer cmd, VkFramebuffer fb, VkExtent2D extent, const DebugParams& p);
    void cleanup();  // idempotent

private:
    ImGuiContext*    m_ctx    = nullptr;
    ImFont*          m_font   = nullptr;  // labels
    ImFont*          m_big    = nullptr;  // speed
    VkDevice         m_device = VK_NULL_HANDLE;
    VkRenderPass     m_pass   = VK_NULL_HANDLE;
    VkDescriptorPool m_pool   = VK_NULL_HANDLE;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
