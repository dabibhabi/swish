#include "PostProcessManager.h"

#include "../../utils/VulkanCheck.h"
#include "../Pipeline/Pipeline.h"
#include "../ResourceManager/ResourceManager.h"

#include <array>
#include <stdexcept>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Public interface
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
                              VkQueue graphicsQueue, VmaAllocator allocator, VkExtent2D extent,
                              VkFormat swapchainFormat, const std::vector<VkImageView>& swapchainImageViews) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    m_allocator      = allocator;
    m_commandPool    = commandPool;
    m_graphicsQueue  = graphicsQueue;

    // SSAA: `extent` is the SWAP extent (composite output). Every offscreen
    // target renders at m_renderExtent = swap × kRenderScale (clamped).
    m_swapExtent   = extent;
    m_renderExtent = scaleExtent(extent);
    m_bloomExtent  = {m_renderExtent.width / 4, m_renderExtent.height / 4};
    m_aoExtent     = {m_renderExtent.width / 2, m_renderExtent.height / 2};

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
    if (m_gbufferRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_gbufferRenderPass, nullptr);
    if (m_lightingRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_lightingRenderPass, nullptr);
    if (m_hdrRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_hdrRenderPass, nullptr);
    if (m_bloomRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_bloomRenderPass, nullptr);
    if (m_aoRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_aoRenderPass, nullptr);
    if (m_compositeRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_compositeRenderPass, nullptr);
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
    m_swapExtent   = extent;
    m_renderExtent = scaleExtent(extent);
    m_bloomExtent  = {m_renderExtent.width / 4, m_renderExtent.height / 4};
    m_aoExtent     = {m_renderExtent.width / 2, m_renderExtent.height / 2};

    // Render passes don't change (format-dependent only)
    createImages();
    createFramebuffers(swapchainImageViews);
    createDescriptors();
    createPipelines();
    primeAOTexture();
}

// ══════════════════════════════════════════════════════════════════════
// Sampler — clamp-to-edge for post-process texture sampling
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createSampler() {
    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_device, &info, nullptr, &m_sampler));

    // Shadow-map sampler. MoltenVK's portability subset reports
    // mutableComparisonSamplers = FALSE, so a compareEnable=TRUE (sampler2DShadow)
    // sampler is rejected at descriptor-write time. We therefore sample the depth
    // as a PLAIN texture and compare manually in lighting.frag (3×3 PCF there).
    // NEAREST because we tap discrete texels ourselves (LINEAR would blend raw
    // depths — meaningless before the compare, and often unsupported on depth
    // formats). CLAMP_TO_BORDER + white border → off-map reads 1.0 (far = lit).
    VkSamplerCreateInfo shadowInfo{};
    shadowInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowInfo.magFilter     = VK_FILTER_NEAREST;
    shadowInfo.minFilter     = VK_FILTER_NEAREST;
    shadowInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // off-map = lit
    shadowInfo.compareEnable = VK_FALSE;
    VK_CHECK(vkCreateSampler(m_device, &shadowInfo, nullptr, &m_shadowSampler));
}

