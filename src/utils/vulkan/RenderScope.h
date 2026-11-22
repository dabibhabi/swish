#pragma once
// ============================================================================
// vkx/RenderScope.h
//
// Vulkan concept : Render pass instance (vkCmdBeginRenderPass / vkCmdEndRenderPass) and the
//                  dynamic viewport + scissor state every Swish pipeline sets per pass.
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Render Pass", section "Render Pass Commands" (anchor #renderpass-commands,
//                  verify); "Fixed-Function Vertex Post-Processing / Controlling the Viewport"
//                  (vkCmdSetViewport / vkCmdSetScissor, verify)
// Guide          : https://docs.vulkan.org/guide/latest/ (render pass page; verify)
// Vulkan-Hpp     : vk::CommandBuffer::beginRenderPass / endRenderPass (vk::RenderPassBeginInfo)
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Each render pass instance maps to a Metal render command encoder; keep them few
//                  and do not interleave compute or transfer commands inside one (Swish records the
//                  spray compute and the HDR copies OUTSIDE any RenderScope).
//
// Purpose        : RAII begin/end of a render pass instance (was the renderer-private struct
//                  `ScopedRenderPass`, renamed: it is a class because it owns the begin/end pairing) plus
//                  the shared viewport/scissor setter. Only calls Vulkan; builds no objects.
// Ownership      : Non-copyable, non-movable; must not outlive the command buffer's recording.
// Test           : none (records commands only; the pure struct fill is makeRenderPassBeginInfo, Init.h).
// See also       : RenderPass.h (creates the VkRenderPass), Init.h, README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include "Init.h"

#include <vulkan/vulkan.h>

namespace swish::vkx {

// Full-extent viewport [0,1] depth and scissor at the origin. All Swish passes use dynamic viewport/scissor.
inline void set_viewport_scissor(VkCommandBuffer cmd, VkExtent2D ext) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(ext.width), static_cast<float>(ext.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

class RenderScope {
public:
    RenderScope(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, const VkClearValue* clears,
                uint32_t clearCount)
        : m_cmd(cmd) {
        auto info              = makeRenderPassBeginInfo();
        info.renderPass        = rp;
        info.framebuffer       = fb;
        info.renderArea.extent = extent;
        info.clearValueCount   = clearCount;
        info.pClearValues      = clears;
        vkCmdBeginRenderPass(m_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Convenience overload for the common single-clear case.
    RenderScope(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, const VkClearValue& clear)
        : RenderScope(cmd, rp, fb, extent, &clear, 1) {}

    ~RenderScope() { vkCmdEndRenderPass(m_cmd); }

    RenderScope(const RenderScope&)            = delete;
    RenderScope& operator=(const RenderScope&) = delete;

private:
    VkCommandBuffer m_cmd;
};

}  // namespace swish::vkx
