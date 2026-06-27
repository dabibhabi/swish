#include "MaterialDescriptors.h"

#include "../../utils/VulkanCheck.h"
#include "../TextureManager/TextureManager.h"

#include <array>
#include <cassert>
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
    "car_0",        // MAT_CAR_0
    "car_1",        // MAT_CAR_1
    "car_2",        // MAT_CAR_2
    "car_3",        // MAT_CAR_3
    "car_4",        // MAT_CAR_4
    "car_5",        // MAT_CAR_5
    "car_6",        // MAT_CAR_6
    "car_7",        // MAT_CAR_7
    "car_8",        // MAT_CAR_8
    "car_9",        // MAT_CAR_9
    "car_10",       // MAT_CAR_10
    "car_11",       // MAT_CAR_11
    "car_12",       // MAT_CAR_12
    "car_13",       // MAT_CAR_13
    "car_14",       // MAT_CAR_14
    "car_15",       // MAT_CAR_15
    "car_16",       // MAT_CAR_16
    "car_17",       // MAT_CAR_17
    "car_18",       // MAT_CAR_18
    "car_19",       // MAT_CAR_19
};

constexpr uint32_t    kTexturesPerMaterial = 3;
constexpr const char* kSuffixes[kTexturesPerMaterial] = {"", "_normal", "_roughness"};

} 

void MaterialDescriptors::init(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, kTexturesPerMaterial> bindings{};
    for (uint32_t i = 0; i < kTexturesPerMaterial; i++) {
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

VkDescriptorSet MaterialDescriptors::get_set(MaterialId mat) const {
    assert(mat < m_sets.size() && "MaterialId out of range");
    return m_sets[mat];
}

void MaterialDescriptors::rebuild(VkDevice device, TextureManager& textures) {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_sets.clear();

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAT_COUNT * kTexturesPerMaterial;  // albedo + normal + roughness per material

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAT_COUNT;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    std::vector<VkDescriptorSetLayout> layouts(MAT_COUNT, m_setLayout);
    VkDescriptorSetAllocateInfo        allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_pool;
    allocInfo.descriptorSetCount = MAT_COUNT;
    allocInfo.pSetLayouts        = layouts.data();

    m_sets.resize(MAT_COUNT);
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()));

    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet>  writes;
    imageInfos.reserve(MAT_COUNT * kTexturesPerMaterial);
    writes.reserve(MAT_COUNT * kTexturesPerMaterial);

    for (uint32_t i = 0; i < MAT_COUNT; i++) {
        for (uint32_t b = 0; b < kTexturesPerMaterial; b++) {
            std::string texName = kMaterialNames[i];
            texName += kSuffixes[b];
            Texture* tex = textures.get_texture(texName);

            // Fall back to the 1×1 white "default" if a normal/roughness map is missing.
            if (!tex) {
                tex = textures.get_texture("default");
            }
            if (!tex) {
                throw std::runtime_error("MaterialDescriptors::rebuild: missing texture '" + texName + "'");
            }

            VkDescriptorImageInfo imgInfo{};
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgInfo.imageView   = tex->view;
            imgInfo.sampler     = textures.get_sampler();
            imageInfos.push_back(imgInfo);

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = m_sets[i];
            w.dstBinding      = b;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.descriptorCount = 1;
            w.pImageInfo      = &imageInfos.back();
            writes.push_back(w);
        }
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

}  