// ══════════════════════════════════════════════════════════════════════
// Render Passes
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createRenderPasses(VkFormat swapchainFormat) {
    // Helper: create a single-color-attachment render pass
    auto makePass = [&](VkFormat colorFormat, VkImageLayout finalLayout, bool hasDepth) -> VkRenderPass {
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = colorFormat;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = finalLayout;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        std::vector<VkAttachmentDescription> attachments = {colorAtt};

        VkAttachmentDescription depthAtt{};
        VkAttachmentReference   depthRef{};
        if (hasDepth) {
            depthAtt.format         = ResourceManager::findDepthFormat(m_physicalDevice);
            depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
            depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;  // needed for SSAO
            depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            depthRef.attachment = 1;
            depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments.push_back(depthAtt);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | (hasDepth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : 0);
        dep.dstStageMask  = dep.srcStageMask;
        dep.srcAccessMask = 0;
        dep.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (hasDepth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments    = attachments.data();
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dep;

        VkRenderPass rp;
        VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &rp));
        return rp;
    };

    m_hdrRenderPass       = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    m_bloomRenderPass     = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);
    m_aoRenderPass        = makePass(VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);
    m_compositeRenderPass = makePass(swapchainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false);
    m_lightingRenderPass  = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);

    // G-Buffer render pass: 3 color MRT (albedo, normal, material) + depth
    {
        VkFormat depthFmt = ResourceManager::findDepthFormat(m_physicalDevice);

        std::array<VkAttachmentDescription, 4> atts{};
        // 0: Albedo (R8G8B8A8_UNORM)
        atts[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
        atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 1: Normal (R16G16B16A16_SFLOAT)
        atts[1]        = atts[0];
        atts[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;

        // 2: Material (R8G8B8A8_UNORM)
        atts[2] = atts[0];

        // 3: Depth
        atts[3].format         = depthFmt;
        atts[3].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[3].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[3].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts[3].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[3].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[3].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<VkAttachmentReference, 3> colorRefs{};
        colorRefs[0]                   = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[1]                   = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[2]                   = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef = {3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 3;
        subpass.pColorAttachments       = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask  = dep.srcStageMask;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(atts.size());
        rpInfo.pAttachments    = atts.data();
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dep;

        VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_gbufferRenderPass));
    }

    // ── Shadow render pass: depth-only (no color attachments) ──────────
    // One D32_SFLOAT attachment, cleared then stored, ending in
    // DEPTH_STENCIL_READ_ONLY_OPTIMAL so lighting.frag can sample it. Two
    // subpass dependencies bracket the pass so the depth WRITE (this pass)
    // completes before the lighting pass's fragment-shader READ.
    {
        VkAttachmentDescription depthAtt{};
        depthAtt.format         = VK_FORMAT_D32_SFLOAT;
        depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 0;
        subpass.pColorAttachments       = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> deps{};
        // external → 0: prior fragment reads (last frame's sampling) finish before
        // this frame writes depth.
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // 0 → external: depth write completes + layout transitions before the
        // lighting pass samples the map in its fragment shader.
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &depthAtt;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        rpInfo.pDependencies   = deps.data();

        VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_shadowRenderPass));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Image creation helpers
// ══════════════════════════════════════════════════════════════════════

VkImageView PostProcessManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{};
    info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image                           = image;
    info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    info.format                          = format;
    info.subresourceRange.aspectMask     = aspect;
    info.subresourceRange.baseMipLevel   = 0;
    info.subresourceRange.levelCount     = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount     = 1;

    VkImageView view;
    VK_CHECK(vkCreateImageView(m_device, &info, nullptr, &view));
    return view;
}

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
    if (out.width == 0)
        out.width = swap.width;
    if (out.height == 0)
        out.height = swap.height;
    return out;
}

