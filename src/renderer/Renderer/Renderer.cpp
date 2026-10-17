// Renderer — construction, init/cleanup, manager registry, swapchain recreation.
// The rest of the class lives in: Renderer_Frame.cpp (drawFrame), Renderer_Record.cpp
// (recordCommandBuffer: begin, the pass registry loop, end) and Renderer_State.cpp (setters, debug-param
// apply, scene-geometry delegations). The passes themselves are in renderer/Passes/ (PassList.def is the frame
// order); weather + IBL rebake are in WeatherController.
#include "Renderer.h"

#include "core/Window/Window.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/CommandManager/CommandManager.h"
#include "renderer/IBLManager/IBLManager.h"
#include "renderer/MaterialDescriptors/MaterialDescriptors.h"
#include "renderer/Passes/PassRegistry.h"
#ifdef SWISH_DEBUG_UI
#include "renderer/Passes/PassRebuild.h"
#endif
#include "renderer/Pipeline/Device/Device.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/RendererConfig.h"
#include "renderer/Swapchain/Swapchain.h"
#include "renderer/SyncObjects/SyncObjects.h"
#include "renderer/VulkanContext/VulkanContext.h"
#include "scene/Camera/Camera.h"
#include "utils/types/Types.h"

#include <array>
#include <cstdio>

namespace swish {

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
      m_materialDescriptors(std::make_unique<MaterialDescriptors>()) {
    m_weather.bind_camera_uniforms(m_cameraUniforms.get());
#ifdef SWISH_DEBUG_UI
    m_weather.bind_debug_params(&m_debugParams);
#endif
}

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
                        m_device->getGraphicsQueue(), m_device->getAllocator(), m_device->getPipelineCache(),
                        m_swapchain->getExtent(), m_swapchain->getImageFormat(), m_swapchain->getImageViews());

    m_postProcess->register_images(m_images);  // named images for the pass registry (re-done on every recreate)

    // Baked-sky prefiltered-cubemap IBL (set 3). Built before the registry passes: the lighting pipeline layout
    // references its descriptor-set layout.
    m_ibl = std::make_unique<IBLManager>();
    m_ibl->init(services());
    m_weather.bind_ibl(m_ibl.get());

#ifdef SWISH_DEBUG_UI
    // Live-tunables UBO (set 4) — must exist before the lighting pipeline layout (LightingPass::init) is built.
    m_sceneParams.init(m_device->getDevice(), m_device->getAllocator(), MAX_FRAMES_IN_FLIGHT);
#endif

    // Registry passes: every Renderer-owned subsystem they read now exists.
    m_passRes.pp             = m_postProcess.get();
    m_passRes.cameraUniforms = m_cameraUniforms.get();
    m_passRes.ibl            = m_ibl.get();
    m_passRes.materials      = m_materialDescriptors.get();
    m_passRes.geometry       = &m_geometry;
#ifdef SWISH_DEBUG_UI
    m_passRes.sceneParams = &m_sceneParams;
    m_passRes.debugUI     = &m_debugUI;
#endif
    m_passes = make_pass_registry(&m_geometry);
    init_passes(false);

#ifdef SWISH_DEBUG_UI
    // S4: validate the passes' declared image reads/writes once (list order, read-before-write) and keep the result
    // for the debug UI. Informational only: never changes the pass order or recording.
    m_passDecls    = collect_pass_declarations(&m_geometry);
    m_declWarnings = validate_pass_declarations(m_passDecls);
    for (const std::string& w : m_declWarnings) std::fprintf(stderr, "[PassDeclaration] WARNING: %s\n", w.c_str());
    // S5: shader hot-reload hooks. Only Composite rebuilds live today; other shaders log TODO(stub) on change.
    m_shaderReload.register_rebuild("composite", [this] { rebuild_composite_pass(*m_postProcess); });
    // S2/S3: GPU timestamp profiler (observes record_all) and frame capture (staging buffers via VMA).
    m_gpuProfiler.init(services(), m_device->getQueueFamilies().graphicsFamily.value(), m_passes);
    m_passes.set_observer(&m_gpuProfiler);
    m_frameCapture.init(m_device->getAllocator());
    // S6: feed the "Render tools" debug panel (borrowed read-only views + two actions).
    {
        RenderToolsView rt;
        rt.gpu                 = &m_gpuProfiler.snapshot();
        rt.capture             = &m_frameCapture.status();
        rt.hotReload           = &m_shaderReload;
        rt.decls               = &m_passDecls;
        rt.declWarnings        = &m_declWarnings;
        rt.requestCapture      = [this](bool hdr) { m_frameCapture.request(hdr); };
        rt.setHotReloadEnabled = [this](bool e) { m_shaderReload.set_enabled(e); };
        m_debugUI.set_render_tools(std::move(rt));
    }
