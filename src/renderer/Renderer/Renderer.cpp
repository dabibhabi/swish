#include "Renderer.h"

#include "../../core/Window/Window.h"
#include "../../scene/Camera/Camera.h"
#include "../../scene/Entity/CarEntity.h"  // CarEntity::kMaxSpeed for windshield speedFactor
#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../CameraUniforms/CameraUniforms.h"
#include "../CommandManager/CommandManager.h"
#include "../GlassPass/GlassPass.h"
#include "../MaterialDescriptors/MaterialDescriptors.h"
#include "../Pipeline/Device/Device.h"
#include "../PostProcessManager/PostProcessManager.h"
#include "../RainSystem/RainSystem.h"
#include "../ResourceManager/ResourceManager.h"
#include "../Swapchain/Swapchain.h"
#include "../SyncObjects/SyncObjects.h"
#include "../TextureManager/TextureManager.h"
#include "../VulkanContext/VulkanContext.h"
#include "../WindshieldRainPass/WindshieldRainPass.h"

#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>

namespace swish {

namespace {

void setViewportAndScissor(VkCommandBuffer cmd, VkExtent2D ext) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(ext.width), static_cast<float>(ext.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

struct ScopedRenderPass {
    VkCommandBuffer cmd;

    ScopedRenderPass(VkCommandBuffer c, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent,
                     const VkClearValue* clears, uint32_t clearCount)
        : cmd(c) {
        auto info              = vk::makeRenderPassBeginInfo();
        info.renderPass        = rp;
        info.framebuffer       = fb;
        info.renderArea.extent = extent;
        info.clearValueCount   = clearCount;
        info.pClearValues      = clears;
        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Convenience overload for the common single-clear case.
    ScopedRenderPass(VkCommandBuffer c, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, const VkClearValue& clear)
        : ScopedRenderPass(c, rp, fb, extent, &clear, 1) {}

    ~ScopedRenderPass() { vkCmdEndRenderPass(cmd); }

    ScopedRenderPass(const ScopedRenderPass&)            = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;
};

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ══════════════════════════════════════════════════════════════════════

Renderer::Renderer()
    : m_context(std::make_unique<VulkanContext>()),
      m_device(std::make_unique<Device>()),
      m_swapchain(std::make_unique<Swapchain>()),
      m_commandManager(std::make_unique<CommandManager>()),
      m_syncObjects(std::make_unique<SyncObjects>()),
      m_cameraUniforms(std::make_unique<CameraUniforms>()),
      m_materialDescriptors(std::make_unique<MaterialDescriptors>()) {}

Renderer::~Renderer() = default;

void Renderer::init(Window& window) {
    m_window = &window;

    // ── Core Vulkan setup ─────────────────────────────────────────
    m_context->init(m_window->getHandle());
    m_device->init(m_context->getInstance(), m_context->getSurface());

    int width, height;
    m_window->getFramebufferSize(&width, &height);
    m_swapchain->init(*m_device, m_context->getSurface(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    m_cameraUniforms->init(m_device->getDevice(), m_device->getPhysicalDevice(), m_device->getAllocator(),
                           MAX_FRAMES_IN_FLIGHT);
    m_materialDescriptors->init(m_device->getDevice());

    m_commandManager->init(m_device->getDevice(), m_device->getQueueFamilies().graphicsFamily.value(),
                           MAX_FRAMES_IN_FLIGHT);

    m_syncObjects->init(m_device->getDevice(), MAX_FRAMES_IN_FLIGHT, m_swapchain->getImageCount());

    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->init(m_device->getDevice(), m_device->getPhysicalDevice(), m_commandManager->getPool(),
                        m_device->getGraphicsQueue(), m_device->getAllocator(), m_swapchain->getExtent(),
                        m_swapchain->getImageFormat(), m_swapchain->getImageViews());

    // SSAA: every offscreen target (incl. the HDR/depth views the forward passes
    // wrap) is sized at the render extent = swap × kRenderScale, not the swap
    // extent. Only the composite output stays swap-sized.
    const VkExtent2D renderExtent = m_postProcess->get_render_extent();

    // ── Rain forward pass — after PostProcessManager (needs HDR + depth views) ─
    {
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> hdrViews{}, depthViews{};
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            hdrViews[i]   = m_postProcess->get_hdr_view(i);
            depthViews[i] = m_postProcess->get_hdr_depth_view(i);
        }

        m_rainSystem = std::make_unique<RainSystem>();
        m_rainSystem->init(services(), hdrViews, depthViews, renderExtent, m_cameraUniforms->get_layout());

        m_glassPass = std::make_unique<GlassPass>();
        m_glassPass->init(services(), hdrViews, depthViews, renderExtent, m_cameraUniforms->get_layout());

        m_windshieldRainPass = std::make_unique<WindshieldRainPass>();
        m_windshieldRainPass->init(services(), hdrViews, depthViews, renderExtent, m_cameraUniforms->get_layout());
    }

    // Sun shadow-map depth pass — fixed 2048² target owned by PostProcessManager.
    m_depthOnlyPipeline.init(m_device->getDevice(), {
                                                        m_postProcess->get_shadow_render_pass(),
                                                        {2048, 2048},
                                                    });

    // Scene + lighting pipelines use dynamic viewport/scissor, so the init
    // extent is cosmetic — pass the render extent (their offscreen target size).
    m_scenePipeline.init(m_device->getDevice(), {
                                                    m_cameraUniforms->get_layout(),
                                                    m_materialDescriptors->get_layout(),
                                                    m_postProcess->get_gbuffer_render_pass(),
                                                    renderExtent,
                                                });
    // Named-field init (order-independent): the debug build appends an extra
    // scene-params set layout, so positional aggregate init would shift.
    DeferredLightingPipeline::Config lightingCfg{};
    lightingCfg.cameraSetLayout    = m_cameraUniforms->get_layout();
    lightingCfg.gbufferSetLayout   = m_postProcess->get_lighting_tex_layout();
    lightingCfg.shadowSetLayout    = m_postProcess->get_shadow_tex_layout();
    lightingCfg.lightingRenderPass = m_postProcess->get_lighting_render_pass();
    lightingCfg.extent             = renderExtent;
#ifdef SWISH_DEBUG_UI
    // Live-tunables UBO (set 3) — must exist before the pipeline layout is built.
    m_sceneParams.init(m_device->getDevice(), m_device->getAllocator(), MAX_FRAMES_IN_FLIGHT);
    lightingCfg.sceneParamsSetLayout = m_sceneParams.get_layout();
#endif
    m_deferredLighting.init(m_device->getDevice(), lightingCfg);
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());

#ifdef SWISH_DEBUG_UI
    m_debugUI.cleanup();  // ImGui shutdown before device/instance teardown
    m_sceneParams.cleanup(m_device->getDevice());  // set 3 UBO + pool + layout
#endif

    m_camera.reset();

    destroy_scene_geometry();
    destroy_dynamic_geometry();

    // RainSystem must be cleaned before PostProcessManager (HDR images it references)
    if (m_rainSystem) {
        m_rainSystem->cleanup(m_device->getDevice());
        m_rainSystem.reset();
    }

    if (m_glassPass) {
        m_glassPass->cleanup(m_device->getDevice());
        m_glassPass.reset();
    }

    if (m_windshieldRainPass) {
        m_windshieldRainPass->cleanup(m_device->getDevice());
        m_windshieldRainPass.reset();
    }

    if (has_post_process()) {
        m_postProcess->cleanup();
        m_postProcess.reset();
    }

    m_materialDescriptors->cleanup(m_device->getDevice());
    m_cameraUniforms->cleanup(m_device->getDevice());

    // ── Core Vulkan teardown ──────────────────────────────────────
    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    m_deferredLighting.cleanup(m_device->getDevice());
    m_depthOnlyPipeline.cleanup(m_device->getDevice());
    m_scenePipeline.cleanup(m_device->getDevice());
    m_swapchain->cleanup(m_device->getDevice());
    m_device->cleanup();
    m_context->cleanup();
}

// ══════════════════════════════════════════════════════════════════════
// Manager registration + getters
// ══════════════════════════════════════════════════════════════════════

void Renderer::register_texture_manager(TextureManager* mgr) {
    m_textureManager = mgr;
}
void Renderer::register_scene_manager(SceneManager* mgr) {
    m_sceneManager = mgr;
}
void Renderer::register_model_manager(ModelManager* mgr) {
    m_modelManager = mgr;
}

TextureManager* Renderer::get_texture_manager() const {
    return m_textureManager;
}
SceneManager* Renderer::get_scene_manager() const {
    return m_sceneManager;
}
ModelManager* Renderer::get_model_manager() const {
    return m_modelManager;
}

// ══════════════════════════════════════════════════════════════════════
// Vulkan handle getters (for managers that need raw handles)
// ══════════════════════════════════════════════════════════════════════

RendererServices Renderer::services() const {
    return RendererServices{
        m_device->getDevice(),        m_device->getPhysicalDevice(), m_commandManager->getPool(),
        m_device->getGraphicsQueue(), m_swapchain->getExtent(),      m_device->getAllocator(),
    };
}

// ══════════════════════════════════════════════════════════════════════
// Camera
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_camera(Camera* camera) {
    m_camera.reset(camera);
}

Camera* Renderer::get_camera() const {
    return m_camera.get();
}

// ══════════════════════════════════════════════════════════════════════
// GPU synchronization
// ══════════════════════════════════════════════════════════════════════

void Renderer::wait_for_idle() {
    vkDeviceWaitIdle(m_device->getDevice());
}

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
        if (vk::is_out_of_date(r)) {
            recreateSwapchain();
            return std::nullopt;
        }
        if (!vk::is_presentable(r))
            throw std::runtime_error("failed to acquire swap chain image!");
        return idx;
    };

    auto submitFrame = [&](uint32_t imageIndex) {
        VkSemaphore          waitSems[]   = {m_syncObjects->getImageAvailable(m_currentFrame)};
        VkSemaphore          signalSems[] = {m_syncObjects->getRenderFinished(imageIndex)};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT};
        VkCommandBuffer      cmdBufs[]    = {m_commandManager->getBuffer(m_currentFrame)};

        auto si                 = vk::makeSubmitInfo();
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

        auto pi               = vk::makePresentInfo();
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = waitSems;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = scs;
        pi.pImageIndices      = &imageIndex;
        return vkQueuePresentKHR(m_device->getPresentQueue(), &pi);
    };

