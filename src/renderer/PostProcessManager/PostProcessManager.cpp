#include "PostProcessManager.h"

#include "renderer/RendererConfig.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Samplers.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Public interface
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
                              VkQueue graphicsQueue, VmaAllocator allocator, VkPipelineCache pipelineCache,
                              VkExtent2D extent, VkFormat swapchainFormat,
                              const std::vector<VkImageView>& swapchainImageViews) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    m_allocator      = allocator;
    m_pipelineCache  = pipelineCache;
    m_commandPool    = commandPool;
    m_graphicsQueue  = graphicsQueue;

    // SSAA: `extent` is the SWAP extent (composite output). Every offscreen
    // target renders at m_renderExtent = swap × kRenderScale (clamped).
    m_swapExtent    = extent;
    m_renderExtent  = scaleExtent(extent);
    m_bloomExtent   = {m_renderExtent.width / 4, m_renderExtent.height / 4};
    m_aoExtent      = {m_renderExtent.width / 2, m_renderExtent.height / 2};
    m_godraysExtent = {m_renderExtent.width / 2, m_renderExtent.height / 2};

    createSampler();
    createRenderPasses(swapchainFormat);
    createImages();
    createFramebuffers(swapchainImageViews);
    createDescriptors();
    createPipelines();
    primeAOTexture();
}

void PostProcessManager::cleanup() {
    destroyPipelines();
    destroyDescriptors();
    destroyFramebuffers();
    destroyImages();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_shadowSampler, nullptr);
        m_shadowSampler = VK_NULL_HANDLE;
    }

    if (m_shadowRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);
        m_shadowRenderPass = VK_NULL_HANDLE;
    }
    if (m_gbufferRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_gbufferRenderPass, nullptr);
    if (m_lightingRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_lightingRenderPass, nullptr);
    if (m_hdrRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_hdrRenderPass, nullptr);
    if (m_bloomRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_bloomRenderPass, nullptr);
    if (m_aoRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_aoRenderPass, nullptr);
    if (m_compositeRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_compositeRenderPass, nullptr);
    m_gbufferRenderPass = m_lightingRenderPass = VK_NULL_HANDLE;
    m_hdrRenderPass = m_bloomRenderPass = m_aoRenderPass = m_compositeRenderPass = VK_NULL_HANDLE;
}

void PostProcessManager::recreate(VkExtent2D extent, VkFormat swapchainFormat,
                                  const std::vector<VkImageView>& swapchainImageViews) {
    destroyPipelines();
    destroyDescriptors();
    destroyFramebuffers();
    destroyImages();

    // SSAA: recompute both extents from the new swap extent.
    m_swapExtent    = extent;
    m_renderExtent  = scaleExtent(extent);
    m_bloomExtent   = {m_renderExtent.width / 4, m_renderExtent.height / 4};
    m_aoExtent      = {m_renderExtent.width / 2, m_renderExtent.height / 2};
    m_godraysExtent = {m_renderExtent.width / 2, m_renderExtent.height / 2};

    // Render passes don't change (format-dependent only)
    createImages();
    createFramebuffers(swapchainImageViews);
    createDescriptors();
    createPipelines();
    primeAOTexture();
}

// ══════════════════════════════════════════════════════════════════════
// Named images (ImageRegistry)
// ══════════════════════════════════════════════════════════════════════

VkImage PostProcessManager::image(Img id, uint32_t f) const {
    switch (id) {
        case Img::Hdr:
            return m_hdrImages[f].handle();
        case Img::HdrDepth:
            return m_hdrDepthImages[f].handle();
        case Img::GbAlbedo:
            return m_gbAlbedoImages[f].handle();
        case Img::GbNormal:
            return m_gbNormalImages[f].handle();
        case Img::GbMaterial:
            return m_gbMaterialImages[f].handle();
        case Img::GbClearcoat:
            return m_gbClearcoatImages[f].handle();
        case Img::Ao:
            return m_aoImage.handle();
        case Img::AoBlur:
            return m_aoBlurImage.handle();
        case Img::BloomExtract:
            return m_bloomExtractImage.handle();
        case Img::BloomBlurH:
            return m_bloomBlurHImage.handle();
        case Img::BloomBlurV:
            return m_bloomBlurVImage.handle();
        case Img::Ssr:
            return m_ssrImage.handle();
        case Img::Godrays:
            return m_godraysImage.handle();
        case Img::Lum:
            return m_lumImage.handle();
        default:
            return VK_NULL_HANDLE;  // Img::Velocity is owned by VelocityPass
    }
}

