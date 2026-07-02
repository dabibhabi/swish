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
    // Camera-app grade (debug UI). Defaults = identity so release output is unchanged
    // (recordCompositePass only overrides these under SWISH_DEBUG_UI).
    float brightness  = 0.0f;
    float contrast    = 1.0f;
    float saturation  = 1.0f;
    float temperature = 0.0f;
    float tint        = 0.0f;
};

// SSAO push block (matches shaders/ssao.frag). 2×mat4 + 4 floats = 144 B, a
// multiple of 16 (MoltenVK-safe). projection avoids a per-sample matrix inverse.
struct SsaoParams {
    Mat4  invProjection;  // clip → view (reconstruct view-space position)
    Mat4  projection;     // view → clip (project a sample back to screen)
    float radius;
    float bias;
    float intensity;
    float _pad0 = 0.0f;
};

// SSR push block (matches shaders/ssr.frag). 2×mat4 + 8 floats = 160 B (16-aligned).
struct SsrParams {
    Mat4  proj;       // view → clip
    Mat4  invProj;    // clip → view
    float maxDist;
    float thickness;
    float stride;
    float intensity;
    float wetness = 0.0f;  // global wetness [0,1] (wet surfaces reflect even if dry-rough)
    float _pad0   = 0.0f;
    float _pad1   = 0.0f;
    float _pad2   = 0.0f;
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

    // ── Shadow-map (CSM depth atlas: NUM_CASCADES × 2048² slices) getters ──
    VkRenderPass  get_shadow_render_pass() const { return m_shadowRenderPass; }
    VkFramebuffer get_shadow_framebuffer(uint32_t frameIndex) const { return m_shadowFramebuffers[frameIndex]; }
    VkDescriptorSetLayout get_shadow_tex_layout() const { return m_shadowTexLayout; }
    VkDescriptorSet       get_shadow_set(uint32_t frameIndex) const { return m_shadowSets[frameIndex]; }
    VkExtent2D            get_shadow_atlas_extent() const { return {kShadowDim * kNumCascades, kShadowDim}; }
    uint32_t              get_shadow_cascade_dim() const { return kShadowDim; }

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
    VkPipeline       get_ssao_pipeline() const { return m_ssaoPipeline; }
    VkPipeline       get_ao_blur_pipeline() const { return m_aoBlurPipeline; }
    VkPipeline       get_ssr_pipeline() const { return m_ssrPipeline; }
    VkPipelineLayout get_postprocess_layout() const { return m_postProcessLayout; }
    VkPipelineLayout get_composite_layout() const { return m_compositeLayout; }
    VkPipelineLayout get_ssao_layout() const { return m_ssaoLayout; }
    VkPipelineLayout get_ssr_layout() const { return m_ssrLayout; }
    // SSR reuses the lighting render pass (same R16F colour format + final layout).
    VkRenderPass     get_ssr_render_pass() const { return m_lightingRenderPass; }
    VkFramebuffer    get_ssr_framebuffer() const { return m_ssrFB; }
    VkImage          get_ssr_image() const { return m_ssrImage.handle(); }
    VkDescriptorSet  get_ssr_hdr_set(uint32_t frameIndex) const { return m_ssrHdrSets[frameIndex]; }

    // Auto-exposure luminance pyramid.
    VkImage    get_lum_image() const { return m_lumImage.handle(); }
    uint32_t   get_lum_dim() const { return kLumDim; }
    uint32_t   get_lum_mips() const { return kLumMips; }
    VkBuffer   get_lum_readback_buffer(uint32_t frameIndex) const { return m_lumReadback[frameIndex].handle(); }
    const void* get_lum_readback_mapped(uint32_t frameIndex) const { return m_lumReadback[frameIndex].mapped(); }

    // ── Descriptor set getters ───────────────────────────────────
    VkDescriptorSet get_bloom_extract_set() const { return m_bloomExtractSet; }
    VkDescriptorSet get_bloom_blur_h_set() const { return m_bloomBlurHSet; }
    VkDescriptorSet get_bloom_blur_v_set() const { return m_bloomBlurVSet; }
    VkDescriptorSet get_ao_set(uint32_t frameIndex) const { return m_aoSets[frameIndex]; }
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
    // SSAA: the whole offscreen chain (G-buffer/HDR/lighting/bloom/AO and the
    // forward rain/glass/windshield passes) renders at m_renderExtent = swap ×
    // kRenderScale; only the composite pass outputs at the swapchain extent,
    // downsampling the high-res HDR → supersample anti-aliasing.
    //   get_full_extent()   == render extent (offscreen size; existing callers
    //                          that mean "offscreen size" stay correct).
    //   get_render_extent() == render extent (explicit alias).
    //   get_swap_extent()   == swapchain extent (composite output size).
    VkExtent2D get_full_extent() const { return m_renderExtent; }
    VkExtent2D get_render_extent() const { return m_renderExtent; }
    VkExtent2D get_swap_extent() const { return m_swapExtent; }
    VkExtent2D get_bloom_extent() const { return m_bloomExtent; }
    VkExtent2D get_ao_extent() const { return m_aoExtent; }

