#include "Window.h"

#include <stdexcept>

namespace swish {

Window::Window() = default;

Window::~Window() {
    cleanup();
}

void Window::init(uint32_t width, uint32_t height, const char* title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

void Window::cleanup() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    glfwPollEvents();
}

void Window::waitEvents() {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    glfwWaitEvents();
}

GLFWwindow* Window::getHandle() const {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    return m_window;
}

void Window::getFramebufferSize(int* width, int* height) const {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    glfwGetFramebufferSize(m_window, width, height);
}

bool Window::wasResized() const {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    return m_resized;
}

void Window::resetResizedFlag() {
    if (!m_window) {
        throw std::runtime_error("Window not initialized");
    }
    m_resized = false;
}

void Window::mark_resized() {
    if (!m_window)
        return;
    m_resized = true;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->m_resized = true;
    }
}

}  // namespace swish
