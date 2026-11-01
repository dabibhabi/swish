#pragma once
// ============================================================================
// vkx/Samplers.h
//
// Vulkan concept : VkSampler (filtering, addressing, LOD range, border colour). Combined image samplers pair one
//                  with an image view in a descriptor.
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Samplers" (anchor #samplers, verify); VkSamplerCreateInfo (verify)
// Guide          : https://docs.vulkan.org/guide/latest/ (sampler page; verify)
// Vulkan-Hpp     : vk::SamplerCreateInfo, vk::Device::createSamplerUnique  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : NO comparison samplers: MoltenVK's portability subset reports mutableComparisonSamplers = FALSE,
//                  so `SamplerDesc` has no compare option on purpose (the shadow map is a plain depth texture with a
//                  manual PCF compare in lighting.frag). Anisotropy is not exposed here (TextureManager keeps its
//                  own sampler).
//
// Purpose        : `SamplerDesc` aggregate + `make_sampler`; replaces 9 hand-filled VkSamplerCreateInfo blocks
//                  (post-process, TAA, DoF, windshield refraction, IBL, shadow). Defaults are the common one:
//                  linear filter, clamp-to-edge, nearest mip, LOD 0.
// Ownership      : Raw handle; the caller vkDestroySampler()s it exactly as before.
// Test           : tests/utils/test_vulkan_Samplers.cpp
// See also       : Descriptors.h, README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include "Check.h"
#include "Init.h"

#include <vulkan/vulkan.h>

namespace swish::vkx {

struct SamplerDesc {
    VkFilter             filter  = VK_FILTER_LINEAR;                       // used for both mag and min
    VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // U, V and W
    VkSamplerMipmapMode  mipmap  = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    float                minLod  = 0.0f;
    float                maxLod  = 0.0f;
    VkBorderColor        border  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;  // only read for CLAMP_TO_BORDER
};

// Pure: the create info for `d` (compareEnable and anisotropyEnable stay VK_FALSE).
inline VkSamplerCreateInfo sampler_create_info(const SamplerDesc& d) {
    auto info         = makeSamplerCreateInfo();
    info.magFilter    = d.filter;
    info.minFilter    = d.filter;
    info.mipmapMode   = d.mipmap;
    info.addressModeU = d.address;
    info.addressModeV = d.address;
    info.addressModeW = d.address;
    info.minLod       = d.minLod;
    info.maxLod       = d.maxLod;
    info.borderColor  = d.border;
    return info;
}

inline VkSampler make_sampler(VkDevice device, const SamplerDesc& d = {}) {
    const auto info    = sampler_create_info(d);
    VkSampler  sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &info, nullptr, &sampler));
    return sampler;
}

}  // namespace swish::vkx
