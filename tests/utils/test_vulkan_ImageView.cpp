// utils/vulkan/ImageView.h: the pure create-info fill. vkCreateImageView is never called.
#include "utils/vulkan/ImageView.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("image_view2d_info is a single-mip single-layer 2D colour view by default", "[vulkan]") {
    const auto i = vkx::image_view2d_info(reinterpret_cast<VkImage>(uintptr_t{0x30}), VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK(i.sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    CHECK(i.image == reinterpret_cast<VkImage>(uintptr_t{0x30}));
    CHECK(i.viewType == VK_IMAGE_VIEW_TYPE_2D);
    CHECK(i.format == VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK(i.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(i.subresourceRange.baseMipLevel == 0);
    CHECK(i.subresourceRange.levelCount == 1);
    CHECK(i.subresourceRange.baseArrayLayer == 0);
    CHECK(i.subresourceRange.layerCount == 1);
    CHECK(i.components.r == VK_COMPONENT_SWIZZLE_IDENTITY);
}

TEST_CASE("image_view2d_info honours the depth aspect", "[vulkan]") {
    const auto i = vkx::image_view2d_info(VK_NULL_HANDLE, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(i.subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT);
}
