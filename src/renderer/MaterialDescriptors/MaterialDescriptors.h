#pragma once

#include "../../scene/SceneTypes.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

class TextureManager;

class MaterialDescriptors {
public:
    void init(VkDevice device); // initialize the descriptor pool and sets
    void cleanup(VkDevice device); // cleanup the descriptor pool and sets

    void rebuild(VkDevice device, TextureManager& textures); // rebuild the descriptor pool and sets

    bool is_built() const { return m_pool != VK_NULL_HANDLE; } // check if the descriptor pool is built

    VkDescriptorSetLayout get_layout() const { return m_setLayout; }
    [[nodiscard]] VkDescriptorSet get_set(MaterialId mat) const;

private:
    VkDescriptorSetLayout        m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool             m_pool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;
};

} 
