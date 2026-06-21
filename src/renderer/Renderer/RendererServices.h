#pragma once

#include <vulkan/vulkan.h>

namespace swish {

struct RendererServices {
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          graphicsQueue   = VK_NULL_HANDLE;
    VkExtent2D       swapchainExtent = {0, 0};
};

}  // namespace swish
