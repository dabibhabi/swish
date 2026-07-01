#pragma once

#include "../../scene/SceneTypes.h"
#include "../../utils/Types.h"
#include "../GpuResource/GpuResource.h"

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// PostProcessManager — Owns all offscreen rendering resources for the
// multi-pass HDR pipeline: bloom, SSAO, and final compositing.
//
// Architecture (per frame):
//   Pass 1: Scene → HDR offscreen (R16G16B16A16_SFLOAT)
//   Pass 2: Bloom extract → blur H → blur V (1/4 resolution)
//   Pass 3: SSAO → AO blur (1/2 resolution)
//   Pass 4: Composite (HDR + bloom + AO → swapchain with ACES)
// ══════════════════════════════════════════════════════════════════════

static constexpr uint32_t PP_MAX_FRAMES = 2;

// Push constants shared by post-process shaders.
// Layout must match the push_constant block in composite.frag exactly.
struct PostProcessParams {
    float threshold;        // bloom threshold (default 1.0)
    float bloom_intensity;  // bloom blend strength (default 0.3)
    float exposure;         // tone mapping exposure (default 1.0)
    float rain_intensity;   // [0,1] rain haze strength — repurposed from _pad0
    float texel_x;          // 1.0/width for blur passes
    float texel_y;          // 1.0/height for blur passes
    float fog_density;      // atmospheric fog blend per rain_intensity unit — repurposed from _pad1
    float _pad2;
};

