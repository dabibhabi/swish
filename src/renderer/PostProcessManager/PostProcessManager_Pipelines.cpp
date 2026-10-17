// PostProcessManager: fullscreen post-process pipelines.

#include "PostProcessManager.h"
#include "renderer/Pipeline/Pipeline.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Pipelines — fullscreen quads with no vertex input
// ══════════════════════════════════════════════════════════════════════

VkPipeline PostProcessManager::makeFullscreenPipeline(const std::string& fragShader, VkRenderPass rp,
                                                      VkPipelineLayout layout, VkExtent2D ext) const {
    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "/fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "/" + fragShader + ".spv";
    cfg.noVertexInput    = true;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.pipelineLayout   = layout;
    return pipeline::create_graphics(m_device, m_pipelineCache, cfg, rp, ext);
}

#ifdef SWISH_DEBUG_UI
// Shader hot-reload (S5): build the new composite pipeline from the freshly written .spv first (a throw leaves the old
// pipeline untouched), then wait for the GPU and swap. Called between frames by ShaderHotReload.
void PostProcessManager::rebuild_composite_pipeline() {
    const VkPipeline fresh =
        makeFullscreenPipeline("composite.frag", m_compositeRenderPass, m_compositeLayout, m_swapExtent);
    vkDeviceWaitIdle(m_device);
    if (m_compositePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_compositePipeline, nullptr);
    m_compositePipeline = fresh;
}
#endif

void PostProcessManager::createPipelines() {
    m_bloomExtractPipeline =
        makeFullscreenPipeline("bloom_extract.frag", m_bloomRenderPass, m_postProcessLayout, m_bloomExtent);
    m_bloomBlurPipeline =
        makeFullscreenPipeline("bloom_blur.frag", m_bloomRenderPass, m_postProcessLayout, m_bloomExtent);
    // SSAO (depth → AO) uses its own 144-B push layout; AO-blur has no push block
    // so it reuses m_postProcessLayout (an unused push range is harmless). Both
    // render at the AO extent (1/2 render res). SSAO is recorded per frame only
    // under SWISH_DEBUG_UI; in release the AO blur image stays primed to white by
    // primeAOTexture() so the composite `hdr *= ao` is a no-op (release unchanged).
    m_ssaoPipeline   = makeFullscreenPipeline("ssao.frag", m_aoRenderPass, m_ssaoLayout, m_aoExtent);
    m_aoBlurPipeline = makeFullscreenPipeline("ao_blur.frag", m_aoRenderPass, m_postProcessLayout, m_aoExtent);
    // SSR: full render-res reflection, reuses the lighting render pass (R16F color).
    m_ssrPipeline = makeFullscreenPipeline("ssr.frag", m_lightingRenderPass, m_ssrLayout, m_renderExtent);
    // God-rays: 1/2 render-res radial blur, reuses the bloom render pass (R16F color).
    m_godraysPipeline = makeFullscreenPipeline("godrays.frag", m_bloomRenderPass, m_godraysLayout, m_godraysExtent);
    // Composite outputs at the swap extent (dynamic viewport/scissor make the
    // extent cosmetic, but keep it consistent with the composite FB size).
    m_compositePipeline =
        makeFullscreenPipeline("composite.frag", m_compositeRenderPass, m_compositeLayout, m_swapExtent);
}

void PostProcessManager::destroyPipelines() {
    auto destroy = [&](VkPipeline& p) {
        if (p != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    };
    destroy(m_bloomExtractPipeline);
    destroy(m_bloomBlurPipeline);
    destroy(m_compositePipeline);
    destroy(m_ssaoPipeline);
    destroy(m_aoBlurPipeline);
    destroy(m_ssrPipeline);
    destroy(m_godraysPipeline);
}

}  // namespace swish
