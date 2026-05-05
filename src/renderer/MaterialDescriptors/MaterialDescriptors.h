#pragma once

#include "../../scene/SceneTypes.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

class TextureManager;

// Owns the per-material descriptor set layout (set 1 — albedo, normal,
// roughness combined image samplers), the pool, and one descriptor set per
// MaterialId. rebuild() (re)points the sets at whatever textures are
// currently loaded in TextureManager — call after textures load and on
// scene switch.
class MaterialDescriptors {
public:
    void init(VkDevice device);
    void cleanup(VkDevice device);

    void rebuild(VkDevice device, TextureManager& textures);

    bool is_built() const { return m_pool != VK_NULL_HANDLE; }

    VkDescriptorSetLayout get_layout() const { return m_setLayout; }
    VkDescriptorSet       get_set(MaterialId mat) const { return m_sets[mat]; }

private:
    VkDescriptorSetLayout        m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool             m_pool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;
};

}  // namespace swish