void PostProcessManager::createImages() {
    VkFormat hdrFormat   = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat = ResourceManager::findDepthFormat(m_physicalDevice);
    VkFormat aoFormat    = VK_FORMAT_R8_UNORM;

    auto makeColorImage = [&](uint32_t w, uint32_t h, VkFormat fmt, GpuImage& img, VkImageView& view,
                              VkImageUsageFlags extraUsage = 0) {
        img  = gpu::deviceLocalImage(m_allocator, w, h, fmt, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage);
        view = createImageView(img.handle(), fmt);
    };

    auto makeDepthImage = [&](uint32_t w, uint32_t h, GpuImage& img, VkImageView& view) {
        img  = gpu::deviceLocalImage(m_allocator, w, h, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        view = createImageView(img.handle(), depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    };

    // Per-frame HDR + depth
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        // HDR needs TRANSFER_SRC so the windshield-rain pass can snapshot it
        // (blit/copy → refraction-source image) for scene-refraction drops.
        makeColorImage(m_renderExtent.width, m_renderExtent.height, hdrFormat, m_hdrImages[i], m_hdrViews[i],
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        makeDepthImage(m_renderExtent.width, m_renderExtent.height, m_hdrDepthImages[i], m_hdrDepthViews[i]);

        // G-Buffer images (per-frame)
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R8G8B8A8_UNORM, m_gbAlbedoImages[i],
                       m_gbAlbedoViews[i]);
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R16G16B16A16_SFLOAT, m_gbNormalImages[i],
                       m_gbNormalViews[i]);
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R8G8B8A8_UNORM, m_gbMaterialImages[i],
                       m_gbMaterialViews[i]);
    }

    // Bloom (1/4 res)
    makeColorImage(m_bloomExtent.width, m_bloomExtent.height, hdrFormat, m_bloomExtractImage, m_bloomExtractView);
    makeColorImage(m_bloomExtent.width, m_bloomExtent.height, hdrFormat, m_bloomBlurHImage, m_bloomBlurHView);
    makeColorImage(m_bloomExtent.width, m_bloomExtent.height, hdrFormat, m_bloomBlurVImage, m_bloomBlurVView);

    // AO (1/2 res)
    makeColorImage(m_aoExtent.width, m_aoExtent.height, aoFormat, m_aoImage, m_aoView);
    makeColorImage(m_aoExtent.width, m_aoExtent.height, aoFormat, m_aoBlurImage, m_aoBlurView);

    // SSR reflection (full render res, HDR format so reflected radiance survives)
    makeColorImage(m_renderExtent.width, m_renderExtent.height, hdrFormat, m_ssrImage, m_ssrView);

    // Auto-exposure luminance pyramid: 128², full mip chain, HDR format (no blit
    // conversion). TRANSFER_DST (blit in) + TRANSFER_SRC (mip downsample + copy out).
    {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.extent        = {kLumDim, kLumDim, 1};
        ii.mipLevels     = kLumMips;
        ii.arrayLayers   = 1;
        ii.format        = hdrFormat;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        m_lumImage = GpuImage(m_allocator, ii, ai);

        // Host-readable 1-pixel destination (RGBA16F = 8 B) per frame in flight.
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = 8;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo bai{};
        bai.usage = VMA_MEMORY_USAGE_AUTO;
        bai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
            m_lumReadback[i] = GpuBuffer(m_allocator, bi, bai);
    }

    // CSM shadow atlas (kShadowAtlasW × kShadowDim, D32_SFLOAT, per frame):
    // DEPTH_STENCIL_ATTACHMENT (written by the depth pass, one cascade per slice)
    // + SAMPLED (read by lighting.frag).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_shadowImages[i] =
            gpu::deviceLocalImage(m_allocator, kShadowAtlasW, kShadowDim, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        m_shadowViews[i] = createImageView(m_shadowImages[i].handle(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

void PostProcessManager::destroyImages() {
    auto destroy = [&](GpuImage& img, VkImageView& view) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        img.reset();  // VMA-backed: frees image + sub-allocation
    };

    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        destroy(m_hdrImages[i], m_hdrViews[i]);
        destroy(m_hdrDepthImages[i], m_hdrDepthViews[i]);
        destroy(m_gbAlbedoImages[i], m_gbAlbedoViews[i]);
        destroy(m_gbNormalImages[i], m_gbNormalViews[i]);
        destroy(m_gbMaterialImages[i], m_gbMaterialViews[i]);
    }
    destroy(m_bloomExtractImage, m_bloomExtractView);
    destroy(m_bloomBlurHImage, m_bloomBlurHView);
    destroy(m_bloomBlurVImage, m_bloomBlurVView);
    destroy(m_aoImage, m_aoView);
    destroy(m_aoBlurImage, m_aoBlurView);
    destroy(m_ssrImage, m_ssrView);
    m_lumImage.reset();  // no view (transfer-only)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_lumReadback[i].reset();
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        destroy(m_shadowImages[i], m_shadowViews[i]);
}

// ══════════════════════════════════════════════════════════════════════
// Framebuffers
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createFramebuffers(const std::vector<VkImageView>& swapchainImageViews) {
    auto makeFB = [&](VkRenderPass rp, const std::vector<VkImageView>& views, VkExtent2D ext) -> VkFramebuffer {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = rp;
        info.attachmentCount = static_cast<uint32_t>(views.size());
        info.pAttachments    = views.data();
        info.width           = ext.width;
        info.height          = ext.height;
        info.layers          = 1;

        VkFramebuffer fb;
        VK_CHECK(vkCreateFramebuffer(m_device, &info, nullptr, &fb));
        return fb;
    };

    // G-Buffer framebuffers (3 color MRT + depth per frame) — render extent (SSAA)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_gbufferFramebuffers[i] =
            makeFB(m_gbufferRenderPass,
                   {m_gbAlbedoViews[i], m_gbNormalViews[i], m_gbMaterialViews[i], m_hdrDepthViews[i]}, m_renderExtent);
    }

    // HDR framebuffers (lighting output, color only per frame) — render extent (SSAA)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_hdrFramebuffers[i]      = makeFB(m_hdrRenderPass, {m_hdrViews[i], m_hdrDepthViews[i]}, m_renderExtent);
        m_lightingFramebuffers[i] = makeFB(m_lightingRenderPass, {m_hdrViews[i]}, m_renderExtent);
    }

    // Bloom framebuffers
    m_bloomExtractFB = makeFB(m_bloomRenderPass, {m_bloomExtractView}, m_bloomExtent);
    m_bloomBlurHFB   = makeFB(m_bloomRenderPass, {m_bloomBlurHView}, m_bloomExtent);
    m_bloomBlurVFB   = makeFB(m_bloomRenderPass, {m_bloomBlurVView}, m_bloomExtent);

    // AO framebuffers
    m_aoFB     = makeFB(m_aoRenderPass, {m_aoView}, m_aoExtent);
    m_aoBlurFB = makeFB(m_aoRenderPass, {m_aoBlurView}, m_aoExtent);

    // SSR framebuffer (render extent) — reuses the lighting render pass (same fmt)
    m_ssrFB = makeFB(m_lightingRenderPass, {m_ssrView}, m_renderExtent);

    // Composite framebuffers (one per swapchain image) — SWAP extent, NOT the
    // render extent: these wrap the swapchain images, and the composite pass
    // downsamples the high-res HDR into them (the SSAA resolve).
    m_compositeFBs.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        m_compositeFBs[i] = makeFB(m_compositeRenderPass, {swapchainImageViews[i]}, m_swapExtent);
    }

    // Shadow atlas framebuffers (kShadowAtlasW × kShadowDim, one depth attachment per frame)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_shadowFramebuffers[i] = makeFB(m_shadowRenderPass, {m_shadowViews[i]}, {kShadowAtlasW, kShadowDim});
    }
}

