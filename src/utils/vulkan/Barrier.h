#pragma once
// ============================================================================
// vkx/Barrier.h
//
// Vulkan concept : Pipeline barriers / image layout transitions (execution +
//                  memory dependency for ONE image, ONE subresource range).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Synchronization and Cache Control", section
//                  "Pipeline Barriers" (anchor #synchronization-pipeline-barriers, verify);
//                  "Image Layouts" (anchor #resources-image-layouts, verify)
// Guide          : https://docs.vulkan.org/guide/latest/synchronization.html (verify)
//                  https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
// Vulkan-Hpp     : vk::CommandBuffer::pipelineBarrier / vk::ImageMemoryBarrier
//                  (vk::ImageMemoryBarrier2 + vk::CommandBuffer::pipelineBarrier2 for sync2)
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Metal has no per-stage pipeline barrier; MoltenVK maps these to
//                  encoder/memory barriers, so over-specifying stages costs less than on
//                  desktop GPUs, but layout correctness still matters for validation.
//                  https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md (verify)
//                  MoltenVK exposes synchronization2 (checked 2026-09-29 with the local 1.4.321 SDK's
//                  vulkaninfo: Apple M3 Max, apiVersion 1.3.313, synchronization2 = true).
//
// Purpose        : A constexpr table (old layout, new layout) -> (src/dst stage, src/dst
//                  access), a pure lookup `barrier_masks()`, `image_barrier()` (classic
//                  VkImageMemoryBarrier + vkCmdPipelineBarrier) and `image_barrier2()`
//                  (VkImageMemoryBarrier2 + vkCmdPipelineBarrier2, synchronization2).
//                  Unsupported pairs FAIL LOUDLY (`valid == false`; the recorders throw; the
//                  template overloads static_assert). There is NO TOP_OF_PIPE->BOTTOM_OF_PIPE
//                  fallback.
// Sync2          : masks are stored as 64-bit VkPipelineStageFlags2/VkAccessFlags2. For
//                  every bit used here the sync2 value equals the classic value
//                  (static_asserted below), so the classic and sync2 recorders share ONE
//                  table. `image_barrier2()` may only be CALLED when the device enabled
//                  VkPhysicalDeviceVulkan13Features::synchronization2 (Device.cpp; exposed as
//                  RendererServices::synchronization2). `image_barrier_auto(cmd, useSync2, ...)`
//                  picks the recorder from that flag so a pass has one call site.
// Explicit masks : `image_barrier_with()` takes a caller-supplied BarrierMasks for the rare
//                  case where one (old,new) pair needs a different scope than the table row
//                  (see kAfterTransferUndefToXferDst). Use sparingly; prefer a table row.
// Ownership      : Stateless. No Vulkan objects are created; nothing needs destroying.
// Swish rules    : Adding a transition = add one row to kTable and one line to the
//                  equivalence check in README.md. Never add a wildcard row.
// Moved from     : ResourceManager::insertImageBarrier (deleted 2026-09-29) /
//                  ::transitionImageLayout, the DofPass/TaaPass/WindshieldRainPass local barrier
//                  helpers and the luminance-pyramid mipBarrier lambda (all migrated; now in
//                  renderer/Passes/HdrFinalizePass.cpp).
// See also       : DebugLabel.h, DeviceHandle.h, README.md in this directory.
//
// This header includes NO renderer header and calls no Vulkan function unless
// image_barrier() is invoked (called from the renderer passes; see README.md).
// ============================================================================

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace swish::vkx {

// Classic and sync2 bit values coincide for every flag used in the table.
#define VKX_SAME_BIT(classic, sync2) static_assert(static_cast<uint64_t>(classic) == static_cast<uint64_t>(sync2))
VKX_SAME_BIT(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
VKX_SAME_BIT(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
VKX_SAME_BIT(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
VKX_SAME_BIT(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
VKX_SAME_BIT(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
VKX_SAME_BIT(VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
VKX_SAME_BIT(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_2_SHADER_READ_BIT);
VKX_SAME_BIT(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
VKX_SAME_BIT(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
VKX_SAME_BIT(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
VKX_SAME_BIT(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
#undef VKX_SAME_BIT

struct BarrierMasks {
    bool                  valid     = false;
    VkPipelineStageFlags2 srcStage  = 0;
    VkPipelineStageFlags2 dstStage  = 0;
    VkAccessFlags2        srcAccess = 0;
    VkAccessFlags2        dstAccess = 0;
};

// Subresource selection; defaults reproduce today's single-mip, single-layer barrier.
struct SubresourceSpan {
    uint32_t baseMip    = 0;
    uint32_t levelCount = 1;
    uint32_t baseLayer  = 0;
    uint32_t layerCount = 1;
};

namespace detail {

struct Row {
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    BarrierMasks  m;
};

constexpr BarrierMasks masks(VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da) {
    return BarrierMasks{true, ss, ds, sa, da};
}

constexpr VkPipelineStageFlags2 TOP   = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
constexpr VkPipelineStageFlags2 XFER  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
constexpr VkPipelineStageFlags2 FRAG  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkPipelineStageFlags2 COLOR = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 EARLY = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
constexpr VkPipelineStageFlags2 LATE  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

constexpr VkImageLayout UNDEF   = VK_IMAGE_LAYOUT_UNDEFINED;
constexpr VkImageLayout COLOR_L = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
constexpr VkImageLayout DEPTH_L = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
constexpr VkImageLayout DEPTHRO = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
constexpr VkImageLayout READ_L  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
constexpr VkImageLayout XSRC_L  = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
constexpr VkImageLayout XDST_L  = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

// One row per transition the codebase performs today (enumerated by grepping every
// insertImageBarrier call site plus ResourceManager::transitionImageLayout, 2026-09-29).
// Site counts below are insertImageBarrier call sites (24 total).
inline constexpr std::array<Row, 14> kTable{{
    // Written by a colour pass, sampled by a later fragment shader (18 sites).
    {COLOR_L, READ_L, masks(COLOR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, FRAG, VK_ACCESS_2_SHADER_READ_BIT)},
    // Sampled, then re-rendered (2 sites: hdr before the forward and SSR passes).
    {READ_L, COLOR_L, masks(FRAG, VK_ACCESS_2_SHADER_READ_BIT, COLOR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)},
    // Depth written, then sampled read-only (1 site: hdr depth after G-buffer).
    {DEPTH_L, DEPTHRO, masks(LATE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, FRAG, VK_ACCESS_2_SHADER_READ_BIT)},
    // First use of a fresh image that is only sampled (1 site: TaaPass history init).
    {UNDEF, READ_L, masks(TOP, 0, FRAG, VK_ACCESS_2_SHADER_READ_BIT)},
    // HDR to blit source (1 site). The legacy helper had NO entry for TRANSFER_SRC and
    // silently used dstStage=BOTTOM_OF_PIPE, dstAccess=0; this row is the correct dependency.
    {COLOR_L, XSRC_L, masks(COLOR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, XFER, VK_ACCESS_2_TRANSFER_READ_BIT)},
    // Blit source back to sampled (1 site). Legacy: srcStage=TOP_OF_PIPE, srcAccess=0
    // (silent fallthrough). Correct: wait for the transfer read (WAR needs execution dep).
    {XSRC_L, READ_L, masks(XFER, VK_ACCESS_2_TRANSFER_READ_BIT, FRAG, VK_ACCESS_2_SHADER_READ_BIT)},
    // ResourceManager::transitionImageLayout (texture upload), 2 rows:
    {UNDEF, XDST_L, masks(TOP, 0, XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT)},
    {XDST_L, READ_L, masks(XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, FRAG, VK_ACCESS_2_SHADER_READ_BIT)},
    // Fresh depth target (not used via insertImageBarrier today; the legacy dst mapping
    // for DEPTH_STENCIL_ATTACHMENT_OPTIMAL is reproduced for UNDEFINED sources).
    {UNDEF, DEPTH_L, masks(TOP, 0, EARLY, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)},
    // Copy-back chain used by DofPass / TaaPass / WindshieldRainPass (masks are exactly the ones their
    // local helpers passed, so these migrations are behaviour-identical):
    // sampled image about to be overwritten by a copy / blit (DoF+TAA hdr; windshield wet image A).
    {READ_L, XDST_L, masks(FRAG, VK_ACCESS_2_SHADER_READ_BIT, XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT)},
    // sampled image about to be read by a copy (windshield wet image B).
    {READ_L, XSRC_L, masks(FRAG, VK_ACCESS_2_SHADER_READ_BIT, XFER, VK_ACCESS_2_TRANSFER_READ_BIT)},
    // copy destination handed back to the colour pipeline (hdr after the DoF/TAA copy-back).
    {XDST_L, COLOR_L, masks(XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, COLOR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)},
    // copy source handed back to the colour pipeline (windshield snapshot: hdr after the refraction copy).
    {XSRC_L, COLOR_L, masks(XFER, VK_ACCESS_2_TRANSFER_READ_BIT, COLOR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)},
    // Blit chain (luminance mip pyramid): a mip just written becomes the next blit's source.
    // TRANSFER -> TRANSFER, so callers pass a SubresourceSpan selecting the single mip.
    {XDST_L, XSRC_L, masks(XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT, XFER, VK_ACCESS_2_TRANSFER_READ_BIT)},
}};

}  // namespace detail

// UNDEFINED -> TRANSFER_DST for an image that a PREVIOUS submission may still be reading with a transfer
// (the luminance pyramid is ONE image shared by all frames in flight, so last frame's readback copy can
// still be running). Unlike the table row (TOP_OF_PIPE source, right for a fresh or per-frame image) this
// waits on TRANSFER so the write-after-read hazard is covered. Pass it to image_barrier_with().
inline constexpr BarrierMasks kAfterTransferUndefToXferDst =
    detail::masks(detail::XFER, 0, detail::XFER, VK_ACCESS_2_TRANSFER_WRITE_BIT);

// Pure lookup. `.valid == false` means the pair is unsupported: add a row, do not guess.
constexpr BarrierMasks barrier_masks(VkImageLayout oldLayout, VkImageLayout newLayout) {
    for (const auto& r : detail::kTable)
        if (r.oldLayout == oldLayout && r.newLayout == newLayout) return r.m;
    return BarrierMasks{};
}

// Compile-time variant: an unsupported pair does not compile.
template <VkImageLayout Old, VkImageLayout New>
constexpr BarrierMasks barrier_masks_v() {
    constexpr BarrierMasks m = barrier_masks(Old, New);
    static_assert(m.valid, "vkx::Barrier: unsupported image layout transition; add a row to detail::kTable");
    return m;
}

// Sync2 struct fill from explicit masks (pure; does not call Vulkan, does not require the feature).
inline VkImageMemoryBarrier2 make_image_barrier2_with(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                                                      VkImageLayout newLayout, const BarrierMasks& m,
                                                      SubresourceSpan span = {}) {
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = m.srcStage;
    b.srcAccessMask       = m.srcAccess;
    b.dstStageMask        = m.dstStage;
    b.dstAccessMask       = m.dstAccess;
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {aspect, span.baseMip, span.levelCount, span.baseLayer, span.layerCount};
    return b;
}

namespace detail {
inline BarrierMasks require_masks(VkImageLayout oldLayout, VkImageLayout newLayout, const char* who) {
    const BarrierMasks m = barrier_masks(oldLayout, newLayout);
    if (!m.valid)
        throw std::logic_error(std::string(who) + ": unsupported layout transition " +
                               std::to_string(static_cast<int>(oldLayout)) + " -> " +
                               std::to_string(static_cast<int>(newLayout)));
    return m;
}
}  // namespace detail

// Sync2 struct fill from the table (pure). Throws std::logic_error for an unsupported pair.
inline VkImageMemoryBarrier2 make_image_barrier2(VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                                                 VkImageLayout newLayout, SubresourceSpan span = {}) {
    return make_image_barrier2_with(image, aspect, oldLayout, newLayout,
                                    detail::require_masks(oldLayout, newLayout, "vkx::make_image_barrier2"), span);
}

// Records a classic (synchronization1) image barrier with caller-supplied masks.
inline void image_barrier_with(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                               VkImageLayout newLayout, const BarrierMasks& m, SubresourceSpan span = {}) {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask       = static_cast<VkAccessFlags>(m.srcAccess);
    b.dstAccessMask       = static_cast<VkAccessFlags>(m.dstAccess);
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {aspect, span.baseMip, span.levelCount, span.baseLayer, span.layerCount};
    vkCmdPipelineBarrier(cmd, static_cast<VkPipelineStageFlags>(m.srcStage),
                         static_cast<VkPipelineStageFlags>(m.dstStage), 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Records a classic (synchronization1) image barrier from the table. Every pair the legacy
// ResourceManager::insertImageBarrier handled has identical masks here; the two legacy fallthrough pairs
// (luminance blit) get the correct TRANSFER masks (see kTable comments).
inline void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                          VkImageLayout newLayout, SubresourceSpan span = {}) {
    image_barrier_with(cmd, image, aspect, oldLayout, newLayout,
                       detail::require_masks(oldLayout, newLayout, "vkx::image_barrier"), span);
}

// Compile-time-checked overload for call sites with literal layouts.
template <VkImageLayout Old, VkImageLayout New>
inline void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, SubresourceSpan span = {}) {
    (void)barrier_masks_v<Old, New>();
    image_barrier(cmd, image, aspect, Old, New, span);
}

// Records a synchronization2 image barrier (vkCmdPipelineBarrier2, core 1.3). ONLY call when the device
// enabled the synchronization2 feature (RendererServices::synchronization2).
inline void image_barrier2_with(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                                VkImageLayout newLayout, const BarrierMasks& m, SubresourceSpan span = {}) {
    const VkImageMemoryBarrier2 b = make_image_barrier2_with(image, aspect, oldLayout, newLayout, m, span);
    VkDependencyInfo            dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

inline void image_barrier2(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout,
                           VkImageLayout newLayout, SubresourceSpan span = {}) {
    image_barrier2_with(cmd, image, aspect, oldLayout, newLayout,
                        detail::require_masks(oldLayout, newLayout, "vkx::image_barrier2"), span);
}

template <VkImageLayout Old, VkImageLayout New>
inline void image_barrier2(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, SubresourceSpan span = {}) {
    (void)barrier_masks_v<Old, New>();
    image_barrier2(cmd, image, aspect, Old, New, span);
}

// One call site for both paths: `useSync2` is RendererServices::synchronization2.
inline void image_barrier_auto(VkCommandBuffer cmd, bool useSync2, VkImage image, VkImageAspectFlags aspect,
                               VkImageLayout oldLayout, VkImageLayout newLayout, SubresourceSpan span = {}) {
    if (useSync2)
        image_barrier2(cmd, image, aspect, oldLayout, newLayout, span);
    else
        image_barrier(cmd, image, aspect, oldLayout, newLayout, span);
}

}  // namespace swish::vkx
