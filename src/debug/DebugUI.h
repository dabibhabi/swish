#pragma once

// ══════════════════════════════════════════════════════════════════════
// DebugUI — Dear ImGui (v1.91.x) integration for the swish renderer.
//
// The ENTIRE header body and its .cpp are wrapped in #ifdef SWISH_DEBUG_UI.
// Built without the define, this translation unit is empty and the class is
// absent — every caller must guard its use with #ifdef SWISH_DEBUG_UI too.
//
// Owns a detached GLFW/Vulkan window and its swapchain. Tuning controls are
// rendered independently from the game's swapchain so driving can continue.
// ══════════════════════════════════════════════════════════════════════

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"
#include "renderer/Swapchain/Swapchain.h"
#include "utils/types/Types.h"  // Mat4

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace swish {

class Device;

// One-shot init payload. All handles/objects are borrowed (not owned by
// DebugUI) except what DebugUI explicitly creates itself.
struct DebugUIInitInfo {
    Device*          deviceOwner;
    VkInstance       instance;
    VkPhysicalDevice physicalDevice;
    VkDevice         device;
    VkQueue          graphicsQueue;
    VkQueue          presentQueue;
    uint32_t         graphicsQueueFamily;

    uint32_t         presentQueueFamily;
    std::function<void(int key, int action)> keyEvent;
};

// "Render tools" panel feed (GPU profiler, frame capture, shader hot-reload, pass-declaration warnings). Borrowed,
// read-only views owned by the Renderer, plus two action callbacks. Any null pointer / empty callback just hides that
// part of the section. Set once after init (the pointed-to objects live as long as the Renderer).
struct GpuProfileSnapshot;
struct CaptureStatus;
class ShaderHotReload;
struct NamedDeclaration;

struct RenderToolsView {
    const GpuProfileSnapshot*            gpu          = nullptr;
    const CaptureStatus*                 capture      = nullptr;
    const ShaderHotReload*               hotReload    = nullptr;
    const std::vector<NamedDeclaration>* decls        = nullptr;  // number of passes checked
    const std::vector<std::string>*      declWarnings = nullptr;
    std::function<void(bool hdr)>        requestCapture;
    std::function<void(bool enabled)>    setHotReloadEnabled;
};

class DebugUI {
public:
    DebugUI()  = default;
    ~DebugUI() = default;

    DebugUI(const DebugUI&)            = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    // Create the detached settings window and its ImGui/Vulkan presentation path.
    void init(const DebugUIInitInfo& info);

    void set_render_tools(RenderToolsView v) { m_tools = std::move(v); }

    // ImGui NewFrame → build the panel (mutates `p`) → ImGui::Render. Call once
    // per game frame, then present() after the game's frame.
    void begin_frame(DebugParams& p, const Mat4& view, const Mat4& proj);
    void present();
    void toggle_visible();

    // Tear everything down. Idempotent (guarded by m_init).
    void cleanup();

private:
    void create_render_pass(VkFormat fmt);
    void create_framebuffers();
    void destroy_framebuffers();
    void recreate_swapchain();
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void focus_callback(GLFWwindow* window, int focused);

    Device*                    m_deviceOwner = nullptr;
    VkInstance                 m_instance = VK_NULL_HANDLE;
    VkDevice                   m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice           m_physicalDevice = VK_NULL_HANDLE;
    VkQueue                    m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue                    m_presentQueue = VK_NULL_HANDLE;
    uint32_t                   m_graphicsQueueFamily = 0;
    uint32_t                   m_presentQueueFamily = 0;
    GLFWwindow*                m_window = nullptr;
    VkSurfaceKHR               m_surface = VK_NULL_HANDLE;
    std::unique_ptr<Swapchain> m_swapchain;
    VkCommandPool              m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore>   m_imageAvailable;
    std::vector<VkSemaphore>   m_renderFinished;
    std::vector<VkFence>       m_inFlightFences;
    std::vector<VkFence>       m_imagesInFlight;
    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
    uint32_t                   m_currentFrame = 0;
    bool                       m_resized = false;
    std::function<void(int, int)> m_keyEvent;
    VkDescriptorPool           m_descriptorPool = VK_NULL_HANDLE;
    bool                       m_init   = false;
    RenderToolsView            m_tools;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
