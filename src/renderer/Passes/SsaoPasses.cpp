// SSAO + bilateral AO blur (debug realism feature). Runs at 1/2 render resolution into the AO images the composite
// multiplies in. Depth is sampled in DEPTH_STENCIL_READ_ONLY (the layout the lighting pass left it in). Each pass ends
// in COLOR_ATTACHMENT and is barriered to SHADER_READ, mirroring the bloom chain. When SSAO is toggled off the
// intensity is forced to 0, so the shader outputs 1.0 (no darkening) rather than leaving a stale AO in the image.
// Release keeps the primed-white AO image (composite `hdr *= ao` is a no-op), so these passes are debug-only.
#ifdef SWISH_DEBUG_UI

#include "debug/DebugParams.h"
#include "renderer/Passes/FullscreenPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/Barrier.h"

#include <glm/glm.hpp>
#include <memory>

namespace swish {

namespace {

// Scene depth -> raw AO.
class SsaoPass final : public FullscreenPass<SsaoParams> {
public:
    const char* name() const override { return "Ssao"; }
    void        declare(PassDeclaration& d) const override { d.read({Img::HdrDepth}).write({Img::Ao}); }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, SsaoParams& sp) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_ao_render_pass();
        d.framebuffer                = pm.get_ao_framebuffer();
        d.extent                     = pm.get_ao_extent();
        d.clear    = clear_color(1.0f, 1.0f, 1.0f, 1.0f);  // 1 = unoccluded (safe if a pixel is skipped)
        d.pipeline = pm.get_ssao_pipeline();
        d.layout   = pm.get_ssao_layout();
        d.sets[0]  = pm.get_ao_set(c.frameIndex);

        const Mat4 proj  = c.camera->get_projection_matrix();
        sp.projection    = proj;
        sp.invProjection = glm::inverse(proj);
        sp.radius        = c.tune.ssaoRadius;
        sp.bias          = c.tune.ssaoBias;
        sp.intensity     = c.tune.ssaoEnabled ? c.tune.ssaoIntensity : 0.0f;
    }
    void after(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::Ao, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }
};

// Bilateral blur: raw AO -> smoothed AO (sampled by composite). No push block.
class AoBlurPass final : public FullscreenPass<NoPush> {
public:
    const char* name() const override { return "AoBlur"; }
    void        declare(PassDeclaration& d) const override { d.read({Img::Ao}).write({Img::AoBlur}); }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, NoPush&) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_ao_render_pass();
        d.framebuffer                = pm.get_ao_blur_framebuffer();
        d.extent                     = pm.get_ao_extent();
        d.clear                      = clear_color(1.0f, 1.0f, 1.0f, 1.0f);
        d.pipeline                   = pm.get_ao_blur_pipeline();
        d.layout                     = pm.get_postprocess_layout();
        d.sets[0]                    = pm.get_ao_blur_set();
    }
    void after(const FrameCtx& c) const override {
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(Img::AoBlur, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
    }
};

}  // namespace

std::unique_ptr<IRenderPass> make_ssao_pass() {
    return std::make_unique<SsaoPass>();
}
std::unique_ptr<IRenderPass> make_ao_blur_pass() {
    return std::make_unique<AoBlurPass>();
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
