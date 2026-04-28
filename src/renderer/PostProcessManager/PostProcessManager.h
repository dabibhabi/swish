#pragma once

#include <vulkan/vulkan.h>

#include "../../utils/Types.h"
#include "../../scene/SceneTypes.h"

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

// Push constants shared by all post-process shaders
struct PostProcessParams {
    float threshold;       // bloom threshold (default 1.0)
    float bloom_intensity; // bloom blend strength (default 0.3)
    float exposure;        // tone mapping exposure (default 1.0)
    float _pad0;
    float texel_x;         // 1.0/width for blur direction
    float texel_y;         // 1.0/height for blur direction
    float _pad1;
    float _pad2;
};

class PostProcessManager {
public:
    PostProcessManager() = default;
    ~PostProcessManager() = default;

    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              VkCommandPool commandPool, VkQueue graphicsQueue,
              VkExtent2D extent, VkFormat swapchainFormat,
              const std::vector<VkImageView>& swapchainImageViews);
    void cleanup();
    void recreate(VkExtent2D extent, VkFormat swapchainFormat,
                  const std::vector<VkImageView>& swapchainImageViews);

    // ── G-Buffer + Lighting render pass getters ────────────────────
    VkRenderPass get_gbuffer_render_pass() const { return m_gbufferRenderPass; }
    VkFramebuffer get_gbuffer_framebuffer(uint32_t frameIndex) const { return m_gbufferFramebuffers[frameIndex]; }
    VkImage get_gbuffer_albedo_image(uint32_t frameIndex) const { return m_gbAlbedoImages[frameIndex]; }
    VkImage get_gbuffer_normal_image(uint32_t frameIndex) const { return m_gbNormalImages[frameIndex]; }
    VkImage get_gbuffer_material_image(uint32_t frameIndex) const { return m_gbMaterialImages[frameIndex]; }

    VkRenderPass get_lighting_render_pass() const { return m_lightingRenderPass; }
    VkFramebuffer get_lighting_framebuffer(uint32_t frameIndex) const { return m_lightingFramebuffers[frameIndex]; }
    VkPipeline get_lighting_pipeline() const { return m_lightingPipeline; }
    VkPipelineLayout get_lighting_layout() const { return m_lightingLayout; }
    VkDescriptorSetLayout get_lighting_tex_layout() const { return m_lightingTexLayout; }
    void set_lighting_layout(VkPipelineLayout layout) { m_lightingLayout = layout; }
    void set_lighting_pipeline(VkPipeline pipeline) { m_lightingPipeline = pipeline; }
    VkDescriptorSet get_lighting_set(uint32_t frameIndex) const { return m_lightingSets[frameIndex]; }

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
    VkPipeline get_bloom_extract_pipeline() const { return m_bloomExtractPipeline; }
    VkPipeline get_bloom_blur_pipeline() const { return m_bloomBlurPipeline; }
    VkPipeline get_composite_pipeline() const { return m_compositePipeline; }
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
    VkImage get_hdr_image(uint32_t frameIndex) const { return m_hdrImages[frameIndex]; }
    VkImage get_hdr_depth_image(uint32_t frameIndex) const { return m_hdrDepthImages[frameIndex]; }
    VkImage get_bloom_extract_image() const { return m_bloomExtractImage; }
    VkImage get_bloom_blur_h_image() const { return m_bloomBlurHImage; }
    VkImage get_bloom_blur_v_image() const { return m_bloomBlurVImage; }
    VkImage get_ao_image() const { return m_aoImage; }
    VkImage get_ao_blur_image() const { return m_aoBlurImage; }

    // ── Extent getters ───────────────────────────────────────────
    VkExtent2D get_full_extent() const { return m_fullExtent; }
    VkExtent2D get_bloom_extent() const { return m_bloomExtent; }
    VkExtent2D get_ao_extent() const { return m_aoExtent; }

