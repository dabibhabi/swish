#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace swish {

class VulkanContext {
public:
    VulkanContext()  = default;
    ~VulkanContext() = default;

    void init(GLFWwindow* window);

    void cleanup();

    VkInstance   getInstance() const;
    VkSurfaceKHR getSurface() const;

private:
    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;

    void createInstance();

    void setupDebugMessenger();

    void                                  createSurface(GLFWwindow* window);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void*                                       pUserData);
};

}  // namespace swish
