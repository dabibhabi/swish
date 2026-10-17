#include "VelocityPass.h"

#include "renderer/RendererConfig.h"

#ifdef SWISH_DEBUG_UI

#include "debug/DebugParams.h"
#include "renderer/GeometryProducer/GeometryProducer.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/Renderer/RendererServices.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "renderer/Vertex.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/ImageView.h"
#include "utils/vulkan/Init.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"

#include <array>
#include <memory>

namespace swish {

void VelocityPass::init(const PassInitCtx& ctx) {
    create(ctx.services, ctx.images.views(Img::HdrDepth), ctx.renderExtent);
    publish(ctx.images);
}

void VelocityPass::onResize(const PassInitCtx& ctx) {
    recreate(ctx.images.views(Img::HdrDepth), ctx.renderExtent, ctx.services.device);
    publish(ctx.images);
}

void VelocityPass::publish(ImageRegistry& images) const {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) images.set(Img::Velocity, i, m_images[i].handle(), m_views[i]);
}

void VelocityPass::update(const FrameUpdate& u) {
    if (!u.tune.taaEnabled || !u.camera) return;
    m_prevViewProj = m_lastUnjit;
    m_lastUnjit    = u.camera->get_projection_matrix_unjittered() * u.camera->get_view_matrix();
}

void VelocityPass::record(const FrameCtx& c) {
    if (!c.tune.taaEnabled) return;
    const SceneGeometry& car = c.res.geometry->dynamic();
    if (!c.camera || !car.has_geometry()) return;

    const Mat4 curViewProj = c.camera->get_projection_matrix() * c.camera->get_view_matrix();
    draw(c.cmd, c.frameIndex, car.get_vertex_buffer(), car.get_index_buffer(), car.get_draw_calls(),
         c.res.geometry->dynamic_prev_models(), curViewProj, m_prevViewProj);
}

void VelocityPass::create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                          VkExtent2D extent) {
    m_allocator   = s.allocator;
    m_device      = s.device;
    m_extent      = extent;
    m_depthFormat = resource::findDepthFormat(s.physicalDevice);

    createImages(s);
    createRenderPass(s.device, m_depthFormat);
    createFramebuffers(s.device, depthViews);
    createPipeline(s.device, s.pipelineCache);
}

void VelocityPass::draw(VkCommandBuffer cmd, uint32_t frameIndex, VkBuffer carVBO, VkBuffer carIBO,
                        const std::vector<DrawCall>& draws, const std::vector<Mat4>& prevModels,
                        const Mat4& curViewProj, const Mat4& prevViewProj) const {
    if (draws.empty() || carVBO == VK_NULL_HANDLE) return;

    // Topology just changed (e.g. wheel frames loaded mid-flight) — fall back to
    // "no motion this frame" for every draw rather than mis-pairing indices.
    const bool havePrev = prevModels.size() == draws.size();

    VkClearValue clearVals[2]{};
    clearVals[0].color          = {{0.0f, 0.0f, 0.0f, 0.0f}};  // rg = velocity, a = validity (0 = none)
    auto beginInfo              = vkx::makeRenderPassBeginInfo();
    beginInfo.renderPass        = m_renderPass;
    beginInfo.framebuffer       = m_framebuffers[frameIndex];
    beginInfo.renderArea.extent = m_extent;
    beginInfo.clearValueCount   = 2;
    beginInfo.pClearValues      = clearVals;

    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &carVBO, &offset);
    vkCmdBindIndexBuffer(cmd, carIBO, 0, VK_INDEX_TYPE_UINT32);

    for (size_t i = 0; i < draws.size(); ++i) {
        const DrawCall& dc = draws[i];
        Push            pc{};
        pc.curViewProj  = curViewProj;
        pc.prevViewProj = prevViewProj;
        pc.curModel     = dc.model;
        pc.prevModel    = havePrev ? prevModels[i] : dc.model;  // zero velocity fallback
        vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push), &pc);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void VelocityPass::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                            VkDevice device) {
    destroyImagesAndFramebuffers(device);
    m_extent = extent;
    RendererServices s{};
    s.device    = m_device;
    s.allocator = m_allocator;
    createImages(s);
    createFramebuffers(device, depthViews);
}

