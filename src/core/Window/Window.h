#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>

namespace swish {

class Window {
public:
    Window();
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // glfwInit + a resizable, no-client-API (Vulkan) window.
    void init(uint32_t width, uint32_t height, const char* title);

    // Destroy the window and terminate GLFW. Idempotent (also run by ~Window).
    void cleanup();

    bool shouldClose() const;
    void pollEvents();

    // Block until at least one GLFW event arrives (used in minimize spin).
    void waitEvents();

    // Raw handle (VulkanContext needs it for surface creation).
    GLFWwindow* getHandle() const;

    // Framebuffer size in pixels (not window size: differs on Retina/HiDPI).
    void getFramebufferSize(int* width, int* height) const;

    // Resize flag: set by the framebuffer-size callback, read by the renderer's
    // present step and cleared only by Renderer::recreateSwapchain (one consumer).
    bool wasResized() const;
    void resetResizedFlag();
    void mark_resized();

private:
    GLFWwindow* m_window          = nullptr;
    bool        m_resized         = false;
    bool        m_glfwInitialized = false;

    // Installed by init(); App later replaces it (and the user pointer) with its own.
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

}  // namespace swish
