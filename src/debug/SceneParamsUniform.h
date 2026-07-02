#pragma once

#ifdef SWISH_DEBUG_UI

#include "../renderer/GpuResource/GpuResource.h"
#include "DebugParams.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// SceneParamsUBO — std140 mirror of the "look" constants promoted out of
// lighting.frag (set 3, binding 0). Every member is a vec4 so each field is
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
    glm::vec4 shadowParams;        // x=bias y=floor (zw unused)
    glm::vec4 wetParams;           // x=porosity y=roughnessCollapse (zw unused)
};
static_assert(sizeof(SceneParamsUBO) == 9 * 16, "SceneParamsUBO must stay 16-aligned vec4 rows");

// Owns the debug-only scene-params UBO bound as set 3 on the deferred-lighting
// pipeline: one persistently-mapped host-visible buffer + descriptor set per
// frame in flight, plus the shared layout. Compiled only under SWISH_DEBUG_UI —
// a release build keeps lighting.frag's literal constants and never sees set 3.
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