private:
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;

    VkExtent2D m_fullExtent  = {};
    VkExtent2D m_bloomExtent = {};
    VkExtent2D m_aoExtent    = {};

    // ── G-Buffer (per frame-in-flight) ─────────────────────────────
    VkRenderPass m_gbufferRenderPass = VK_NULL_HANDLE;
    std::array<VkImage, PP_MAX_FRAMES>        m_gbAlbedoImages{};
    std::array<VkDeviceMemory, PP_MAX_FRAMES> m_gbAlbedoMemory{};
    std::array<VkImageView, PP_MAX_FRAMES>    m_gbAlbedoViews{};
    std::array<VkImage, PP_MAX_FRAMES>        m_gbNormalImages{};
    std::array<VkDeviceMemory, PP_MAX_FRAMES> m_gbNormalMemory{};
    std::array<VkImageView, PP_MAX_FRAMES>    m_gbNormalViews{};
    std::array<VkImage, PP_MAX_FRAMES>        m_gbMaterialImages{};
    std::array<VkDeviceMemory, PP_MAX_FRAMES> m_gbMaterialMemory{};
    std::array<VkImageView, PP_MAX_FRAMES>    m_gbMaterialViews{};
    std::array<VkFramebuffer, PP_MAX_FRAMES>  m_gbufferFramebuffers{};

    // ── Deferred lighting ────────────────────────────────────────
    VkRenderPass m_lightingRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, PP_MAX_FRAMES> m_lightingFramebuffers{};
    VkPipelineLayout      m_lightingLayout     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_lightingTexLayout  = VK_NULL_HANDLE;
    VkPipeline            m_lightingPipeline   = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PP_MAX_FRAMES> m_lightingSets{};

    // ── Render passes ────────────────────────────────────────────
    VkRenderPass m_hdrRenderPass       = VK_NULL_HANDLE;
    VkRenderPass m_bloomRenderPass     = VK_NULL_HANDLE;
    VkRenderPass m_aoRenderPass        = VK_NULL_HANDLE;
    VkRenderPass m_compositeRenderPass = VK_NULL_HANDLE;

    // ── HDR offscreen (per frame-in-flight) ──────────────────────
    std::array<VkImage, PP_MAX_FRAMES>        m_hdrImages{};
    std::array<VkDeviceMemory, PP_MAX_FRAMES> m_hdrMemory{};
    std::array<VkImageView, PP_MAX_FRAMES>    m_hdrViews{};
    std::array<VkImage, PP_MAX_FRAMES>        m_hdrDepthImages{};
    std::array<VkDeviceMemory, PP_MAX_FRAMES> m_hdrDepthMemory{};
    std::array<VkImageView, PP_MAX_FRAMES>    m_hdrDepthViews{};
    std::array<VkFramebuffer, PP_MAX_FRAMES>  m_hdrFramebuffers{};

    // ── Bloom images (shared, 1/4 resolution) ────────────────────
    VkImage        m_bloomExtractImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_bloomExtractMemory = VK_NULL_HANDLE;
    VkImageView    m_bloomExtractView   = VK_NULL_HANDLE;
    VkFramebuffer  m_bloomExtractFB     = VK_NULL_HANDLE;

    VkImage        m_bloomBlurHImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_bloomBlurHMemory = VK_NULL_HANDLE;
    VkImageView    m_bloomBlurHView   = VK_NULL_HANDLE;
    VkFramebuffer  m_bloomBlurHFB     = VK_NULL_HANDLE;

    VkImage        m_bloomBlurVImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_bloomBlurVMemory = VK_NULL_HANDLE;
    VkImageView    m_bloomBlurVView   = VK_NULL_HANDLE;
    VkFramebuffer  m_bloomBlurVFB     = VK_NULL_HANDLE;

    // ── AO images (shared, 1/2 resolution) ───────────────────────
    VkImage        m_aoImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_aoMemory = VK_NULL_HANDLE;
    VkImageView    m_aoView   = VK_NULL_HANDLE;
    VkFramebuffer  m_aoFB     = VK_NULL_HANDLE;

    VkImage        m_aoBlurImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_aoBlurMemory = VK_NULL_HANDLE;
    VkImageView    m_aoBlurView   = VK_NULL_HANDLE;
    VkFramebuffer  m_aoBlurFB     = VK_NULL_HANDLE;

    // ── Composite framebuffers (one per swapchain image) ─────────
    std::vector<VkFramebuffer> m_compositeFBs;

    // ── Sampler (clamp-to-edge for post-processing) ──────────────
    VkSampler m_sampler = VK_NULL_HANDLE;

    // ── Pipelines ────────────────────────────────────────────────
    VkPipelineLayout      m_postProcessLayout    = VK_NULL_HANDLE;
    VkPipelineLayout      m_compositeLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_singleTexLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compositeTexLayout   = VK_NULL_HANDLE;

    VkPipeline m_bloomExtractPipeline = VK_NULL_HANDLE;
    VkPipeline m_bloomBlurPipeline    = VK_NULL_HANDLE;
    VkPipeline m_compositePipeline    = VK_NULL_HANDLE;

    // ── Descriptor pool + sets ───────────────────────────────────
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet  m_bloomExtractSet = VK_NULL_HANDLE;
    VkDescriptorSet  m_bloomBlurHSet   = VK_NULL_HANDLE;
    VkDescriptorSet  m_bloomBlurVSet   = VK_NULL_HANDLE;
    VkDescriptorSet  m_aoSet           = VK_NULL_HANDLE;
    VkDescriptorSet  m_aoBlurSet       = VK_NULL_HANDLE;
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

    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
};

}  // namespace swish
