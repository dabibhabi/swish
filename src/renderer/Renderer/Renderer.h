#pragma once
// scene headers
#include "../../scene/SceneTypes.h"
#include "../DeferredLightingPipeline/DeferredLightingPipeline.h"
#include "../DepthOnlyPipeline/DepthOnlyPipeline.h"
#include "../SceneGeometry/SceneGeometry.h"
#include "../ScenePipeline/ScenePipeline.h"
#include "RendererServices.h"

// DebugParams is a Vulkan/ImGui-free struct; include it unconditionally so the
// release god-rays path can read its defaults (the shipped look). The rest of the
// debug UI (ImGui / gizmos / set-3 UBO) stays compiled only under SWISH_DEBUG_UI.
#include "../../debug/DebugParams.h"
#ifdef SWISH_DEBUG_UI
#include "../../debug/DebugUI.h"
#include "../../debug/SceneParamsUniform.h"
#endif
// vulkan headers
#include <vulkan/vulkan.h>
// stl headers
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace swish {

// Forward declarations
class Window;
class VulkanContext;
class Device;
class Swapchain;
class CommandManager;
class SyncObjects;
class Camera;
class TextureManager;
class SceneManager;
class ModelManager;
class PostProcessManager;
class CameraUniforms;
class MaterialDescriptors;
class RainSystem;
class SpraySystem;
class GlassPass;
class WindshieldRainPass;
class IBLManager;
class TaaPass;

// The Renderer orchestrates the Vulkan draw loop and acts as a
// central registry for managers (rind-style architecture).
//
// Architecture:
//   App
//    ├── Window
//    ├── Renderer (Vulkan core + manager registry)
//    │    ├── VulkanContext, Device, Swapchain, CommandManager, SyncObjects
//    │    ├── CameraUniforms              (set 0 — camera + lights UBOs)
//    │    ├── MaterialDescriptors         (set 1 — PBR textures)
//    │    ├── PostProcessManager          (G-buffer + HDR + bloom + composite)
//    │    ├── ScenePipeline               (deferred G-buffer pipeline)
//    │    ├── DeferredLightingPipeline    (lighting pipeline + layout)
//    │    ├── SceneGeometry               (vertex/index buffers, draw calls)
//    │    └── Camera*                     (set by scene)
//    ├── TextureManager (owns all textures, ctor takes RendererServices)
//    ├── SceneManager   (owns scenes, handles switching)
//    └── ModelManager   (placeholder for cars)
//
class Renderer {
public:
    Renderer();
    ~Renderer();

    // ── Core lifecycle ────────────────────────────────────────────────
    void init(Window& window);
    void cleanup();
    void drawFrame(float deltaTime);

    // ── Manager registration (called by App after init) ───────────────
    void register_texture_manager(TextureManager* mgr);
    void register_scene_manager(SceneManager* mgr);
    void register_model_manager(ModelManager* mgr);

    // ── Manager getters ───────────────────────────────────────────────
    TextureManager* get_texture_manager() const;
    SceneManager*   get_scene_manager() const;
    ModelManager*   get_model_manager() const;

    // Bundles the raw Vulkan handles that subsystems need at init / upload
    // time. Subsystems copy what they need; nothing should retain the bundle
    // long-term (the swapchain extent in particular is invalidated on every
    // recreate). See RendererServices.h.
    RendererServices services() const;

    // ── Scene geometry (called by Scene lambdas) ──────────────────────
    void upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void destroy_scene_geometry();

    // ── Dynamic geometry (moving objects — uploaded once, updated per frame) ──
    void upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void update_dynamic_draw_calls(const std::vector<DrawCall>& draws);
    void destroy_dynamic_geometry();

    // ── Material descriptors (called after TextureManager loads) ──────
    void rebuild_material_descriptors();

    // ── Camera (owned by Renderer, set by Scene) ──────────────────────
    void    set_camera(Camera* camera);
    Camera* get_camera() const;

    // ── Scene lights (called by Scene lambdas) ─────────────────────────
    void set_scene_lights(const std::vector<LightDesc>& lights);

    // ── Rain control (called by App; R key cycles intensity) ──────────
    void set_rain_intensity(float intensity);  // [0,1]
    void set_car_velocity(Vec3 velocity);      // WU/s; drives rain streak lean at speed
    void set_car_position(Vec3 position);      // WU; rear-axle spray spawn origin
    void set_wiper_enabled(bool enabled);      // V key toggles the windshield wiper

    // ── Weather preset (called by App; G key toggles) ─────────────────
    // Clear day = bright sunny preset (deep-blue sky, high white sun) and dry:
    // it forces rain off. Overcast restores the original rainy-capable look.
    void set_clear_day(bool clear);

#ifdef SWISH_DEBUG_UI
    // ── Live debug/tuning UI (make debug) ─────────────────────────────
    // Call AFTER App installs its GLFW callbacks (ImGui chains onto them).
    void debug_init();
    void set_debug_edit_mode(bool edit);
    // Live debug params — App writes the steering-wheel pivot + reads the steer
    // override (the gizmo lives in the ImGui frame, which the Renderer drives).
    DebugParams& debug_params() { return m_debugParams; }
#endif

    // ── Glass + windshield rain (updated each frame by App) ───────────
    // Replaces the stored glass draw call list (call after update_dynamic_draw_calls).
    void update_glass_draw_calls(const std::vector<DrawCall>& glassDCs);
    void update_windshield_draw_calls(const std::vector<DrawCall>& windshieldDCs);

    // ── GPU synchronization ───────────────────────────────────────────
    void wait_for_idle();

private:
    // ── State predicates (named replacements for raw null/handle checks) ──
    bool has_post_process() const { return m_postProcess != nullptr; }
    // VkResult predicates moved to utils/VulkanInit.h (swish::vk namespace).

    // ── Owned subsystems ───────────────────────────────────────────
    std::unique_ptr<VulkanContext>  m_context;
    std::unique_ptr<Device>         m_device;
    std::unique_ptr<Swapchain>      m_swapchain;
    std::unique_ptr<CommandManager> m_commandManager;
    std::unique_ptr<SyncObjects>    m_syncObjects;

    std::unique_ptr<PostProcessManager>  m_postProcess;
    std::unique_ptr<CameraUniforms>      m_cameraUniforms;
    std::unique_ptr<MaterialDescriptors> m_materialDescriptors;
    std::unique_ptr<RainSystem>          m_rainSystem;
    std::unique_ptr<SpraySystem>         m_spraySystem;  // GPU compute particle road-spray
    std::unique_ptr<GlassPass>           m_glassPass;
    std::unique_ptr<WindshieldRainPass>  m_windshieldRainPass;
    std::unique_ptr<IBLManager>          m_ibl;  // baked-sky prefiltered-cubemap IBL (set 3)

    // ── Manager pointers (NOT owned — App owns these) ─────────────
    TextureManager* m_textureManager = nullptr;
    SceneManager*   m_sceneManager   = nullptr;
    ModelManager*   m_modelManager   = nullptr;

    // ── Pipelines ─────────────────────────────────────────────────
    ScenePipeline            m_scenePipeline;
    DeferredLightingPipeline m_deferredLighting;
    DepthOnlyPipeline        m_depthOnlyPipeline;  // sun shadow-map depth pass

    // ── Scene geometry ────────────────────────────────────────────
    SceneGeometry m_sceneGeometry;
    SceneGeometry m_dynamicGeometry;

    // ── Camera ────────────────────────────────────────────────────
    std::unique_ptr<Camera> m_camera;

    // ── Rain state ────────────────────────────────────────────────
    float m_rainIntensity = 0.0f;
    Vec3  m_rainWind      = Vec3(4500.0f, 0.0f, 1200.0f);  // WU/s base gale (≈4.5 m/s × drift)
    Vec3  m_carVelocity   = Vec3(0.0f, 0.0f, 0.0f);        // set by App each frame
    Vec3  m_carPosition   = Vec3(0.0f, 0.0f, 0.0f);        // set by App each frame (spray spawn)
    bool  m_wiperEnabled  = false;                         // V key toggles the wiper
    float m_windTime      = 0.0f;                          // accumulated time for gust oscillation
    bool  m_clearDay      = false;                         // G key: bright clear-day preset (dry)

    // ── Sun direction (drives the shadow-map light-space matrix) ──
    // Kept in sync with the Vec3 passed to set_weather in set_clear_day.
    Vec3 m_sunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
    // Per-cascade sun light-space view*proj + split far-distances (view space) for
    // the current frame (computed in drawFrame, consumed by recordShadowPass +
    // written into the camera UBO for CSM lookup).
    std::array<Mat4, NUM_CASCADES> m_cascadeVP{};
    Vec3                           m_cascadeSplits{0.0f};

    // ── IBL bake state (weather the cubemaps reflect) ─────────────
    // Kept in sync with set_weather (both builds) so the baked-sky IBL can be
    // re-baked when the sky changes. Baked-state is a dirty-check cache so
    // maybeRebakeIBL() only re-bakes on an actual change (no per-frame thrash).
    float m_clarity       = 0.0f;
    Vec3  m_sunColor      = Vec3(1.0f, 0.95f, 0.85f);
    Vec3  m_bakedSunDir   = Vec3(1e9f);  // sentinel → first call always bakes
    float m_bakedClarity  = -1.0f;
    Vec3  m_bakedSunColor = Vec3(-1.0f);

#ifdef SWISH_DEBUG_UI
    // Live debug/tuning UI + its editable parameters (make debug only).
    DebugUI     m_debugUI;
    DebugParams m_debugParams;
    // Set 3 on the deferred-lighting pipeline: the live-tunable "look" constants
    // (sky/fog/reflection/shadow/wet) promoted out of lighting.frag.
    SceneParamsUniform m_sceneParams;

    // Auto-exposure state: smoothed scene luminance + the exposure it yields.
    float m_aeAdaptedLum = 0.5f;
    float m_aeExposure   = 0.45f;

    // TAA (debug-only): resolve pass + reprojection state. SSAA remains the release
    // default; TAA is an alternative the debug UI toggles. m_prevViewProjUnjit is the
    // previous frame's un-jittered world→clip for motion-vector reprojection.
    std::unique_ptr<TaaPass> m_taa;
    Mat4                     m_prevViewProjUnjit = Mat4(1.0f);
    uint32_t                 m_taaFrameCounter   = 0;
#endif

    // ── Glass + windshield state ──────────────────────────────────
    std::vector<DrawCall> m_glassDrawCalls;
    std::vector<DrawCall> m_windshieldDrawCalls;

    // ── Frame tracking ────────────────────────────────────────────
    uint32_t m_currentFrame = 0;

    // ── Pointer back to window ────────────────────────────────────
    Window* m_window = nullptr;

    // ── Private helpers ───────────────────────────────────────────
    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);

    // Per-pass command recording (called by recordCommandBuffer)
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex);  // CSM: reads m_cascadeVP
    // Fit the per-cascade light-space matrices + split distances to the camera
    // frustum for the current frame. Fills m_cascadeVP / m_cascadeSplits.
    void computeCascades();
    void recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void transitionGBufferForLighting(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordLightingPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void recordRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    // GPU road-spray: binds camera set 0, then the SpraySystem draws its additive
    // particle billboards. The compute sim is dispatched separately (outside any
    // render pass) via SpraySystem::record_compute in recordCommandBuffer.
    void recordSprayPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordGlassPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordWindshieldRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordBloomExtract(VkCommandBuffer cmd, VkExtent2D extent);
    void recordBloomBlur(VkCommandBuffer cmd, VkExtent2D extent, bool horizontal);
    // God-rays (screen-space light shafts): sun-anchored radial blur of the lit HDR,
    // added at composite. Ships in release (un-gated) — reads the DebugParams defaults
    // when SWISH_DEBUG_UI is off, the live params when on. Recorded in the SSR slot.
    void recordGodRaysPass(VkCommandBuffer cmd, uint32_t frameIndex);
#ifdef SWISH_DEBUG_UI
    // SSAO (depth → AO) + bilateral blur, recorded between lighting and the forward
    // passes (depth is in DEPTH_STENCIL_READ_ONLY there). Debug-only; release keeps
    // the primed-white AO image so the composite `hdr *= ao` is a no-op.
    void recordSsaoPasses(VkCommandBuffer cmd, uint32_t frameIndex);
    // SSR: barrier the lit HDR to readable, ray-march reflections into the SSR
    // image, restore HDR for the forward passes. Debug-only; release primes the
    // SSR image black so the composite add is a no-op.
    void recordSsrPass(VkCommandBuffer cmd, uint32_t frameIndex);
    // Auto-exposure: blit the lit HDR down a mip chain to 1×1 (average) and copy
    // that pixel to a host buffer; leaves the HDR in SHADER_READ for bloom.
    void recordLuminancePyramid(VkCommandBuffer cmd, uint32_t frameIndex);
    // Read the previous frame's average luminance, smooth it (eye adaptation), and
    // set m_aeExposure. Call at drawFrame start.
    void updateAutoExposure(float dt);
#endif
    void recordCompositePass(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);

    // Re-bake the baked-sky IBL cubemaps if the weather (sun/clarity/colour)
    // changed since the last bake. Cheap dirty-check; the bake itself stalls the
    // queue, so it only fires on an actual change. Also does the initial bake.
    void maybeRebakeIBL();

    void recreateSwapchain();

#ifdef SWISH_DEBUG_UI
    // Push the current DebugParams into the live uniforms each frame.
    void apply_debug_params();
#endif
};

}  // namespace swish
