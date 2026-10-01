// utils/vulkan/Init.h: sType-tagged struct factories + swapchain result predicates.
#include "utils/vulkan/Init.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("make*Info factories set sType and zero the rest", "[vulkan]") {
    const auto si = vkx::makeSubmitInfo();
    CHECK(si.sType == VK_STRUCTURE_TYPE_SUBMIT_INFO);
    CHECK(si.commandBufferCount == 0);
    CHECK(vkx::makePresentInfo().sType == VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    CHECK(vkx::makeRenderPassBeginInfo().sType == VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    CHECK(vkx::makeImageViewCreateInfo().sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    CHECK(vkx::makeSamplerCreateInfo().sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    CHECK(vkx::makeSamplerCreateInfo().pNext == nullptr);
}

TEST_CASE("swapchain result predicates classify VkResult", "[vulkan]") {
    CHECK(vkx::is_success(VK_SUCCESS));
    CHECK_FALSE(vkx::is_success(VK_SUBOPTIMAL_KHR));
    CHECK(vkx::is_suboptimal(VK_SUBOPTIMAL_KHR));
    CHECK(vkx::is_out_of_date(VK_ERROR_OUT_OF_DATE_KHR));
    CHECK(vkx::is_presentable(VK_SUCCESS));
    CHECK(vkx::is_presentable(VK_SUBOPTIMAL_KHR));
    CHECK_FALSE(vkx::is_presentable(VK_ERROR_OUT_OF_DATE_KHR));
    CHECK(vkx::swapchain_needs_recreation(VK_ERROR_OUT_OF_DATE_KHR));
    CHECK(vkx::swapchain_needs_recreation(VK_SUBOPTIMAL_KHR));
    CHECK_FALSE(vkx::swapchain_needs_recreation(VK_SUCCESS));
    CHECK_FALSE(vkx::swapchain_needs_recreation(VK_ERROR_DEVICE_LOST));
}
