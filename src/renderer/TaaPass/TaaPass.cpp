#include "TaaPass.h"

#include "renderer/RendererConfig.h"

#ifdef SWISH_DEBUG_UI

#include "debug/DebugParams.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/Renderer/RendererServices.h"
#include "scene/Camera/Camera.h"
#include "utils/math/Sequences.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Descriptors.h"
#include "utils/vulkan/ImageView.h"
#include "utils/vulkan/Init.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/Rendering.h"
#include "utils/vulkan/Samplers.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace swish {

void TaaPass::init(const PassInitCtx& ctx) {
    create(ctx.services, ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.images.views(Img::Velocity),
           ctx.renderExtent);
}

void TaaPass::onResize(const PassInitCtx& ctx) {
    recreate(ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.images.views(Img::Velocity),
             ctx.renderExtent, ctx.services.device);
}

void TaaPass::beginFrame(const FrameUpdate& u) {
    if (!u.camera)
        return;
    if (u.tune.taaEnabled) {
        const uint32_t n = (m_frameCounter % 8u) + 1u;
        u.camera->set_jitter(Vec2((util::math::halton(n, 2) - 0.5f) * 2.0f / static_cast<float>(m_extent.width),
                                  (util::math::halton(n, 3) - 0.5f) * 2.0f / static_cast<float>(m_extent.height)));
        m_frameCounter++;
    } else {
        u.camera->set_jitter(Vec2(0.0f, 0.0f));
    }
}

void TaaPass::record(const FrameCtx& c) {
    if (!c.tune.taaEnabled || !c.camera)
        return;
    const Mat4 view     = c.camera->get_view_matrix();
    const Mat4 curInvVP = glm::inverse(c.camera->get_projection_matrix() * view);
    TaaParams  tp;
    tp.enabled         = true;
    tp.historyBlend    = c.tune.taaHistoryBlend;
    tp.motionBlur      = c.tune.motionBlurEnabled;
    tp.motionBlurScale = c.tune.motionBlurScale;
    resolve(c.cmd, c.frameIndex, c.images.image(Img::Hdr, c.frameIndex), tp, m_prevViewProj, curInvVP);
    // Store this frame's un-jittered VP as next frame's reprojection source.
    m_prevViewProj = c.camera->get_projection_matrix_unjittered() * view;
}

void TaaPass::create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                     const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                     const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews, VkExtent2D extent) {
    m_allocator        = s.allocator;
    m_device           = s.device;
    m_commandPool      = s.commandPool;
    m_graphicsQueue    = s.graphicsQueue;
    m_extent           = extent;
    m_dynamicRendering = s.dynamicRendering;
    m_sync2            = s.synchronization2;

    // Classic path only: the render pass + framebuffers are not created when rendering dynamically.
    if (!m_dynamicRendering)
        createRenderPass(s.device);
    createImages(s);
    if (!m_dynamicRendering)
        createFramebuffers(s.device);

    m_sampler = vkx::make_sampler(s.device);  // linear, clamp-to-edge

    createDescriptors(s.device, hdrViews, depthViews, velocityViews);
    createPipeline(s.device, s.pipelineCache);
    primeHistory(s);
}

void TaaPass::resolve(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const TaaParams& params,
                      const Mat4& prevViewProj, const Mat4& curInvViewProj) {
    if (!params.enabled)
        return;

    const uint32_t writeIdx = frameIndex;  // history read is taa[1-frameIndex] (bound in the set)

    // Every transition goes through the shared vkx table; sync2 (vkCmdPipelineBarrier2) when the device
    // enabled it, classic vkCmdPipelineBarrier otherwise. Same masks either way.
    auto transition = [&](VkImage img, VkImageLayout oldL, VkImageLayout newL) {
        vkx::image_barrier_auto(cmd, m_sync2, img, VK_IMAGE_ASPECT_COLOR_BIT, oldL, newL);
    };
    constexpr VkImageLayout kColor = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    constexpr VkImageLayout kRead  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    constexpr VkImageLayout kXSrc  = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    constexpr VkImageLayout kXDst  = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    // HDR (this frame's lit+forward result) → readable as the TAA input.
    transition(hdrImage, kColor, kRead);

    // Resolve into taa[writeIdx].
    {
        if (m_dynamicRendering) {
            // No render pass to move the target for us: taa[writeIdx] rests in SHADER_READ (last frame's
            // history read / primeHistory), so make it a colour attachment explicitly. This barrier also
            // covers the write-after-read against last frame's history sample. Contents are fully
            // overwritten (DONT_CARE load), matching the classic render pass.
            transition(m_images[writeIdx].handle(), kRead, kColor);
            vkx::RenderingInfo ri(m_extent);
            ri.color(vkx::color_attachment(m_views[writeIdx], VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                           VK_ATTACHMENT_STORE_OP_STORE));
            vkx::begin_rendering(cmd, ri);
        } else {
            VkClearValue clear{};
            auto         bi      = vkx::makeRenderPassBeginInfo();
            bi.renderPass        = m_renderPass;
            bi.framebuffer       = m_framebuffers[writeIdx];
            bi.renderArea.extent = m_extent;
            bi.clearValueCount   = 1;
            bi.pClearValues      = &clear;
            vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        }

        VkViewport vp{0.0f, 0.0f, float(m_extent.width), float(m_extent.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, m_extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_sets[writeIdx], 0,
                                nullptr);

        Push pc{};
        pc.prevViewProj = prevViewProj;
        pc.invViewProj  = curInvViewProj;
        pc.params       = Vec4(params.historyBlend, params.motionBlur ? params.motionBlurScale : 0.0f,
                               1.0f / float(m_extent.width), 1.0f / float(m_extent.height));
        vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        if (m_dynamicRendering)
            vkx::end_rendering(cmd);
        else
            vkCmdEndRenderPass(cmd);
    }

    // Copy the resolved output back into HDR so bloom/composite read it unchanged. Both paths leave
    // taa[writeIdx] in COLOR_ATTACHMENT here (render pass finalLayout / the explicit barrier above).
    transition(m_images[writeIdx].handle(), kColor, kXSrc);
    transition(hdrImage, kRead, kXDst);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImage(cmd, m_images[writeIdx].handle(), kXSrc, hdrImage, kXDst, 1, &region);

    // Restore HDR to COLOR_ATTACHMENT (as the forward passes left it) so the existing
    // downstream barrier/bloom logic is unchanged; leave taa[writeIdx] readable as
    // next frame's history.
    transition(hdrImage, kXDst, kColor);
    transition(m_images[writeIdx].handle(), kXSrc, kRead);
}

void TaaPass::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                       const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                       const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews, VkExtent2D extent,
                       VkDevice device) {
    destroyImagesAndFramebuffers(device);
    m_extent = extent;
    RendererServices s{};
    s.device        = m_device;
    s.commandPool   = m_commandPool;
    s.graphicsQueue = m_graphicsQueue;
    s.allocator     = m_allocator;
    createImages(s);
    if (!m_dynamicRendering)
        createFramebuffers(device);
    writeDescriptors(device, hdrViews, depthViews, velocityViews);
    primeHistory(s);
}

