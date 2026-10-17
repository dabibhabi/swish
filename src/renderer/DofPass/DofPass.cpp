#include "DofPass.h"

#include "renderer/RendererConfig.h"

#ifdef SWISH_DEBUG_UI

#include "debug/DebugParams.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/Renderer/RendererServices.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Descriptors.h"
#include "utils/vulkan/ImageView.h"
#include "utils/vulkan/Init.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/Samplers.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace swish {

void DofPass::init(const PassInitCtx& ctx) {
    create(ctx.services, ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent);
}

void DofPass::onResize(const PassInitCtx& ctx) {
    recreate(ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent, ctx.services.device);
}

void DofPass::record(const FrameCtx& c) {
    if (!c.tune.dofEnabled || !c.camera) return;
    DofParams dp;
    dp.enabled         = true;
    dp.focusDist       = c.tune.dofFocusDist;
    dp.focusRange      = c.tune.dofFocusRange;
    dp.maxCoC          = c.tune.dofMaxCoC;
    const Mat4 invProj = glm::inverse(c.camera->get_projection_matrix());
    resolve(c.cmd, c.frameIndex, c.images.image(Img::Hdr, c.frameIndex), dp, invProj);
}

void DofPass::create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                     const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent) {
    m_allocator     = s.allocator;
    m_device        = s.device;
    m_commandPool   = s.commandPool;
    m_graphicsQueue = s.graphicsQueue;
    m_extent        = extent;

    createRenderPass(s.device);
    createImages(s);
    createFramebuffers(s.device);

    m_sampler = vkx::make_sampler(s.device);  // linear, clamp-to-edge

    createDescriptors(s.device, hdrViews, depthViews);
    createPipeline(s.device, s.pipelineCache);
}

void DofPass::resolve(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const DofParams& params,
                      const Mat4& invProj) {
    if (!params.enabled) return;

    const uint32_t writeIdx = frameIndex;

    // HDR (this frame's lit+forward result) → readable as the DOF input.
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT);

    // Resolve into dof[writeIdx].
    {
        VkClearValue clear{};
        auto         bi      = vkx::makeRenderPassBeginInfo();
        bi.renderPass        = m_renderPass;
        bi.framebuffer       = m_framebuffers[writeIdx];
        bi.renderArea.extent = m_extent;
        bi.clearValueCount   = 1;
        bi.pClearValues      = &clear;
        vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{0.0f, 0.0f, float(m_extent.width), float(m_extent.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, m_extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_sets[writeIdx], 0,
                                nullptr);

        Push pc{};
        pc.invProj = invProj;
        pc.focus   = Vec4(params.focusDist, params.focusRange, params.maxCoC, 0.0f);
        pc.texel   = Vec4(1.0f / float(m_extent.width), 1.0f / float(m_extent.height), 0.0f, 0.0f);
        vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // Copy the resolved output back into HDR so bloom/composite read it unchanged.
    // COLOR_ATTACHMENT -> TRANSFER_SRC: COLOR_OUTPUT/COLOR_WRITE -> TRANSFER/TRANSFER_READ (identical to the
    // previous explicit masks).
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
        cmd, m_images[writeIdx].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL>(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImage(cmd, m_images[writeIdx].handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hdrImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Restore HDR to COLOR_ATTACHMENT (as the god-rays pass left it) so the existing
    // downstream barrier/bloom logic is unchanged; leave the scratch readable.
    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL>(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT);
    // TRANSFER_SRC -> SHADER_READ: TRANSFER/TRANSFER_READ -> FRAGMENT/SHADER_READ (identical to the previous
    // explicit masks).
    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_images[writeIdx].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
}

void DofPass::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                       const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                       VkDevice device) {
    destroyImagesAndFramebuffers(device);
    m_extent = extent;
    RendererServices s{};
    s.device        = m_device;
    s.commandPool   = m_commandPool;
    s.graphicsQueue = m_graphicsQueue;
    s.allocator     = m_allocator;
    createImages(s);
    createFramebuffers(device);
    writeDescriptors(device, hdrViews, depthViews);
}

void DofPass::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipeLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
    if (m_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
    destroyImagesAndFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_renderPass, nullptr);
    m_pipeline   = VK_NULL_HANDLE;
    m_pipeLayout = VK_NULL_HANDLE;
    m_pool       = VK_NULL_HANDLE;
    m_setLayout  = VK_NULL_HANDLE;
    m_sampler    = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
}

// ── Private ─────────────────────────────────────────────────────────────

void DofPass::createImages(const RendererServices& s) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_images[i] = gpu::deviceLocalImage(
            s.allocator, m_extent.width, m_extent.height, m_format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_views[i] = vkx::make_image_view2d(s.device, m_images[i].handle(), m_format);
    }
}

void DofPass::createRenderPass(VkDevice device) {
    vkx::ColorPassDesc d;
    d.color      = {.format = m_format};  // DONT_CARE load: every pixel is written
    m_renderPass = vkx::make_color_pass(device, d);
}

void DofPass::createFramebuffers(VkDevice device) {
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

void DofPass::destroyImagesAndFramebuffers(VkDevice device) {
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

void DofPass::createDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    m_setLayout = vkx::make_sampler_layout(device, 2);
    m_pool =
        vkx::make_pool(device, {.maxSets = MAX_FRAMES_IN_FLIGHT, .combinedImageSamplers = 2 * MAX_FRAMES_IN_FLIGHT});
    vkx::allocate_sets(device, m_pool, m_setLayout, m_sets);

    writeDescriptors(device, hdrViews, depthViews);
}

void DofPass::writeDescriptors(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                               const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
        // Set f resolves frame f: reads this frame's HDR (binding 0) + depth (binding 1).
        // Depth is sampled while it sits in DEPTH_STENCIL_READ_ONLY_OPTIMAL (set by the
        // lighting pass, still held when DOF runs right after the god-rays pass) — not the
        // generic SHADER_READ layout, or the descriptor-layout-match validation trips.
        VkDescriptorImageInfo infos[2] = {
            {m_sampler, hdrViews[f], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_sampler, depthViews[f], VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        };
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t b = 0; b < 2; b++) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = m_sets[f];
            writes[b].dstBinding      = b;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].descriptorCount = 1;
            writes[b].pImageInfo      = &infos[b];
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

void DofPass::createPipeline(VkDevice device, VkPipelineCache cache) {
    const VkPushConstantRange pc = vkx::push_range<Push>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_pipeLayout                 = pipeline::create_layout(device, {m_setLayout}, {pc});

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "dof.frag.spv";
    cfg.noVertexInput    = true;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.pipelineLayout   = m_pipeLayout;
    m_pipeline           = pipeline::create_graphics(device, cache, cfg, m_renderPass, m_extent);
}

std::unique_ptr<IRenderPass> make_dof_pass() { return std::make_unique<DofPass>(); }

}  // namespace swish

#endif  // SWISH_DEBUG_UI