void PostProcessManager::destroyFramebuffers() {
    auto destroy = [&](VkFramebuffer& fb) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        }
    };

    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        destroy(m_gbufferFramebuffers[i]);
        destroy(m_hdrFramebuffers[i]);
        destroy(m_lightingFramebuffers[i]);
    }
    destroy(m_bloomExtractFB);
    destroy(m_bloomBlurHFB);
    destroy(m_bloomBlurVFB);
    destroy(m_aoFB);
    destroy(m_aoBlurFB);
    destroy(m_ssrFB);
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        destroy(m_shadowFramebuffers[i]);
    for (auto& fb : m_compositeFBs)
        destroy(fb);
    m_compositeFBs.clear();
}

// ══════════════════════════════════════════════════════════════════════
// Descriptors — one-texture sets for bloom/AO, three-texture for composite
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createDescriptors() {
    // Layout: single combined_image_sampler
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings    = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_singleTexLayout));
    }

    // Layout: 4 combined_image_samplers (HDR + bloom + AO + SSR)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t i = 0; i < bindings.size(); i++) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings    = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_compositeTexLayout));
    }

    // Pipeline layouts (with push constants)
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(PostProcessParams);

    m_postProcessLayout = Pipeline::createLayout(m_device, {m_singleTexLayout}, {pcRange});
    m_compositeLayout   = Pipeline::createLayout(m_device, {m_compositeTexLayout}, {pcRange});

    // SSAO layout: single depth texture (set 0) + a larger 144-B push block
    // (2×mat4 + params). Kept separate from m_postProcessLayout, whose push range
    // is too small for it.
    VkPushConstantRange ssaoPC{};
    ssaoPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssaoPC.offset     = 0;
    ssaoPC.size       = sizeof(SsaoParams);
    m_ssaoLayout      = Pipeline::createLayout(m_device, {m_singleTexLayout}, {ssaoPC});

    // Lighting descriptor layout: 4 G-buffer textures (albedo, normal, material, depth)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t i = 0; i < 4; i++) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 4;
        info.pBindings    = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_lightingTexLayout));
    }

    // SSR layout (needs m_lightingTexLayout, created just above): set 0 = G-buffer
    // (depth/normal/material), set 1 = lit HDR + a 144-B push block.
    VkPushConstantRange ssrPC{};
    ssrPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ssrPC.offset     = 0;
    ssrPC.size       = sizeof(SsrParams);
    m_ssrLayout      = Pipeline::createLayout(m_device, {m_lightingTexLayout, m_singleTexLayout}, {ssrPC});

    // Shadow descriptor layout: 1 combined_image_sampler (sampler2DShadow), fragment
    // stage — set 2 in the lighting pipeline.
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings    = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_shadowTexLayout));
    }

    // Lighting pipeline layout: set 0 = camera+lights (from Renderer), set 1 = G-buffer textures
    // Push constants: invView + invProj = 128 bytes
    VkPushConstantRange lightingPC{};
    lightingPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightingPC.offset     = 0;
    lightingPC.size       = 128;  // 2 × mat4

    // NOTE: The lighting pass needs the camera/lights descriptor set (set 0) from Renderer
    // plus its own G-buffer set (set 1). We pass the Renderer's set 0 layout externally.
    // For now, create the lighting layout with just the G-buffer set.
    // The Renderer will bind set 0 from its own pool and set 1 from ours.
    // This requires the lighting pipeline layout to reference BOTH set layouts.
    // We'll store the Renderer's set 0 layout and build the combined layout at pipeline creation time.
    // SIMPLIFIED: just use set 0 = G-buffer textures + push constants with invView/invProj.
    // Camera/lights data embedded in push constants or a UBO bound at set 0.
    // ACTUALLY: cleanest approach is lighting layout = set 0 = camera/lights (Renderer manages),
    // set 1 = G-buffer textures (PostProcessManager manages).
    // But we don't have the Renderer's set 0 layout here. So we'll create the lighting pipeline
    // layout in Renderer.cpp, not here. We'll just store the G-buffer descriptor set layout and sets.

    // Descriptor pool — 6 single (bloom×3 + AO-depth×PP_MAX_FRAMES + AO-blur) +
    // 2 composite + 2 lighting + 2 shadow = 12 sets, 22 samplers. Sized to 16/32
    // for headroom.
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 32;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = 16;
    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));

    // Allocate single-texture sets
    auto allocSingleSet = [&]() -> VkDescriptorSet {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_singleTexLayout;
        VkDescriptorSet set;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &set));
        return set;
    };

    m_bloomExtractSet = allocSingleSet();
    m_bloomBlurHSet   = allocSingleSet();
    m_bloomBlurVSet   = allocSingleSet();
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_aoSets[i] = allocSingleSet();  // per-frame: SSAO samples that frame's depth
    m_aoBlurSet       = allocSingleSet();
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        m_ssrHdrSets[i] = allocSingleSet();  // per-frame: SSR samples that frame's lit HDR

    // Allocate composite sets (per frame)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_compositeTexLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &m_compositeSets[i]));
    }

    // Write descriptor sets
    auto writeSingleLayout = [&](VkDescriptorSet set, VkImageView view, VkImageLayout layout) {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = layout;
        imgInfo.imageView   = view;
        imgInfo.sampler     = m_sampler;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = set;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    };
    auto writeSingle = [&](VkDescriptorSet set, VkImageView view) {
        writeSingleLayout(set, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };

    // Bloom extract reads HDR (use frame 0 for now; we rebind per-frame in composite)
    writeSingle(m_bloomExtractSet, m_hdrViews[0]);
    writeSingle(m_bloomBlurHSet, m_bloomExtractView);
    writeSingle(m_bloomBlurVSet, m_bloomBlurHView);
    // SSAO samples the scene depth (a depth image → DEPTH_STENCIL_READ_ONLY layout),
    // per-frame so each frame reads its own depth (no cross-frame ghosting).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingleLayout(m_aoSets[i], m_hdrDepthViews[i], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    writeSingle(m_aoBlurSet, m_aoView);
    // SSR samples each frame's lit HDR (colour, SHADER_READ).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        writeSingle(m_ssrHdrSets[i], m_hdrViews[i]);

    // Composite sets: HDR (per-frame) + bloom (shared) + AO (shared) + SSR (shared)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        std::array<VkDescriptorImageInfo, 4> imgInfos{};
        imgInfos[0] = {m_sampler, m_hdrViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imgInfos[1] = {m_sampler, m_bloomBlurVView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imgInfos[2] = {m_sampler, m_aoBlurView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        imgInfos[3] = {m_sampler, m_ssrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 4> writes{};
        for (uint32_t b = 0; b < imgInfos.size(); b++) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_compositeSets[i];
            writes[b].dstBinding      = b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &imgInfos[b];
        }
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Lighting sets: 4 G-buffer textures per frame (albedo, normal, material, depth)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_lightingTexLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &m_lightingSets[i]));

        std::array<VkDescriptorImageInfo, 4> gbInfos{};
        gbInfos[0] = {m_sampler, m_gbAlbedoViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbInfos[1] = {m_sampler, m_gbNormalViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbInfos[2] = {m_sampler, m_gbMaterialViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        gbInfos[3] = {m_sampler, m_hdrDepthViews[i], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 4> gbWrites{};
        for (uint32_t b = 0; b < 4; b++) {
            gbWrites[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gbWrites[b].dstSet          = m_lightingSets[i];
            gbWrites[b].dstBinding      = b;
            gbWrites[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbWrites[b].descriptorCount = 1;
            gbWrites[b].pImageInfo      = &gbInfos[b];
        }
        vkUpdateDescriptorSets(m_device, 4, gbWrites.data(), 0, nullptr);
    }

    // Shadow sets: one sampler2DShadow per frame, bound with the compare sampler.
    // Layout is DEPTH_STENCIL_READ_ONLY_OPTIMAL (the shadow render pass leaves the
    // image in that layout on store).
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_shadowTexLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &m_shadowSets[i]));

        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler     = m_shadowSampler;
        shadowInfo.imageView   = m_shadowViews[i];
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_shadowSets[i];
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &shadowInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

void PostProcessManager::destroyDescriptors() {
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_postProcessLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_postProcessLayout, nullptr);
        m_postProcessLayout = VK_NULL_HANDLE;
    }
    if (m_compositeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_compositeLayout, nullptr);
        m_compositeLayout = VK_NULL_HANDLE;
    }
    if (m_ssaoLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_ssaoLayout, nullptr);
        m_ssaoLayout = VK_NULL_HANDLE;
    }
    if (m_ssrLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_ssrLayout, nullptr);
        m_ssrLayout = VK_NULL_HANDLE;
    }
    if (m_singleTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_singleTexLayout, nullptr);
        m_singleTexLayout = VK_NULL_HANDLE;
    }
    if (m_compositeTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_compositeTexLayout, nullptr);
        m_compositeTexLayout = VK_NULL_HANDLE;
    }
    if (m_lightingTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_lightingTexLayout, nullptr);
        m_lightingTexLayout = VK_NULL_HANDLE;
    }
    if (m_shadowTexLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_shadowTexLayout, nullptr);
        m_shadowTexLayout = VK_NULL_HANDLE;
    }
}

