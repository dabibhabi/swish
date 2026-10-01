// PostProcessManager: render pass creation (shadow, G-buffer, lighting, HDR, bloom, AO, composite).

#include "PostProcessManager.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/RenderPass.h"

#include <array>
#include <stdexcept>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Render Passes
// ══════════════════════════════════════════════════════════════════════

void PostProcessManager::createRenderPasses(VkFormat swapchainFormat) {
    // Helper: create a single-color-attachment render pass (colour CLEAR/STORE, optional depth CLEAR/STORE).
    auto makePass = [&](VkFormat colorFormat, VkImageLayout finalLayout, bool hasDepth) -> VkRenderPass {
        const VkPipelineStageFlags stages =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            (hasDepth ? static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT) : 0u);
        const std::array<VkSubpassDependency, 1> deps = {vkx::subpass_dependency(
            VK_SUBPASS_EXTERNAL, 0, stages, stages, 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                (hasDepth ? static_cast<VkAccessFlags>(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) : 0u))};

        vkx::ColorPassDesc d;
        d.color = {.format = colorFormat, .load = VK_ATTACHMENT_LOAD_OP_CLEAR, .final = finalLayout};
        if (hasDepth)  // depth is stored: SSAO samples it
            d.depth = vkx::AttachmentDesc{.format = resource::findDepthFormat(m_physicalDevice),
                                          .load   = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          .final  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        d.dependencies = deps;
        return vkx::make_color_pass(m_device, d);
    };

    m_hdrRenderPass       = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    m_bloomRenderPass     = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);
    m_aoRenderPass        = makePass(VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);
    m_compositeRenderPass = makePass(swapchainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false);
    m_lightingRenderPass  = makePass(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);

    // G-Buffer render pass: 4 color MRT (albedo, normal, material, clearcoat+flake) + depth
    {
        VkFormat depthFmt = resource::findDepthFormat(m_physicalDevice);

        std::array<VkAttachmentDescription, 5> atts{};
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

        // 3: Clearcoat + flake (R8G8B8A8_UNORM) — always-on (not SWISH_DEBUG_UI-gated;
        // see tasks/todo.md "Clearcoat + flake car paint"). RG = octahedral flake
        // normal, B = clearcoat strength, A = flake mask; all in [0,1].
        atts[3] = atts[0];

        // 4: Depth
        atts[4].format         = depthFmt;
        atts[4].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[4].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[4].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts[4].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[4].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[4].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<VkAttachmentReference, 4> colorRefs{};
        colorRefs[0]                   = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[1]                   = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[2]                   = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[3]                   = {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 4;
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

}  // namespace swish
