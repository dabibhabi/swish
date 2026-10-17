#pragma once
// Declared sampler inputs: which named image feeds which binding of a pass's descriptor set. The binding numbers
// come from shaders/common/bindings.glsl (through renderer/Bindings.h), so the host list and the shader cannot
// drift: adding a composite input is one #define in bindings.glsl, one line here, and the shader's sampler.
//
// PostProcessManager builds the composite / lighting descriptor layouts and writes their sets from these lists.

#include "renderer/Bindings.h"
#include "renderer/Passes/ImageRegistry.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace swish {

struct SamplerInput {
    Img           img;
    uint32_t      binding;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // layout the image is in when sampled
};

// The binding numbers of a list, in list order (what the layout factory takes).
template <std::size_t N>
constexpr std::array<uint32_t, N> binding_numbers(const std::array<SamplerInput, N>& in) {
    std::array<uint32_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = in[i].binding;
    return out;
}

// True when the list's bindings are exactly 0..N-1 in some order (no gaps, no duplicates): what the descriptor
// layouts and the shaders assume.
template <std::size_t N>
constexpr bool bindings_dense(const std::array<SamplerInput, N>& in) {
    std::array<bool, N> seen{};
    for (const SamplerInput& s : in) {
        if (s.binding >= N || seen[s.binding]) return false;
        seen[s.binding] = true;
    }
    return true;
}

// composite.frag, set 0: HDR scene, bloom, AO, SSR, god-rays.
inline constexpr std::array<SamplerInput, 5> kCompositeInputs{{
    {Img::Hdr, bind::kBindCompHdr},
    {Img::BloomBlurV, bind::kBindCompBloom},
    {Img::AoBlur, bind::kBindCompAo},
    {Img::Ssr, bind::kBindCompSsr},
    {Img::Godrays, bind::kBindCompGodrays},
}};
static_assert(bindings_dense(kCompositeInputs), "composite inputs must be bindings 0..N-1");

// lighting.frag set 1 (also set 0 of ssr.frag / godrays.frag, which declare bindings 0-3 only): the G-buffer.
// Depth stays at binding 3 and is sampled in DEPTH_STENCIL_READ_ONLY.
inline constexpr std::array<SamplerInput, 5> kGBufferInputs{{
    {Img::GbAlbedo, bind::kBindGbAlbedo},
    {Img::GbNormal, bind::kBindGbNormal},
    {Img::GbMaterial, bind::kBindGbMaterial},
    {Img::HdrDepth, bind::kBindGbDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
    {Img::GbClearcoat, bind::kBindGbClearcoatFlake},
}};
static_assert(bindings_dense(kGBufferInputs), "G-buffer inputs must be bindings 0..N-1");

}  // namespace swish