    auto handlePresent = [&](VkResult r) {
        if (vk::swapchain_needs_recreation(r) || m_window->wasResized()) {
            m_window->resetResizedFlag();
            recreateSwapchain();
        } else if (!vk::is_success(r)) {
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
    m_debugUI.begin_frame(m_debugParams);
    apply_debug_params();
#endif

    // ── CSM: fit per-cascade sun light-space matrices to the view frustum ──
    computeCascades();
    m_cameraUniforms->set_cascades(m_cascadeVP, m_cascadeSplits);

    m_cameraUniforms->update(m_currentFrame, *m_camera);

#ifdef SWISH_DEBUG_UI
    // Push the live "look" tunables into set 3 for this frame (identity to the
    // shipped constants at defaults, so nothing changes until a slider moves).
    m_sceneParams.update(m_currentFrame, m_debugParams);
#endif

    if (m_rainSystem) {
        // Time-varying gusts: superimpose a slow swing and a faster gust on the
        // base horizontal wind so the streaks visibly slant and the slant DRIFTS
        // over time. Two decorrelated sinusoids (periods ≈ 2π/0.35 ≈ 18 s and
        // 2π/1.30 ≈ 4.8 s) give a wandering gust rather than a metronome. The
        // gust modulates a ±1 envelope around a steady mean of ~1.0 so the wind
        // never fully stalls. Vertical wind stays 0 (fall is handled by dropSpeed).
        m_windTime += deltaTime;
        float gust = 1.0f + 0.55f * std::sin(m_windTime * 0.35f) + 0.35f * std::sin(m_windTime * 1.30f + 1.7f);
        Vec3  gustWind(m_rainWind.x * gust, 0.0f, m_rainWind.z * gust);

        // Effective wind = gusting wind minus car velocity; rain leans forward at speed
        Vec3 effectiveWind = gustWind - m_carVelocity;
        m_rainSystem->update(m_currentFrame, deltaTime, m_rainIntensity, effectiveWind);
        m_cameraUniforms->set_wetness(m_currentFrame, m_rainSystem->get_wetness());
    }

    if (m_windshieldRainPass) {
        // Project car forward direction to screen space for the flow direction.
        // m_carVelocity = carForward * speed; normalize to get direction.
        float carSpeed = glm::length(m_carVelocity);
        Vec2  screenFwd(0.0f, -1.0f);  // default: straight up (driving forward)
        if (carSpeed > 0.001f && m_camera) {
            Vec3 fwdDir = m_carVelocity / carSpeed;
            Vec4 vsFwd  = m_camera->get_view_matrix() * Vec4(fwdDir, 0.0f);
            Vec2 raw(vsFwd.x, vsFwd.y);
            if (glm::length(raw) > 0.001f)
                screenFwd = glm::normalize(raw);
        }
        // Normalize over the car's actual top speed (kMaxSpeed, now 92000 WU/s)
        // so speedFactor still spans 0..1 across the full speed range.
        float speedFactor = glm::clamp(carSpeed / CarEntity::kMaxSpeed, 0.0f, 1.0f);
        float wetness     = m_rainSystem ? m_rainSystem->get_wetness() : 0.0f;
        float intensity   = m_rainSystem ? m_rainSystem->get_intensity() : 0.0f;

        m_windshieldRainPass->update(m_currentFrame, deltaTime, screenFwd, speedFactor, wetness, intensity,
                                     m_wiperEnabled);
    }

    recordCommandBuffer(m_currentFrame, *imageIndex);

    submitFrame(*imageIndex);
    handlePresent(presentFrame(*imageIndex));

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// command buffer recording

void Renderer::recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex) {
    m_commandManager->beginRecording(frameIndex);
    VkCommandBuffer cmd = m_commandManager->getBuffer(frameIndex);

    // SSAA: the whole offscreen chain runs at the render extent; only the
    // composite pass outputs at the swap extent (downsampling the HDR).
    VkExtent2D renderExtent = m_postProcess->get_render_extent();
    VkExtent2D swapExtent   = m_postProcess->get_swap_extent();
    VkExtent2D bloomExtent  = m_postProcess->get_bloom_extent();

    // Sun shadow map first — depth-only from the sun POV, sampled by the
    // lighting pass. Must precede the G-buffer/lighting passes.
    recordShadowPass(cmd, frameIndex);

    recordGBufferPass(cmd, frameIndex, renderExtent);
    transitionGBufferForLighting(cmd, frameIndex);

    recordLightingPass(cmd, frameIndex, renderExtent);

#ifdef SWISH_DEBUG_UI
    // SSAO + SSR here: the scene depth is still in DEPTH_STENCIL_READ_ONLY (set for
    // the lighting pass, unchanged by it) and the forward passes haven't reclaimed
    // it as a depth attachment yet. SSAO writes the AO the composite multiplies in;
    // SSR reads the lit HDR (reflecting the deferred scene) into the SSR image the
    // composite adds. Both must run before the forward rain/glass passes.
    recordSsaoPasses(cmd, frameIndex);
    recordSsrPass(cmd, frameIndex);
#endif

    // Rain forward pass — loads HDR, renders streaks additively, no barrier needed between
    recordRainPass(cmd, frameIndex);

    // Glass forward transparent pass — renders BLEND windows onto HDR with alpha blending
    recordGlassPass(cmd, frameIndex);

    // Step the persistent wetness map, then snapshot the post-glass HDR scene so
    // the windshield rain can refract it (drops act as lenses). Only needed when
    // the windshield is drawn; leaves HDR in COLOR_ATTACHMENT_OPTIMAL.
    if (m_windshieldRainPass && !m_windshieldDrawCalls.empty() && m_dynamicGeometry.has_geometry()) {
        m_windshieldRainPass->record_wetness_update(cmd);
        m_windshieldRainPass->record_scene_snapshot(cmd, frameIndex, m_postProcess->get_hdr_image(frameIndex),
                                                    renderExtent);
    }

    // Windshield rain — refractive water drops on the front windshield
    recordWindshieldRainPass(cmd, frameIndex);

    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_image(frameIndex),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomExtract(cmd, bloomExtent);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_extract_image(),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomBlur(cmd, bloomExtent, /*horizontal=*/true);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_h_image(),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordBloomBlur(cmd, bloomExtent, /*horizontal=*/false);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_bloom_blur_v_image(),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    recordCompositePass(cmd, frameIndex, imageIndex, swapExtent);

#ifdef SWISH_DEBUG_UI
    // Debug overlay last — own LOAD render pass over the swapchain image (composite
    // left it in PRESENT_SRC), so the panel sits on top of the final frame.
    m_debugUI.record(cmd, imageIndex, swapExtent);
#endif

    m_commandManager->endRecording(frameIndex);
}

// ── Pass 0: sun shadow map — CSM depth atlas (NUM_CASCADES slices side by side) ─
// One depth-only render pass over the whole atlas (cleared once). Each cascade is
// drawn into its horizontal sub-rect with its own light-space matrix; the scene
// geometry is drawn once per cascade (viewport-scoped).
void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    const VkExtent2D atlasExtent = m_postProcess->get_shadow_atlas_extent();
    const uint32_t   cascadeDim  = m_postProcess->get_shadow_cascade_dim();

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};  // far = 1.0 (LESS compare, [0,1] clip-Z)