// ══════════════════════════════════════════════════════════════════════
// Pipelines — fullscreen quads with no vertex input
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createPipelines() {
    auto makeFullscreenPipeline = [&](const std::string& fragShader, VkRenderPass rp, VkPipelineLayout layout,
                                      VkExtent2D ext) -> VkPipeline {
        PipelineConfig cfg{};
        cfg.vertShaderPath   = std::string(SHADER_DIR) + "/fullscreen.vert.spv";
        cfg.fragShaderPath   = std::string(SHADER_DIR) + "/" + fragShader + ".spv";
        cfg.noVertexInput    = true;
        cfg.enableDepthTest  = false;
        cfg.enableDepthWrite = false;
        cfg.cullMode         = VK_CULL_MODE_NONE;
        cfg.pipelineLayout   = layout;
        return Pipeline::create(m_device, cfg, rp, ext);
    };

    m_bloomExtractPipeline =
        makeFullscreenPipeline("bloom_extract.frag", m_bloomRenderPass, m_postProcessLayout, m_bloomExtent);
    m_bloomBlurPipeline =
        makeFullscreenPipeline("bloom_blur.frag", m_bloomRenderPass, m_postProcessLayout, m_bloomExtent);
    // SSAO (depth → AO) uses its own 144-B push layout; AO-blur has no push block
    // so it reuses m_postProcessLayout (an unused push range is harmless). Both
    // render at the AO extent (1/2 render res). SSAO is recorded per frame only
    // under SWISH_DEBUG_UI; in release the AO blur image stays primed to white by
    // primeAOTexture() so the composite `hdr *= ao` is a no-op (release unchanged).
    m_ssaoPipeline   = makeFullscreenPipeline("ssao.frag", m_aoRenderPass, m_ssaoLayout, m_aoExtent);
    m_aoBlurPipeline = makeFullscreenPipeline("ao_blur.frag", m_aoRenderPass, m_postProcessLayout, m_aoExtent);
    // SSR: full render-res reflection, reuses the lighting render pass (R16F color).
    m_ssrPipeline    = makeFullscreenPipeline("ssr.frag", m_lightingRenderPass, m_ssrLayout, m_renderExtent);
    // Composite outputs at the swap extent (dynamic viewport/scissor make the
    // extent cosmetic, but keep it consistent with the composite FB size).
    m_compositePipeline =
        makeFullscreenPipeline("composite.frag", m_compositeRenderPass, m_compositeLayout, m_swapExtent);
}

