#pragma once
// ============================================================================
// vkx/Init.h
//
// Vulkan concept : sType-tagged create/info structs, and swapchain acquire/present results.
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Fundamentals", section "Valid Usage" / VkStructureType (verify);
//                  "Presentation" (VK_SUBOPTIMAL_KHR / VK_ERROR_OUT_OF_DATE_KHR semantics, verify)
// Vulkan-Hpp     : vk::SubmitInfo etc. set sType automatically; these factories give the C API the same.
// MoltenVK notes : none specific.
//
// Purpose        : make*Info() return a zero-initialised struct with sType already set (a common source
//                  of validation errors); is_*() / swapchain_needs_recreation() classify the VkResult of
//                  vkAcquireNextImageKHR / vkQueuePresentKHR.
// Ownership      : Stateless.
// Test           : tests/utils/test_vulkan_Init.cpp
// See also       : Check.h, README.md in this directory.
// ============================================================================

#include <vulkan/vulkan.h>

namespace swish::vkx {

inline VkSubmitInfo makeSubmitInfo() {
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    return si;
}

inline VkPresentInfoKHR makePresentInfo() {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    return pi;
}

inline VkRenderPassBeginInfo makeRenderPassBeginInfo() {
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    return info;
}

inline VkImageViewCreateInfo makeImageViewCreateInfo() {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    return info;
}

inline VkSamplerCreateInfo makeSamplerCreateInfo() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    return info;
}

inline bool is_success(VkResult r) {
    return r == VK_SUCCESS;
}
inline bool is_out_of_date(VkResult r) {
    return r == VK_ERROR_OUT_OF_DATE_KHR;
}
inline bool is_suboptimal(VkResult r) {
    return r == VK_SUBOPTIMAL_KHR;
}
inline bool is_presentable(VkResult r) {
    return is_success(r) || is_suboptimal(r);
}
inline bool swapchain_needs_recreation(VkResult r) {
    return is_out_of_date(r) || is_suboptimal(r);
}

}  // namespace swish::vkx