    ScopedRenderPass pass(cmd, m_postProcess->get_shadow_render_pass(),
                          m_postProcess->get_shadow_framebuffer(frameIndex), atlasExtent, clear);

    float biasConst = DepthOnlyPipeline::kDefaultDepthBiasConst;
    float biasSlope = DepthOnlyPipeline::kDefaultDepthBiasSlope;
#ifdef SWISH_DEBUG_UI
    biasConst = m_debugParams.depthBiasConst;
    biasSlope = m_debugParams.depthBiasSlope;
#endif
    m_depthOnlyPipeline.bind(cmd, biasConst, biasSlope);  // pipeline + dynamic depth bias

    for (uint32_t c = 0; c < NUM_CASCADES; ++c) {
        VkRect2D subRect{{static_cast<int32_t>(c * cascadeDim), 0}, {cascadeDim, cascadeDim}};
        m_depthOnlyPipeline.set_cascade(cmd, subRect, m_cascadeVP[c]);
        m_sceneGeometry.record_depth(cmd, m_depthOnlyPipeline);
        m_dynamicGeometry.record_depth(cmd, m_depthOnlyPipeline);
    }
}

// ── CSM cascade fit — per-frame light-space matrices + split distances ───────
void Renderer::computeCascades() {
    const float camNear = m_camera->get_near();
    const float camFar  = m_camera->get_far();

    // Shadows only matter within a bounded range; cap the far cascade far short of
    // the ~2 km camera far so cascades pack resolution where shadows are visible.
    float shadowFar = 400000.0f;  // ~400 m
    float lambda    = 0.7f;       // practical-split blend (log vs uniform)
#ifdef SWISH_DEBUG_UI
    shadowFar = m_debugParams.csmShadowFar;
    lambda    = m_debugParams.csmLambda;
#endif
    shadowFar                = std::min(shadowFar, camFar);
    const float shadowNear   = camNear;

    // Practical split scheme (Zhang et al.): blend logarithmic and uniform splits.
    float splitFar[NUM_CASCADES];
    for (uint32_t i = 0; i < NUM_CASCADES; ++i) {
        float p    = static_cast<float>(i + 1) / static_cast<float>(NUM_CASCADES);
        float logS = shadowNear * std::pow(shadowFar / shadowNear, p);
        float uniS = shadowNear + (shadowFar - shadowNear) * p;
        splitFar[i] = lambda * logS + (1.0f - lambda) * uniS;
    }

    // Unproject the camera frustum's near/far corners to world (NDC z∈[0,1]).
    const Mat4 invVP = glm::inverse(m_camera->get_projection_matrix() * m_camera->get_view_matrix());
    Vec3       nearC[4], farC[4];
    const Vec2 ndc[4] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    for (int i = 0; i < 4; ++i) {
        Vec4 n = invVP * Vec4(ndc[i], 0.0f, 1.0f);
        Vec4 f = invVP * Vec4(ndc[i], 1.0f, 1.0f);
        nearC[i] = Vec3(n) / n.w;
        farC[i]  = Vec3(f) / f.w;
    }

    float prevFar = shadowNear;
    for (uint32_t c = 0; c < NUM_CASCADES; ++c) {
        const float tNear = (prevFar - camNear) / (camFar - camNear);
        const float tFar  = (splitFar[c] - camNear) / (camFar - camNear);

        // This cascade's 8 world corners (linear interp along each frustum edge).
        Vec3 corners[8];
        for (int i = 0; i < 4; ++i) {
            const Vec3 edge = farC[i] - nearC[i];
            corners[i]      = nearC[i] + edge * tNear;
            corners[i + 4]  = nearC[i] + edge * tFar;
        }

        Vec3 center(0.0f);
        for (const Vec3& p : corners)
            center += p;
        center /= 8.0f;

        // Bounding-sphere fit → a stable square ortho (less shimmer than an AABB).
        float radius = 0.0f;
        for (const Vec3& p : corners)
            radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius);

        const float margin    = radius;  // pull the near plane back to catch occluders behind the slice
        const Vec3  eye       = center - m_sunDir * (radius + margin);
        const Mat4  lightView = glm::lookAt(eye, center, Vec3(0.0f, 1.0f, 0.0f));
        const Mat4  lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + margin);

