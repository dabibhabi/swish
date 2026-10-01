#pragma once

// TaaPass — temporal anti-aliasing resolve + per-pixel motion blur (debug-only).
//
// A self-contained pass (like IBLManager / SpraySystem) that is instantiated ONLY
// under SWISH_DEBUG_UI, so a release build never compiles it into the pipeline and
// stays on SSAA (byte-identical). It reprojects the previous frame via depth,
// neighborhood-clamps + blends the history, and copies the resolved result back
// INTO the HDR image — so the existing bloom/composite chain is untouched. Pair it
// with per-frame sub-pixel camera jitter (Camera::set_jitter) for supersampled TAA.
//
// Two recording paths behind the device capability flags (RendererServices):
//   dynamicRendering  true  -> vkCmdBeginRendering/EndRendering, pipeline built with
//                              VkPipelineRenderingCreateInfo, NO VkRenderPass / VkFramebuffer.
//                     false -> the classic render pass + framebuffers.
//   synchronization2  true  -> vkCmdPipelineBarrier2, false -> vkCmdPipelineBarrier (same masks).
// The output image state at the end of record() is identical on both paths.

#ifdef SWISH_DEBUG_UI

#include "renderer/GpuResource/GpuResource.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/RendererConfig.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace swish {

struct RendererServices;

// Live-tunable TAA knobs (mirrors the DebugParams TAA block).
struct TaaParams {
    bool  enabled         = false;  // off by default → identical to today until toggled
    float historyBlend    = 0.9f;   // weight of reprojected history [0,1]
    bool  motionBlur      = false;
    float motionBlurScale = 1.0f;  // velocity smear strength
};

class TaaPass final : public IRenderPass {
public:
    TaaPass()           = default;
    ~TaaPass() override = default;

    const char* name() const override { return "Taa"; }

    // HDR, depth and the per-object velocity buffer (Img::Velocity, VelocityPass) come from the image registry;
    // velocity is sampled as an extra input taa.frag uses in place of its depth-only motion estimate wherever valid
    // (todo.md "Per-object velocity buffer").
    void init(const PassInitCtx& ctx) override;
    void onResize(const PassInitCtx& ctx) override;

    // Sub-pixel camera jitter (Halton 2,3) BEFORE the camera UBO / lighting invProj are built, so the whole frame
    // and its depth reconstruction stay self-consistent. Zero when TAA is off, so the projection is exactly the
    // unjittered matrix (SSAA / release unaffected). The sequence counter is private history of this pass.
    void beginFrame(const FrameUpdate& u) override;

    // Resolve this frame when tune.taaEnabled: read HDR + depth + velocity + history, write the resolved output,
    // copy it back into HDR. Leaves HDR in COLOR_ATTACHMENT (as the forward passes did) so the downstream
    // barrier/bloom logic is unchanged. Records its own barriers + render pass (or dynamic rendering). The
    // reprojection uses last frame's UN-jittered view-projection (private history) + this frame's jittered
    // clip-to-world (matching the depth).
    void record(const FrameCtx& ctx) override;

    void cleanup(VkDevice device) override;

private:
    void create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews, VkExtent2D extent);
    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews, VkExtent2D extent,
                  VkDevice device);
    void resolve(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const TaaParams& params,
                 const Mat4& prevViewProj, const Mat4& curInvViewProj);

    // Push block (matches taa.frag).
    struct Push {
        Mat4 prevViewProj;
        Mat4 invViewProj;
        Vec4 params;  // x = historyBlend, y = motionBlurScale, zw = texel size
    };

    void createImages(const RendererServices& s);
    void createRenderPass(VkDevice device);    // classic path only
    void createFramebuffers(VkDevice device);  // classic path only
    void destroyImagesAndFramebuffers(VkDevice device);
    void createDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                           const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                           const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews);
    void writeDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                          const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                          const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews);
    void createPipeline(VkDevice device, VkPipelineCache cache);
    void primeHistory(const RendererServices& s);

    VmaAllocator  m_allocator     = nullptr;
    VkDevice      m_device        = VK_NULL_HANDLE;
    VkCommandPool m_commandPool   = VK_NULL_HANDLE;
    VkQueue       m_graphicsQueue = VK_NULL_HANDLE;

    // Ping-pong history/output (read taa[1-frame], write taa[frame]).
    std::array<GpuImage, MAX_FRAMES_IN_FLIGHT>      m_images{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>   m_views{};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    VkRenderPass                                      m_renderPass = VK_NULL_HANDLE;
    VkSampler                                         m_sampler    = VK_NULL_HANDLE;  // linear clamp
    VkDescriptorPool                                  m_pool       = VK_NULL_HANDLE;
    VkDescriptorSetLayout                             m_setLayout  = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_sets{};
    VkPipelineLayout                                  m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline                                        m_pipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent = {};
    VkFormat   m_format = VK_FORMAT_R16G16B16A16_SFLOAT;

    Mat4     m_prevViewProj = Mat4(1.0f);  // last TAA frame's un-jittered world->clip (cross-frame history)
    uint32_t m_frameCounter = 0;           // Halton jitter sequence position

    bool m_dynamicRendering = false;  // RendererServices::dynamicRendering captured at init()
    bool m_sync2            = false;  // RendererServices::synchronization2 captured at init()
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
