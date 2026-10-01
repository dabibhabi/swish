// PostProcessManager: offscreen images, framebuffers and the AO/SSR prime submit.

#include "PostProcessManager.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/ImageView.h"

#include <array>
#include <stdexcept>

namespace swish {

void PostProcessManager::createImages() {
    VkFormat hdrFormat   = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat = resource::findDepthFormat(m_physicalDevice);
    VkFormat aoFormat    = VK_FORMAT_R8_UNORM;

    auto makeColorImage = [&](uint32_t w, uint32_t h, VkFormat fmt, GpuImage& img, VkImageView& view,
                              VkImageUsageFlags extraUsage = 0) {
        img  = gpu::deviceLocalImage(m_allocator, w, h, fmt, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage);
        view = vkx::make_image_view2d(m_device, img.handle(), fmt);
    };

    auto makeDepthImage = [&](uint32_t w, uint32_t h, GpuImage& img, VkImageView& view) {
        img  = gpu::deviceLocalImage(m_allocator, w, h, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        view = vkx::make_image_view2d(m_device, img.handle(), depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    };

    // Per-frame HDR + depth
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        // HDR needs TRANSFER_SRC so the windshield-rain pass can snapshot it
        // (blit/copy → refraction-source image) for scene-refraction drops. In debug
        // builds it also needs TRANSFER_DST so the TAA pass can copy its resolved
        // result back into HDR (release omits it → identical image, no TAA).
        VkImageUsageFlags hdrExtra = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
#ifdef SWISH_DEBUG_UI
        hdrExtra |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#endif
        makeColorImage(m_renderExtent.width, m_renderExtent.height, hdrFormat, m_hdrImages[i], m_hdrViews[i], hdrExtra);
        makeDepthImage(m_renderExtent.width, m_renderExtent.height, m_hdrDepthImages[i], m_hdrDepthViews[i]);

        // G-Buffer images (per-frame)
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R8G8B8A8_UNORM, m_gbAlbedoImages[i],
                       m_gbAlbedoViews[i]);
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R16G16B16A16_SFLOAT, m_gbNormalImages[i],
                       m_gbNormalViews[i]);
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R8G8B8A8_UNORM, m_gbMaterialImages[i],
                       m_gbMaterialViews[i]);
        // Clearcoat + flake MRT (always-on) — see get_gbuffer_clearcoat_image.
        makeColorImage(m_renderExtent.width, m_renderExtent.height, VK_FORMAT_R8G8B8A8_UNORM, m_gbClearcoatImages[i],
                       m_gbClearcoatViews[i]);
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

    // God-rays light shafts (1/2 render res — radial blur is low-frequency; the
    // composite's linear upsample is smooth). HDR format so bright shafts survive.
    makeColorImage(m_godraysExtent.width, m_godraysExtent.height, hdrFormat, m_godraysImage, m_godraysView);

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
        m_shadowViews[i] = vkx::make_image_view2d(m_device, m_shadowImages[i].handle(), VK_FORMAT_D32_SFLOAT,
                                                  VK_IMAGE_ASPECT_DEPTH_BIT);
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
        destroy(m_gbClearcoatImages[i], m_gbClearcoatViews[i]);
    }
    destroy(m_bloomExtractImage, m_bloomExtractView);
    destroy(m_bloomBlurHImage, m_bloomBlurHView);
    destroy(m_bloomBlurVImage, m_bloomBlurVView);
    destroy(m_aoImage, m_aoView);
    destroy(m_aoBlurImage, m_aoBlurView);
    destroy(m_ssrImage, m_ssrView);
    destroy(m_godraysImage, m_godraysView);
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

    // G-Buffer framebuffers (4 color MRT + depth per frame) — render extent (SSAA)
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++) {
        m_gbufferFramebuffers[i] = makeFB(
            m_gbufferRenderPass,
            {m_gbAlbedoViews[i], m_gbNormalViews[i], m_gbMaterialViews[i], m_gbClearcoatViews[i], m_hdrDepthViews[i]},
            m_renderExtent);
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

    // God-rays framebuffer (1/2 render extent) — reuses the bloom render pass (same R16F fmt)
    m_godraysFB = makeFB(m_bloomRenderPass, {m_godraysView}, m_godraysExtent);

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
    destroy(m_godraysFB);
    for (uint32_t i = 0; i < PP_MAX_FRAMES; i++)
        destroy(m_shadowFramebuffers[i]);
    for (auto& fb : m_compositeFBs)
        destroy(fb);
    m_compositeFBs.clear();
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

    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_aoBlurImage.handle(), VK_IMAGE_ASPECT_COLOR_BIT);

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
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_ssrImage.handle(), VK_IMAGE_ASPECT_COLOR_BIT);

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
