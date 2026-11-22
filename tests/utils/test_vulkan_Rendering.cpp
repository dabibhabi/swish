// utils/vulkan/Rendering.h: the pure builders only. vkCmdBeginRendering / vkCmdEndRendering are never called.
#include "utils/vulkan/Rendering.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("color_attachment fills a VkRenderingAttachmentInfo", "[vulkan]") {
    VkClearColorValue clear{};
    clear.float32[0] = 0.25f;
    const auto a = vkx::color_attachment(reinterpret_cast<VkImageView>(uintptr_t{0x10}), VK_ATTACHMENT_LOAD_OP_CLEAR,
                                         VK_ATTACHMENT_STORE_OP_STORE, clear);
    CHECK(a.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    CHECK(a.imageView == reinterpret_cast<VkImageView>(uintptr_t{0x10}));
    CHECK(a.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(a.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
    CHECK(a.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    CHECK(a.clearValue.color.float32[0] == 0.25f);
    CHECK(a.resolveMode == VK_RESOLVE_MODE_NONE);
}

TEST_CASE("depth_attachment defaults to the reverse-Z clear and the depth attachment layout", "[vulkan]") {
    const auto a = vkx::depth_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    CHECK(a.imageLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    CHECK(a.clearValue.depthStencil.depth == 0.0f);
}

TEST_CASE("make_pipeline_rendering_info points at the caller's format array", "[vulkan]") {
    const VkFormat formats[2] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM};
    const auto     r          = vkx::make_pipeline_rendering_info(formats, 2, VK_FORMAT_D32_SFLOAT);
    CHECK(r.sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    CHECK(r.colorAttachmentCount == 2);
    CHECK(r.pColorAttachmentFormats == formats);
    CHECK(r.depthAttachmentFormat == VK_FORMAT_D32_SFLOAT);
    CHECK(r.stencilAttachmentFormat == VK_FORMAT_UNDEFINED);
    CHECK(r.viewMask == 0);
}

TEST_CASE("RenderingInfo builds a colour-only VkRenderingInfo", "[vulkan]") {
    vkx::RenderingInfo ri({640, 360});
    ri.color(vkx::color_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE));
    const VkRenderingInfo r = ri.get();
    CHECK(r.sType == VK_STRUCTURE_TYPE_RENDERING_INFO);
    CHECK(r.renderArea.offset.x == 0);
    CHECK(r.renderArea.extent.width == 640);
    CHECK(r.renderArea.extent.height == 360);
    CHECK(r.layerCount == 1);
    CHECK(r.colorAttachmentCount == 1);
    CHECK(r.pColorAttachments != nullptr);
    CHECK(r.pDepthAttachment == nullptr);
    CHECK(r.pColorAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE);
}

TEST_CASE("RenderingInfo with depth and several colour targets", "[vulkan]") {
    vkx::RenderingInfo ri({8, 8});
    for (int i = 0; i < 3; ++i)
        ri.color(vkx::color_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE));
    ri.depth(vkx::depth_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_NONE));
    const VkRenderingInfo r = ri.get();
    CHECK(ri.color_count() == 3);
    CHECK(ri.has_depth());
    CHECK(r.colorAttachmentCount == 3);
    REQUIRE(r.pDepthAttachment != nullptr);
    CHECK(r.pDepthAttachment->storeOp == VK_ATTACHMENT_STORE_OP_NONE);
}

TEST_CASE("RenderingInfo rejects a ninth colour attachment", "[vulkan]") {
    vkx::RenderingInfo ri({1, 1});
    for (uint32_t i = 0; i < vkx::RenderingInfo::kMaxColor; ++i)
        ri.color(vkx::color_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE));
    CHECK_THROWS_AS(
        ri.color(vkx::color_attachment(VK_NULL_HANDLE, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE)),
        std::logic_error);
}

TEST_CASE("an empty RenderingInfo has no attachment pointers", "[vulkan]") {
    vkx::RenderingInfo ri({4, 4});
    const auto         r = ri.get();
    CHECK(r.colorAttachmentCount == 0);
    CHECK(r.pColorAttachments == nullptr);
    CHECK(r.pDepthAttachment == nullptr);
}
