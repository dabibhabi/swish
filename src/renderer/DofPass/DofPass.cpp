#include "DofPass.h"

#ifdef SWISH_DEBUG_UI

#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../Pipeline/Pipeline.h"
#include "../Renderer/RendererServices.h"
#include "../ResourceManager/ResourceManager.h"

#include <array>

namespace swish {

namespace {
// Explicit color-image barrier with the given stages/access (insertImageBarrier
// doesn't cover the TRANSFER layouts the copy-back needs).
void barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
             VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = oldL;
    b.newLayout           = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.srcAccessMask       = srcA;
    b.dstAccessMask       = dstA;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}
}  // namespace

void DofPass::init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                   const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent) {
    m_allocator     = s.allocator;
    m_device        = s.device;
    m_commandPool   = s.commandPool;
    m_graphicsQueue = s.graphicsQueue;
    m_extent        = extent;

    createRenderPass(s.device);
    createImages(s);
    createFramebuffers(s.device);

    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(s.device, &si, nullptr, &m_sampler));

    createDescriptors(s.device, hdrViews, depthViews);
    createPipeline(s.device);
}

void DofPass::record(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage, const DofParams& params,
                     const Mat4& invProj) {
    if (!params.enabled)
        return;

    const uint32_t writeIdx = frameIndex;

    // HDR (this frame's lit+forward result) → readable as the DOF input.
    ResourceManager::insertImageBarrier(cmd, hdrImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Resolve into dof[writeIdx].
    {
        VkClearValue clear{};
        auto         bi      = vk::makeRenderPassBeginInfo();
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
    barrier(cmd, m_images[writeIdx].handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(cmd, hdrImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImage(cmd, m_images[writeIdx].handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hdrImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Restore HDR to COLOR_ATTACHMENT (as the god-rays pass left it) so the existing
    // downstream barrier/bloom logic is unchanged; leave the scratch readable.
    barrier(cmd, hdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    barrier(cmd, m_images[writeIdx].handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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

void DofPass::createImages(const RendererServices& s) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_images[i] = gpu::deviceLocalImage(
            s.allocator, m_extent.width, m_extent.height, m_format, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkImageViewCreateInfo vi{};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = m_images[i].handle();
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = m_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(s.device, &vi, nullptr, &m_views[i]));
    }
}

void DofPass::createRenderPass(VkDevice device) {
    VkAttachmentDescription att{};
    att.format         = m_format;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // every pixel is written
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription  subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkRenderPassCreateInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments    = &att;
    rp.subpassCount    = 1;
    rp.pSubpasses      = &subpass;
    VK_CHECK(vkCreateRenderPass(device, &rp, nullptr, &m_renderPass));
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
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t b = 0; b < 2; b++) {
        bindings[b].binding         = b;
        bindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings    = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &m_setLayout));

    VkDescriptorPoolSize       poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MAX_FRAMES_IN_FLIGHT};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &poolSize;
    pi.maxSets       = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &m_pool));

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_pool;
    ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    ai.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, m_sets.data()));

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

void DofPass::createPipeline(VkDevice device) {
    VkPushConstantRange pc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    m_pipeLayout = Pipeline::createLayout(device, {m_setLayout}, {pc});

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "dof.frag.spv";
    cfg.noVertexInput    = true;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.pipelineLayout   = m_pipeLayout;
    m_pipeline           = Pipeline::create(device, cfg, m_renderPass, m_extent);
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