        m_cascadeVP[c]     = lightProj * lightView;
        m_cascadeSplits[c] = splitFar[c];
        prevFar            = splitFar[c];
    }
}

// ── Pass 1: scene → G-buffer ─────────────────────────────────────────
void Renderer::recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent) {
    std::array<VkClearValue, 4> clear{};
    clear[0].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // albedo
    clear[1].color        = {{0.5f, 0.5f, 1.0f, 0.0f}};  // normal (up = 0,0,1 encoded)
    clear[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // material
    clear[3].depthStencil = {1.0f, 0};

    ScopedRenderPass pass(cmd, m_postProcess->get_gbuffer_render_pass(),
                          m_postProcess->get_gbuffer_framebuffer(frameIndex), extent, clear.data(),
                          static_cast<uint32_t>(clear.size()));

    m_scenePipeline.bind(cmd, extent, m_cameraUniforms->get_set(frameIndex));

    m_sceneGeometry.record_draws(cmd, m_scenePipeline, *m_materialDescriptors);
    m_dynamicGeometry.record_draws(cmd, m_scenePipeline, *m_materialDescriptors);
}

// ── G-buffer attachments → SHADER_READ_ONLY for the lighting pass ───
void Renderer::transitionGBufferForLighting(VkCommandBuffer cmd, uint32_t frameIndex) {
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_albedo_image(frameIndex),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_normal_image(frameIndex),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_gbuffer_material_image(frameIndex),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_depth_image(frameIndex),
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// ── Pass 2: deferred lighting (G-buffer → HDR) ───────────────────────
void Renderer::recordLightingPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd, m_postProcess->get_lighting_render_pass(),
                          m_postProcess->get_lighting_framebuffer(frameIndex), extent, clear);

    Mat4 invView = glm::inverse(m_camera->get_view_matrix());
    Mat4 invProj = glm::inverse(m_camera->get_projection_matrix());

    m_deferredLighting.bind_and_record(cmd, m_cameraUniforms->get_set(frameIndex),
                                       m_postProcess->get_lighting_set(frameIndex),
                                       m_postProcess->get_shadow_set(frameIndex),
#ifdef SWISH_DEBUG_UI
                                       m_sceneParams.get_set(frameIndex),
#endif
                                       invView, invProj, extent);
}

