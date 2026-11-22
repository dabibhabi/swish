#pragma once
// ============================================================================
// vkx/Descriptors.h
//
// Vulkan concept : Descriptor set layouts, descriptor pools, descriptor set allocation and descriptor writes
//                  for combined image samplers (the only descriptor kind the post-process passes use).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Resource Descriptors": VkDescriptorSetLayout, VkDescriptorPool,
//                  vkAllocateDescriptorSets, vkUpdateDescriptorSets (anchor #descriptorsets, verify)
// Guide          : https://docs.vulkan.org/guide/latest/mapping_data_to_shaders.html (verify)
// Vulkan-Hpp     : vk::DescriptorSetLayoutCreateInfo, vk::DescriptorPoolCreateInfo, vk::DescriptorPoolSize,
//                  vk::Device::allocateDescriptorSets, vk::WriteDescriptorSet
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Descriptor sets are emulated with Metal argument buffers / direct bindings; pool exhaustion
//                  reports VK_ERROR_OUT_OF_POOL_MEMORY like any driver, so size pools with headroom (Swish rule:
//                  count sets x samplers per set, then round up). No update-after-bind is used.
//
// Purpose        : Removes the create-info boilerplate repeated 15 times (layouts), 17 times (allocation) and
//                  the hand-counted pools. `PoolSizes` is an aggregate (C++20 designated initialisers), not a
//                  fluent builder. Pure parts (`sampler_layout_bindings`, `pool_size_list`) are unit-tested; the
//                  functions that call Vulkan are thin and VK_CHECKed.
// Ownership      : Every function returns a raw handle; the caller destroys it exactly as before
//                  (vkDestroyDescriptorSetLayout / vkDestroyDescriptorPool; sets die with their pool).
// Test           : tests/utils/test_vulkan_Descriptors.cpp
// See also       : Samplers.h, README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include "Check.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

namespace swish::vkx {

// ── layouts ──────────────────────────────────────────────────────────────

// Pure: one combined-image-sampler binding per entry of `bindings` (explicit numbers, so a declared-input list
// can dictate the indices), all visible to `stages`.
inline std::vector<VkDescriptorSetLayoutBinding> sampler_layout_bindings(std::span<const uint32_t> bindings,
                                                                         VkShaderStageFlags        stages) {
    std::vector<VkDescriptorSetLayoutBinding> out(bindings.size());
    for (size_t i = 0; i < bindings.size(); ++i) {
        out[i].binding         = bindings[i];
        out[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        out[i].descriptorCount = 1;
        out[i].stageFlags      = stages;
    }
    return out;
}

// Layout of combined image samplers at the given binding numbers.
inline VkDescriptorSetLayout make_sampler_layout(VkDevice device, std::span<const uint32_t> bindings,
                                                 VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT) {
    const auto                      b = sampler_layout_bindings(bindings, stages);
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount            = static_cast<uint32_t>(b.size());
    info.pBindings               = b.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

// Layout of `count` combined image samplers at bindings 0..count-1.
inline VkDescriptorSetLayout make_sampler_layout(VkDevice device, uint32_t count,
                                                 VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT) {
    std::vector<uint32_t> idx(count);
    for (uint32_t i = 0; i < count; ++i) idx[i] = i;
    return make_sampler_layout(device, std::span<const uint32_t>(idx), stages);
}

// ── pools ────────────────────────────────────────────────────────────────

// Pool capacity. Zero counts are omitted from the pool-size list (a zero-sized VkDescriptorPoolSize is invalid).
struct PoolSizes {
    uint32_t maxSets               = 0;
    uint32_t combinedImageSamplers = 0;
    uint32_t uniformBuffers        = 0;
    uint32_t storageBuffers        = 0;
};

// Pure: the VkDescriptorPoolSize list for `sizes`, in a fixed order (samplers, uniform, storage).
inline std::vector<VkDescriptorPoolSize> pool_size_list(const PoolSizes& sizes) {
    std::vector<VkDescriptorPoolSize> out;
    if (sizes.combinedImageSamplers)
        out.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sizes.combinedImageSamplers});
    if (sizes.uniformBuffers) out.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sizes.uniformBuffers});
    if (sizes.storageBuffers) out.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sizes.storageBuffers});
    return out;
}

inline VkDescriptorPool make_pool(VkDevice device, const PoolSizes& sizes) {
    const auto                 list = pool_size_list(sizes);
    VkDescriptorPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount    = static_cast<uint32_t>(list.size());
    info.pPoolSizes       = list.data();
    info.maxSets          = sizes.maxSets;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &pool));
    return pool;
}

// ── sets ─────────────────────────────────────────────────────────────────

// One set from `pool` with `layout`.
inline VkDescriptorSet allocate_set(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;
    VkDescriptorSet set   = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, &set));
    return set;
}

// out.size() sets, all with `layout`, in one call.
inline void allocate_sets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
                          std::span<VkDescriptorSet> out) {
    std::vector<VkDescriptorSetLayout> layouts(out.size(), layout);
    VkDescriptorSetAllocateInfo        ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = static_cast<uint32_t>(out.size());
    ai.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, out.data()));
}

// Pure: the write for one combined image sampler; `info` must outlive the vkUpdateDescriptorSets call.
inline VkWriteDescriptorSet sampler_write(VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo* info) {
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo      = info;
    return w;
}

// Write one sampled image at `binding` (one vkUpdateDescriptorSets call).
inline void write_sampler(VkDevice device, VkDescriptorSet set, uint32_t binding, VkSampler sampler, VkImageView view,
                          VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    VkDescriptorImageInfo info{sampler, view, layout};
    const auto            w = sampler_write(set, binding, &info);
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

}  // namespace swish::vkx
