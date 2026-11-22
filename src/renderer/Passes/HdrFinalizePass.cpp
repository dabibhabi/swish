// HdrFinalize: the last step before bloom. Leaves the lit + forward HDR image in SHADER_READ_ONLY for bloom and the
// composite. With auto-exposure on (the default, release too, L1.2) it does that as the end of a luminance-pyramid
// blit chain and owns the auto-exposure state (adapted luminance); otherwise it is the single
// COLOR_ATTACHMENT -> SHADER_READ barrier.
#include "debug/DebugParams.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "utils/light/Photometry.h"
#include "utils/vulkan/Barrier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <memory>

namespace swish {

namespace {

namespace ul = util::light;

class HdrFinalizePass final : public IRenderPass {
public:
    const char* name() const override { return "HdrFinalize"; }
    void        init(const PassInitCtx&) override {}
    void        cleanup(VkDevice) override {}
    void        declare(PassDeclaration& d) const override {
        d.read({Img::Hdr});
        d.write({Img::Lum});  // luminance pyramid (auto-exposure)
    }

    // Read the previous frame's average luminance (its 1x1 mip was copied to this frame slot's host buffer), adapt,
    // and publish the exposure the composite uses when tune.autoExposure is on.
    void beginFrame(const FrameUpdate& u) override {
        if (!u.tune.autoExposure) {
            u.state.preExposure = ul::kPreExposure;
            return;
        }
        const void* mapped = u.res.pp->get_lum_readback_mapped(u.frameIndex);
        if (!mapped) return;
        const uint16_t* h = reinterpret_cast<const uint16_t*>(mapped);  // RGBA16F, 4 halfs
        Vec3            avg(glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]));
        // The HDR image holds luminance * pre-exposure (L1.3). This slot was written PP_MAX_FRAMES frames ago, so
        // undo the pre-exposure THAT frame used to get cd/m². Adaptation runs in cd/m², so it is unaffected when
        // the pre-exposure itself moves (adaptive mode).
        const float nits =
            std::max(glm::dot(avg, Vec3(0.2126f, 0.7152f, 0.0722f)) / m_slotPreExposure[u.frameIndex], 1.0e-3f);
        const float rate = glm::clamp(1.0f - std::exp(-u.dt * std::max(u.tune.aeSpeed, 0.0f)), 0.0f, 1.0f);
        m_adaptedNits    = std::max(m_adaptedNits + (nits - m_adaptedNits) * rate, 1.0e-3f);
        // Compensation brightens when positive, so it is SUBTRACTED from the metered EV. The clamps are the active
        // weather preset's (FrameState, copied from WeatherController).
        const float evMetered = ul::ev100_from_luminance(m_adaptedNits);
        const float evWanted  = evMetered - u.tune.evCompensation;
        const float ev        = glm::clamp(evWanted, u.state.evMin, u.state.evMax);
        // Fixed pre-exposure (daylight, the shipped path) or one that follows the exposure (lab / night).
        u.state.preExposure = u.tune.adaptivePreExposure ? ul::pre_exposure_for_ev100(ev) : ul::kPreExposure;
        u.state.exposure    = ul::hdr_exposure_from_ev100(ev, u.state.preExposure);
        u.state.meterNits   = m_adaptedNits;
        u.state.meterEv     = evMetered;
        u.state.meterEvUsed = ev;
        u.state.meterClamp  = evWanted < u.state.evMin ? -1 : (evWanted > u.state.evMax ? 1 : 0);
    }

    void record(const FrameCtx& c) override {
        m_slotPreExposure[c.frameIndex] = c.state.preExposure;  // what this slot's readback will have been shaded with
        if (c.tune.autoExposure) {
            // Blits the lit HDR down to 1x1 (average) for auto-exposure, then leaves the HDR in SHADER_READ
            // (so it replaces the barrier below).
            record_luminance_pyramid(c);
            return;
        }
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }

private:
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

    // Auto-exposure state, private to this pass, in cd/m². 19,000 is the average that meters to the shipped
    // exposure (EV 15.7 + 1.53 compensation), so the first frames do not flash.
    float m_adaptedNits = 19000.0f;
    // The pre-exposure each frame slot was shaded with (its readback is consumed PP_MAX_FRAMES frames later).
    std::array<float, PP_MAX_FRAMES> m_slotPreExposure{ul::kPreExposure, ul::kPreExposure};
};

}  // namespace

std::unique_ptr<IRenderPass> make_hdr_finalize_pass() { return std::make_unique<HdrFinalizePass>(); }

}  // namespace swish
