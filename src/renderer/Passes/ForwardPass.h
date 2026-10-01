#pragma once
// The render pass shared by the forward passes that draw onto the lit HDR image with the scene depth read-only
// (rain streaks, road spray, car glass): colour LOAD/STORE in COLOR_ATTACHMENT, depth LOAD in
// DEPTH_STENCIL_READ_ONLY, one external dependency that makes the previous colour writes (and the depth reads of the
// lighting/SSR passes) visible. The windshield-rain pass has its own variant (it also reads the refraction snapshot).

#include "utils/vulkan/RenderPass.h"

#include <array>

namespace swish {

inline VkRenderPass make_forward_hdr_pass(VkDevice device, VkFormat depthFormat) {
    // After the lighting pass: colour was written (COLOR_ATTACHMENT_OUTPUT) and depth was sampled by the lighting
    // shader (FRAGMENT_SHADER). Both must be visible to the forward pass.
    const std::array<VkSubpassDependency, 1> deps = {vkx::subpass_dependency(
        VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)};

    vkx::ColorPassDesc d;
    d.color = {.format  = VK_FORMAT_R16G16B16A16_SFLOAT,
               .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
               .initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               .final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    // Depth: read-only — the scene depth is in DEPTH_STENCIL_READ_ONLY_OPTIMAL after the G-buffer -> lighting
    // barriers; the forward passes inherit that state.
    d.depth        = vkx::AttachmentDesc{.format  = depthFormat,
                                         .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                         .store   = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         .initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                         .final   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    d.dependencies = deps;
    return vkx::make_color_pass(device, d);
}

}  // namespace swish
