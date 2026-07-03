#pragma once

#include "../../utils/Types.h"
#include "../GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace swish {

struct RendererServices;

// Particle count. MUST be a multiple of the compute local size (64) so the single
// dispatch covers exactly the buffer with no out-of-range invocations.
static constexpr uint32_t kSprayMaxParticles = 4096;

// One simulated particle (std430; mirrors spray_sim.comp / spray.vert).
struct SprayParticle {
    Vec4 posLife;  // xyz = world position (WU), w = life [0,1] (0 = dead)
    Vec4 velSize;  // xyz = velocity (WU/s), w = billboard size (WU)
};

// Per-frame simulation inputs (std140; mirrors the `Sim` UBO in spray_sim.comp).
struct SpraySimUBO {
    Vec4 spawnPosDt;  // xyz = rear-axle spawn centre (WU), w = dt (s)
    Vec4 carVel;      // xyz = car velocity (WU/s), w = emit probability [0,1]
    Vec4 params;      // x = lifetime (s), y = size base (WU), z = gravity (WU/s²), w = drag (1/s)
    Vec4 params2;     // x = lateral spread (WU), y = frame seed, z = up speed (WU/s), w = back speed (WU/s)
};

// Live-tunable spray knobs (mirrors the DebugParams spray block; a release build
// passes a default-constructed instance — with emission gated by wetness, the dry
// release scene emits nothing, so the pass is a no-op there).
struct SprayParams {
    bool  enabled  = true;
    float density  = 0.35f;   // per-dead-particle respawn chance at full wetness × speed
    float lifetime = 1.4f;    // particle lifetime (s)
    float size     = 700.0f;  // billboard size (WU ≈ 0.7 m)
    float opacity  = 0.12f;   // additive strength
};

// ══════════════════════════════════════════════════════════════════════
// SpraySystem — GPU particle road-spray/mist kicked up behind the car.
//
// The renderer's first compute pipeline: a single shared particle SSBO is advanced
// in place by spray_sim.comp each frame (compute dispatch, outside any render pass),
// then drawn as additive camera-facing billboards in a forward pass onto the HDR
// buffer (modelled on RainSystem). Emission is gated by wetness × speed, so it only
// appears on a wet road at speed and is invisible in the dry (release) scene.
// ══════════════════════════════════════════════════════════════════════
class SpraySystem {
public:
    SpraySystem()  = default;
    ~SpraySystem() = default;

    void init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
              VkDescriptorSetLayout cameraSetLayout);

    // Upload this frame's sim UBO + cache the record-time activity flag. spawnCentre
    // is the rear-axle world position (WU); carVelocity is WU/s; wetness is [0,1].
    void update(uint32_t frameIndex, float deltaTime, Vec3 spawnCentre, Vec3 carVelocity, float wetness,
                const SprayParams& params);

    // Advance the particles. MUST be recorded OUTSIDE a render pass. Inserts the
    // compute-write → vertex-read buffer barrier so the draw sees the new positions.
    void record_compute(VkCommandBuffer cmd, uint32_t frameIndex) const;

    // Draw the additive billboards (own render pass). Set 0 (camera) must already be
    // bound by the caller, exactly like RainSystem::record_draws.
    void record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const;

    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);

    void cleanup(VkDevice device);

    VkPipelineLayout get_pipeline_layout() const { return m_drawPipeLayout; }

private:
    void createParticleBuffer(const RendererServices& s);
    void createSimUBOs(const RendererServices& s);
    void createComputeDescriptors(VkDevice device);
    void createComputePipeline(VkDevice device);
    void createRenderPass(VkDevice device);
    void createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyFramebuffers(VkDevice device);
    void createDrawDescriptors(VkDevice device);
    void createDrawPipeline(VkDevice device, VkDescriptorSetLayout cameraSetLayout);

    // ── Particle SSBO (single, shared, continuously simulated in place) ──
    GpuBuffer m_particleBuffer;

    // ── Per-frame sim UBOs (host-visible, persistently mapped) ──────────
    std::array<GpuBuffer, MAX_FRAMES_IN_FLIGHT> m_simUBOs{};

    // ── Compute ─────────────────────────────────────────────────────────
    VkDescriptorPool                                  m_computePool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout                             m_computeSetLayout  = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_computeSets{};
    VkPipelineLayout                                  m_computePipeLayout = VK_NULL_HANDLE;
    VkPipeline                                        m_computePipeline   = VK_NULL_HANDLE;

    // ── Draw (graphics forward pass) ────────────────────────────────────
    VkRenderPass                                    m_renderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};
    VkDescriptorPool                                m_drawPool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout                           m_drawSetLayout  = VK_NULL_HANDLE;
    VkDescriptorSet                                 m_drawSet        = VK_NULL_HANDLE;  // single (SSBO is shared)
    VkPipelineLayout                                m_drawPipeLayout = VK_NULL_HANDLE;
    VkPipeline                                      m_drawPipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent      = {};
    VkFormat   m_depthFormat = VK_FORMAT_UNDEFINED;

    // ── Record-time state (set in update) ───────────────────────────────
    bool  m_active    = false;  // params.enabled && wetness > threshold
    float m_opacity   = 0.12f;
    float m_frameSeed = 0.0f;
};

}  // namespace swish