VkImageView PostProcessManager::view(Img id, uint32_t f) const {
    switch (id) {
        case Img::Hdr:
            return m_hdrViews[f];
        case Img::HdrDepth:
            return m_hdrDepthViews[f];
        case Img::GbAlbedo:
            return m_gbAlbedoViews[f];
        case Img::GbNormal:
            return m_gbNormalViews[f];
        case Img::GbMaterial:
            return m_gbMaterialViews[f];
        case Img::GbClearcoat:
            return m_gbClearcoatViews[f];
        case Img::Ao:
            return m_aoView;
        case Img::AoBlur:
            return m_aoBlurView;
        case Img::BloomExtract:
            return m_bloomExtractView;
        case Img::BloomBlurH:
            return m_bloomBlurHView;
        case Img::BloomBlurV:
            return m_bloomBlurVView;
        case Img::Ssr:
            return m_ssrView;
        case Img::Godrays:
            return m_godraysView;
        default:
            return VK_NULL_HANDLE;  // Img::Lum has no view; Img::Velocity is owned by VelocityPass
    }
}

void PostProcessManager::register_images(ImageRegistry& reg) const {
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
        for (Img id : {Img::Hdr, Img::HdrDepth, Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat})
            reg.set(id, f, image(id, f), view(id, f));
    }
    for (Img id :
         {Img::Ao, Img::AoBlur, Img::BloomExtract, Img::BloomBlurH, Img::BloomBlurV, Img::Ssr, Img::Godrays, Img::Lum})
        reg.set_shared(id, image(id, 0), view(id, 0));
}

// ══════════════════════════════════════════════════════════════════════
// Sampler — clamp-to-edge for post-process texture sampling
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createSampler() {
    m_sampler = vkx::make_sampler(m_device, {});  // linear, clamp-to-edge

    // Shadow-map sampler. MoltenVK's portability subset reports
    // mutableComparisonSamplers = FALSE, so a compareEnable=TRUE (sampler2DShadow)
    // sampler is rejected at descriptor-write time. We therefore sample the depth
    // as a PLAIN texture and compare manually in lighting.frag (3×3 PCF there).
    // NEAREST because we tap discrete texels ourselves (LINEAR would blend raw
    // depths — meaningless before the compare, and often unsupported on depth
    // formats). CLAMP_TO_BORDER + white border → off-map reads 1.0 (far = lit).
    m_shadowSampler = vkx::make_sampler(m_device, {.filter  = VK_FILTER_NEAREST,
                                                   .address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                                   .border  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE});  // off-map = lit
}

// ══════════════════════════════════════════════════════════════════════
// Image creation helpers
// ══════════════════════════════════════════════════════════════════════

// SSAA: render at swap × kRenderScale. If that would exceed the device's
// maxImageDimension2D (MoltenVK/Metal caps this at 16384 on Apple silicon), the
// scale is clamped DOWN so the larger of width/height fits exactly — never
// letting an offscreen image exceed the limit.
VkExtent2D PostProcessManager::scaleExtent(VkExtent2D swap) const {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    const uint32_t maxDim = props.limits.maxImageDimension2D;

    float scale = m_renderScale;  // live SSAA factor (defaults to kRenderScale)
    // Largest scaled dimension must fit in maxDim; clamp the factor if not.
    const uint32_t largestSwap = swap.width > swap.height ? swap.width : swap.height;
    if (largestSwap > 0 && static_cast<float>(largestSwap) * scale > static_cast<float>(maxDim)) {
        scale = static_cast<float>(maxDim) / static_cast<float>(largestSwap);
    }
    if (scale < 1.0f)  // never render below native (would upsample, not SSAA)
        scale = 1.0f;

    VkExtent2D out{};
    out.width  = static_cast<uint32_t>(static_cast<float>(swap.width) * scale);
    out.height = static_cast<uint32_t>(static_cast<float>(swap.height) * scale);
    // Guard against zero (minimized window) exactly like the swap extent would be.
    if (out.width == 0) out.width = swap.width;
    if (out.height == 0) out.height = swap.height;
    return out;
}

}  // namespace swish
