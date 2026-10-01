// Bloom: extract bright pixels (HDR -> 1/4 res), then a separable blur H and V. Three fullscreen passes, each
// leaving its target in SHADER_READ_ONLY for the next. GPU objects live in PostProcessManager.
#include "debug/DebugParams.h"
#include "renderer/Passes/FullscreenPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "utils/vulkan/Barrier.h"

#include <memory>

namespace swish {

namespace {

void to_shader_read(const FrameCtx& c, Img img) {
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        c.cmd, c.images.image(img, c.frameIndex), VK_IMAGE_ASPECT_COLOR_BIT);
}

// HDR -> bright pixels at 1/4 resolution.
class BloomExtractPass final : public FullscreenPass<PostProcessParams> {
public:
    const char* name() const override { return "BloomExtract"; }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, PostProcessParams& pp) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_bloom_render_pass();
        d.framebuffer                = pm.get_bloom_extract_framebuffer();
        d.extent                     = pm.get_bloom_extent();
        d.clear                      = clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        d.pipeline                   = pm.get_bloom_extract_pipeline();
        d.layout                     = pm.get_postprocess_layout();
        d.sets[0]                    = pm.get_bloom_extract_set(c.frameIndex);

        pp.threshold       = 1.77f;  // lie preset (was 1.0)
        pp.bloom_intensity = 0.45f;  // retuned: lie preset 1.097 was a 3.7x wash
        pp.exposure        = 1.0f;
#ifdef SWISH_DEBUG_UI
        pp.threshold       = c.tune.bloomThreshold;
        pp.bloom_intensity = c.tune.bloomIntensity;
#endif
    }
    void after(const FrameCtx& c) const override { to_shader_read(c, Img::BloomExtract); }
};

// One direction of the separable blur (H: extract -> blurH, V: blurH -> blurV).
class BloomBlurPass final : public FullscreenPass<PostProcessParams> {
public:
    explicit BloomBlurPass(bool horizontal) : m_horizontal(horizontal) {}
    const char* name() const override { return m_horizontal ? "BloomBlurH" : "BloomBlurV"; }

protected:
    void describe(const FrameCtx& c, FullscreenDraw& d, PostProcessParams& pp) const override {
        const PostProcessManager& pm = *c.res.pp;
        d.renderPass                 = pm.get_bloom_render_pass();
        d.framebuffer = m_horizontal ? pm.get_bloom_blur_h_framebuffer() : pm.get_bloom_blur_v_framebuffer();
        d.extent      = pm.get_bloom_extent();
        d.clear       = clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        d.pipeline    = pm.get_bloom_blur_pipeline();
        d.layout      = pm.get_postprocess_layout();
        d.sets[0]     = m_horizontal ? pm.get_bloom_blur_h_set() : pm.get_bloom_blur_v_set();

        pp.texel_x = m_horizontal ? 1.0f / static_cast<float>(d.extent.width) : 0.0f;
        pp.texel_y = m_horizontal ? 0.0f : 1.0f / static_cast<float>(d.extent.height);
    }
    void after(const FrameCtx& c) const override {
        to_shader_read(c, m_horizontal ? Img::BloomBlurH : Img::BloomBlurV);
    }

private:
    bool m_horizontal;
};

}  // namespace

std::unique_ptr<IRenderPass> make_bloom_extract_pass() {
    return std::make_unique<BloomExtractPass>();
}
std::unique_ptr<IRenderPass> make_bloom_blur_h_pass() {
    return std::make_unique<BloomBlurPass>(true);
}
std::unique_ptr<IRenderPass> make_bloom_blur_v_pass() {
    return std::make_unique<BloomBlurPass>(false);
}

}  // namespace swish
