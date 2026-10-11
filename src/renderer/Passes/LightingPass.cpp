// Deferred lighting: G-buffer + shadow atlas + IBL (+ live tunables in debug builds) -> HDR. A fullscreen draw
// through DeferredLightingPipeline (lighting.frag, five descriptor sets), which this pass owns.
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/DeferredLightingPipeline/DeferredLightingPipeline.h"
#include "renderer/IBLManager/IBLManager.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/Renderer/RendererServices.h"
#include "scene/Camera/Camera.h"
#include "utils/vulkan/RenderScope.h"
#ifdef SWISH_DEBUG_UI
#include "debug/SceneParamsUniform.h"
#endif

#include <glm/glm.hpp>
#include <memory>

namespace swish {

namespace {

class LightingPass final : public IRenderPass {
public:
    const char* name() const override { return "Lighting"; }

    void declare(PassDeclaration& d) const override {
        d.read({Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth}).write({Img::Hdr});
    }

    void init(const PassInitCtx& ctx) override {
        const PostProcessManager& pp = *ctx.res.pp;
        // Named-field init (order-independent): the debug build appends an extra scene-params set layout, so
        // positional aggregate init would shift.
        DeferredLightingPipeline::Config cfg{};
        cfg.cameraSetLayout    = ctx.cameraSetLayout;
        cfg.gbufferSetLayout   = pp.get_lighting_tex_layout();
        cfg.shadowSetLayout    = pp.get_shadow_tex_layout();
        cfg.iblSetLayout       = ctx.res.ibl->get_set_layout();
        cfg.lightingRenderPass = pp.get_lighting_render_pass();
        cfg.extent             = ctx.renderExtent;
        cfg.pipelineCache      = ctx.services.pipelineCache;
#ifdef SWISH_DEBUG_UI
        cfg.sceneParamsSetLayout = ctx.res.sceneParams->get_layout();  // live-tunables UBO (set 4)
#endif
        m_pipeline.init(ctx.services.device, cfg);
    }

    void onResize(const PassInitCtx& ctx) override {
        m_pipeline.recreate(ctx.services.device, ctx.res.pp->get_lighting_render_pass(), ctx.renderExtent);
    }

    void record(const FrameCtx& c) override {
        const PostProcessManager& pp = *c.res.pp;
        VkClearValue              clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        vkx::RenderScope pass(c.cmd, pp.get_lighting_render_pass(), pp.get_lighting_framebuffer(c.frameIndex),
                              c.renderExtent, clear);

        const Mat4 invView = glm::inverse(c.camera->get_view_matrix());
        const Mat4 invProj = glm::inverse(c.camera->get_projection_matrix());

        m_pipeline.bind_and_record(c.cmd, c.res.cameraUniforms->get_set(c.frameIndex),
                                   pp.get_lighting_set(c.frameIndex), pp.get_shadow_set(c.frameIndex),
                                   c.res.ibl->get_set(),
#ifdef SWISH_DEBUG_UI
                                   c.res.sceneParams->get_set(c.frameIndex),
#endif
                                   invView, invProj, c.renderExtent);
    }

    void cleanup(VkDevice device) override { m_pipeline.cleanup(device); }

private:
    DeferredLightingPipeline m_pipeline;
};

}  // namespace

std::unique_ptr<IRenderPass> make_lighting_pass() {
    return std::make_unique<LightingPass>();
}

}  // namespace swish
