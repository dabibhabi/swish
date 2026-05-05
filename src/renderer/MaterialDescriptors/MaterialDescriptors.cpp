#include "MaterialDescriptors.h"

#include "../../utils/VulkanCheck.h"
#include "../TextureManager/TextureManager.h"

#include <array>
#include <stdexcept>
#include <string>

namespace swish {

namespace {

// MaterialId → texture base name in TextureManager. Suffixes ("", "_normal",
// "_roughness") are appended to pick the per-binding texture.
constexpr const char* kMaterialNames[MAT_COUNT] = {
    "asphalt",      // MAT_ASPHALT
    "grass",        // MAT_GRASS
    "concrete",     // MAT_CONCRETE
    "metal",        // MAT_METAL
    "default",      // MAT_DEFAULT
    "rumble",       // MAT_RUMBLE
    "dirt",         // MAT_DIRT
    "tree_leaves",  // MAT_TREE
    "sign_00",      // MAT_SIGN_0
    "sign_01",      // MAT_SIGN_1
    "sign_02",      // MAT_SIGN_2
    "sign_03",      // MAT_SIGN_3
    "sign_04",      // MAT_SIGN_4
    "sign_05",      // MAT_SIGN_5
    "sign_06",      // MAT_SIGN_6
    "sign_07",      // MAT_SIGN_7
};

constexpr const char* kSuffixes[3] = {"", "_normal", "_roughness"};

}  // namespace

void MaterialDescriptors::init(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; i++) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings    = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &m_setLayout));
}

void MaterialDescriptors::cleanup(VkDevice device) {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_sets.clear();
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
}

void MaterialDescriptors::rebuild(VkDevice device, TextureManager& textures) {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_sets.clear();

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAT_COUNT * 3;  // albedo + normal + roughness per material

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAT_COUNT;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    std::vector<VkDescriptorSetLayout> layouts(MAT_COUNT, m_setLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_pool;
    allocInfo.descriptorSetCount = MAT_COUNT;
    allocInfo.pSetLayouts        = layouts.data();

    m_sets.resize(MAT_COUNT);
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()));

    for (uint32_t i = 0; i < MAT_COUNT; i++) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        std::array<VkWriteDescriptorSet, 3>  writes{};

        for (uint32_t b = 0; b < 3; b++) {
            std::string texName = std::string(kMaterialNames[i]) + kSuffixes[b];
            Texture*    tex     = textures.get_texture(texName);

            // Fall back to the 1×1 white "default" if a normal/roughness map is missing.
            if (!tex) {
                tex = textures.get_texture("default");
            }
            if (!tex) {
                throw std::runtime_error("MaterialDescriptors::rebuild: missing texture '" +
                                         texName + "'");
            }

            imageInfos[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[b].imageView   = tex->view;
            imageInfos[b].sampler     = textures.get_sampler();

            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_sets[i];
            writes[b].dstBinding      = b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &imageInfos[b];
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

}  // namespace swish