class PostProcessManager {
public:
    PostProcessManager()  = default;
    ~PostProcessManager() = default;

    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue,
              VmaAllocator allocator, VkExtent2D extent, VkFormat swapchainFormat,
              const std::vector<VkImageView>& swapchainImageViews);
    void cleanup();
    void recreate(VkExtent2D extent, VkFormat swapchainFormat, const std::vector<VkImageView>& swapchainImageViews);

    // ── G-Buffer + Lighting render pass getters ────────────────────
    VkRenderPass  get_gbuffer_render_pass() const { return m_gbufferRenderPass; }
    VkFramebuffer get_gbuffer_framebuffer(uint32_t frameIndex) const { return m_gbufferFramebuffers[frameIndex]; }
    VkImage       get_gbuffer_albedo_image(uint32_t frameIndex) const { return m_gbAlbedoImages[frameIndex].handle(); }
    VkImage       get_gbuffer_normal_image(uint32_t frameIndex) const { return m_gbNormalImages[frameIndex].handle(); }
    VkImage       get_gbuffer_material_image(uint32_t frameIndex) const { return m_gbMaterialImages[frameIndex].handle(); }

    VkRenderPass  get_lighting_render_pass() const { return m_lightingRenderPass; }
    VkFramebuffer get_lighting_framebuffer(uint32_t frameIndex) const { return m_lightingFramebuffers[frameIndex]; }
    VkDescriptorSetLayout get_lighting_tex_layout() const { return m_lightingTexLayout; }
    VkDescriptorSet       get_lighting_set(uint32_t frameIndex) const { return m_lightingSets[frameIndex]; }

    // ── Render pass getters ──────────────────────────────────────
    VkRenderPass get_hdr_render_pass() const { return m_hdrRenderPass; }
    VkRenderPass get_bloom_render_pass() const { return m_bloomRenderPass; }
    VkRenderPass get_ao_render_pass() const { return m_aoRenderPass; }
    VkRenderPass get_composite_render_pass() const { return m_compositeRenderPass; }

    // ── Framebuffer getters ──────────────────────────────────────
    VkFramebuffer get_hdr_framebuffer(uint32_t frameIndex) const { return m_hdrFramebuffers[frameIndex]; }
    VkFramebuffer get_bloom_extract_framebuffer() const { return m_bloomExtractFB; }
    VkFramebuffer get_bloom_blur_h_framebuffer() const { return m_bloomBlurHFB; }
    VkFramebuffer get_bloom_blur_v_framebuffer() const { return m_bloomBlurVFB; }
    VkFramebuffer get_ao_framebuffer() const { return m_aoFB; }
    VkFramebuffer get_ao_blur_framebuffer() const { return m_aoBlurFB; }
    VkFramebuffer get_composite_framebuffer(uint32_t imageIndex) const { return m_compositeFBs[imageIndex]; }

    // ── Pipeline getters ─────────────────────────────────────────
    VkPipeline       get_bloom_extract_pipeline() const { return m_bloomExtractPipeline; }
    VkPipeline       get_bloom_blur_pipeline() const { return m_bloomBlurPipeline; }
    VkPipeline       get_composite_pipeline() const { return m_compositePipeline; }
    VkPipelineLayout get_postprocess_layout() const { return m_postProcessLayout; }
    VkPipelineLayout get_composite_layout() const { return m_compositeLayout; }

    // ── Descriptor set getters ───────────────────────────────────
    VkDescriptorSet get_bloom_extract_set() const { return m_bloomExtractSet; }
    VkDescriptorSet get_bloom_blur_h_set() const { return m_bloomBlurHSet; }
    VkDescriptorSet get_bloom_blur_v_set() const { return m_bloomBlurVSet; }
    VkDescriptorSet get_ao_set() const { return m_aoSet; }
    VkDescriptorSet get_ao_blur_set() const { return m_aoBlurSet; }
    VkDescriptorSet get_composite_set(uint32_t frameIndex) const { return m_compositeSets[frameIndex]; }

    // ── Image getters (for barriers in Renderer) ─────────────────
    VkImage     get_hdr_image(uint32_t frameIndex) const { return m_hdrImages[frameIndex].handle(); }
    VkImageView get_hdr_view(uint32_t frameIndex) const { return m_hdrViews[frameIndex]; }
    VkImage     get_hdr_depth_image(uint32_t frameIndex) const { return m_hdrDepthImages[frameIndex].handle(); }
    VkImageView get_hdr_depth_view(uint32_t frameIndex) const { return m_hdrDepthViews[frameIndex]; }
    VkImage     get_bloom_extract_image() const { return m_bloomExtractImage.handle(); }
    VkImage     get_bloom_blur_h_image() const { return m_bloomBlurHImage.handle(); }
    VkImage     get_bloom_blur_v_image() const { return m_bloomBlurVImage.handle(); }
    VkImage     get_ao_image() const { return m_aoImage.handle(); }
    VkImage     get_ao_blur_image() const { return m_aoBlurImage.handle(); }

    // ── Extent getters ───────────────────────────────────────────
    VkExtent2D get_full_extent() const { return m_fullExtent; }
    VkExtent2D get_bloom_extent() const { return m_bloomExtent; }
    VkExtent2D get_ao_extent() const { return m_aoExtent; }

