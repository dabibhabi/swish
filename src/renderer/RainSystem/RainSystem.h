#pragma once

#include "../../utils/Types.h"

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
class RainSystem {
public:
    RainSystem()  = default;
    ~RainSystem() = default;

    // hdrViews: per-frame HDR color image views from PostProcessManager.
    // depthViews: per-frame HDR depth views — used for depth testing against scene geometry
    //             so rain does not appear inside the car cabin.
    void init(const RendererServices& s,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
              VkExtent2D extent,
              VkDescriptorSetLayout cameraSetLayout);

    // Advance the rain simulation and upload per-frame UBO.
    // Call once per frame before record_draws.
    void update(uint32_t frameIndex, float deltaTime, float intensity, Vec3 wind);

    // Record all rain draw calls into cmd (inside its own render pass).
    void record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const;

    // Rebuild framebuffers when the swapchain (and HDR images) are recreated.
    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                  VkExtent2D extent,
                  VkDevice device);

    void cleanup(VkDevice device);

    float            get_wetness()        const { return m_wetness; }
    float            get_intensity()      const { return m_intensity; }
    VkPipelineLayout get_pipeline_layout() const { return m_pipeLayout; }

private:
    void createRenderPass(VkDevice device);
    void createFramebuffers(VkDevice device,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyFramebuffers(VkDevice device);
    void createGeometry(const RendererServices& s);
    void createInstanceBuffer(const RendererServices& s);
    void createRainUBOs(const RendererServices& s);
    void createDescriptors(VkDevice device, VkDescriptorSetLayout cameraSetLayout);
    void createPipeline(VkDevice device, VkDescriptorSetLayout cameraSetLayout);

    // ── Static geometry ────────────────────────────────────────────
    VkBuffer       m_quadVBO       = VK_NULL_HANDLE;
    VkDeviceMemory m_quadVBOMemory = VK_NULL_HANDLE;
    VkBuffer       m_quadIBO       = VK_NULL_HANDLE;
    VkDeviceMemory m_quadIBOMemory = VK_NULL_HANDLE;

    // ── Instance buffer (static seeds, never updated after init) ──
    VkBuffer       m_instanceBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

    // ── Per-frame rain UBOs ────────────────────────────────────────
    std::array<VkBuffer,       MAX_FRAMES_IN_FLIGHT> m_rainUBOs{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_rainUBOMemories{};
    std::array<void*,          MAX_FRAMES_IN_FLIGHT> m_rainUBOMapped{};

    // ── Render pass + per-frame framebuffers ───────────────────────
    VkRenderPass                                    m_renderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    // ── Descriptors (set 1: rain UBO) ──────────────────────────────
    VkDescriptorPool                                    m_descPool   = VK_NULL_HANDLE;
    VkDescriptorSetLayout                               m_rainLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT>   m_descSets{};

    // ── Pipeline ───────────────────────────────────────────────────
    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent      = {};
    VkFormat   m_depthFormat = VK_FORMAT_UNDEFINED;

    // ── Simulation state ───────────────────────────────────────────
    float m_time      = 0.0f;
    float m_intensity = 0.0f;
    float m_wetness   = 0.0f;
};

}  // namespace swish
