#pragma once

#include "../../utils/Types.h"
#include "../GpuResource/GpuResource.h"
#include "../Renderer/RendererServices.h"

#include <vulkan/vulkan.h>

#include <array>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// IBLManager — prefiltered-cubemap image-based lighting, baked from the
// procedural sky.
//
// Bake chain (a one-time GPU precompute; the sky is static per weather):
//   procedural sky → environment cubemap
//                  → diffuse irradiance cubemap        (cosine convolution)
//                  → GGX-prefiltered specular cubemap  (roughness mips)
//   + split-sum BRDF integration LUT (sky-independent, baked once in init)
//
// lighting.frag samples the irradiance cube + prefiltered cube + BRDF LUT
// (descriptor set 3) instead of the old analytic per-fragment approximation.
// Re-baked when the weather (sun direction / clarity / colour) changes.
// ══════════════════════════════════════════════════════════════════════
class IBLManager {
public:
    // Sky parameters for a bake — the CPU folds the clarity blend so the bake
    // shader is a pure function of these (no camera/scene-params UBO needed,
    // works at init before the camera exists). Mirrors compute_sky_color().
    struct SkyBakeParams {
        Vec3  horizon{0.70f, 0.80f, 0.90f};  // clarity-mixed sky-horizon colour
        Vec3  zenith{0.35f, 0.55f, 0.85f};   // clarity-mixed sky-zenith colour
        Vec3  sunDir{0.0f, 1.0f, 0.0f};      // normalized sun direction (world)
        Vec3  sunColor{1.0f, 0.95f, 0.85f};  // sun colour
        float discExp = 32.0f;               // clarity-mixed sun-disc exponent
        float discStr = 0.3f;                // clarity-mixed sun-disc strength
    };

    IBLManager()  = default;
    ~IBLManager() = default;

    void init(const RendererServices& svc);
    void cleanup();

    // Set 3 layout/set for the deferred-lighting pipeline (irradiance cube +
    // prefiltered cube + BRDF LUT). Stable across swapchain recreate.
    VkDescriptorSetLayout get_set_layout() const { return m_iblSetLayout; }
    VkDescriptorSet       get_set() const { return m_iblSet; }

    // (Re)bake the sky-dependent chain (env → irradiance → prefilter). Fully
    // self-contained: allocates a one-time command buffer, submits, waits idle
    // both before (so no in-flight frame is sampling the cubes) and after.
    void bake(const SkyBakeParams& sky);

private:
    static constexpr uint32_t kEnvDim        = 256;
    static constexpr uint32_t kIrrDim        = 32;
    static constexpr uint32_t kPrefilterDim  = 256;
    static constexpr uint32_t kPrefilterMips = 5;  // 256,128,64,32,16 → roughness 0..1 (sharper mip0 gloss)
    static constexpr uint32_t kBrdfDim       = 256;
    static constexpr uint32_t kFaces         = 6;
    static constexpr VkFormat kCubeFormat    = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kBrdfFormat    = VK_FORMAT_R16G16_SFLOAT;

    // Push-constant blocks (must match shaders/ibl_*.frag).
    struct SkyPush {
        Vec4 faceR, faceU, faceF;                // per-face basis (xyz)
        Vec4 horizon, zenith, sunDir, sunColor;  // sky params (zenith.w=exp, sunDir.w=str)
    };
    struct ConvolvePush {
        Vec4 faceR, faceU, faceF;  // per-face basis
        Vec4 params;               // x = roughness (prefilter only)
    };

    RendererServices m_svc{};
    VkDevice         m_device = VK_NULL_HANDLE;

    // Images.
    GpuImage m_envImage, m_irrImage, m_prefilterImage, m_brdfImage;

    // Sampling views (bound to descriptor sets).
    VkImageView m_envCubeView       = VK_NULL_HANDLE;  // env sampled by convolution passes
    VkImageView m_irrCubeView       = VK_NULL_HANDLE;  // set 3, binding 0
    VkImageView m_prefilterCubeView = VK_NULL_HANDLE;  // set 3, binding 1 (all mips)
    VkImageView m_brdfView          = VK_NULL_HANDLE;  // set 3, binding 2 (also the render target)

    // Render-target views (one per face / per face-mip).
    std::array<VkImageView, kFaces>                  m_envFaceViews{};
    std::array<VkImageView, kFaces>                  m_irrFaceViews{};
    std::array<VkImageView, kFaces * kPrefilterMips> m_prefilterFaceMipViews{};

    // Framebuffers.
    std::array<VkFramebuffer, kFaces>                  m_envFBs{};
    std::array<VkFramebuffer, kFaces>                  m_irrFBs{};
    std::array<VkFramebuffer, kFaces * kPrefilterMips> m_prefilterFBs{};
    VkFramebuffer                                      m_brdfFB = VK_NULL_HANDLE;

    VkRenderPass m_cubeRenderPass = VK_NULL_HANDLE;  // RGBA16F single colour
    VkRenderPass m_brdfRenderPass = VK_NULL_HANDLE;  // RG16F single colour

    VkSampler m_cubeSampler = VK_NULL_HANDLE;  // trilinear, clamp — cubes
    VkSampler m_lutSampler  = VK_NULL_HANDLE;  // bilinear, clamp — BRDF LUT

    VkDescriptorSetLayout m_iblSetLayout = VK_NULL_HANDLE;  // 3 samplers (set 3)
    VkDescriptorSetLayout m_envSetLayout = VK_NULL_HANDLE;  // 1 samplerCube (bake)
    VkDescriptorPool      m_pool         = VK_NULL_HANDLE;
    VkDescriptorSet       m_iblSet       = VK_NULL_HANDLE;
    VkDescriptorSet       m_envSet       = VK_NULL_HANDLE;

    VkPipelineLayout m_skyLayout         = VK_NULL_HANDLE;  // push only
    VkPipelineLayout m_convolveLayout    = VK_NULL_HANDLE;  // env set + push
    VkPipelineLayout m_brdfLayout        = VK_NULL_HANDLE;  // no set / no push
    VkPipeline       m_skyPipeline       = VK_NULL_HANDLE;
    VkPipeline       m_irrPipeline       = VK_NULL_HANDLE;
    VkPipeline       m_prefilterPipeline = VK_NULL_HANDLE;
    VkPipeline       m_brdfPipeline      = VK_NULL_HANDLE;

    void createImagesAndViews();
    void createRenderPasses();
    void createFramebuffers();
    void createSamplers();
    void createDescriptors();
    void createPipelines();
    void bakeBRDF();  // one-time in init (sky-independent)

    VkImageView makeView(VkImage img, VkImageViewType type, VkFormat fmt, uint32_t baseMip, uint32_t mipCount,
                         uint32_t baseLayer, uint32_t layerCount);
};

}  // namespace swish