#endif

    // Initial weather HDRI bake (default partly cloudy). Falls back to procedural
    // sky via WeatherController::maybe_rebake_ibl if the HDRI file is missing.
    m_weather.set_preset(WeatherPreset::PartlyCloudy);
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());

#ifdef SWISH_DEBUG_UI
    m_debugUI.cleanup();                           // ImGui shutdown before device/instance teardown
    m_sceneParams.cleanup(m_device->getDevice());  // set 4 UBO + pool + layout
    m_passes.set_observer(nullptr);
    m_gpuProfiler.cleanup();   // query pool
    m_frameCapture.cleanup();  // staging buffers: before the VMA allocator goes away
#endif

    m_camera.reset();

    destroy_scene_geometry();
    destroy_dynamic_geometry();

    m_passes.cleanup_all(m_device->getDevice());

    if (m_ibl) {
        m_ibl->cleanup();
        m_ibl.reset();
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
    m_swapchain->cleanup(m_device->getDevice());
    m_device->cleanup();
    m_context->cleanup();
}

// ══════════════════════════════════════════════════════════════════════
// Manager registration + getters
// ══════════════════════════════════════════════════════════════════════

void Renderer::register_texture_manager(TextureManager* mgr) { m_textureManager = mgr; }

TextureManager* Renderer::get_texture_manager() const { return m_textureManager; }

// ══════════════════════════════════════════════════════════════════════
// Vulkan handle getters (for managers that need raw handles)
// ══════════════════════════════════════════════════════════════════════

RendererServices Renderer::services() const {
    return RendererServices{
        m_device->getDevice(),
        m_device->getPhysicalDevice(),
        m_commandManager->getPool(),
        m_device->getGraphicsQueue(),
        m_swapchain->getExtent(),
        m_device->getAllocator(),
        m_device->supportsDynamicRendering(),
        m_device->supportsSynchronization2(),
        m_device->getPipelineCache(),
    };
}

// ══════════════════════════════════════════════════════════════════════
// Camera
// ══════════════════════════════════════════════════════════════════════

void Renderer::set_camera(Camera* camera) { m_camera.reset(camera); }

Camera* Renderer::get_camera() const { return m_camera.get(); }

// ══════════════════════════════════════════════════════════════════════
// GPU synchronization
// ══════════════════════════════════════════════════════════════════════

void Renderer::wait_for_idle() { vkDeviceWaitIdle(m_device->getDevice()); }

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
    // Any recreate (present OOD, acquire OOD, SSAA apply) satisfies a pending resize;
    // this is the only place the flag is cleared.
    m_window->resetResizedFlag();

    m_syncObjects->recreateRenderFinishedSemaphores(m_device->getDevice(), m_swapchain->getImageCount());

    if (has_post_process()) {
        m_postProcess->recreate(m_swapchain->getExtent(), m_swapchain->getImageFormat(), m_swapchain->getImageViews());
        m_postProcess->register_images(m_images);

        // SSAA: the offscreen chain (incl. the forward passes' framebuffers over the HDR/depth views) is now sized
        // at the recomputed render extent; every registry pass rebuilds what depends on it (init_passes(true)).
        init_passes(true);
    }

#ifdef SWISH_DEBUG_UI
    m_debugUI.recreate(m_swapchain->getImageFormat(), m_swapchain->getImageViews(), m_swapchain->getExtent());
#endif
}

// (Re)build the PassInitCtx and init (first time) or resize (after a swapchain / SSAA recreate) every registry pass.
void Renderer::init_passes(bool resize) {
    const RendererServices svc = services();
    const PassInitCtx      ctx{
        svc, m_images, m_passRes, m_passes, m_postProcess->get_render_extent(), m_cameraUniforms->get_layout()};
    if (resize)
        m_passes.resize_all(ctx);
    else
        m_passes.init_all(ctx);
}

FrameUpdate Renderer::make_frame_update(float dt) {
    return FrameUpdate{m_currentFrame,  dt,        m_camera.get(), tune(),
                       m_frameState,    m_passRes, m_carVelocity,  m_carPosition,
                       m_rainIntensity, m_wiper};
}

const DebugParams& Renderer::tune() const {
#ifdef SWISH_DEBUG_UI
    return m_debugParams;
#else
    return m_defaultTune;
#endif
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
#endif

}  // namespace swish
