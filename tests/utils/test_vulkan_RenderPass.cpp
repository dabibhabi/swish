// utils/vulkan/RenderPass.h + RenderScope.h: the pure builder only. vkCreateRenderPass is never called.
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/RenderScope.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace vkx = swish::vkx;

static_assert(!std::is_copy_constructible_v<vkx::RenderScope>, "RenderScope is an RAII pairing: not copyable");
static_assert(!std::is_copy_constructible_v<vkx::RenderPassBuild>, "RenderPassBuild points into itself");

TEST_CASE("subpass_dependency fills every field and leaves flags zero", "[vulkan]") {
    constexpr auto d = vkx::subpass_dependency(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    CHECK(d.srcSubpass == VK_SUBPASS_EXTERNAL);
    CHECK(d.dstSubpass == 0);
    CHECK(d.srcStageMask == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(d.dstStageMask == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CHECK(d.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    CHECK(d.dstAccessMask == VK_ACCESS_SHADER_READ_BIT);
    CHECK(d.dependencyFlags == 0);
}

TEST_CASE("colour-only pass: one attachment, no depth reference, no dependencies", "[vulkan]") {
    vkx::ColorPassDesc desc;
    desc.color = {.format  = VK_FORMAT_R16G16B16A16_SFLOAT,
                  .load    = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .initial = VK_IMAGE_LAYOUT_UNDEFINED,
                  .final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const vkx::RenderPassBuild b(desc);
    const auto&                i = b.info();
    CHECK(i.sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    REQUIRE(i.attachmentCount == 1);
    CHECK(i.pAttachments[0].format == VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK(i.pAttachments[0].samples == VK_SAMPLE_COUNT_1_BIT);
    CHECK(i.pAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    CHECK(i.pAttachments[0].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    CHECK(i.pAttachments[0].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    CHECK(i.pAttachments[0].stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
    CHECK(i.pAttachments[0].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(i.pAttachments[0].finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    REQUIRE(i.subpassCount == 1);
    CHECK(i.pSubpasses[0].colorAttachmentCount == 1);
    CHECK(i.pSubpasses[0].pColorAttachments[0].attachment == 0);
    CHECK(i.pSubpasses[0].pColorAttachments[0].layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(i.pSubpasses[0].pDepthStencilAttachment == nullptr);
    CHECK(i.dependencyCount == 0);
    CHECK(i.pDependencies == nullptr);
}

TEST_CASE("forward pass: LOAD colour + read-only depth reference, dependencies copied", "[vulkan]") {
    const VkSubpassDependency deps[2] = {
        vkx::subpass_dependency(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        vkx::subpass_dependency(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT)};
    vkx::ColorPassDesc desc;
    desc.color        = {.format  = VK_FORMAT_R16G16B16A16_SFLOAT,
                         .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                         .initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         .final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    desc.depth        = vkx::AttachmentDesc{.format  = VK_FORMAT_D32_SFLOAT,
                                            .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                            .store   = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                            .initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                            .final   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    desc.dependencies = deps;
    const vkx::RenderPassBuild b(desc);
    const auto&                i = b.info();
    REQUIRE(i.attachmentCount == 2);
    CHECK(i.pAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
    CHECK(i.pAttachments[1].format == VK_FORMAT_D32_SFLOAT);
    CHECK(i.pAttachments[1].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
    REQUIRE(i.pSubpasses[0].pDepthStencilAttachment != nullptr);
    CHECK(i.pSubpasses[0].pDepthStencilAttachment->attachment == 1);
    CHECK(i.pSubpasses[0].pDepthStencilAttachment->layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    REQUIRE(i.dependencyCount == 2);
    CHECK(i.pDependencies[0].dstAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    CHECK(i.pDependencies[1].dstSubpass == VK_SUBPASS_EXTERNAL);
}

TEST_CASE("depth-writing pass references the depth attachment in ATTACHMENT layout", "[vulkan]") {
    vkx::ColorPassDesc desc;
    desc.color = {.format = VK_FORMAT_R8_UNORM, .load = VK_ATTACHMENT_LOAD_OP_CLEAR};
    desc.depth = vkx::AttachmentDesc{.format  = VK_FORMAT_D32_SFLOAT,
                                     .load    = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     .initial = VK_IMAGE_LAYOUT_UNDEFINED,
                                     .final   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const vkx::RenderPassBuild b(desc);
    CHECK(b.info().pSubpasses[0].pDepthStencilAttachment->layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}
