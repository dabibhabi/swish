#pragma once

#include <vulkan/vulkan.h>

namespace swish::vk {

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

}  // namespace swish::vk
