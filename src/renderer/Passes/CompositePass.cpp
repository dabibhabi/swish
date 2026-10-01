// Composite: HDR + bloom + AO + SSR + god-rays -> swapchain image (AgX tonemap + grade). The inputs are the declared
// list kCompositeInputs (PassInputs.h); the descriptor set is written by PostProcessManager from that list.
#include "debug/DebugParams.h"
#include "renderer/Passes/FullscreenPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"

#include <memory>

namespace swish {

namespace {

class CompositePass final : public FullscreenPass<PostProcessParams> {
public:
    const char* name() const override { return "Composite"; }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, PostProcessParams& pp) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_composite_render_pass();
        d.framebuffer                = pm.get_composite_framebuffer(c.imageIndex);
        d.extent                     = c.swapExtent;
        d.clear                      = clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        d.pipeline                   = pm.get_composite_pipeline();
        d.layout                     = pm.get_composite_layout();
        d.sets[0]                    = pm.get_composite_set(c.frameIndex);

        pp.bloom_intensity = 0.45f;  // retuned: lie preset 1.097 was a 3.7x wash
        // Shipped grade (composite.frag). Exposure feeds the pre-AgX multiply; contrast /
        // saturation are the post-tonemap "look". Raised from the old 0.45 — that value was
        // fighting the double-gamma washout; now the AgX EOTF (pow 2.2) linearises correctly,
        // the scene is no longer hot, so exposure returns to ~1.0 with a mild punchy grade.
        pp.exposure   = 1.55f;  // retuned between restored 1.25 and lie preset 2.0
        pp.contrast   = 1.12f;  // EXPERIMENT: restored (lie preset had 1.499)
        pp.saturation = 1.2f;   // EXPERIMENT: restored (lie preset had 0.988)
        pp.brightness = 0.0f;   // EXPERIMENT: restored (lie preset had 0.032)
#ifdef SWISH_DEBUG_UI
        pp.bloom_intensity = c.tune.bloomIntensity;
        // Auto-exposure drives the exposure when enabled; else the manual slider.
        pp.exposure    = c.tune.autoExposure ? c.state.exposure : c.tune.exposure;
        pp.brightness  = c.tune.brightness;
        pp.contrast    = c.tune.contrast;
        pp.saturation  = c.tune.saturation;
        pp.temperature = c.tune.temperature;
        pp.tint        = c.tune.tint;
#endif
        pp.rain_intensity = c.state.rainIntensity;
        pp.fog_density    = 0.04f;  // multiplied by rain_intensity in the shader
    }
};

}  // namespace

std::unique_ptr<IRenderPass> make_composite_pass() {
    return std::make_unique<CompositePass>();
}

}  // namespace swish
