// HdrFinalize: the last step before bloom. Leaves the lit + forward HDR image in SHADER_READ_ONLY for bloom and the
// composite. In debug builds with auto-exposure on it does that as the end of a luminance-pyramid blit chain and owns
// the auto-exposure state (adapted luminance); otherwise it is the single COLOR_ATTACHMENT -> SHADER_READ barrier.
#include "debug/DebugParams.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "utils/vulkan/Barrier.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <memory>

namespace swish {

namespace {

class HdrFinalizePass final : public IRenderPass {
public:
    const char* name() const override { return "HdrFinalize"; }
    void        init(const PassInitCtx&) override {}
    void        cleanup(VkDevice) override {}
    void        declare(PassDeclaration& d) const override {
        d.read({Img::Hdr});
#ifdef SWISH_DEBUG_UI
        d.write({Img::Lum});  // luminance pyramid (auto-exposure); release only barriers the HDR
#endif
    }

#ifdef SWISH_DEBUG_UI
    // Read the previous frame's average luminance (its 1x1 mip was copied to this frame slot's host buffer), adapt,
    // and publish the exposure the composite uses when tune.autoExposure is on.
    void beginFrame(const FrameUpdate& u) override {
        if (!u.tune.autoExposure)
            return;
        const void* mapped = u.res.pp->get_lum_readback_mapped(u.frameIndex);
        if (!mapped)
            return;
        const uint16_t* h = reinterpret_cast<const uint16_t*>(mapped);  // RGBA16F, 4 halfs
        Vec3            avg(glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]));
        float           lum  = std::max(glm::dot(avg, Vec3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
        float           rate = glm::clamp(1.0f - std::exp(-u.dt * std::max(u.tune.aeSpeed, 0.0f)), 0.0f, 1.0f);
        m_adaptedLum         = std::max(m_adaptedLum + (lum - m_adaptedLum) * rate, 1e-4f);
        u.state.exposure     = glm::clamp(u.tune.aeKey / m_adaptedLum, u.tune.aeMin, u.tune.aeMax);
    }
#endif

    void record(const FrameCtx& c) override {
#ifdef SWISH_DEBUG_UI
        if (c.tune.autoExposure) {
            // Blits the lit HDR down to 1x1 (average) for auto-exposure, then leaves the HDR in SHADER_READ
            // (so it replaces the barrier below).
            record_luminance_pyramid(c);
            return;
        }
#endif
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }

private:
#ifdef SWISH_DEBUG_UI
    // Blit the lit HDR down a mip chain to 1x1 (average). Leaves the HDR in SHADER_READ. The 1x1 mip is copied to a
    // host buffer the CPU reads next frame (beginFrame).
    void record_luminance_pyramid(const FrameCtx& c) const {
        VkCommandBuffer           cmd  = c.cmd;
        const PostProcessManager& pp   = *c.res.pp;
        VkImage                   hdr  = c.images.image(Img::Hdr, c.frameIndex);
        VkImage                   lum  = c.images.image(Img::Lum, c.frameIndex);
        const uint32_t            mips = pp.get_lum_mips();
        const int32_t             dim  = static_cast<int32_t>(pp.get_lum_dim());
        const VkExtent2D          rext = c.renderExtent;

        constexpr auto kAspect = VK_IMAGE_ASPECT_COLOR_BIT;

        // HDR (COLOR_ATTACHMENT after the forward passes) -> TRANSFER_SRC. Masks are now the correct
        // COLOR_OUTPUT/COLOR_WRITE -> TRANSFER/TRANSFER_READ (legacy helper fell through to BOTTOM_OF_PIPE/0).
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(cmd, hdr,
                                                                                                           kAspect);
        // All lum mips -> TRANSFER_DST. Explicit masks: the pyramid is one image shared by every frame in flight,
        // so wait (TRANSFER) on the previous frame's readback copy, exactly as the old local lambda did.
        vkx::image_barrier_with(cmd, lum, kAspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                vkx::kAfterTransferUndefToXferDst, {0, mips, 0, 1});

        // HDR (full render extent) -> lum mip 0 (dim^2), LINEAR box-ish downsample.
        VkImageBlit b0{};
        b0.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b0.srcOffsets[1]  = {static_cast<int32_t>(rext.width), static_cast<int32_t>(rext.height), 1};
        b0.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b0.dstOffsets[1]  = {dim, dim, 1};
        vkCmdBlitImage(cmd, hdr, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lum, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &b0, VK_FILTER_LINEAR);

        // Generate the mip chain (each level halves; LINEAR = 2x2 box average -> true avg at 1x1).
        for (uint32_t i = 1; i < mips; ++i) {
            vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
                cmd, lum, kAspect, {i - 1, 1, 0, 1});
            int32_t     src = dim >> (i - 1), dst = dim >> i;
            VkImageBlit b{};
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
            b.srcOffsets[1]  = {src, src, 1};
            b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
            b.dstOffsets[1]  = {dst > 0 ? dst : 1, dst > 0 ? dst : 1, 1};
            vkCmdBlitImage(cmd, lum, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lum, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &b, VK_FILTER_LINEAR);
        }

        // Last mip (1x1) is still TRANSFER_DST -> TRANSFER_SRC, then copy to the host buffer.
        vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
            cmd, lum, kAspect, {mips - 1, 1, 0, 1});
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 0, 1};
        copy.imageExtent      = {1, 1, 1};
        vkCmdCopyImageToBuffer(cmd, lum, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pp.get_lum_readback_buffer(c.frameIndex),
                               1, &copy);

        // HDR -> SHADER_READ for bloom + composite. Masks are now the correct TRANSFER/TRANSFER_READ ->
        // FRAGMENT/SHADER_READ (legacy helper fell through to TOP_OF_PIPE/0 as the source).
        vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(cmd, hdr,
                                                                                                           kAspect);
    }

    float m_adaptedLum = 0.5f;  // auto-exposure state, private to this pass
#endif
};

}  // namespace

std::unique_ptr<IRenderPass> make_hdr_finalize_pass() {
    return std::make_unique<HdrFinalizePass>();
}

}  // namespace swish
