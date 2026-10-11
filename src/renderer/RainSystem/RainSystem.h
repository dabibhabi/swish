#pragma once

#include "renderer/GpuResource/GpuResource.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/RendererConfig.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace swish {

struct RendererServices;

// GPU rain constants exposed for CMake configuration
static constexpr uint32_t kRainMaxDrops = 8192;

// GPU-layout UBO (std140): all Vec4, mirrors rain.vert binding 1
struct RainUBO {
    Vec4 windAndTime;  // xyz = wind velocity (WU/s), w = accumulated time
    Vec4 params;       // x = intensity [0,1], y = streakLen (WU), z = dropSpeed (WU/s), w = halfExtent (WU)
};

// Per-vertex data for the static billboard quad (4 vertices, 6 indices)
struct RainQuadVertex {
    Vec2 localPos;  // x in [-0.5, 0.5] (perp), y in [0, 1] (along fall)
    Vec2 uv;
};

// Per-instance seed, uploaded once at init and never changed
struct RainInstance {
    Vec4 seed;  // xyz = position seed in [0,1], w = size/alpha variation in [0,1]
};

// ══════════════════════════════════════════════════════════════════════
// RainSystem — GPU-instanced forward rain pass, rendered additively on
// top of the deferred HDR buffer between the lighting and bloom passes.
//
// The rain volume (a box of halfExtent WU around the camera) is animated
// entirely by the vertex shader via: pos = (seed * volume + camPos + vel * time) mod volume
// No instance-buffer updates needed after init — all motion is on GPU.
// ══════════════════════════════════════════════════════════════════════
class RainSystem final : public IRenderPass {
public:
    RainSystem()           = default;
    ~RainSystem() override = default;

    const char* name() const override { return "Rain"; }
    // Forward draw onto the lit HDR, scene depth read-only (ForwardPass.h).
    void declare(PassDeclaration& d) const override { d.read({Img::Hdr, Img::HdrDepth}).write({Img::Hdr}); }
    // HDR color + depth views come from the image registry; depth is used for depth testing against scene geometry
    // so rain does not appear inside the car cabin.
    void init(const PassInitCtx& ctx) override;
    // Rebuild framebuffers when the swapchain (and HDR images) are recreated.
    void onResize(const PassInitCtx& ctx) override;
    // Advance the rain simulation (gusting wind minus car velocity), upload the per-frame UBO and publish the
    // integrated wetness + adapted intensity in FrameState. Runs in beginFrame, not update(), so passes that record
    // EARLIER in the list but need this frame's wetness (the spray sim) can read it in their update().
    void beginFrame(const FrameUpdate& u) override;
    // Rain streaks: additive draw onto HDR in the pass's own render pass (LOAD, no barrier between lighting and rain).
    void record(const FrameCtx& ctx) override;
    void cleanup(VkDevice device) override;

private:
    void create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                VkDescriptorSetLayout cameraSetLayout);
    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);
    // Advance the simulation and upload the UBOs (the body of beginFrame).
    void step(uint32_t frameIndex, float deltaTime, float intensity, Vec3 wind);
    // Record all rain draw calls into cmd (inside its own render pass). Camera set 0 is bound by record().
    void record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const;

    void createRenderPass(VkDevice device);
    void createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyFramebuffers(VkDevice device);
    void createGeometry(const RendererServices& s);
    void createInstanceBuffer(const RendererServices& s);
    void createRainUBOs(const RendererServices& s);
    void createDescriptors(VkDevice device, VkDescriptorSetLayout cameraSetLayout);
    void createPipeline(VkDevice device, VkPipelineCache cache, VkDescriptorSetLayout cameraSetLayout);

    // ── Static geometry ────────────────────────────────────────────
    GpuBuffer m_quadVBO;  // RAII (VMA), host-visible
    GpuBuffer m_quadIBO;

    // ── Instance buffer (static seeds, never updated after init) ──
    GpuBuffer m_instanceBuffer;  // RAII (VMA), host-visible; seeded once at init

    // ── Per-frame rain UBOs ────────────────────────────────────────
    std::array<GpuBuffer, MAX_FRAMES_IN_FLIGHT> m_rainUBOs{};  // RAII, persistently mapped

    // ── Per-frame "far" parallax rain UBOs (scaled params, dimmer/farther) ──
    std::array<GpuBuffer, MAX_FRAMES_IN_FLIGHT> m_rainUBOsFar{};  // RAII, persistently mapped

    // ── Render pass + per-frame framebuffers ───────────────────────
    VkRenderPass                                    m_renderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    // ── Descriptors (set 1: rain UBO) ──────────────────────────────
    VkDescriptorPool                                  m_descPool   = VK_NULL_HANDLE;
    VkDescriptorSetLayout                             m_rainLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descSets{};
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descSetsFar{};

    // ── Pipeline ───────────────────────────────────────────────────
    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent      = {};
    VkFormat   m_depthFormat = VK_FORMAT_UNDEFINED;

    // ── Simulation state ───────────────────────────────────────────
    // Time-varying gusts (private to the pass): base horizontal wind before the gust envelope.
    static constexpr float kBaseWindX = 4500.0f;
    static constexpr float kBaseWindZ = 1200.0f;
    float                  m_windTime = 0.0f;

    float m_time      = 0.0f;
    float m_intensity = 0.0f;
    float m_wetness   = 0.0f;
    float m_streakLen = 3200.0f;  // base streak length (WU) ≈ 3.2 m (shipped default)
};

}  // namespace swish