// ── Rain forward pass — additively onto existing HDR ──────────────────
void Renderer::recordRainPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!m_rainSystem)
        return;
    // Bind camera set (set 0) using the rain pipeline layout before delegating.
    // RainSystem::record_draws binds its own set 1 (rain UBO) internally.
    VkDescriptorSet camSet = m_cameraUniforms->get_set(frameIndex);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rainSystem->get_pipeline_layout(), 0, 1, &camSet, 0,
                            nullptr);
    m_rainSystem->record_draws(cmd, frameIndex);
}

// ── Glass forward pass — alpha-blended windows onto HDR ───────────────
void Renderer::recordGlassPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!m_glassPass || m_glassDrawCalls.empty())
        return;
    if (!m_dynamicGeometry.has_geometry())
        return;

    VkDescriptorSet camSet = m_cameraUniforms->get_set(frameIndex);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_glassPass->get_pipeline_layout(), 0, 1, &camSet, 0,
                            nullptr);
    m_glassPass->record_draws(cmd, frameIndex, m_dynamicGeometry.get_vertex_buffer(),
                              m_dynamicGeometry.get_index_buffer(), m_glassDrawCalls);
}

// ── Windshield rain pass — additive rivulets on windshield geometry ───
void Renderer::recordWindshieldRainPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!m_windshieldRainPass || m_windshieldDrawCalls.empty())
        return;
    if (!m_dynamicGeometry.has_geometry())
        return;

    VkDescriptorSet camSet = m_cameraUniforms->get_set(frameIndex);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_windshieldRainPass->get_pipeline_layout(), 0, 1,
                            &camSet, 0, nullptr);
    m_windshieldRainPass->record_draws(cmd, frameIndex, m_dynamicGeometry.get_vertex_buffer(),
                                       m_dynamicGeometry.get_index_buffer(), m_windshieldDrawCalls);
}

