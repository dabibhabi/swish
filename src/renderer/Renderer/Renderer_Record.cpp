// Renderer command-buffer recording. The frame is the pass registry's ordered list (renderer/Passes/PassList.def):
// this file only brackets it with begin/end recording and builds the FrameCtx.
#include "Renderer.h"
#include "renderer/CommandManager/CommandManager.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/Swapchain/Swapchain.h"
#include "scene/Camera/Camera.h"

namespace swish {

void Renderer::recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    m_commandManager->beginRecording(frameIndex);
    VkCommandBuffer cmd = m_commandManager->getBuffer(frameIndex);

    // SSAA: the whole offscreen chain runs at the render extent; only the
    // composite pass outputs at the swap extent (downsampling the HDR).
    const FrameCtx ctx{cmd,
                       frameIndex,
                       imageIndex,
                       m_postProcess->get_render_extent(),
                       m_postProcess->get_swap_extent(),
                       m_images,
                       m_passRes,
                       m_camera.get(),
                       m_frameState,
                       tune(),
                       m_weather.sun_dir()};

    m_passes.record_all(ctx);

#ifdef SWISH_DEBUG_UI
    // Last frame's cull tallies for the debug readout (GeometryProducer measures them while recording).
    m_debugParams.cullShadowSubmitted = static_cast<int>(m_geometry.last_shadow_cull().submitted);
    m_debugParams.cullShadowTotal     = static_cast<int>(m_geometry.last_shadow_cull().total);
    m_debugParams.cullMainSubmitted   = static_cast<int>(m_geometry.last_main_cull().submitted);
    m_debugParams.cullMainTotal       = static_cast<int>(m_geometry.last_main_cull().total);

    // S3: if a capture was requested, append the swapchain (+ optional Img::Hdr) copy to the staging buffer after the
    // last pass. The file is written two frames later in drawFrame once this slot's fence has been waited.
    m_frameCapture.record(cmd, frameIndex, m_swapchain->getImages()[imageIndex], m_swapchain->getImageFormat(),
                          m_swapchain->getExtent(), m_swapchain->supportsTransferSrc(),
                          m_images.image(Img::Hdr, frameIndex), m_postProcess->get_render_extent());
#endif

    m_commandManager->endRecording(frameIndex);
}

}  // namespace swish