private:
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VmaAllocator     m_allocator      = nullptr;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;

    VkExtent2D m_fullExtent  = {};
    VkExtent2D m_bloomExtent = {};
    VkExtent2D m_aoExtent    = {};

    // ── G-Buffer (per frame-in-flight) ─────────────────────────────
    VkRenderPass                             m_gbufferRenderPass = VK_NULL_HANDLE;
    std::array<GpuImage, PP_MAX_FRAMES>      m_gbAlbedoImages{};
    std::array<VkImageView, PP_MAX_FRAMES>   m_gbAlbedoViews{};
    std::array<GpuImage, PP_MAX_FRAMES>      m_gbNormalImages{};
    std::array<VkImageView, PP_MAX_FRAMES>   m_gbNormalViews{};
    std::array<GpuImage, PP_MAX_FRAMES>      m_gbMaterialImages{};
    std::array<VkImageView, PP_MAX_FRAMES>   m_gbMaterialViews{};
    std::array<VkFramebuffer, PP_MAX_FRAMES> m_gbufferFramebuffers{};

    // ── Deferred lighting ────────────────────────────────────────
    // The lighting pipeline + layout live in DeferredLightingPipeline
    // (Renderer-owned). Here we only own the render pass, framebuffers,
    // descriptor-set layout for set 1 (G-buffer samplers), and the
    // per-frame descriptor sets that bind G-buffer images.
    VkRenderPass                               m_lightingRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, PP_MAX_FRAMES>   m_lightingFramebuffers{};
    VkDescriptorSetLayout                      m_lightingTexLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_lightingSets{};

    // ── Render passes ────────────────────────────────────────────
    VkRenderPass m_hdrRenderPass       = VK_NULL_HANDLE;
    VkRenderPass m_bloomRenderPass     = VK_NULL_HANDLE;
    VkRenderPass m_aoRenderPass        = VK_NULL_HANDLE;
    VkRenderPass m_compositeRenderPass = VK_NULL_HANDLE;

    // ── HDR offscreen (per frame-in-flight) ──────────────────────
    std::array<GpuImage, PP_MAX_FRAMES>      m_hdrImages{};
    std::array<VkImageView, PP_MAX_FRAMES>   m_hdrViews{};
    std::array<GpuImage, PP_MAX_FRAMES>      m_hdrDepthImages{};
    std::array<VkImageView, PP_MAX_FRAMES>   m_hdrDepthViews{};
    std::array<VkFramebuffer, PP_MAX_FRAMES> m_hdrFramebuffers{};

    // ── Bloom images (shared, 1/4 resolution) ────────────────────
    GpuImage      m_bloomExtractImage;
    VkImageView   m_bloomExtractView = VK_NULL_HANDLE;
    VkFramebuffer m_bloomExtractFB   = VK_NULL_HANDLE;

    GpuImage      m_bloomBlurHImage;
    VkImageView   m_bloomBlurHView = VK_NULL_HANDLE;
    VkFramebuffer m_bloomBlurHFB   = VK_NULL_HANDLE;

    GpuImage      m_bloomBlurVImage;
    VkImageView   m_bloomBlurVView = VK_NULL_HANDLE;
    VkFramebuffer m_bloomBlurVFB   = VK_NULL_HANDLE;

    // ── AO images (shared, 1/2 resolution) ───────────────────────
    GpuImage      m_aoImage;
    VkImageView   m_aoView = VK_NULL_HANDLE;
    VkFramebuffer m_aoFB   = VK_NULL_HANDLE;

    GpuImage      m_aoBlurImage;
    VkImageView   m_aoBlurView = VK_NULL_HANDLE;
    VkFramebuffer m_aoBlurFB   = VK_NULL_HANDLE;

    // ── Composite framebuffers (one per swapchain image) ─────────
    std::vector<VkFramebuffer> m_compositeFBs;

    // ── Sampler (clamp-to-edge for post-processing) ──────────────
    VkSampler m_sampler = VK_NULL_HANDLE;

    // ── Pipelines ────────────────────────────────────────────────
    VkPipelineLayout      m_postProcessLayout  = VK_NULL_HANDLE;
    VkPipelineLayout      m_compositeLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_singleTexLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compositeTexLayout = VK_NULL_HANDLE;

    VkPipeline m_bloomExtractPipeline = VK_NULL_HANDLE;
    VkPipeline m_bloomBlurPipeline    = VK_NULL_HANDLE;
    VkPipeline m_compositePipeline    = VK_NULL_HANDLE;

    // ── Descriptor pool + sets ───────────────────────────────────
    VkDescriptorPool                           m_descriptorPool  = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomExtractSet = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomBlurHSet   = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomBlurVSet   = VK_NULL_HANDLE;
    VkDescriptorSet                            m_aoSet           = VK_NULL_HANDLE;
    VkDescriptorSet                            m_aoBlurSet       = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_compositeSets{};

    // ── Private helpers ──────────────────────────────────────────
    void createRenderPasses(VkFormat swapchainFormat);
    void createImages();
    void createFramebuffers(const std::vector<VkImageView>& swapchainImageViews);
    void createSampler();
    void createDescriptors();
    void createPipelines();
    void primeAOTexture();

    void destroyImages();
    void destroyFramebuffers();
    void destroyPipelines();
    void destroyDescriptors();

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
};

}  // namespace swish