    // Internal supersampling factor. ~kRenderScale² the pixels/VRAM
    // (1.5² = 2.25×). Tunable; clamped per-device to maxImageDimension2D.
    static constexpr float kRenderScale = 1.5f;

    // Live SSAA scale (debug UI). Takes effect on the next recreate() — the
    // Renderer sets this then rebuilds the offscreen chain. Defaults to the
    // shipped kRenderScale so release behaviour is unchanged.
    void  set_render_scale(float s) { m_renderScale = s; }
    float get_render_scale() const { return m_renderScale; }

private:
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VmaAllocator     m_allocator      = nullptr;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;

    // SSAA extents: m_renderExtent (== swap × kRenderScale, clamped to
    // maxImageDimension2D) sizes every offscreen image/FB; m_swapExtent sizes
    // only the composite framebuffers (swapchain images) + composite viewport.
    VkExtent2D m_renderExtent = {};
    VkExtent2D m_swapExtent   = {};
    VkExtent2D m_bloomExtent  = {};  // m_renderExtent / 4
    VkExtent2D m_aoExtent     = {};  // m_renderExtent / 2
    float      m_renderScale  = kRenderScale;  // live SSAA factor (see set_render_scale)

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

    // ── Sun shadow map (CSM depth atlas) ─────────────────────────
    // A 2048² slice per cascade, laid out side by side in one depth image
    // (kShadowDim × kNumCascades wide, kShadowDim tall), rendered from the sun POV
    // each frame and sampled by lighting.frag (set 2). NOT tied to swapchain extent.
    static constexpr uint32_t kShadowDim    = 2048;
    static constexpr uint32_t kNumCascades  = NUM_CASCADES;  // shared with SceneTypes / lighting.frag
    static constexpr uint32_t kShadowAtlasW = kShadowDim * kNumCascades;

    VkRenderPass                               m_shadowRenderPass = VK_NULL_HANDLE;
    std::array<GpuImage, PP_MAX_FRAMES>        m_shadowImages{};
    std::array<VkImageView, PP_MAX_FRAMES>     m_shadowViews{};
    std::array<VkFramebuffer, PP_MAX_FRAMES>   m_shadowFramebuffers{};
    VkSampler                                  m_shadowSampler = VK_NULL_HANDLE;  // compare sampler (hardware PCF)
    VkDescriptorSetLayout                      m_shadowTexLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_shadowSets{};

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

    // ── SSR reflection image (render extent, R16G16B16A16_SFLOAT) ─
    GpuImage      m_ssrImage;
    VkImageView   m_ssrView = VK_NULL_HANDLE;
    VkFramebuffer m_ssrFB   = VK_NULL_HANDLE;

    // ── Auto-exposure luminance pyramid (fixed 128², full mip chain to 1×1) ──
    // The HDR is blit into mip 0 then box-downsampled (LINEAR blits) to 1×1 =
    // average colour; that texel is copied to a per-frame host buffer the CPU
    // reads (previous frame, no stall) to drive the composite exposure. Same
    // format as the HDR so the blit needs no format conversion (MoltenVK-safe).
    static constexpr uint32_t kLumDim  = 128;
    static constexpr uint32_t kLumMips = 8;  // 128,64,32,16,8,4,2,1
    GpuImage                  m_lumImage;
    std::array<GpuBuffer, PP_MAX_FRAMES> m_lumReadback{};  // host-readable 1px (RGBA16F = 8 B)

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
    VkPipeline m_ssaoPipeline         = VK_NULL_HANDLE;  // SSAO (depth → AO)
    VkPipeline m_aoBlurPipeline       = VK_NULL_HANDLE;  // bilateral AO blur
    VkPipeline m_ssrPipeline          = VK_NULL_HANDLE;  // screen-space reflections
    // SSAO/SSR read a mat4×2 + params push block (144 B) that doesn't fit the shared
    // 32-B postprocess layout, so they need their own layouts.
    VkPipelineLayout m_ssaoLayout = VK_NULL_HANDLE;
    // SSR layout: set 0 = G-buffer (lightingTexLayout), set 1 = HDR (singleTexLayout) + 144-B push.
    VkPipelineLayout m_ssrLayout = VK_NULL_HANDLE;

    // ── Descriptor pool + sets ───────────────────────────────────
    VkDescriptorPool                           m_descriptorPool  = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomExtractSet = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomBlurHSet   = VK_NULL_HANDLE;
    VkDescriptorSet                            m_bloomBlurVSet   = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_aoSets{};  // per-frame: samples that frame's depth
    VkDescriptorSet                            m_aoBlurSet       = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_ssrHdrSets{};  // per-frame: SSR samples that frame's lit HDR
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

    // Scale a swapchain extent by kRenderScale for SSAA, clamping the factor
    // down if width/height would exceed the device's maxImageDimension2D.
    VkExtent2D scaleExtent(VkExtent2D swap) const;
};

}  // namespace swish
