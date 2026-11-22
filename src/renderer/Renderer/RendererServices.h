#pragma once

#include <vulkan/vulkan.h>

typedef struct VmaAllocator_T* VmaAllocator;  // forward-declared (see Device.h)

namespace swish {

struct RendererServices {
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          graphicsQueue   = VK_NULL_HANDLE;
    VkExtent2D       swapchainExtent = {0, 0};
    VmaAllocator     allocator       = nullptr;  // single VMA sub-allocator (Device::getAllocator)
    // Vulkan 1.3 features enabled on the device (Device::supportsDynamicRendering / ...Synchronization2).
    // Passes with a dynamic-rendering path (TaaPass) branch on these; false keeps the classic path.
    bool dynamicRendering = false;
    bool synchronization2 = false;
    // Device-owned VkPipelineCache (Device::getPipelineCache); passed to pipeline::create_graphics / create_compute.
    // VK_NULL_HANDLE is valid (the driver compiles from scratch). Not a global: it travels with the services.
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
};

}  // namespace swish
