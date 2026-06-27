#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

namespace swish {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

class Device {
public:
    Device()  = default;
    ~Device() = default;

    void init(VkInstance instance, VkSurfaceKHR surface);

    void cleanup();

    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice         getDevice() const;
    VkQueue          getGraphicsQueue() const;
    VkQueue          getPresentQueue() const;

    // Cached result of the queue family search performed during init().
    // Use this instead of calling findQueueFamilies() after init completes.
    const QueueFamilyIndices& getQueueFamilies() const;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;

private:
    VkPhysicalDevice   m_physicalDevice = VK_NULL_HANDLE;
    VkDevice           m_device         = VK_NULL_HANDLE;
    VkQueue            m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue            m_presentQueue   = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    int rateDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
};

}  // namespace swish