void VelocityPass::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
        m_pipeLayout = VK_NULL_HANDLE;
    }
    destroyImagesAndFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// ── Private helpers ────────────────────────────────────────────────────

void VelocityPass::createImages(const RendererServices& s) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_images[i] =
            gpu::deviceLocalImage(s.allocator, m_extent.width, m_extent.height, m_format, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        m_views[i] = vkx::make_image_view2d(m_device, m_images[i].handle(), m_format);
    }
}

void VelocityPass::createRenderPass(VkDevice device, VkFormat depthFormat) {
    const std::array<VkSubpassDependency, 3> deps = {
        // Entry: wait for whatever last wrote/read the depth buffer (the G-buffer pass -> lighting transition) before
        // this pass depth-tests it.
        vkx::subpass_dependency(VK_SUBPASS_EXTERNAL, 0,
                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT),
        // Exit: this pass's colour write must finish before TaaPass samples it.
        vkx::subpass_dependency(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT),
        // Colour entry: the UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL layout transition and the CLEAR loadOp are both
        // colour-attachment writes, so the implicit external dependency (dstAccess 0) leaves the clear unordered
        // after the transition (sync validation WRITE_AFTER_WRITE). Also orders this frame's clear after the
        // previous use of this per-frame target (TaaPass sampling it, FRAGMENT_SHADER read).
        vkx::subpass_dependency(VK_SUBPASS_EXTERNAL, 0,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)};

    vkx::ColorPassDesc d;
    d.color        = {.format = m_format,
                      .load   = VK_ATTACHMENT_LOAD_OP_CLEAR,  // full clear: 0 = "no per-object velocity here"
                      .final  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};  // ready for TaaPass to sample
    d.depth        = vkx::AttachmentDesc{.format  = depthFormat,
                                         .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                         .store   = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         .initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                         .final   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    d.dependencies = deps;
    m_renderPass   = vkx::make_color_pass(device, d);
}

void VelocityPass::createFramebuffers(VkDevice                                             device,
                                      const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView atts[] = {m_views[i], depthViews[i]};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments    = atts;
        fbInfo.width           = m_extent.width;
        fbInfo.height          = m_extent.height;
        fbInfo.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]));
    }
}

void VelocityPass::destroyImagesAndFramebuffers(VkDevice device) {
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

void VelocityPass::createPipeline(VkDevice device, VkPipelineCache cache) {
    const VkPushConstantRange pcRange = vkx::push_range<Push>(VK_SHADER_STAGE_VERTEX_BIT);
    m_pipeLayout                      = pipeline::create_layout(device, {}, {pcRange});

    auto binding = Vertex::getBindingDescription();
    auto attrs   = Vertex::getAttributeDescriptions();

    PipelineConfig cfg{};
    cfg.vertShaderPath = std::string(SHADER_DIR) + "velocity.vert.spv";
    cfg.fragShaderPath = std::string(SHADER_DIR) + "velocity.frag.spv";
    // velocity.vert reads only inPosition (location 0). Same binding (full Vertex stride), position attribute
    // only: declaring locations 1-3 that the shader never consumes trips the pipeline-creation validation warning.
    cfg.vertexBindings   = {binding};
    cfg.vertexAttributes = {attrs[0]};
    cfg.cullMode         = VK_CULL_MODE_BACK_BIT;  // matches ScenePipeline (opaque car geometry)
    cfg.enableDepthTest  = true;
    cfg.enableDepthWrite = false;  // read-only depth test against the already-resolved scene depth
    cfg.depthCompareOp   = VK_COMPARE_OP_GREATER_OR_EQUAL;  // reverse-Z; tolerant of this pass's own vertex math
    cfg.pipelineLayout   = m_pipeLayout;

    m_pipeline = pipeline::create_graphics(device, cache, cfg, m_renderPass, m_extent);
}

std::unique_ptr<IRenderPass> make_velocity_pass() { return std::make_unique<VelocityPass>(); }

}  // namespace swish

#endif  // SWISH_DEBUG_UI
