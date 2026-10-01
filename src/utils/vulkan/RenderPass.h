#pragma once
// ============================================================================
// vkx/RenderPass.h
//
// Vulkan concept : VkRenderPass with one colour attachment, an optional depth attachment, ONE subpass and an
//                  explicit list of subpass dependencies (the shape of every non-G-buffer pass in Swish).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Render Pass", sections "Render Pass Creation" and "Render Pass Compatibility"
//                  (anchor #renderpass, verify); VkSubpassDependency semantics (anchor #synchronization-dependencies,
//                  verify)
// Guide          : https://docs.vulkan.org/guide/latest/ (render passes page; verify)
// Vulkan-Hpp     : vk::RenderPassCreateInfo, vk::AttachmentDescription, vk::SubpassDependency,
//                  vk::Device::createRenderPassUnique  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Attachment load/store ops become Metal load/store actions, so LOAD vs DONT_CARE is a real
//                  bandwidth choice on Apple GPUs. Layout transitions in initial/final are emulated by MoltenVK
//                  (no-ops in Metal); the subpass dependencies still matter for validation.
//
// Purpose        : `ColorPassDesc` (aggregate, C++20 designated initialisers) + `RenderPassBuild` (pure: fills the
//                  VkRenderPassCreateInfo and the arrays it points to) + `make_color_pass` (the only Vulkan call).
//                  Replaces ~15 hand-written copies (Rain, Spray, Glass, Windshield, Velocity, DoF, TAA, IBL and the
//                  PostProcessManager single-colour passes). Not used for the G-buffer (4 MRT) or shadow (depth only)
//                  passes: two users only, both with different shapes.
// Ownership      : `make_color_pass` returns the raw handle; the caller destroys it (vkDestroyRenderPass) exactly as
//                  before. RenderPassBuild owns its arrays, points into itself, and is non-copyable/non-movable.
// Test           : tests/utils/test_vulkan_RenderPass.cpp
// See also       : RenderScope.h (begin/end), Barrier.h, README.md in this directory.
//
// This header includes NO renderer header.
// ============================================================================

#include "Check.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace swish::vkx {

// One attachment. Stencil ops are always DONT_CARE and samples 1 (no Swish pass uses stencil or MSAA).
struct AttachmentDesc {
    VkFormat            format  = VK_FORMAT_UNDEFINED;
    VkAttachmentLoadOp  load    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp store   = VK_ATTACHMENT_STORE_OP_STORE;
    VkImageLayout       initial = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout       final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
};

struct ColorPassDesc {
    AttachmentDesc color{};
    // The subpass references the depth attachment in layout `depth->final` (read-only depth passes set
    // initial == final == DEPTH_STENCIL_READ_ONLY_OPTIMAL; depth-writing passes use ..._ATTACHMENT_OPTIMAL).
    std::optional<AttachmentDesc> depth{};
    // Copied into the builder, so a temporary array at the call site is fine.
    std::span<const VkSubpassDependency> dependencies{};
};

// Convenience constructor for a VkSubpassDependency (all fields explicit, no flags).
constexpr VkSubpassDependency subpass_dependency(uint32_t srcSubpass, uint32_t dstSubpass,
                                                 VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                                 VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkSubpassDependency d{};
    d.srcSubpass    = srcSubpass;
    d.dstSubpass    = dstSubpass;
    d.srcStageMask  = srcStage;
    d.dstStageMask  = dstStage;
    d.srcAccessMask = srcAccess;
    d.dstAccessMask = dstAccess;
    return d;
}

// Pure builder: no Vulkan calls. info() is valid while this object is alive.
class RenderPassBuild {
public:
    explicit RenderPassBuild(const ColorPassDesc& d) : m_deps(d.dependencies.begin(), d.dependencies.end()) {
        m_atts[0]                      = describe(d.color);
        m_colorRef                     = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        m_subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        m_subpass.colorAttachmentCount = 1;
        m_subpass.pColorAttachments    = &m_colorRef;
        m_attCount                     = 1;
        if (d.depth) {
            m_atts[1]                         = describe(*d.depth);
            m_depthRef                        = {1, d.depth->final};
            m_subpass.pDepthStencilAttachment = &m_depthRef;
            m_attCount                        = 2;
        }
        m_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        m_info.attachmentCount = m_attCount;
        m_info.pAttachments    = m_atts;
        m_info.subpassCount    = 1;
        m_info.pSubpasses      = &m_subpass;
        m_info.dependencyCount = static_cast<uint32_t>(m_deps.size());
        m_info.pDependencies   = m_deps.empty() ? nullptr : m_deps.data();
    }
    RenderPassBuild(const RenderPassBuild&)            = delete;
    RenderPassBuild& operator=(const RenderPassBuild&) = delete;

    const VkRenderPassCreateInfo& info() const { return m_info; }

private:
    static VkAttachmentDescription describe(const AttachmentDesc& a) {
        VkAttachmentDescription d{};
        d.format         = a.format;
        d.samples        = VK_SAMPLE_COUNT_1_BIT;
        d.loadOp         = a.load;
        d.storeOp        = a.store;
        d.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        d.initialLayout  = a.initial;
        d.finalLayout    = a.final;
        return d;
    }

    VkAttachmentDescription          m_atts[2]{};
    VkAttachmentReference            m_colorRef{};
    VkAttachmentReference            m_depthRef{};
    VkSubpassDescription             m_subpass{};
    std::vector<VkSubpassDependency> m_deps;
    uint32_t                         m_attCount = 0;
    VkRenderPassCreateInfo           m_info{};
};

// Creates the render pass (VK_CHECKed). The caller owns and destroys the handle.
inline VkRenderPass make_color_pass(VkDevice device, const ColorPassDesc& desc) {
    const RenderPassBuild build(desc);
    VkRenderPass          rp = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(device, &build.info(), nullptr, &rp));
    return rp;
}

}  // namespace swish::vkx
