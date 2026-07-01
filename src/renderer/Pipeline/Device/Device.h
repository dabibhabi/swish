#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

// Forward-declare the VMA allocator handle to keep this header light (the full
// vk_mem_alloc.h is included only in Device.cpp and the resource wrappers).
typedef struct VmaAllocator_T* VmaAllocator;

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

    // Single VMA sub-allocator for the whole renderer: sub-allocates all buffers
    // and images out of a few large device allocations instead of one
    // vkAllocateMemory per resource (which risks maxMemoryAllocationCount).
    VmaAllocator getAllocator() const { return m_allocator; }

    // Cached result of the queue family search performed during init().
    // Use this instead of calling findQueueFamilies() after init completes.
    const QueueFamilyIndices& getQueueFamilies() const;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;

private:
    VkPhysicalDevice   m_physicalDevice = VK_NULL_HANDLE;
    VkDevice           m_device         = VK_NULL_HANDLE;
    VmaAllocator       m_allocator      = nullptr;
    VkQueue            m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue            m_presentQueue   = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;

    int rateDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
};

}  // namespace swish
