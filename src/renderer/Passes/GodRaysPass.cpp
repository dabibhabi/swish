// God-rays / volumetric light shafts (ships in release). A half-render-res radial blur of the lit HDR toward the
// projected sun, occluded by scene geometry (Mitchell 2007 / GPU Gems 3 Ch.13). Added on top of the HDR in the
// composite. Runs every frame in BOTH builds: the tunables come from FrameCtx::tune (live DebugParams in debug, the
// shipped defaults in release). Mirrors the SSR pass's HDR read -> restore barriers; the god-rays image is left
// SHADER_READ for the composite add.
#include "debug/DebugParams.h"
#include "renderer/Passes/FullscreenPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/Barrier.h"

#include <glm/glm.hpp>
#include <memory>

namespace swish {

namespace {

class GodRaysPass final : public FullscreenPass<GodRaysParams> {
public:
    const char* name() const override { return "GodRays"; }
    void        declare(PassDeclaration& d) const override {
        // set 0 = the G-buffer set (depth is what the shader samples), set 1 = lit HDR.
        d.read({Img::HdrDepth, Img::Hdr}).write({Img::Godrays});
    }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, GodRaysParams& gp) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_godrays_render_pass();
        d.framebuffer                = pm.get_godrays_framebuffer();
        d.extent                     = pm.get_godrays_extent();
        d.clear    = clear_color(0.0f, 0.0f, 0.0f, 0.0f);  // off-screen sun -> 0 (composite add is a no-op)
        d.pipeline = pm.get_godrays_pipeline();
        d.layout   = pm.get_godrays_layout();
        d.sets[0]  = pm.get_lighting_set(c.frameIndex);     // set 0: G-buffer (depth)
        d.sets[1]  = pm.get_godrays_hdr_set(c.frameIndex);  // set 1: lit HDR
        d.setCount = 2;

        // Project the sun (a world-space direction -> point at infinity) to screen space
        // and fade the effect out as it leaves the frame (or goes behind the camera).
        const Mat4 view = c.camera->get_view_matrix();
        const Mat4 proj = c.camera->get_projection_matrix();
        Vec4       clip = proj * view * Vec4(c.sunDir, 0.0f);
        Vec2       sunUV(0.0f);
        float      vis = 0.0f;
        if (clip.w > 1e-4f) {  // sun in front of the camera
            sunUV       = (Vec2(clip.x, clip.y) / clip.w) * 0.5f + 0.5f;
            Vec2 loFade = glm::smoothstep(Vec2(-0.2f), Vec2(0.0f), sunUV);  // fade in from the low edge
            Vec2 hiFade = glm::smoothstep(Vec2(1.2f), Vec2(1.0f), sunUV);   // fade out past the high edge
            vis         = loFade.x * loFade.y * hiFade.x * hiFade.y;
        }

        const DebugParams& t = c.tune;  // defaults = the shipped look (single source of truth)
        gp.sunUV             = Vec4(sunUV, vis, 0.0f);
        gp.tune = Vec4(t.godrayDensity, t.godrayDecay, t.godrayWeight, t.godraysEnabled ? t.godrayIntensity : 0.0f);
    }

    // Lit HDR -> readable (the shaft source).
    void before(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Restore HDR for the forward passes; god-rays image -> readable for the composite.
    void after(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL>(
            c.cmd, c.images.image(Img::Hdr, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Godrays, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }
};

}  // namespace

std::unique_ptr<IRenderPass> make_godrays_pass() { return std::make_unique<GodRaysPass>(); }

}  // namespace swish
