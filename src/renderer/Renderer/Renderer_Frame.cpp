// Renderer::drawFrame — one iteration of the render loop (acquire -> per-frame updates ->
// record -> submit -> present). Split out of Renderer.cpp as a pure move.
#include "Renderer.h"
#include "core/Window/Window.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/CommandManager/CommandManager.h"
#include "renderer/Pipeline/Device/Device.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/RendererConfig.h"
#include "renderer/Swapchain/Swapchain.h"
#include "renderer/SyncObjects/SyncObjects.h"
#include "scene/Camera/Camera.h"
#include "utils/types/Types.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Init.h"

#include <glm/glm.hpp>
#include <optional>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// drawFrame() — ONE FRAME OF THE RENDER LOOP
// ══════════════════════════════════════════════════════════════════════

void Renderer::drawFrame(float deltaTime) {
    m_syncObjects->waitForFence(m_device->getDevice(), m_currentFrame);

#ifdef SWISH_DEBUG_UI
    // Live SSAA rescale: the "Apply SSAA" button set this flag last frame. Rebuild
    // the whole offscreen chain at the new scale here — before acquiring an image,
    // and after the fence wait — so no frame is in flight during the recreate. The
    // flag is consumed here (not in apply_debug_params) so the recreate never runs
    // mid-frame. Skip this frame; the next renders at the new resolution.
    if (m_debugParams.ssaaApplyRequested) {
        m_debugParams.ssaaApplyRequested = false;
        if (has_post_process() && m_postProcess->get_render_scale() != m_debugParams.ssaaScale) {
            m_postProcess->set_render_scale(m_debugParams.ssaaScale);
            recreateSwapchain();
        }
        return;
    }
#endif

    // Each frame phase is a named lambda — the state machine reads as
    // (acquire) → record → (submit) → (present) → handlePresent.
    auto acquireImage = [&]() -> std::optional<uint32_t> {
        uint32_t idx;
        VkResult r = vkAcquireNextImageKHR(m_device->getDevice(), m_swapchain->getSwapchain(), UINT64_MAX,
                                           m_syncObjects->getImageAvailable(m_currentFrame), VK_NULL_HANDLE, &idx);
        if (vkx::is_out_of_date(r)) {
            recreateSwapchain();
            return std::nullopt;
        }
        if (!vkx::is_presentable(r))
            throw std::runtime_error("failed to acquire swap chain image!");
        return idx;
    };

    auto submitFrame = [&](uint32_t imageIndex) {
        VkSemaphore          waitSems[]   = {m_syncObjects->getImageAvailable(m_currentFrame)};
        VkSemaphore          signalSems[] = {m_syncObjects->getRenderFinished(imageIndex)};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT};
        VkCommandBuffer      cmdBufs[]    = {m_commandManager->getBuffer(m_currentFrame)};

        auto si                 = vkx::makeSubmitInfo();
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = waitSems;
        si.pWaitDstStageMask    = waitStages;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = cmdBufs;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = signalSems;

        VK_CHECK(vkQueueSubmit(m_device->getGraphicsQueue(), 1, &si, m_syncObjects->getInFlightFence(m_currentFrame)));
    };

    auto presentFrame = [&](uint32_t imageIndex) -> VkResult {
        VkSemaphore    waitSems[] = {m_syncObjects->getRenderFinished(imageIndex)};
        VkSwapchainKHR scs[]      = {m_swapchain->getSwapchain()};

        auto pi               = vkx::makePresentInfo();
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = waitSems;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = scs;
        pi.pImageIndices      = &imageIndex;
        return vkQueuePresentKHR(m_device->getPresentQueue(), &pi);
    };

    auto handlePresent = [&](VkResult r) {
        if (vkx::swapchain_needs_recreation(r) || m_window->wasResized()) {
            recreateSwapchain();  // clears the window's resize flag
        } else if (!vkx::is_success(r)) {
            throw std::runtime_error("failed to present swap chain image!");
        }
    };

    auto imageIndex = acquireImage();
    if (!imageIndex)
        return;

    m_syncObjects->resetFence(m_device->getDevice(), m_currentFrame);
    m_commandManager->resetBuffer(m_currentFrame);

#ifdef SWISH_DEBUG_UI
    // Build the debug panel (mutates m_debugParams) and push the edits into the
    // live uniforms BEFORE the camera UBO is written below — so drags update now.
    m_debugUI.begin_frame(m_debugParams, m_camera->get_view_matrix(), m_camera->get_projection_matrix());
    apply_debug_params();
#endif

    // Pre-camera-UBO CPU work of the registry passes (debug: auto-exposure readback, published in m_frameState).
    const FrameUpdate frameUpdate = make_frame_update(deltaTime);
    m_passes.begin_frame(frameUpdate);

    // ── CSM: fit per-cascade sun light-space matrices to the view frustum ──
    m_geometry.compute_cascades(*m_camera, m_weather.sun_dir(), tune());
    m_cameraUniforms->set_cascades(m_geometry.cascade_vp(), m_geometry.cascade_splits());

    m_cameraUniforms->update(m_currentFrame, *m_camera);

#ifdef SWISH_DEBUG_UI
    // Push the live "look" tunables into set 4 for this frame (identity to the
    // shipped constants at defaults, so nothing changes until a slider moves).
    m_sceneParams.update(m_currentFrame, m_debugParams);
#endif

    // Registry passes' per-frame CPU work after the camera UBO is written (windshield rain: wiper + flow + UBO).
    m_passes.update(frameUpdate);
    // The rain pass published this frame's integrated wetness in beginFrame; patch it into the camera UBO that
    // update() just wrote (camPos.w).
    m_cameraUniforms->set_wetness(m_currentFrame, m_frameState.wetness);

    recordCommandBuffer(m_currentFrame, *imageIndex);

    submitFrame(*imageIndex);
    handlePresent(presentFrame(*imageIndex));

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

}  // namespace swish
