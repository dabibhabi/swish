#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

// Wrap every Vulkan call that returns VkResult with this.
// Throws on failure so errors don't cascade into mystery crashes.
// Usage: VK_CHECK(vkCreateBuffer(...))
#define VK_CHECK(x)                                                                                \
    do {                                                                                           \
        VkResult result = (x);                                                                     \
        if (result != VK_SUCCESS) {                                                                \
            throw std::runtime_error("Vulkan error: " + std::to_string(static_cast<int>(result))); \
        }                                                                                          \
    } while (0)
