#include "CameraUniforms.h"

#include "../../scene/Camera/Camera.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../ResourceManager/ResourceManager.h"

#include <array>
#include <cstring>

namespace swish {

void CameraUniforms::init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight) {
    m_frames = framesInFlight;
    createLayout(device);
    createBuffers(device, physicalDevice);
    createDescriptors(device);
}

void CameraUniforms::cleanup(VkDevice device) {
    for (uint32_t i = 0; i < m_frames; i++) {
        if (m_cameraMapped.size() > i && m_cameraMapped[i] != nullptr) {
            vkUnmapMemory(device, m_cameraMemory[i]);
            m_cameraMapped[i] = nullptr;
        }
        if (m_cameraBuffers.size() > i && m_cameraBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_cameraBuffers[i], nullptr);
            m_cameraBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_cameraMemory.size() > i && m_cameraMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_cameraMemory[i], nullptr);
            m_cameraMemory[i] = VK_NULL_HANDLE;
        }

        if (m_lightsMapped.size() > i && m_lightsMapped[i] != nullptr) {
            vkUnmapMemory(device, m_lightsMemory[i]);
            m_lightsMapped[i] = nullptr;
        }
        if (m_lightsBuffers.size() > i && m_lightsBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_lightsBuffers[i], nullptr);
            m_lightsBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_lightsMemory.size() > i && m_lightsMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_lightsMemory[i], nullptr);
            m_lightsMemory[i] = VK_NULL_HANDLE;
        }
    }

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
}

void CameraUniforms::update(uint32_t frameIndex, const Camera& camera) {
    CameraUBO ubo{};
    ubo.view     = camera.get_view_matrix();
    ubo.proj     = camera.get_projection_matrix();
    ubo.camPos   = Vec4(camera.get_position(), 1.0f);
    ubo.sunDir   = Vec4(glm::normalize(Vec3(0.3f, 0.6f, 0.15f)), 1.0f);
    ubo.sunColor = Vec4(1.0f, 0.95f, 0.85f, 0.30f);
    std::memcpy(m_cameraMapped[frameIndex], &ubo, sizeof(ubo));

    LightsUBO lightsUbo{};
    uint32_t  count = std::min(static_cast<uint32_t>(m_lights.size()), MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < count; i++) {
        lightsUbo.pointLights[i].positionRadius = Vec4(m_lights[i].position, m_lights[i].radius);
        lightsUbo.pointLights[i].colorIntensity = Vec4(m_lights[i].color, m_lights[i].intensity);
    }
    lightsUbo.numPointLights = glm::uvec4(count, 0, 0, 0);
    std::memcpy(m_lightsMapped[frameIndex], &lightsUbo, sizeof(lightsUbo));
}

void CameraUniforms::createLayout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings    = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &m_setLayout));
}

void CameraUniforms::createBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_cameraBuffers.resize(m_frames);
    m_cameraMemory.resize(m_frames);
    m_cameraMapped.resize(m_frames);
    m_lightsBuffers.resize(m_frames);
    m_lightsMemory.resize(m_frames);
    m_lightsMapped.resize(m_frames);

    const VkDeviceSize          cameraSize = sizeof(CameraUBO);
    const VkDeviceSize          lightsSize = sizeof(LightsUBO);
    const VkMemoryPropertyFlags hostFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t i = 0; i < m_frames; i++) {
        ResourceManager::createBuffer(device, physicalDevice, cameraSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostFlags,
                                      m_cameraBuffers[i], m_cameraMemory[i]);
        vkMapMemory(device, m_cameraMemory[i], 0, cameraSize, 0, &m_cameraMapped[i]);

        ResourceManager::createBuffer(device, physicalDevice, lightsSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostFlags,
                                      m_lightsBuffers[i], m_lightsMemory[i]);
        vkMapMemory(device, m_lightsMemory[i], 0, lightsSize, 0, &m_lightsMapped[i]);
    }
}

void CameraUniforms::createDescriptors(VkDevice device) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = m_frames * 2;  // camera + lights per frame

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
        VkDescriptorBufferInfo cameraInfo{m_cameraBuffers[i], 0, sizeof(CameraUBO)};
        VkDescriptorBufferInfo lightsInfo{m_lightsBuffers[i], 0, sizeof(LightsUBO)};

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_sets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &cameraInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_sets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &lightsInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

}  // namespace swish