// ── Pass 2a: bloom extract (HDR → 1/4 res bright pixels) ─────────────
void Renderer::recordBloomExtract(VkCommandBuffer cmd, VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd, m_postProcess->get_bloom_render_pass(), m_postProcess->get_bloom_extract_framebuffer(),
                          extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_bloom_extract_pipeline());
    setViewportAndScissor(cmd, extent);

    VkDescriptorSet bloomExtSet = m_postProcess->get_bloom_extract_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_postprocess_layout(), 0, 1,
                            &bloomExtSet, 0, nullptr);

    PostProcessParams pp{};
    pp.threshold       = 1.0f;
    pp.bloom_intensity = 0.3f;
    pp.exposure        = 1.0f;
#ifdef SWISH_DEBUG_UI
    pp.threshold       = m_debugParams.bloomThreshold;
    pp.bloom_intensity = m_debugParams.bloomIntensity;
#endif
    vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ── Pass 2b/2c: separable bloom blur (one direction per call) ────────
void Renderer::recordBloomBlur(VkCommandBuffer cmd, VkExtent2D extent, bool horizontal) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkFramebuffer fb =
        horizontal ? m_postProcess->get_bloom_blur_h_framebuffer() : m_postProcess->get_bloom_blur_v_framebuffer();

    ScopedRenderPass pass(cmd, m_postProcess->get_bloom_render_pass(), fb, extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_bloom_blur_pipeline());
    setViewportAndScissor(cmd, extent);

    VkDescriptorSet blurSet =
        horizontal ? m_postProcess->get_bloom_blur_h_set() : m_postProcess->get_bloom_blur_v_set();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_postprocess_layout(), 0, 1,
                            &blurSet, 0, nullptr);

    PostProcessParams pp{};
    pp.texel_x = horizontal ? 1.0f / static_cast<float>(extent.width) : 0.0f;
    pp.texel_y = horizontal ? 0.0f : 1.0f / static_cast<float>(extent.height);
    vkCmdPushConstants(cmd, m_postProcess->get_postprocess_layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

#ifdef SWISH_DEBUG_UI
// ── SSAO + bilateral AO blur (debug realism feature) ─────────────────
// Runs at 1/2 render resolution into the AO images the composite multiplies in.
// Depth is sampled in DEPTH_STENCIL_READ_ONLY (the layout the lighting pass left
// it in). Each pass ends in COLOR_ATTACHMENT → barrier to SHADER_READ, mirroring
// the bloom chain. When SSAO is toggled off the intensity is forced to 0, so the
// shader outputs 1.0 (no darkening) rather than leaving a stale AO in the image.
void Renderer::recordSsaoPasses(VkCommandBuffer cmd, uint32_t frameIndex) {
    const VkExtent2D aoExtent = m_postProcess->get_ao_extent();
    VkClearValue     clear{};
    clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};  // 1 = unoccluded (safe if a pixel is skipped)

    // SSAO: scene depth → raw AO.
    {
        ScopedRenderPass pass(cmd, m_postProcess->get_ao_render_pass(), m_postProcess->get_ao_framebuffer(), aoExtent,
                              clear);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_ssao_pipeline());
        setViewportAndScissor(cmd, aoExtent);

        VkDescriptorSet depthSet = m_postProcess->get_ao_set(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_ssao_layout(), 0, 1, &depthSet,
                                0, nullptr);

        const Mat4 proj = m_camera->get_projection_matrix();
        SsaoParams sp{};
        sp.projection    = proj;
        sp.invProjection = glm::inverse(proj);
        sp.radius        = m_debugParams.ssaoRadius;
        sp.bias          = m_debugParams.ssaoBias;
        sp.intensity     = m_debugParams.ssaoEnabled ? m_debugParams.ssaoIntensity : 0.0f;
        vkCmdPushConstants(cmd, m_postProcess->get_ssao_layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sp), &sp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_ao_image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Bilateral blur: raw AO → smoothed AO (sampled by composite).
    {
        ScopedRenderPass pass(cmd, m_postProcess->get_ao_render_pass(), m_postProcess->get_ao_blur_framebuffer(),
                              aoExtent, clear);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_ao_blur_pipeline());
        setViewportAndScissor(cmd, aoExtent);

        VkDescriptorSet aoSet = m_postProcess->get_ao_blur_set();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_postprocess_layout(), 0, 1,
                                &aoSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_ao_blur_image(),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// ── SSR (screen-space reflections, debug realism feature) ────────────
// Runs after lighting (HDR = the lit deferred scene, depth still readable). The
// lit HDR is briefly transitioned to SHADER_READ so the ray-march can sample it
// as the reflected colour, then restored to COLOR_ATTACHMENT for the forward
// passes; the SSR image is left SHADER_READ for the composite add.
void Renderer::recordSsrPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    const VkExtent2D ext = m_postProcess->get_render_extent();

    // Lit HDR → readable.
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_image(frameIndex),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    {
        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // miss → 0 (composite add is a no-op)
        ScopedRenderPass pass(cmd, m_postProcess->get_ssr_render_pass(), m_postProcess->get_ssr_framebuffer(), ext,
                              clear);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_ssr_pipeline());
        setViewportAndScissor(cmd, ext);

        VkDescriptorSet sets[2] = {m_postProcess->get_lighting_set(frameIndex),   // set 0: G-buffer
                                   m_postProcess->get_ssr_hdr_set(frameIndex)};   // set 1: lit HDR
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_ssr_layout(), 0, 2, sets, 0,
                                nullptr);

        const Mat4 proj = m_camera->get_projection_matrix();
        SsrParams  sp{};
        sp.proj      = proj;
        sp.invProj   = glm::inverse(proj);
        sp.maxDist   = m_debugParams.ssrMaxDist;
        sp.thickness = m_debugParams.ssrThickness;
        sp.stride    = m_debugParams.ssrStride;
        sp.intensity = m_debugParams.ssrEnabled ? m_debugParams.ssrIntensity : 0.0f;
        vkCmdPushConstants(cmd, m_postProcess->get_ssr_layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sp), &sp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    // Restore HDR for the forward passes; SSR image → readable for the composite.
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_hdr_image(frameIndex),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ResourceManager::insertImageBarrier(cmd, m_postProcess->get_ssr_image(),
                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
#endif

// ── Pass 3: composite (HDR + bloom → swapchain, ACES tonemap) ────────
void Renderer::recordCompositePass(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    ScopedRenderPass pass(cmd, m_postProcess->get_composite_render_pass(),
                          m_postProcess->get_composite_framebuffer(imageIndex), extent, clear);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_composite_pipeline());
    setViewportAndScissor(cmd, extent);

    VkDescriptorSet compSet = m_postProcess->get_composite_set(frameIndex);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postProcess->get_composite_layout(), 0, 1, &compSet,
                            0, nullptr);

    PostProcessParams pp{};
    pp.bloom_intensity = 0.3f;
    // Global exposure trim applied before AgX (composite.frag). The scene was
    // running hot / over-exposed; pulled well below 1.0 now that sun shadows
    // restore contrast. Tune to taste.
    pp.exposure        = 0.45f;
