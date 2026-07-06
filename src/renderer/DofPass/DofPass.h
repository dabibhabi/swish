#pragma once

// DofPass — depth-of-field post-process resolve (debug-only).
//
// A self-contained pass (like TaaPass) instantiated ONLY under SWISH_DEBUG_UI, so a
// release build never compiles it into the pipeline and stays byte-identical. It reads
// the lit+forward HDR image + scene depth, computes a per-pixel circle-of-confusion from
// the reverse-Z view-space distance, gathers a disk blur, and copies the resolved result
// back INTO the HDR image — so the existing bloom/composite chain is untouched. Simpler
// than TAA: no history ping-pong, no motion vectors, a single full-screen resolve.

#ifdef SWISH_DEBUG_UI

#include "../../utils/Types.h"
#include "../GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace swish {

struct RendererServices;

// Live-tunable DOF knobs (mirrors the DebugParams DOF block).
struct DofParams {
    bool  enabled    = false;      // off by default → identical to today until toggled
    float focusDist  = 40000.0f;   // in-focus distance (WU)
    float focusRange = 120000.0f;  // distance over which CoC ramps to max (WU)
    float maxCoC     = 6.0f;       // max circle-of-confusion radius (texels)
};

class DofPass {
public:
    DofPass()  = default;
    ~DofPass() = default;

    void init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
              const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent);

    // Resolve this frame: read HDR (`hdrImage`/its view) + depth, write the blurred output,
    // copy it back into `hdrImage`. Leaves HDR in COLOR_ATTACHMENT (as the god-rays pass did)
    // so the downstream barrier/bloom logic is unchanged. `invProj` is clip→view (matches the
    // scene depth); records its own barriers + render pass.
    void record(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const DofParams& params,
                const Mat4& invProj);

    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);

    void cleanup(VkDevice device);

private:
    // Push block (matches dof.frag). 64 + 16 + 16 = 96 B (16-byte aligned).
    struct Push {
        Mat4 invProj;
        Vec4 focus;  // x = focusDist, y = focusRange, z = maxCoC, w = pad
        Vec4 texel;  // xy = texel size (1/extent), zw = pad
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

    VmaAllocator  m_allocator     = nullptr;
    VkDevice      m_device        = VK_NULL_HANDLE;
    VkCommandPool m_commandPool   = VK_NULL_HANDLE;
    VkQueue       m_graphicsQueue = VK_NULL_HANDLE;

    // One scratch output per frame-in-flight (render target → copy source).
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
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
