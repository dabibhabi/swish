// SSR (screen-space reflections: wet-road reflections, ships in release). Runs after lighting (HDR = the lit
// deferred scene, depth still readable). The lit HDR is briefly transitioned to SHADER_READ so the ray-march can
// sample it as the reflected colour, then restored to COLOR_ATTACHMENT for the forward passes; the SSR image is left
// SHADER_READ for the composite add. The tunables come from FrameCtx::tune (live DebugParams in debug, the shipped
// defaults in release); ssr.frag gates the release contribution to wet/pooled surfaces so a dry scene adds nothing
// (the SSR image stays primed black).
#include "debug/DebugParams.h"
#include "renderer/Passes/FullscreenPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/Barrier.h"

#include <glm/glm.hpp>
#include <memory>

namespace swish {

namespace {

class SsrPass final : public FullscreenPass<SsrParams> {
public:
    const char* name() const override { return "Ssr"; }
    void        declare(PassDeclaration& d) const override {
        d.read({Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth, Img::Hdr})
            .write({Img::Ssr});
    }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& dr, SsrParams& sp) const override {
        const PostProcessManager& pm = *c.res.pp;
        dr.renderPass                = pm.get_ssr_render_pass();
        dr.framebuffer               = pm.get_ssr_framebuffer();
        dr.extent                    = c.renderExtent;
        dr.clear                     = clear_color(0.0f, 0.0f, 0.0f, 0.0f);  // miss -> 0 (composite add is a no-op)
        dr.pipeline                  = pm.get_ssr_pipeline();
        dr.layout                    = pm.get_ssr_layout();
        dr.sets[0]                   = pm.get_lighting_set(c.frameIndex);  // set 0: G-buffer
        dr.sets[1]                   = pm.get_ssr_hdr_set(c.frameIndex);   // set 1: lit HDR
        dr.setCount                  = 2;

        const DebugParams& tune = c.tune;  // defaults = the shipped look (single source of truth)
        const Mat4         proj = c.camera->get_projection_matrix();
        sp.proj                 = proj;
        sp.invProj              = glm::inverse(proj);
        sp.invView              = glm::inverse(c.camera->get_view_matrix());
        // Field copies generated from src/debug/Tunables.def (maxDist/thickness/stride/intensity/puddleCoverage).
        {
            const DebugParams& d = tune;  // the X-macro below spells the source as `d.<name>`
#define TUNE_B(n, g, k, def, label, rel)
#define TUNE_F(n, g, k, def, lo, hi, label, sfmt, prec, push, rel) push d.n;
#define PUSH(m) sp.m =
#define NOPUSH (void)
#include "debug/Tunables.def"
            if (!d.ssrEnabled)
                sp.intensity = 0.0f;  // derived gate stays hand-written (== enabled ? intensity : 0)
            sp.wetness = c.state.wetness;
            // sp.puddleCoverage (generated copy above) is d.puddleCoverage in BOTH builds: release uses the
            // DebugParams default 0.501, which lighting.frag's release literal SP_PUDDLE_COVERAGE mirrors (pinned
            // by tests/test_debug_params_io.cpp), so SSR reflects in exactly the pools the lit pass turns to mirrors.
            if (!d.puddlesEnabled)
                sp.puddleCoverage =
                    0.0f;  // derived gate (== enabled ? coverage : 0), as SceneParamsUniform packs wetParams.z
        }
    }

    // Lit HDR -> readable.
    void before(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Restore HDR for the forward passes; SSR image -> readable for the composite.
    void after(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Ssr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }
};

}  // namespace

std::unique_ptr<IRenderPass> make_ssr_pass() {
    return std::make_unique<SsrPass>();
}

}  // namespace swish