#ifdef SWISH_DEBUG_UI
    pp.bloom_intensity = m_debugParams.bloomIntensity;
    pp.exposure        = m_debugParams.exposure;
    pp.brightness      = m_debugParams.brightness;
    pp.contrast        = m_debugParams.contrast;
    pp.saturation      = m_debugParams.saturation;
    pp.temperature     = m_debugParams.temperature;
    pp.tint            = m_debugParams.tint;
#endif
    pp.rain_intensity  = m_rainSystem ? m_rainSystem->get_intensity() : 0.0f;
    pp.fog_density     = 0.04f;  // multiplied by rain_intensity in the shader
    vkCmdPushConstants(cmd, m_postProcess->get_composite_layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pp), &pp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// ══════════════════════════════════════════════════════════════════════
// recreateSwapchain()
// ══════════════════════════════════════════════════════════════════════

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    m_window->getFramebufferSize(&width, &height);
    while (width == 0 || height == 0) {
        m_window->waitEvents();
        m_window->getFramebufferSize(&width, &height);
    }

    vkDeviceWaitIdle(m_device->getDevice());

    m_swapchain->cleanup(m_device->getDevice());

    m_swapchain->init(*m_device, m_context->getSurface(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    m_syncObjects->recreateRenderFinishedSemaphores(m_device->getDevice(), m_swapchain->getImageCount());

    if (has_post_process()) {
        m_postProcess->recreate(m_swapchain->getExtent(), m_swapchain->getImageFormat(), m_swapchain->getImageViews());

        // SSAA: the offscreen chain (incl. the forward passes' framebuffers over
        // the HDR/depth views) is now sized at the recomputed render extent.
        const VkExtent2D renderExtent = m_postProcess->get_render_extent();

        m_deferredLighting.recreate(m_device->getDevice(), m_postProcess->get_lighting_render_pass(), renderExtent);

        if (m_rainSystem) {
            std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> hdrViews{}, depthViews{};
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                hdrViews[i]   = m_postProcess->get_hdr_view(i);
                depthViews[i] = m_postProcess->get_hdr_depth_view(i);
            }
            m_rainSystem->recreate(hdrViews, depthViews, renderExtent, m_device->getDevice());

            if (m_glassPass)
                m_glassPass->recreate(hdrViews, depthViews, renderExtent, m_device->getDevice());

            if (m_windshieldRainPass)
                m_windshieldRainPass->recreate(hdrViews, depthViews, renderExtent, m_device->getDevice());
        }
    }

#ifdef SWISH_DEBUG_UI
    m_debugUI.recreate(m_swapchain->getImageFormat(), m_swapchain->getImageViews(), m_swapchain->getExtent());
#endif
}

