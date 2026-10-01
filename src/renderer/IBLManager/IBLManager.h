#pragma once

#include "renderer/GpuResource/GpuResource.h"
#include "renderer/Renderer/RendererServices.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <string>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// IBLManager — prefiltered-cubemap image-based lighting.
//
// Bake chain: env source → env cube → irradiance → GGX prefilter + BRDF LUT.
// Env source: Radiance .hdr via set_hdri() (weather presets), else procedural sky.
// ══════════════════════════════════════════════════════════════════════
class IBLManager {
public:
    struct SkyBakeParams {
        Vec3  horizon{0.70f, 0.80f, 0.90f};
        Vec3  zenith{0.35f, 0.55f, 0.85f};
        Vec3  sunDir{0.0f, 1.0f, 0.0f};
        Vec3  sunColor{1.0f, 0.95f, 0.85f};
        float discExp = 32.0f;
        float discStr = 0.3f;
    };

    IBLManager()  = default;
    ~IBLManager() = default;

    void init(const RendererServices& svc);
    void cleanup();

    VkDescriptorSetLayout get_set_layout() const { return m_iblSetLayout; }
    VkDescriptorSet       get_set() const { return m_iblSet; }

    bool uses_hdri() const { return m_hdriLoaded; }

    // Load/replace HDRI and rebake convolutions. Returns false if load failed.
    bool set_hdri(const std::string& path, float exposure = 1.0f, float yaw_rad = 0.0f);

    // Procedural bake — no-op while an HDRI is active.
    void bake(const SkyBakeParams& sky);

private:
    static constexpr uint32_t kEnvDim         = 256;
    static constexpr uint32_t kIrrDim         = 32;
    static constexpr uint32_t kPrefilterDim   = 256;
    static constexpr uint32_t kPrefilterMips  = 5;
    static constexpr uint32_t kBrdfDim        = 256;
    static constexpr uint32_t kFaces          = 6;
    static constexpr VkFormat kCubeFormat     = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kBrdfFormat     = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat kEquirectFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

    struct SkyPush {
        Vec4 faceR, faceU, faceF;
        Vec4 horizon, zenith, sunDir, sunColor;
    };
    struct EquirectPush {
        Vec4 faceR, faceU, faceF;
        Vec4 params;  // x = exposure, y = yaw radians
    };
    struct ConvolvePush {
        Vec4 faceR, faceU, faceF;
        Vec4 params;  // x = roughness
    };

    RendererServices m_svc{};
    VkDevice         m_device = VK_NULL_HANDLE;

    GpuImage m_envImage, m_irrImage, m_prefilterImage, m_brdfImage;
    GpuImage m_equirectImage;

    VkImageView m_envCubeView       = VK_NULL_HANDLE;
    VkImageView m_irrCubeView       = VK_NULL_HANDLE;
    VkImageView m_prefilterCubeView = VK_NULL_HANDLE;
    VkImageView m_brdfView          = VK_NULL_HANDLE;
    VkImageView m_equirectView      = VK_NULL_HANDLE;

    std::array<VkImageView, kFaces>                  m_envFaceViews{};
    std::array<VkImageView, kFaces>                  m_irrFaceViews{};
    std::array<VkImageView, kFaces * kPrefilterMips> m_prefilterFaceMipViews{};

    std::array<VkFramebuffer, kFaces>                  m_envFBs{};
    std::array<VkFramebuffer, kFaces>                  m_irrFBs{};
    std::array<VkFramebuffer, kFaces * kPrefilterMips> m_prefilterFBs{};
    VkFramebuffer                                      m_brdfFB = VK_NULL_HANDLE;

    VkRenderPass m_cubeRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_brdfRenderPass = VK_NULL_HANDLE;

    VkSampler m_cubeSampler     = VK_NULL_HANDLE;
    VkSampler m_lutSampler      = VK_NULL_HANDLE;
    VkSampler m_equirectSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_iblSetLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_envSetLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_equirectSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_pool              = VK_NULL_HANDLE;
    VkDescriptorSet       m_iblSet            = VK_NULL_HANDLE;
    VkDescriptorSet       m_envSet            = VK_NULL_HANDLE;
    VkDescriptorSet       m_equirectSet       = VK_NULL_HANDLE;

    VkPipelineLayout m_skyLayout         = VK_NULL_HANDLE;
    VkPipelineLayout m_equirectLayout    = VK_NULL_HANDLE;
    VkPipelineLayout m_convolveLayout    = VK_NULL_HANDLE;
    VkPipelineLayout m_brdfLayout        = VK_NULL_HANDLE;
    VkPipeline       m_skyPipeline       = VK_NULL_HANDLE;
    VkPipeline       m_equirectPipeline  = VK_NULL_HANDLE;
    VkPipeline       m_irrPipeline       = VK_NULL_HANDLE;
    VkPipeline       m_prefilterPipeline = VK_NULL_HANDLE;
    VkPipeline       m_brdfPipeline      = VK_NULL_HANDLE;

    bool  m_hdriLoaded   = false;
    float m_hdriExposure = 1.0f;
    float m_hdriYawRad   = 0.0f;

    void createImagesAndViews();
    void createRenderPasses();
    void createFramebuffers();
    void createSamplers();
    void createDescriptors();
    void createPipelines();
    void bakeBRDF();
    void destroyEquirect();
    bool loadEquirect(const std::string& path);
    void bakeFromHdri();
    void bakeConvolutions(VkCommandBuffer cmd);

    VkImageView makeView(VkImage img, VkImageViewType type, VkFormat fmt, uint32_t baseMip, uint32_t mipCount,
                         uint32_t baseLayer, uint32_t layerCount);
};

}  // namespace swish
