#pragma once

#include "../../scene/SceneTypes.h"
#include "../../utils/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace swish {

struct RendererServices;

// GPU layout UBO (std140): mirrors set 1, binding 0 in windshield_rain.frag.
struct WindshieldRainUBO {
    Vec4 flowAndTime;    // xy = screen-space flow direction (normalized),
                         // z  = speed factor [0,1], w = accumulated time
    Vec4 params;         // x = wetness [0,1], y = rain intensity [0,1],
                         // z = drop density (cells across glass), w = refraction strength
    Vec4 screenAndRefr;  // x = framebuffer width (px), y = height (px),
                         // z = max blur LOD (reserved), w = Fresnel rim gain
    Vec4 wiperState;     // x = blade angle (rad), y = phase [0,2π],
                         // z = enabled (0/1), w = sweep speed (rad/s)
};

// ══════════════════════════════════════════════════════════════════════
// WindshieldRainPass — realistic rain on the windshield via screen-space
// refraction + a persistent wetness map.
//
//   • Wetness map: a fullscreen pass maintains an R16F screen-space "how wet"
//     field. Rain adds wetness, it advects along the flow (down at rest, up at
//     speed), the wiper subtracts along its swept band (so cleared glass stays
//     clear and re-wets over time), and it slowly evaporates. Ping-pong A/B.
//   • Drops: a layered Voronoi height field (glass space) → normal (finite
//     differences) → refracts a snapshot of the HDR scene (drops are lenses,
//     not glowing geometry). Gated by the sampled wetness map and confined to
//     the forward-facing windshield pane.
//
// Per frame the Renderer must:
//   1. update(...)                — advance time/wiper, write UBO + wet params
//   2. record_wetness_update(...) — step the wetness map (after glass)
//   3. record_scene_snapshot(...) — blit HDR → refraction source
//   4. record_draws(...)          — draw the windshield with refractive drops
// ══════════════════════════════════════════════════════════════════════
class WindshieldRainPass {
public:
    WindshieldRainPass()  = default;
    ~WindshieldRainPass() = default;

    void init(const RendererServices& s,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
              VkExtent2D extent,
              VkDescriptorSetLayout cameraSetLayout);

    // Update per-frame state.
    //   screenFlowDir — car forward dir projected to screen XY (normalized).
    //   speedFactor   — car speed / kMaxSpeed, clamped to [0,1].
    //   wetness       — from RainSystem::get_wetness().
    //   intensity     — from RainSystem::get_intensity().
    //   wiperEnabled  — V-key toggle; runs the continuous wiper sweep.
    void update(uint32_t frameIndex, float deltaTime,
                Vec2 screenFlowDir, float speedFactor,
                float wetness, float intensity, bool wiperEnabled);

    // Step the persistent wetness map (fullscreen pass): rain + advection +
    // wiper + evaporation. Call AFTER the glass pass, BEFORE the snapshot/draw.
    void record_wetness_update(VkCommandBuffer cmd);

    // Snapshot the HDR scene into the per-frame refraction-source image so the
    // fragment shader can refract it. Records layout transitions + a copy into
    // the command buffer. Call while hdrImage is COLOR_ATTACHMENT_OPTIMAL
    // (restored to it on return).
    void record_scene_snapshot(VkCommandBuffer cmd, uint32_t frameIndex,
                               VkImage hdrImage, VkExtent2D extent);

    // Draw windshield draw calls using the shared car VBO/IBO.
    // Camera set 0 must already be bound on m_pipeLayout before this call.
    void record_draws(VkCommandBuffer cmd,
                      uint32_t frameIndex,
                      VkBuffer carVBO,
                      VkBuffer carIBO,
                      const std::vector<DrawCall>& windshieldDCs) const;

    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                  VkExtent2D extent,
                  VkDevice device);

    void cleanup(VkDevice device);

    VkPipelineLayout get_pipeline_layout() const { return m_pipeLayout; }

private:
    void createRenderPass(VkDevice device, VkFormat depthFormat);
    void createFramebuffers(VkDevice device,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyFramebuffers(VkDevice device);
    void createUBOs(const RendererServices& s);
    void createRefractionResources(VkDevice device, VkPhysicalDevice physicalDevice, VkExtent2D extent);
    void destroyRefractionResources(VkDevice device);
    void createWetnessResources(VkDevice device, VkPhysicalDevice physicalDevice, VkExtent2D extent);
    void destroyWetnessResources(VkDevice device);
    void clearWetnessImages(VkDevice device);  // one-time zero of both maps
    void createDescriptors(VkDevice device);
    void writeDescriptors(VkDevice device);
    void createPipeline(VkDevice device, VkDescriptorSetLayout cameraSetLayout);
    void createWetnessPipeline(VkDevice device);

    VkRenderPass                                    m_renderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    std::array<VkBuffer,       MAX_FRAMES_IN_FLIGHT> m_ubos{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_uboMemories{};
    std::array<void*,          MAX_FRAMES_IN_FLIGHT> m_uboMapped{};

    // Refraction source: per-frame snapshot of the HDR scene (sampled by frag).
    std::array<VkImage,        MAX_FRAMES_IN_FLIGHT> m_refrImages{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_refrMemories{};
    std::array<VkImageView,    MAX_FRAMES_IN_FLIGHT> m_refrViews{};
    VkSampler                                        m_refrSampler = VK_NULL_HANDLE;

    // Persistent wetness map (ping-pong: [0] = history/read, [1] = current/write).
    std::array<VkImage,        2> m_wetImages{};
    std::array<VkDeviceMemory, 2> m_wetMemories{};
    std::array<VkImageView,    2> m_wetViews{};
    VkFramebuffer         m_wetFramebuffer = VK_NULL_HANDLE;  // targets m_wetImages[1]
    VkRenderPass          m_wetRenderPass  = VK_NULL_HANDLE;
    VkPipeline            m_wetPipeline    = VK_NULL_HANDLE;
    VkPipelineLayout      m_wetPipeLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_wetSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSet       m_wetDescSet     = VK_NULL_HANDLE;  // reads m_wetImages[0]

    VkDescriptorPool                                  m_descPool  = VK_NULL_HANDLE;
    VkDescriptorSetLayout                             m_ownLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descSets{};

    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_queue          = VK_NULL_HANDLE;
    VkExtent2D       m_extent         = {};
    VkFormat         m_depthFormat    = VK_FORMAT_UNDEFINED;
    VkFormat         m_refrFormat     = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat         m_wetFormat      = VK_FORMAT_R16_SFLOAT;

    float m_time         = 0.0f;
    float m_wiperPhase   = 0.0f;
    bool  m_wiperEnabled = false;

    // Wetness-pass push-constant params (filled by update(), used by record_wetness_update()).
    Vec2  m_waterFlow   = Vec2(0.0f, 1.0f);
    float m_curIntensity = 0.0f;
    float m_dt           = 0.0f;
    float m_wiperAngle   = 0.0f;
    float m_advect       = 0.0f;
};

}  // namespace swish
