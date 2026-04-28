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

    // TODO: Create the GLFW window.
    // - Call glfwInit()
    // - Set glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)  ← NO OpenGL context
    // - Set glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE)
    // - Create window with glfwCreateWindow(width, height, title, nullptr,
    // nullptr)
    // - Set framebuffer resize callback (for swap chain recreation)
    void init(uint32_t width, uint32_t height, const char* title);

    // TODO: Destroy the GLFW window and call glfwTerminate()
    void cleanup();

    // TODO: Return true if user hasn't closed the window
    bool shouldClose() const;

    // TODO: Call glfwPollEvents()
    void pollEvents();

    // TODO: Return the raw GLFWwindow* (needed by VulkanContext for surface
    // creation)
    GLFWwindow* getHandle() const;

    // TODO: Get framebuffer size in pixels (important for Retina/HiDPI displays)
    // Use glfwGetFramebufferSize(), NOT glfwGetWindowSize()
    void getFramebufferSize(int* width, int* height) const;

    // TODO: Track whether the window was resized (set by the GLFW callback)
    bool wasResized() const;
    void resetResizedFlag();
    void mark_resized();

private:
    GLFWwindow* m_window  = nullptr;
    bool        m_resized = false;

    // TODO: Static callback — GLFW calls this when the framebuffer resizes.
    // It should set m_resized = true on the Window instance.
    // Retrieve the Window* via glfwGetWindowUserPointer().
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

}  // namespace swish