void TaaPass::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipeLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
    if (m_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, m_sampler, nullptr);
    destroyImagesAndFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, m_renderPass, nullptr);
    m_pipeline   = VK_NULL_HANDLE;
    m_pipeLayout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_setLayout  = VK_NULL_HANDLE;
    m_sampler    = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
}

// ── Private ─────────────────────────────────────────────────────────────

void TaaPass::createImages(const RendererServices& s) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_images[i] = gpu::deviceLocalImage(
            s.allocator, m_extent.width, m_extent.height, m_format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_views[i] = vkx::make_image_view2d(s.device, m_images[i].handle(), m_format);
    }
}

void TaaPass::createRenderPass(VkDevice device) {
    vkx::ColorPassDesc d;
    d.color      = {.format = m_format};  // DONT_CARE load: every pixel is written
    m_renderPass = vkx::make_color_pass(device, d);
}

void TaaPass::createFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments    = &m_views[i];
        fb.width           = m_extent.width;
        fb.height          = m_extent.height;
        fb.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &m_framebuffers[i]));
    }
}

void TaaPass::destroyImagesAndFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
        if (m_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_views[i], nullptr);
            m_views[i] = VK_NULL_HANDLE;
        }
        m_images[i].reset();
    }
}

void TaaPass::createDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews) {
    m_setLayout = vkx::make_sampler_layout(device, 4);
    m_pool =
        vkx::make_pool(device, {.maxSets = MAX_FRAMES_IN_FLIGHT, .combinedImageSamplers = 4 * MAX_FRAMES_IN_FLIGHT});
    vkx::allocate_sets(device, m_pool, m_setLayout, m_sets);

    writeDescriptors(device, hdrViews, depthViews, velocityViews);
}

void TaaPass::writeDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                               const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                               const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& velocityViews) {
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        // Set f resolves frame f: reads this frame's HDR + depth + per-object velocity
        // (VelocityPass, todo.md), and taa[1-f] history. Depth is sampled while it sits in
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL (set by the lighting pass, kept by the read-only-
        // depth forward passes) — not the generic SHADER_READ layout, or the descriptor-
        // layout-match validation trips. The velocity buffer is written by VelocityPass and
        // left in SHADER_READ_ONLY_OPTIMAL by its render pass's final layout.
        VkDescriptorImageInfo infos[4] = {
            {m_sampler, hdrViews[f], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_sampler, depthViews[f], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {m_sampler, m_views[(f + 1) % MAX_FRAMES_IN_FLIGHT], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_sampler, velocityViews[f], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        VkWriteDescriptorSet writes[4]{};
        for (uint32_t b = 0; b < 4; b++) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_sets[f];
            writes[b].dstBinding      = b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &infos[b];
        }
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    }
}

void TaaPass::createPipeline(VkDevice device, VkPipelineCache cache) {
    const VkPushConstantRange pc = vkx::push_range<Push>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_pipeLayout                 = pipeline::create_layout(device, {m_setLayout}, {pc});

    PipelineConfig cfg{};
    if (m_dynamicRendering) {
        cfg.dynamicRendering    = true;  // formats replace the render pass; renderPass argument is null
        cfg.dynamicColorFormats = {m_format};
    }
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "taa.frag.spv";
    cfg.noVertexInput    = true;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.pipelineLayout   = m_pipeLayout;
    m_pipeline =
        pipeline::create_graphics(device, cache, cfg, m_dynamicRendering ? VK_NULL_HANDLE : m_renderPass, m_extent);
}

void TaaPass::primeHistory(const RendererServices& s) {
    // Transition both images UNDEFINED → SHADER_READ so the first history sample is a
    // valid layout (content is garbage, but the neighborhood clamp bounds it).
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = s.commandPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd   = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(s.device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        vkx::image_barrier_auto(cmd, m_sync2, m_images[i].handle(), VK_IMAGE_ASPECT_COLOR_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(s.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(s.graphicsQueue);
    vkFreeCommandBuffers(s.device, s.commandPool, 1, &cmd);
}

std::unique_ptr<IRenderPass> make_taa_pass() {
    return std::make_unique<TaaPass>();
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
