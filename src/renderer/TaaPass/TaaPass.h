#pragma once

// TaaPass — temporal anti-aliasing resolve + per-pixel motion blur (debug-only).
//
// A self-contained pass (like IBLManager / SpraySystem) that is instantiated ONLY
// under SWISH_DEBUG_UI, so a release build never compiles it into the pipeline and
// stays on SSAA (byte-identical). It reprojects the previous frame via depth,
// neighborhood-clamps + blends the history, and copies the resolved result back
// INTO the HDR image — so the existing bloom/composite chain is untouched. Pair it
// with per-frame sub-pixel camera jitter (Camera::set_jitter) for supersampled TAA.

#ifdef SWISH_DEBUG_UI

#include "../../utils/Types.h"
#include "../GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace swish {

struct RendererServices;

// Live-tunable TAA knobs (mirrors the DebugParams TAA block).
struct TaaParams {
    bool  enabled        = false;  // off by default → identical to today until toggled
    float historyBlend   = 0.9f;   // weight of reprojected history [0,1]
    bool  motionBlur     = false;
    float motionBlurScale = 1.0f;  // velocity smear strength
};

class TaaPass {
public:
    TaaPass()  = default;
    ~TaaPass() = default;

    void init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent);

    // Resolve this frame: read HDR (`hdrImage`/its view) + depth + history, write the
    // resolved output, copy it back into `hdrImage`. Leaves HDR in COLOR_ATTACHMENT
    // (as the forward passes did) so the downstream barrier/bloom logic is unchanged.
    // prevViewProj = last frame's UN-jittered world→clip; curInvViewProj = this frame's
    // clip→world (jittered, matching the depth). Records its own barriers + render pass.
    void record(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const TaaParams& params, const Mat4& prevViewProj,
                const Mat4& curInvViewProj);

    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);

    void cleanup(VkDevice device);

private:
    // Push block (matches taa.frag).
    struct Push {
        Mat4 prevViewProj;
        Mat4 invViewProj;
        Vec4 params;  // x = historyBlend, y = motionBlurScale, zw = texel size
    };

    void createImages(const RendererServices& s);
    void createRenderPass(VkDevice device);
    void createFramebuffers(VkDevice device);
    void destroyImagesAndFramebuffers(VkDevice device);
    void createDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                           const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void writeDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                          const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void createPipeline(VkDevice device);
    void primeHistory(const RendererServices& s);

    VmaAllocator  m_allocator     = nullptr;
    VkDevice      m_device        = VK_NULL_HANDLE;
    VkCommandPool m_commandPool   = VK_NULL_HANDLE;
    VkQueue       m_graphicsQueue = VK_NULL_HANDLE;

    // Ping-pong history/output (read taa[1-frame], write taa[frame]).
    std::array<GpuImage, MAX_FRAMES_IN_FLIGHT>      m_images{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>   m_views{};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    VkRenderPass          m_renderPass = VK_NULL_HANDLE;
    VkSampler             m_sampler    = VK_NULL_HANDLE;  // linear clamp
    VkDescriptorPool      m_pool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout  = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_sets{};
    VkPipelineLayout      m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent = {};
    VkFormat   m_format = VK_FORMAT_R16G16B16A16_SFLOAT;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
