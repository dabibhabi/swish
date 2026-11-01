// utils/vulkan/Samplers.h: the pure create-info fill. vkCreateSampler is never called.
#include "utils/vulkan/Samplers.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("default SamplerDesc is linear clamp-to-edge with no compare (MoltenVK) and no anisotropy", "[vulkan]") {
    const auto i = vkx::sampler_create_info({});
    CHECK(i.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    CHECK(i.magFilter == VK_FILTER_LINEAR);
    CHECK(i.minFilter == VK_FILTER_LINEAR);
    CHECK(i.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST);
    CHECK(i.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK(i.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK(i.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CHECK(i.compareEnable == VK_FALSE);
    CHECK(i.anisotropyEnable == VK_FALSE);
    CHECK(i.minLod == 0.0f);
    CHECK(i.maxLod == 0.0f);
}

TEST_CASE("the shadow sampler recipe: nearest, border white, still no compare", "[vulkan]") {
    const auto i = vkx::sampler_create_info({.filter  = VK_FILTER_NEAREST,
                                             .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                             .border  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE});
    CHECK(i.magFilter == VK_FILTER_NEAREST);
    CHECK(i.minFilter == VK_FILTER_NEAREST);
    CHECK(i.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    CHECK(i.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    CHECK(i.compareEnable == VK_FALSE);
}

TEST_CASE("the IBL cube sampler recipe: linear mips with an LOD range", "[vulkan]") {
    const auto i = vkx::sampler_create_info({.mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR, .maxLod = 5.0f});
    CHECK(i.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR);
    CHECK(i.maxLod == 5.0f);
}
