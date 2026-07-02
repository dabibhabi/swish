#include "SceneParamsUniform.h"

#ifdef SWISH_DEBUG_UI

#include "../utils/VulkanCheck.h"

#include <cstring>

namespace swish {

void SceneParamsUniform::init(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight) {
    m_frames = framesInFlight;
    createLayout(device);
    createBuffers(allocator);
    createDescriptors(device);
}

void SceneParamsUniform::cleanup(VkDevice device) {
    // GpuBuffer is RAII (VMA): clearing frees the sub-allocations + unmaps.
    m_buffers.clear();
    m_frames = 0;

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
}

void SceneParamsUniform::update(uint32_t frameIndex, const DebugParams& p) {
    SceneParamsUBO ubo{};
    ubo.skyHorizonOvercast = glm::vec4(p.skyHorizonOvercast, 0.0f);
    ubo.skyHorizonClear    = glm::vec4(p.skyHorizonClear, 0.0f);
    ubo.skyZenithOvercast  = glm::vec4(p.skyZenithOvercast, 0.0f);
    ubo.skyZenithClear     = glm::vec4(p.skyZenithClear, 0.0f);
    ubo.sunDisc            = glm::vec4(p.sunDiscExpMin, p.sunDiscExpMax, p.sunDiscStrMin, p.sunDiscStrMax);
    ubo.fogColor           = glm::vec4(p.fogColor, 0.0f);
    ubo.fogParams          = glm::vec4(p.fogDist63, p.fogMax, p.envGlossExp, 0.0f);
    ubo.shadowParams       = glm::vec4(p.shadowBias, p.shadowFloor, 0.0f, 0.0f);
    ubo.wetParams          = glm::vec4(p.wetPorosity, p.wetRoughness, 0.0f, 0.0f);
    std::memcpy(m_buffers[frameIndex].mapped(), &ubo, sizeof(ubo));
}

void SceneParamsUniform::createLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &m_setLayout));
}

void SceneParamsUniform::createBuffers(VmaAllocator allocator) {
    m_buffers.resize(m_frames);
    for (uint32_t i = 0; i < m_frames; i++) {
        m_buffers[i] = gpu::hostVisibleBuffer(allocator, sizeof(SceneParamsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
}

void SceneParamsUniform::createDescriptors(VkDevice device) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = m_frames;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = m_frames;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool));

    std::vector<VkDescriptorSetLayout> layouts(m_frames, m_setLayout);
    VkDescriptorSetAllocateInfo        allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_pool;
    allocInfo.descriptorSetCount = m_frames;
    allocInfo.pSetLayouts        = layouts.data();

    m_sets.resize(m_frames);
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()));

    for (uint32_t i = 0; i < m_frames; i++) {
        VkDescriptorBufferInfo bufInfo{m_buffers[i].handle(), 0, sizeof(SceneParamsUBO)};

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_sets[i];
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &bufInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
