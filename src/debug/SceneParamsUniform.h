#pragma once

#ifdef SWISH_DEBUG_UI

#include "DebugParams.h"
#include "renderer/GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// SceneParamsUBO — std140 mirror of the "look" constants promoted out of
// lighting.frag (set 4, binding 0). Every member is a vec4 so each field is
// 16-byte aligned: this sidesteps std140 vec3 padding entirely, which is the
// safe layout on MoltenVK (the portability subset is unforgiving about it).
// The unused .zw / .w lanes are pure padding — free to repurpose later.
// ══════════════════════════════════════════════════════════════════════
struct SceneParamsUBO {
    glm::vec4 skyHorizonOvercast;  // rgb (w unused)
    glm::vec4 skyHorizonClear;     // rgb (w unused)
    glm::vec4 skyZenithOvercast;   // rgb (w unused)
    glm::vec4 skyZenithClear;      // rgb (w unused)
    glm::vec4 sunDisc;             // x=expMin y=expMax z=strMin w=strMax
    glm::vec4 fogColor;            // rgb (w unused)
    glm::vec4 fogParams;           // x=dist63 y=max z=envGlossExp (w unused)
    glm::vec4 shadowParams;        // x=bias y=floor z=light size w=rain clock s (rain P4)
    glm::vec4 wetParams;           // x=porosity y=roughnessCollapse z=puddleCoverage w=rut water (rain P3)
    glm::vec4 iblParams;           // x=diffuse y=specular intensity z=ripple strength w=ripple cell WU (rain P4)
    glm::vec4 paintParams;         // x=clearcoat roughness y=flake roughness z=rut water roughness (w unused)
    // Lighting lab roof volumes (util/light/SkyVisibility.h), appended so the earlier rows keep their offsets.
    glm::vec4 roofMin[12];   // xyz = box min (WU), w = fade distance (WU)
    glm::vec4 roofMax[12];   // xyz = box max (WU), w = open sides (0 = X ends, 2 = Z ends, 3 = all four, -1 = closed)
    glm::vec4 roofInfo[12];  // x = visibility left deep inside
    glm::vec4 roofCount;     // x = volumes in use
    // Procedural sky (lighting L2): the inputs of atmSky for the visible sky pixels (util/light/Atmosphere.h).
    glm::vec4 atmoSun;     // xyz = sun direction, w = lux at the top of the atmosphere
    glm::vec4 atmoMoon;    // xyz = moon direction, w = its lux at the top of the atmosphere
    glm::vec4 atmoParams;  // x = turbidity, y = cloud blend, z = airglow nits, w = overcast zenith nits
    glm::vec4 atmoFlags;   // x = 1 when the procedural sky is active, y = 1 to draw stars, z = rain-sky fade 0..1
    // Local-light and material switches (lighting L3 to L8), all debug-only.
    glm::vec4 lightFlags;  // x = cascade blend band (0..1 of a cascade), y = energy compensation on, z = retro gain, w
                           // = sun angular radius (rad)
};
static_assert(sizeof(SceneParamsUBO) == (11 + 3 * 12 + 1 + 5) * 16, "SceneParamsUBO must stay 16-aligned vec4 rows");
static_assert(util::light::kMaxRoofVolumes == 12, "the roof rows above and in lighting.frag hold 12 volumes");

// Owns the debug-only scene-params UBO bound as set 4 on the deferred-lighting
// pipeline: one persistently-mapped host-visible buffer + descriptor set per
// frame in flight, plus the shared layout. Compiled only under SWISH_DEBUG_UI —
// a release build keeps lighting.frag's literal constants and never sees set 4.
class SceneParamsUniform {
public:
    void init(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight);
    void cleanup(VkDevice device);

    // Repack the live DebugParams into this frame's mapped UBO. Call before the
    // lighting pass records (mirrors CameraUniforms::update timing).
    void update(uint32_t frameIndex, const DebugParams& p);

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
    std::vector<GpuBuffer>       m_buffers;  // RAII (VMA); cleared frees + unmaps
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
