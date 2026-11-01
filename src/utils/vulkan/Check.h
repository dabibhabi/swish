#pragma once
// ============================================================================
// vkx/Check.h
//
// Vulkan concept : VkResult error handling (every vk* call that returns VkResult).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Fundamentals", section "Return Codes" (anchor #fundamentals-errorcodes, verify)
// Vulkan-Hpp     : vk::resultCheck / vk::Result (throws vk::SystemError); this is the C-API equivalent.
// MoltenVK notes : none specific. Swapchain codes (VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR) are
//                  EXPECTED on resize; route them through the predicates in Init.h, not VK_CHECK.
//
// Purpose        : VK_CHECK(expr) evaluates expr once and throws std::runtime_error with file, line
//                  and the symbolic VkResult name when it is not VK_SUCCESS. vkResultString() maps
//                  the common codes to names ("VK_UNKNOWN_ERROR" otherwise).
// Ownership      : Stateless; the macro keeps its historical name (24 call sites).
// Test           : tests/utils/test_vulkan_Check.cpp
// See also       : Init.h, README.md in this directory.
// ============================================================================

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace swish::vkx {

inline const char* vkResultString(VkResult r) {
    switch (r) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        default:
            return "VK_UNKNOWN_ERROR";
    }
}

}  // namespace swish::vkx

#define VK_CHECK(x)                                                                           \
    do {                                                                                      \
        VkResult _vkr = (x);                                                                  \
        if (_vkr != VK_SUCCESS) {                                                             \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                     " Vulkan error: " + swish::vkx::vkResultString(_vkr));   \
        }                                                                                     \
    } while (0)
