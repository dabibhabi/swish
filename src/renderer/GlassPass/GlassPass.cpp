#include "GlassPass.h"

#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/GeometryProducer/GeometryProducer.h"
#include "renderer/Passes/ForwardPass.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/Renderer/RendererServices.h"
#include "renderer/RendererConfig.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "renderer/Vertex.h"
#include "scene/SceneTypes.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Init.h"

#include <memory>

namespace swish {

void GlassPass::init(const PassInitCtx& ctx) {
    create(ctx.services, ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent,
           ctx.cameraSetLayout);
}

void GlassPass::onResize(const PassInitCtx& ctx) {
    recreate(ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent, ctx.services.device);
}

void GlassPass::record(const FrameCtx& c) {
    const GeometryProducer& geo = *c.res.geometry;
    const SceneGeometry&    car = geo.dynamic();
    if (geo.glass_draws().empty() || !car.has_geometry()) return;

    VkDescriptorSet camSet = c.res.cameraUniforms->get_set(c.frameIndex);
    vkCmdBindDescriptorSets(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &camSet, 0, nullptr);
    record_draws(c.cmd, c.frameIndex, car.get_vertex_buffer(), car.get_index_buffer(), geo.glass_draws());
}

void GlassPass::create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                       const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                       VkDescriptorSetLayout cameraSetLayout) {
    m_extent      = extent;
    m_depthFormat = resource::findDepthFormat(s.physicalDevice);

    createRenderPass(s.device, m_depthFormat);
    createFramebuffers(s.device, hdrViews, depthViews);
    createPipeline(s.device, s.pipelineCache, cameraSetLayout);
}

void GlassPass::record_draws(VkCommandBuffer cmd, uint32_t frameIndex, VkBuffer carVBO, VkBuffer carIBO,
                             const std::vector<DrawCall>& glassDCs) const {
    if (glassDCs.empty() || carVBO == VK_NULL_HANDLE) return;

    VkClearValue clearVals[2]{};
    auto         beginInfo      = vkx::makeRenderPassBeginInfo();
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

    for (const auto& dc : glassDCs) {
        PushConstantData push{};
        push.model = dc.model;
        push.color = dc.color;
        vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           kPushConstantModelColorSize, &push);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void GlassPass::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                         const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                         VkDevice device) {
    m_extent = extent;
    destroyFramebuffers(device);
    createFramebuffers(device, hdrViews, depthViews);
}

void GlassPass::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
        m_pipeLayout = VK_NULL_HANDLE;
    }
    destroyFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// ── Private helpers ────────────────────────────────────────────────────

void GlassPass::createRenderPass(VkDevice device, VkFormat depthFormat) {
    // Same structure as RainSystem: LOAD both attachments, depth read-only; see ForwardPass.h.
    m_renderPass = make_forward_hdr_pass(device, depthFormat);
}

void GlassPass::createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                   const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView atts[] = {hdrViews[i], depthViews[i]};

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

void GlassPass::destroyFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void GlassPass::createPipeline(VkDevice device, VkPipelineCache cache, VkDescriptorSetLayout cameraSetLayout) {
    // Layout: set 0 = camera UBO, push constants = model + color (PushConstantData).
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = kPushConstantModelColorSize;  // glass shaders use model+color only

    m_pipeLayout = pipeline::create_layout(device, {cameraSetLayout}, {pcRange});

    auto binding = Vertex::getBindingDescription();
    auto attrs   = Vertex::getAttributeDescriptions();

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "glass.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "glass.frag.spv";
    cfg.vertexBindings   = {binding};
    cfg.vertexAttributes = {attrs[0], attrs[1], attrs[2], attrs[3]};
    cfg.cullMode         = VK_CULL_MODE_NONE;  // glass is double-sided
    cfg.enableDepthTest  = true;
    cfg.enableDepthWrite = false;  // glass never writes depth
    cfg.enableBlending   = true;   // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
    cfg.pipelineLayout   = m_pipeLayout;

    m_pipeline = pipeline::create_graphics(device, cache, cfg, m_renderPass, m_extent);
}

std::unique_ptr<IRenderPass> make_glass_pass() { return std::make_unique<GlassPass>(); }

}  // namespace swish
