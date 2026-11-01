#pragma once
// ============================================================================
// vkx/Rendering.h
//
// Vulkan concept : Dynamic rendering (Vulkan 1.3 core, VK_KHR_dynamic_rendering): render
//                  directly into image views with vkCmdBeginRendering / vkCmdEndRendering.
//                  No VkRenderPass, no VkFramebuffer; attachment layouts are NOT baked in a
//                  render pass, so the caller transitions them with explicit barriers first.
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Render Pass", section "Render Pass Instances / dynamic render pass
//                  instances" (anchor #renderpass, verify);
//                  https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdBeginRendering.html (verify);
//                  https://registry.khronos.org/vulkan/specs/latest/man/html/VkPipelineRenderingCreateInfo.html
//                  (verify)
// Guide          : https://docs.vulkan.org/guide/latest/extensions/VK_KHR_dynamic_rendering.html (verify)
// Vulkan-Hpp     : vk::RenderingInfo, vk::RenderingAttachmentInfo, vk::PipelineRenderingCreateInfo,
//                  vk::CommandBuffer::beginRendering / endRendering
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Supported (local 1.4.321 SDK vulkaninfo, 2026-09-29: Apple M3 Max, MoltenVK
//                  apiVersion 1.3.313, dynamicRendering = true). MoltenVK translates each
//                  vkCmdBeginRendering to a Metal render pass descriptor. Use only the plain
//                  color/depth attachment path here (no local-read, no suspend/resume, no
//                  multiview); those are the parts drivers implement least uniformly.
//                  https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md (verify)
//
// Purpose        : Pure struct builders (`color_attachment`, `depth_attachment`,
//                  `make_pipeline_rendering_info`, the `RenderingInfo` builder) plus thin
//                  `begin_rendering` / `end_rendering` recorders. Only the two recorders call
//                  Vulkan; they may only be CALLED when the device enabled
//                  VkPhysicalDeviceVulkan13Features::dynamicRendering
//                  (Device.cpp; exposed as RendererServices::dynamicRendering).
// Ownership      : Stateless. `RenderingInfo` owns its attachment array, points into itself, is
//                  therefore non-copyable/non-movable, and must outlive the begin_rendering call.
// Swish rules    : A pass keeps its classic VkRenderPass path behind the capability flag; both
//                  compile. When using this path the pass must transition each attachment to its
//                  attachment layout itself (vkx::image_barrier_auto) and leave it in the layout
//                  the classic render pass's finalLayout would have produced.
// See also       : Barrier.h (layout transitions), renderer/Pipeline/Pipeline.h (PipelineConfig
//                  dynamicRendering fields), README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <stdexcept>

namespace swish::vkx {

// A colour attachment description (pure). `clear` is only read when load == CLEAR.
inline VkRenderingAttachmentInfo color_attachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store,
                                                  VkClearColorValue clear  = {},
                                                  VkImageLayout     layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    VkRenderingAttachmentInfo a{};
    a.sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageView        = view;
    a.imageLayout      = layout;
    a.loadOp           = load;
    a.storeOp          = store;
    a.clearValue.color = clear;
    return a;
}

// A depth attachment description (pure). Reverse-Z callers clear to 0.0.
inline VkRenderingAttachmentInfo depth_attachment(VkImageView view, VkAttachmentLoadOp load, VkAttachmentStoreOp store,
                                                  float         clearDepth = 0.0f,
                                                  VkImageLayout layout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
    VkRenderingAttachmentInfo a{};
    a.sType                         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageView                     = view;
    a.imageLayout                   = layout;
    a.loadOp                        = load;
    a.storeOp                       = store;
    a.clearValue.depthStencil.depth = clearDepth;
    return a;
}

// Pipeline-side counterpart: chain into VkGraphicsPipelineCreateInfo::pNext with renderPass = VK_NULL_HANDLE.
// `colors` must outlive vkCreateGraphicsPipelines.
inline VkPipelineRenderingCreateInfo make_pipeline_rendering_info(const VkFormat* colors, uint32_t colorCount,
                                                                  VkFormat depth   = VK_FORMAT_UNDEFINED,
                                                                  VkFormat stencil = VK_FORMAT_UNDEFINED) {
    VkPipelineRenderingCreateInfo r{};
    r.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    r.colorAttachmentCount    = colorCount;
    r.pColorAttachmentFormats = colors;
    r.depthAttachmentFormat   = depth;
    r.stencilAttachmentFormat = stencil;
    return r;
}

// Builder for VkRenderingInfo. Up to kMaxColor colour attachments and an optional depth attachment.
class RenderingInfo {
public:
    static constexpr uint32_t kMaxColor = 8;

    explicit RenderingInfo(VkExtent2D extent) : m_area{{0, 0}, extent} {}
    RenderingInfo(const RenderingInfo&)            = delete;
    RenderingInfo& operator=(const RenderingInfo&) = delete;

    RenderingInfo& color(const VkRenderingAttachmentInfo& a) {
        if (m_colorCount >= kMaxColor) throw std::logic_error("vkx::RenderingInfo: too many colour attachments");
        m_colors[m_colorCount++] = a;
        return *this;
    }
    RenderingInfo& depth(const VkRenderingAttachmentInfo& a) {
        m_depth    = a;
        m_hasDepth = true;
        return *this;
    }
    RenderingInfo& layers(uint32_t n) {
        m_layers = n;
        return *this;
    }

    uint32_t color_count() const { return m_colorCount; }
    bool     has_depth() const { return m_hasDepth; }

    // Fills the pointers at call time; the returned struct is valid while *this is alive and unmodified.
    VkRenderingInfo get() const {
        VkRenderingInfo r{};
        r.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        r.renderArea           = m_area;
        r.layerCount           = m_layers;
        r.colorAttachmentCount = m_colorCount;
        r.pColorAttachments    = m_colorCount ? m_colors.data() : nullptr;
        r.pDepthAttachment     = m_hasDepth ? &m_depth : nullptr;
        return r;
    }

private:
    VkRect2D                                         m_area{};
    uint32_t                                         m_layers     = 1;
    uint32_t                                         m_colorCount = 0;
    bool                                             m_hasDepth   = false;
    std::array<VkRenderingAttachmentInfo, kMaxColor> m_colors{};
    VkRenderingAttachmentInfo                        m_depth{};
};

// Recorders (core 1.3 entry points). Only call when RendererServices::dynamicRendering is true.
inline void begin_rendering(VkCommandBuffer cmd, const RenderingInfo& info) {
    const VkRenderingInfo r = info.get();
    vkCmdBeginRendering(cmd, &r);
}

inline void end_rendering(VkCommandBuffer cmd) { vkCmdEndRendering(cmd); }

}  // namespace swish::vkx
