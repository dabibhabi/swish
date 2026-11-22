#pragma once
// ============================================================================
// vkx/ImageView.h
//
// Vulkan concept : VkImageView (a typed window onto a VkImage subresource range).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Resource Creation", section "Image Views" (anchor #resources-image-views, verify)
// Guide          : https://docs.vulkan.org/guide/latest/ (image views; verify)
// Vulkan-Hpp     : vk::ImageViewCreateInfo, vk::Device::createImageViewUnique
// https://github.com/KhronosGroup/Vulkan-Hpp MoltenVK notes : Depth views used for sampling must select only the DEPTH
// aspect (depth+stencil formats cannot be
//                  sampled with both aspects); Swish depth is D32_SFLOAT so `aspect = DEPTH` is the only case.
//
// Purpose        : `make_image_view2d`: one mip, one layer, 2D, identity swizzle. Replaces the private
//                  PostProcessManager::createImageView and the hand-filled copies in TaaPass, DofPass, VelocityPass,
//                  WindshieldRainPass and IBLManager.
// Ownership      : Raw handle; the caller vkDestroyImageView()s it exactly as before.
// Test           : tests/utils/test_vulkan_ImageView.cpp (pure create-info part)
// See also       : Init.h (makeImageViewCreateInfo), README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include "Check.h"
#include "Init.h"

#include <vulkan/vulkan.h>

namespace swish::vkx {

// Pure: create info for a single-mip, single-layer 2D view.
inline VkImageViewCreateInfo image_view2d_info(VkImage image, VkFormat format,
                                               VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    auto info                            = makeImageViewCreateInfo();
    info.image                           = image;
    info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    info.format                          = format;
    info.subresourceRange.aspectMask     = aspect;
    info.subresourceRange.baseMipLevel   = 0;
    info.subresourceRange.levelCount     = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount     = 1;
    return info;
}

inline VkImageView make_image_view2d(VkDevice device, VkImage image, VkFormat format,
                                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    const auto  info = image_view2d_info(image, format, aspect);
    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device, &info, nullptr, &view));
    return view;
}

}  // namespace swish::vkx
