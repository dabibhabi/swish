#pragma once

#include "../../scene/SceneTypes.h"
#include "../GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace swish {

class Camera;

// Owns the per-frame camera + lights uniform buffers, the descriptor set
// layout (set 0 in scene + lighting pipelines), the pool, and the descriptor
// sets that bind those buffers. Renderer-facing surface is intentionally
// narrow: a layout, a per-frame descriptor set, and an update entry point.
class CameraUniforms {
public:
    void init(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, uint32_t framesInFlight);
    void cleanup(VkDevice device);

    // Write this frame's camera matrices + sun + active lights into the
    // mapped UBO memory. Must be called before binding the set for the frame.
    void update(uint32_t frameIndex, const Camera& camera);

    void set_lights(const std::vector<LightDesc>& lights) {
        m_lights      = lights;
        m_lightsDirty = true;
    }
    bool has_lights() const { return !m_lights.empty(); }

    // Patches camPos.w in the already-mapped camera UBO for this frame.
    // Call after update() — does not disturb xyz.
    void set_wetness(uint32_t frameIndex, float wetness);

    // Weather preset written into the camera UBO by update() each frame:
    // sun direction (xyz normalized, w intensity), sun color (rgb, a ambient),
    // and a clarity scalar (0 = overcast, 1 = clear day). Set once on a toggle.
    void set_weather(const Vec4& sunDir, const Vec4& sunColor, float clarity);

    // Sun light-space view*projection matrix, recomputed each frame by the
    // Renderer from the camera position + sun direction. Written into the
    // camera UBO by update() and consumed by lighting.frag for shadow lookup.
    void set_light_matrix(const Mat4& lightViewProj) { m_lightViewProj = lightViewProj; }

    VkDescriptorSetLayout get_layout() const { return m_setLayout; }
    VkDescriptorSet       get_set(uint32_t frameIndex) const { return m_sets[frameIndex]; }

private:
    void createLayout(VkDevice device);
    void createBuffers(VmaAllocator allocator);
    void createDescriptors(VkDevice device);

    uint32_t m_frames = 0;

    VkDescriptorSetLayout        m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool             m_pool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;

    // RAII, persistently-mapped host-visible UBOs (VMA). Access the pointer via
    // .mapped(); the sub-allocation is freed when the vector is cleared/destroyed.
    std::vector<GpuBuffer> m_cameraBuffers;
    std::vector<GpuBuffer> m_lightsBuffers;

    std::vector<LightDesc> m_lights;
    bool                   m_lightsDirty = false;

    // Weather (overcast default — matches the original hardcoded sun). Renderer
    // swaps these via set_weather() when the clear-day preset is toggled.
    Vec4  m_sunDir   = Vec4(glm::normalize(Vec3(0.3f, 0.6f, 0.15f)), 1.0f);
    Vec4  m_sunColor = Vec4(1.0f, 0.95f, 0.85f, 0.22f);
    float m_clarity  = 0.0f;

    // Sun light-space view*projection (identity until the Renderer sets it each
    // frame). Written into CameraUBO::lightViewProj by update().
    Mat4  m_lightViewProj = Mat4(1.0f);
};

}  // namespace swish
