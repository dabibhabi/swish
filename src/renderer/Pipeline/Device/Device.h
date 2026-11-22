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

    // Pipeline cache, created (and loaded from disk) during init and saved + destroyed during cleanup. Owned here,
    // published as RendererServices::pipelineCache and passed to pipeline::create_graphics / create_compute so
    // repeat launches / swapchain recreations skip recompiling identical pipelines.
    VkPipelineCache getPipelineCache() const { return m_pipelineCache; }

    // Vulkan 1.3 features that were actually ENABLED on the logical device (queried via
    // vkGetPhysicalDeviceFeatures2 + VkPhysicalDeviceVulkan13Features in init()). False means the
    // device lacks the feature (or SWISH_CLASSIC_VULKAN=1 forced the classic path); callers must then
    // use VkRenderPass/VkFramebuffer and classic vkCmdPipelineBarrier. Never a hard failure.
    bool supportsDynamicRendering() const { return m_dynamicRendering; }
    bool supportsSynchronization2() const { return m_synchronization2; }

    // Cached result of the queue family search performed during init().
    // Use this instead of calling findQueueFamilies() after init completes.
    const QueueFamilyIndices& getQueueFamilies() const;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) const;

private:
    // Create the pipeline cache, seeding it with config/pipeline_cache.bin if a
    // valid blob exists (the driver ignores data from a different device/vendor).
    void createPipelineCache();
    // Write the current cache blob back to config/pipeline_cache.bin.
    void savePipelineCache() const;

    VkPhysicalDevice   m_physicalDevice = VK_NULL_HANDLE;
    VkDevice           m_device         = VK_NULL_HANDLE;
    VmaAllocator       m_allocator      = nullptr;
    VkPipelineCache    m_pipelineCache  = VK_NULL_HANDLE;
    VkQueue            m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue            m_presentQueue   = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilies;
    bool               m_dynamicRendering = false;
    bool               m_synchronization2 = false;

    int rateDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
};

}  // namespace swish
