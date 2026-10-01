// PostProcessManager: descriptor set layouts, pipeline layouts, pool and sets.

#include "PostProcessManager.h"
#include "renderer/Passes/PassInputs.h"
#include "renderer/Pipeline/Pipeline.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Descriptors.h"
#include "utils/vulkan/PushConstants.h"

#include <array>

namespace swish {

namespace {
constexpr std::size_t kMaxDeclaredInputs = 8;  // capacity of the batched declared-input write
static_assert(kCompositeInputs.size() <= kMaxDeclaredInputs && kGBufferInputs.size() <= kMaxDeclaredInputs);
}  // namespace

// ══════════════════════════════════════════════════════════════════════
// Descriptors — one-texture sets for bloom/AO, three-texture for composite
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createDescriptors() {
    createDescriptorLayouts();
    createDescriptorPool();
    createDescriptorSets();
}

void PostProcessManager::createDescriptorLayouts() {
    // Layout: single combined_image_sampler
    m_singleTexLayout = vkx::make_sampler_layout(m_device, 1);

    // Layout: the composite inputs (HDR + bloom + AO + SSR + god-rays), bindings from the declared list.
    m_compositeTexLayout = vkx::make_sampler_layout(m_device, binding_numbers(kCompositeInputs));

    // Pipeline layouts (with push constants)
    const VkPushConstantRange pcRange = vkx::push_range<PostProcessParams>(VK_SHADER_STAGE_FRAGMENT_BIT);

    m_postProcessLayout = pipeline::create_layout(m_device, {m_singleTexLayout}, {pcRange});
    m_compositeLayout   = pipeline::create_layout(m_device, {m_compositeTexLayout}, {pcRange});

    // SSAO layout: single depth texture (set 0) + a larger 144-B push block
    // (2×mat4 + params). Kept separate from m_postProcessLayout, whose push range
    // is too small for it.
    const VkPushConstantRange ssaoPC = vkx::push_range<SsaoParams>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_ssaoLayout                     = pipeline::create_layout(m_device, {m_singleTexLayout}, {ssaoPC});

    // Lighting descriptor layout: the G-buffer (albedo, normal, material, depth, clearcoat+flake), bindings from
    // kGBufferInputs. SSR/god-rays reuse this same layout (m_ssrLayout/m_godraysLayout below) but their
    // shaders only declare bindings 0-3 — an unused 5th binding in the layout is harmless there.
    m_lightingTexLayout = vkx::make_sampler_layout(m_device, binding_numbers(kGBufferInputs));

    // SSR layout (needs m_lightingTexLayout, created just above): set 0 = G-buffer
    // (depth/normal/material), set 1 = lit HDR + a 224-B push block (SsrParams).
    const VkPushConstantRange ssrPC = vkx::push_range<SsrParams>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_ssrLayout = pipeline::create_layout(m_device, {m_lightingTexLayout, m_singleTexLayout}, {ssrPC});

    // God-rays layout: set 0 = G-buffer (depth), set 1 = lit HDR + a 32-B push block.
    const VkPushConstantRange godraysPC = vkx::push_range<GodRaysParams>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_godraysLayout = pipeline::create_layout(m_device, {m_lightingTexLayout, m_singleTexLayout}, {godraysPC});

    // Shadow descriptor layout: 1 combined_image_sampler (a plain sampler2D depth texture —
    // MoltenVK has no comparison samplers, so PCF is done manually in lighting.frag),
    // fragment stage — set 2 in the lighting pipeline.
    m_shadowTexLayout = vkx::make_sampler_layout(m_device, 1);

    // The lighting pipeline layout (set 0 camera+lights, set 1 G-buffer, set 2 shadow, set 3 IBL, [set 4 params],
    // 128-B push of invView + invProj) is built by DeferredLightingPipeline::init (owned by LightingPass); this class
    // only provides the set 1 / set 2 layouts and the per-frame sets.
}

void PostProcessManager::createDescriptorPool() {
    // Descriptor pool (PP_MAX_FRAMES = 2) — single-texture sets (bloom-extract×PP_MAX_FRAMES + bloom blur×2 +
    // AO-depth×PP_MAX_FRAMES + AO-blur + SSR-HDR×PP_MAX_FRAMES + god-rays-HDR×PP_MAX_FRAMES = 11 sets, 1 sampler
    // each = 11) + 2 composite (5 samplers each = 10) + 2 lighting (5 samplers each = 10: albedo, normal,
    // material, clearcoat+flake, depth) + 2 shadow (1 each = 2) = 17 sets, 33 samplers. Sized to
    // 20 sets / 48 samplers for headroom.
    m_descriptorPool = vkx::make_pool(m_device, {.maxSets = 20, .combinedImageSamplers = 48});
}

void PostProcessManager::createDescriptorSets() {
    // Allocate single-texture sets
    auto allocSingleSet = [&]() -> VkDescriptorSet {
        return vkx::allocate_set(m_device, m_descriptorPool, m_singleTexLayout);
    };

    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_bloomExtractSets[i] = allocSingleSet();  // per-frame: extract samples that frame's lit HDR
    m_bloomBlurHSet = allocSingleSet();
    m_bloomBlurVSet = allocSingleSet();
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_aoSets[i] = allocSingleSet();  // per-frame: SSAO samples that frame's depth
    m_aoBlurSet = allocSingleSet();
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_ssrHdrSets[i] = allocSingleSet();  // per-frame: SSR samples that frame's lit HDR
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_godraysHdrSets[i] = allocSingleSet();  // per-frame: god-rays samples that frame's lit HDR

    // Allocate composite sets (per frame)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_compositeSets[i] = vkx::allocate_set(m_device, m_descriptorPool, m_compositeTexLayout);

    // Write descriptor sets
    auto writeSingleLayout = [&](VkDescriptorSet set, VkImageView view, VkImageLayout layout) {
        vkx::write_sampler(m_device, set, 0, m_sampler, view, layout);
    };
    auto writeSingle = [&](VkDescriptorSet set, VkImageView view) {
        writeSingleLayout(set, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };

    // Bloom extract samples each frame's own lit HDR. (It used to share one set pinned to HDR[0], so odd
    // frames sampled the other frame's image without a barrier in their command buffer.)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingle(m_bloomExtractSets[i], m_hdrViews[i]);
    writeSingle(m_bloomBlurHSet, m_bloomExtractView);
    writeSingle(m_bloomBlurVSet, m_bloomBlurHView);
    // SSAO samples the scene depth (a depth image → DEPTH_STENCIL_READ_ONLY layout),
    // per-frame so each frame reads its own depth (no cross-frame ghosting).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingleLayout(m_aoSets[i], m_hdrDepthViews[i], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    writeSingle(m_aoBlurSet, m_aoView);
    // SSR samples each frame's lit HDR (colour, SHADER_READ).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingle(m_ssrHdrSets[i], m_hdrViews[i]);
    // God-rays samples each frame's lit HDR too (the shaft "source").
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingle(m_godraysHdrSets[i], m_hdrViews[i]);

    // Composite sets: the declared inputs (HDR is per-frame, the rest are shared images), one write per input.
    // Batched in one vkUpdateDescriptorSets call, as before.
    auto writeDeclared = [&](VkDescriptorSet set, const auto& inputs, uint32_t frame) {
        std::array<VkDescriptorImageInfo, kMaxDeclaredInputs> infos{};
        std::array<VkWriteDescriptorSet, kMaxDeclaredInputs>  writes{};
        uint32_t                                              n = 0;
        for (const SamplerInput& in : inputs) {
            infos[n]  = {m_sampler, view(in.img, frame), in.layout};
            writes[n] = vkx::sampler_write(set, in.binding, &infos[n]);
            ++n;
        }
        vkUpdateDescriptorSets(m_device, n, writes.data(), 0, nullptr);
    };
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeDeclared(m_compositeSets[i], kCompositeInputs, i);

    // Lighting sets: the G-buffer per frame, from kGBufferInputs (albedo, normal, material, depth, clearcoat+flake).
    // Depth stays at binding 3 (unchanged) so SSR/god-rays, which share m_lightingTexLayout but only declare bindings
    // 0-3, need no shader change; clearcoat+flake is appended at binding 4 (lighting.frag only).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_lightingSets[i] = vkx::allocate_set(m_device, m_descriptorPool, m_lightingTexLayout);
        writeDeclared(m_lightingSets[i], kGBufferInputs, i);
    }

    // Shadow sets: one plain sampler2D depth texture per frame, bound with the non-comparison
    // shadow sampler (manual PCF in lighting.frag).
    // Layout is DEPTH_STENCIL_READ_ONLY_OPTIMAL (the shadow render pass leaves the
    // image in that layout on store).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_shadowSets[i] = vkx::allocate_set(m_device, m_descriptorPool, m_shadowTexLayout);
        vkx::write_sampler(m_device, m_shadowSets[i], 0, m_shadowSampler, m_shadowViews[i],
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
}

void PostProcessManager::destroyDescriptors() {
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_postProcessLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_postProcessLayout, nullptr);
        m_postProcessLayout = VK_NULL_HANDLE;
    }
    if (m_compositeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_compositeLayout, nullptr);
        m_compositeLayout = VK_NULL_HANDLE;
    }
    if (m_ssaoLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_ssaoLayout, nullptr);
        m_ssaoLayout = VK_NULL_HANDLE;
    }
    if (m_ssrLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_ssrLayout, nullptr);
        m_ssrLayout = VK_NULL_HANDLE;
    }
    if (m_godraysLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_godraysLayout, nullptr);
        m_godraysLayout = VK_NULL_HANDLE;
    }
    if (m_singleTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_singleTexLayout, nullptr);
        m_singleTexLayout = VK_NULL_HANDLE;
    }
    if (m_compositeTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_compositeTexLayout, nullptr);
        m_compositeTexLayout = VK_NULL_HANDLE;
    }
    if (m_lightingTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_lightingTexLayout, nullptr);
        m_lightingTexLayout = VK_NULL_HANDLE;
    }
    if (m_shadowTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_shadowTexLayout, nullptr);
        m_shadowTexLayout = VK_NULL_HANDLE;
    }
}

}  // namespace swish