void PostProcessManager::destroyPipelines() {
    auto destroy = [&](VkPipeline& p) {
        if (p != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    };
    destroy(m_bloomExtractPipeline);
    destroy(m_bloomBlurPipeline);
    destroy(m_compositePipeline);
    destroy(m_ssaoPipeline);
    destroy(m_aoBlurPipeline);
    destroy(m_ssrPipeline);
}

// The composite shader always samples the AO blur image (`hdr *= ao`) and the SSR
// image (`hdr += ssr`). When those passes are bypassed (SSAO/SSR are debug-only),
// prime the images once so those ops are no-ops: AO → white, SSR → black. Leaves
// both in SHADER_READ_ONLY_OPTIMAL. In debug the per-frame passes overwrite them.
void PostProcessManager::primeAOTexture() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clear{};
    clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = m_aoRenderPass;
    rp.framebuffer       = m_aoBlurFB;
    rp.renderArea.extent = m_aoExtent;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &clear;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);

    ResourceManager::insertImageBarrier(cmd, m_aoBlurImage.handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // SSR image → black (composite `hdr += ssr` is then a no-op until SSR runs).
    VkClearValue ssrClear{};
    ssrClear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo ssrRp{};
    ssrRp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ssrRp.renderPass        = m_lightingRenderPass;  // SSR reuses this pass
    ssrRp.framebuffer       = m_ssrFB;
    ssrRp.renderArea.extent = m_renderExtent;
    ssrRp.clearValueCount   = 1;
    ssrRp.pClearValues      = &ssrClear;
    vkCmdBeginRenderPass(cmd, &ssrRp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
    ResourceManager::insertImageBarrier(cmd, m_ssrImage.handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

}  // namespace swish
