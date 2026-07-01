#include "CameraUniforms.h"

#include "../../scene/Camera/Camera.h"
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <numeric>

namespace swish {

void CameraUniforms::init(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                          uint32_t framesInFlight) {
    (void)physicalDevice;  // VMA holds the physical device; kept for signature symmetry
    m_frames = framesInFlight;
    createLayout(device);
    createBuffers(allocator);
    createDescriptors(device);
}

void CameraUniforms::cleanup(VkDevice device) {
    // GpuBuffer is RAII (VMA): clearing the vectors frees the buffers +
    // sub-allocations and unmaps the persistent mapping — no manual loop.
    m_cameraBuffers.clear();
    m_lightsBuffers.clear();
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

void CameraUniforms::update(uint32_t frameIndex, const Camera& camera) {
    CameraUBO ubo{};
    ubo.view     = camera.get_view_matrix();
    ubo.proj     = camera.get_projection_matrix();
    ubo.camPos   = Vec4(camera.get_position(), 1.0f);
    ubo.sunDir   = m_sunDir;
    ubo.sunColor = m_sunColor;
    ubo.weather  = Vec4(m_clarity, 0.0f, 0.0f, 0.0f);
    ubo.lightViewProj = m_lightViewProj;
    std::memcpy(m_cameraBuffers[frameIndex].mapped(), &ubo, sizeof(ubo));

    // Select the MAX_POINT_LIGHTS lights nearest the camera each frame. The
    // road may carry far more lamps than the UBO can hold, so uploading the
    // first N would leave the road dark away from the start. Recomputed every
    // frame because the nearest set changes as the car drives.
    const Vec3 camPos = camera.get_position();
    const auto total  = static_cast<uint32_t>(m_lights.size());
    const uint32_t count = std::min(total, MAX_POINT_LIGHTS);

    std::vector<uint32_t> order(total);
    std::iota(order.begin(), order.end(), 0u);

    auto distSq = [&](uint32_t i) {
        const Vec3 d = m_lights[i].position - camPos;
        return glm::dot(d, d);
    };
    std::partial_sort(order.begin(), order.begin() + count, order.end(),
                      [&](uint32_t a, uint32_t b) { return distSq(a) < distSq(b); });

    LightsUBO lightsUbo{};
    for (uint32_t i = 0; i < count; i++) {
        const LightDesc& l = m_lights[order[i]];
        lightsUbo.pointLights[i].positionRadius = Vec4(l.position, l.radius);
        lightsUbo.pointLights[i].colorIntensity = Vec4(l.color, l.intensity);
    }
    lightsUbo.numPointLights = glm::uvec4(count, 0, 0, 0);
    std::memcpy(m_lightsBuffers[frameIndex].mapped(), &lightsUbo, sizeof(lightsUbo));
    m_lightsDirty = false;
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

void CameraUniforms::createBuffers(VmaAllocator allocator) {
    m_cameraBuffers.resize(m_frames);
    m_lightsBuffers.resize(m_frames);

    const VkDeviceSize cameraSize = sizeof(CameraUBO);
    const VkDeviceSize lightsSize = sizeof(LightsUBO);

    for (uint32_t i = 0; i < m_frames; i++) {
        // Host-visible + persistently mapped (VMA) — write via .mapped().
        m_cameraBuffers[i] = gpu::hostVisibleBuffer(allocator, cameraSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_lightsBuffers[i] = gpu::hostVisibleBuffer(allocator, lightsSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
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
        VkDescriptorBufferInfo cameraInfo{m_cameraBuffers[i].handle(), 0, sizeof(CameraUBO)};
        VkDescriptorBufferInfo lightsInfo{m_lightsBuffers[i].handle(), 0, sizeof(LightsUBO)};

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

void CameraUniforms::set_wetness(uint32_t frameIndex, float wetness) {
    // camPos is the third Vec4 in CameraUBO: offset = sizeof(Mat4)*2 + sizeof(Vec4)*0
    // We write only the w component (byte offset +12 within the Vec4).
    auto* ubo     = reinterpret_cast<CameraUBO*>(m_cameraBuffers[frameIndex].mapped());
    ubo->camPos.w = wetness;
}

void CameraUniforms::set_weather(const Vec4& sunDir, const Vec4& sunColor, float clarity) {
    m_sunDir   = sunDir;
    m_sunColor = sunColor;
    m_clarity  = clarity;
}

}  // namespace swish
