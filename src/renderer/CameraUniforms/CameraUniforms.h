#pragma once

#include "renderer/GpuResource/GpuResource.h"
#include "scene/SceneTypes.h"
#include "utils/light/Photometry.h"

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

    // Car spot lights (headlights / tail / brake), replaced every frame by the App.
    // Uploaded into LightsUBO::spotLights in debug builds only (release layout is
    // unchanged; see SceneTypes.h). Empty = no spot lights.
    void set_spot_lights(const std::vector<SpotLightDesc>& spots) { m_spots = spots; }

    // Street lamps and other world spot lights (lighting L6). Unlike the car's they are many, so update()
    // uploads the car's lights first and fills what is left of MAX_SPOT_LIGHTS with the nearest of these.
    void     set_world_spot_lights(const std::vector<SpotLightDesc>& spots) { m_worldSpots = spots; }
    uint32_t spots_uploaded() const { return m_spotsUploaded; }
    // Rectangle area lights (the nearest MAX_RECT_LIGHTS are uploaded) and the car's box for their grounding
    // shadow. Debug shaders only.
    void     set_rect_lights(const std::vector<RectLightDesc>& rects) { m_rects = rects; }
    void     set_car_occluder(const CarOccluder& box) { m_carBox = box; }
    uint32_t spots_total() const { return static_cast<uint32_t>(m_spots.size() + m_worldSpots.size()); }

    // Patches camPos.w in the already-mapped camera UBO for this frame.
    // Call after update() — does not disturb xyz.
    void set_wetness(uint32_t frameIndex, float wetness);

    // Weather preset written into the camera UBO by update() each frame:
    // sun direction (xyz normalized, w = sun illuminance in lux), sun tint (rgb, a ambient gain),
    // a clarity scalar (0 = overcast, 1 = clear day), the sky scale in cd/m² applied to the baked
    // environment cubes, and the pre-exposure that brings lux/nits into the half-float HDR target
    // (see util::light::kPreExposure). Set once on a toggle.
    void set_weather(const Vec4& sunDir, const Vec4& sunColor, float clarity, float skyNits, float preExposure);
    // Per-frame override of the pre-exposure alone (auto-exposure publishes it; see FrameState::preExposure).
    void set_pre_exposure(float preExposure) { m_preExposure = preExposure; }

    // Per-cascade sun light-space view*projection + split far-distances (view
    // space, +Z forward), recomputed each frame by the Renderer. Written into the
    // camera UBO by update() and consumed by lighting.frag for CSM shadow lookup.
    void set_cascades(const std::array<Mat4, NUM_CASCADES>& vps, const Vec3& splits) {
        m_cascadeVP     = vps;
        m_cascadeSplits = Vec4(splits, 0.0f);
    }

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

    std::vector<LightDesc>     m_lights;
    bool                       m_lightsDirty = false;
    std::vector<SpotLightDesc> m_spots;
    std::vector<SpotLightDesc> m_worldSpots;
    std::vector<RectLightDesc> m_rects;
    CarOccluder                m_carBox;
    uint32_t                   m_spotsUploaded = 0;

    // Weather (overcast default — matches the original hardcoded sun). Renderer
    // swaps these via set_weather() when the clear-day preset is toggled.
    Vec4  m_sunDir      = Vec4(glm::normalize(Vec3(0.3f, 0.6f, 0.15f)), util::light::kLegacyUnitLux);
    Vec4  m_sunColor    = Vec4(1.0f, 0.95f, 0.85f, 0.22f);
    float m_clarity     = 0.0f;
    float m_skyNits     = util::light::kLegacyUnitLux;  // scale on the baked sky cubes (cd/m²)
    float m_preExposure = util::light::kPreExposure;

    // Per-cascade light-space view*proj + split distances (identity/zero until the
    // Renderer sets them each frame). Written into the CameraUBO tail by update().
    std::array<Mat4, NUM_CASCADES> m_cascadeVP{};
    Vec4                           m_cascadeSplits{0.0f};
};

}  // namespace swish