// ══════════════════════════════════════════════════════════════════════
// Scene lights + material descriptors — thin delegations to subsystems.
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_scene_lights(const std::vector<LightDesc>& lights) {
    m_cameraUniforms->set_lights(lights);
}

void Renderer::set_rain_intensity(float intensity) {
    m_rainIntensity = intensity;
}

void Renderer::set_car_velocity(Vec3 v) {
    m_carVelocity = v;
}

void Renderer::set_wiper_enabled(bool enabled) {
    m_wiperEnabled = enabled;
}

void Renderer::set_clear_day(bool clear) {
    m_clearDay = clear;
    if (!m_cameraUniforms)
        return;
    if (clear) {
        // A clear day is dry — kill any active rain so wetness decays to 0.
        m_rainIntensity = 0.0f;
        // Bright midday sun: higher + more frontal, near-white. Ambient kept LOW
        // (0.24) — the enclosed cabin gets no direct sun, so a high ambient fill is
        // exactly what over-exposed it; shadows + the sun disc do the lifting now.
        // clarity = 1 deepens the sky gradient + sharpens the sun disc.
        m_sunDir = glm::normalize(Vec3(0.25f, 0.85f, 0.20f));
        m_cameraUniforms->set_weather(Vec4(m_sunDir, 1.0f), Vec4(1.00f, 0.98f, 0.92f, 0.24f), 1.0f);
    } else {
        // Original overcast preset (matches the pre-existing hardcoded sun).
        m_sunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
        m_cameraUniforms->set_weather(Vec4(m_sunDir, 1.0f), Vec4(1.0f, 0.95f, 0.85f, 0.22f), 0.0f);
    }
}

#ifdef SWISH_DEBUG_UI
void Renderer::debug_init() {
    DebugUIInitInfo info{};
    info.instance            = m_context->getInstance();
    info.physicalDevice      = m_device->getPhysicalDevice();
    info.device              = m_device->getDevice();
    info.graphicsQueue       = m_device->getGraphicsQueue();
    info.graphicsQueueFamily = m_device->getQueueFamilies().graphicsFamily.value();
    info.window              = m_window->getHandle();
    info.swapchainFormat     = m_swapchain->getImageFormat();
    info.swapchainViews      = m_swapchain->getImageViews();
    info.extent              = m_swapchain->getExtent();
    info.imageCount          = m_swapchain->getImageCount();
    info.minImageCount       = m_swapchain->getImageCount();
    m_debugUI.init(info);
}

void Renderer::set_debug_edit_mode(bool edit) {
    m_debugParams.editMode = edit;
}

void Renderer::apply_debug_params() {
    // Rain intensity slider drives the same scalar as the R-key cycle.
    m_rainIntensity = m_debugParams.rainIntensity;
    // Sun colour/ambient/clarity feed the CameraUBO (written by update() right after).
    // Direction stays m_sunDir for now (azimuth/elevation wiring is a later phase).
    m_cameraUniforms->set_weather(Vec4(m_sunDir, 1.0f),
                                  Vec4(m_debugParams.sunColor, m_debugParams.sunAmbient),
                                  m_debugParams.clarity);
    // Rain streak length (base, before intensity scaling).
    if (m_rainSystem)
        m_rainSystem->set_streak_len(m_debugParams.streakLen);
    // NOTE: ssaaApplyRequested is consumed at the top of drawFrame (recreate must
    // not run mid-frame), not here — shadow depth-bias is fed in recordShadowPass.
}
#endif

void Renderer::update_glass_draw_calls(const std::vector<DrawCall>& glassDCs) {
    m_glassDrawCalls = glassDCs;
}

void Renderer::update_windshield_draw_calls(const std::vector<DrawCall>& windshieldDCs) {
    m_windshieldDrawCalls = windshieldDCs;
}

void Renderer::rebuild_material_descriptors() {
    m_materialDescriptors->rebuild(m_device->getDevice(), *m_textureManager);
}

// ══════════════════════════════════════════════════════════════════════
// Scene geometry — thin delegations to SceneGeometry. Called by Scene
// lambdas after mesh generation / on scene unload.
// ══════════════════════════════════════════════════════════════════════

void Renderer::upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_sceneGeometry.upload(services(), mesh, draws);
}

void Renderer::destroy_scene_geometry() {
    m_sceneGeometry.cleanup(m_device->getDevice());
    m_cameraUniforms->set_lights({});
}

void Renderer::upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws) {
    m_dynamicGeometry.upload(services(), mesh, draws);
}

void Renderer::update_dynamic_draw_calls(const std::vector<DrawCall>& draws) {
    m_dynamicGeometry.update_draw_calls(draws);
}

void Renderer::destroy_dynamic_geometry() {
    m_dynamicGeometry.cleanup(m_device->getDevice());
}

}  // namespace swish
