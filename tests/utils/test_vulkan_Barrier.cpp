// utils/vulkan/Barrier.h: the pure parts (mask table + sync2 struct fill). No Vulkan calls are made.
#include "utils/vulkan/Barrier.h"

#include <catch2/catch_test_macros.hpp>

namespace vkx = swish::vkx;

TEST_CASE("barrier_masks: colour write -> shader read is the common transition", "[vulkan]") {
    constexpr auto m =
        vkx::barrier_masks(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    STATIC_REQUIRE(m.valid);
    STATIC_REQUIRE(m.srcStage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    STATIC_REQUIRE(m.dstStage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    STATIC_REQUIRE(m.srcAccess == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    STATIC_REQUIRE(m.dstAccess == VK_ACCESS_2_SHADER_READ_BIT);
}

TEST_CASE("barrier_masks: an unsupported pair is invalid not silently TOP/BOTTOM", "[vulkan]") {
    const auto m = vkx::barrier_masks(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CHECK_FALSE(m.valid);
    CHECK_THROWS_AS(vkx::make_image_barrier2(VK_NULL_HANDLE, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
                    std::logic_error);
}

TEST_CASE("make_image_barrier2 fills the sync2 struct from the table", "[vulkan]") {
    const auto b = vkx::make_image_barrier2(VK_NULL_HANDLE, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {0, 3, 0, 1});
    CHECK(b.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    CHECK(b.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    CHECK(b.dstStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    CHECK(b.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CHECK(b.subresourceRange.levelCount == 3);
    CHECK(b.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
}

// ── Rows added for the copy-back chain, the mip chain and the held luminance pairs ─────────────

namespace {
constexpr auto kColorL = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
constexpr auto kReadL  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
constexpr auto kXSrcL  = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
constexpr auto kXDstL  = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}  // namespace

TEST_CASE("barrier_masks: rows that replaced the DoF/TAA/Windshield local helpers keep their exact masks", "[vulkan]") {
    // SHADER_READ -> TRANSFER_DST (hdr before the copy-back; windshield wet image A)
    constexpr auto a = vkx::barrier_masks(kReadL, kXDstL);
    STATIC_REQUIRE(a.valid);
    STATIC_REQUIRE(a.srcStage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    STATIC_REQUIRE(a.srcAccess == VK_ACCESS_2_SHADER_READ_BIT);
    STATIC_REQUIRE(a.dstStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(a.dstAccess == VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // SHADER_READ -> TRANSFER_SRC (windshield wet image B)
    constexpr auto b = vkx::barrier_masks(kReadL, kXSrcL);
    STATIC_REQUIRE(b.valid);
    STATIC_REQUIRE(b.srcStage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    STATIC_REQUIRE(b.srcAccess == VK_ACCESS_2_SHADER_READ_BIT);
    STATIC_REQUIRE(b.dstStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(b.dstAccess == VK_ACCESS_2_TRANSFER_READ_BIT);

    // TRANSFER_DST -> COLOR_ATTACHMENT (hdr after the copy-back)
    constexpr auto c = vkx::barrier_masks(kXDstL, kColorL);
    STATIC_REQUIRE(c.valid);
    STATIC_REQUIRE(c.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(c.srcAccess == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    STATIC_REQUIRE(c.dstStage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    STATIC_REQUIRE(c.dstAccess == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // TRANSFER_SRC -> COLOR_ATTACHMENT (windshield snapshot: hdr after the refraction copy)
    constexpr auto d = vkx::barrier_masks(kXSrcL, kColorL);
    STATIC_REQUIRE(d.valid);
    STATIC_REQUIRE(d.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(d.srcAccess == VK_ACCESS_2_TRANSFER_READ_BIT);
    STATIC_REQUIRE(d.dstStage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    STATIC_REQUIRE(d.dstAccess == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

TEST_CASE("barrier_masks: TRANSFER_DST -> TRANSFER_SRC is the mip-chain step (transfer to transfer)", "[vulkan]") {
    constexpr auto m = vkx::barrier_masks(kXDstL, kXSrcL);
    STATIC_REQUIRE(m.valid);
    STATIC_REQUIRE(m.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(m.dstStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(m.srcAccess == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    STATIC_REQUIRE(m.dstAccess == VK_ACCESS_2_TRANSFER_READ_BIT);
}

TEST_CASE("barrier_masks: the two formerly held luminance pairs use TRANSFER, not TOP/BOTTOM_OF_PIPE", "[vulkan]") {
    // Legacy insertImageBarrier: dst BOTTOM_OF_PIPE / access 0 (hdr -> blit source).
    constexpr auto toSrc = vkx::barrier_masks(kColorL, kXSrcL);
    STATIC_REQUIRE(toSrc.dstStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(toSrc.dstAccess == VK_ACCESS_2_TRANSFER_READ_BIT);
    STATIC_REQUIRE(toSrc.srcStage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    STATIC_REQUIRE(toSrc.srcAccess == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Legacy: src TOP_OF_PIPE / access 0 (blit source -> sampled).
    constexpr auto toRead = vkx::barrier_masks(kXSrcL, kReadL);
    STATIC_REQUIRE(toRead.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(toRead.srcAccess == VK_ACCESS_2_TRANSFER_READ_BIT);
    STATIC_REQUIRE(toRead.dstStage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    STATIC_REQUIRE(toRead.dstAccess == VK_ACCESS_2_SHADER_READ_BIT);
}

TEST_CASE("barrier_masks: every table row is a real dependency (no empty stage, no duplicates, no identity)",
          "[vulkan]") {
    const auto& t = vkx::detail::kTable;
    for (size_t i = 0; i < t.size(); ++i) {
        CHECK(t[i].m.valid);
        CHECK(t[i].m.srcStage != 0);
        CHECK(t[i].m.dstStage != 0);
        CHECK(t[i].oldLayout != t[i].newLayout);
        for (size_t j = i + 1; j < t.size(); ++j)
            CHECK_FALSE((t[i].oldLayout == t[j].oldLayout && t[i].newLayout == t[j].newLayout));
    }
}

TEST_CASE("kAfterTransferUndefToXferDst waits on TRANSFER (write-after-read on the shared luminance image)",
          "[vulkan]") {
    constexpr auto m = vkx::kAfterTransferUndefToXferDst;
    STATIC_REQUIRE(m.valid);
    STATIC_REQUIRE(m.srcStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(m.srcAccess == 0);
    STATIC_REQUIRE(m.dstStage == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    STATIC_REQUIRE(m.dstAccess == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // ... and differs from the table row on purpose (that one is TOP_OF_PIPE for fresh images).
    constexpr auto row = vkx::barrier_masks(VK_IMAGE_LAYOUT_UNDEFINED, kXDstL);
    STATIC_REQUIRE(row.srcStage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
}

TEST_CASE("make_image_barrier2 carries a mip span for the TRANSFER<->TRANSFER chain", "[vulkan]") {
    const auto b = vkx::make_image_barrier2(VK_NULL_HANDLE, VK_IMAGE_ASPECT_COLOR_BIT, kXDstL, kXSrcL, {4, 1, 0, 1});
    CHECK(b.oldLayout == kXDstL);
    CHECK(b.newLayout == kXSrcL);
    CHECK(b.subresourceRange.baseMipLevel == 4);
    CHECK(b.subresourceRange.levelCount == 1);
    CHECK(b.srcStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    CHECK(b.dstAccessMask == VK_ACCESS_2_TRANSFER_READ_BIT);
}

TEST_CASE("make_image_barrier2_with uses the caller's masks verbatim and never consults the table", "[vulkan]") {
    // PRESENT_SRC -> TRANSFER_DST has no table row, yet the explicit variant must not throw.
    const auto b =
        vkx::make_image_barrier2_with(VK_NULL_HANDLE, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                      kXDstL, vkx::kAfterTransferUndefToXferDst, {0, 11, 0, 1});
    CHECK(b.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    CHECK(b.srcStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    CHECK(b.srcAccessMask == 0);
    CHECK(b.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CHECK(b.subresourceRange.levelCount == 11);
}
