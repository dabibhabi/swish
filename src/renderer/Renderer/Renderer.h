#pragma once

#include "../../scene/SceneTypes.h"
#include "../DeferredLightingPipeline/DeferredLightingPipeline.h"
#include "../SceneGeometry/SceneGeometry.h"
#include "../ScenePipeline/ScenePipeline.h"
#include "RendererServices.h"

#include <vulkan/vulkan.h>

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
class GlassPass;
class WindshieldRainPass;

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
    void set_wiper_enabled(bool enabled);      // V key toggles the windshield wiper

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
    std::unique_ptr<GlassPass>           m_glassPass;
    std::unique_ptr<WindshieldRainPass>  m_windshieldRainPass;

    // ── Manager pointers (NOT owned — App owns these) ─────────────
    TextureManager* m_textureManager = nullptr;
    SceneManager*   m_sceneManager   = nullptr;
    ModelManager*   m_modelManager   = nullptr;

    // ── Pipelines ─────────────────────────────────────────────────
    ScenePipeline            m_scenePipeline;
    DeferredLightingPipeline m_deferredLighting;

    // ── Scene geometry ────────────────────────────────────────────
    SceneGeometry m_sceneGeometry;
    SceneGeometry m_dynamicGeometry;

    // ── Camera ────────────────────────────────────────────────────
    std::unique_ptr<Camera> m_camera;

    // ── Rain state ────────────────────────────────────────────────
    float m_rainIntensity = 0.0f;
    Vec3  m_rainWind      = Vec3(1500.0f, 0.0f, 400.0f);  // WU/s (≈1.5 m/s × drift)
    Vec3  m_carVelocity   = Vec3(0.0f, 0.0f, 0.0f);       // set by App each frame
    bool  m_wiperEnabled  = false;                        // V key toggles the wiper

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
    void recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void transitionGBufferForLighting(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordLightingPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void recordRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordGlassPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordWindshieldRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordBloomExtract(VkCommandBuffer cmd, VkExtent2D extent);
    void recordBloomBlur(VkCommandBuffer cmd, VkExtent2D extent, bool horizontal);
    void recordCompositePass(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);

    void recreateSwapchain();
};

}  // namespace swish
