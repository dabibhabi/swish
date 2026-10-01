#pragma once
// scene headers
#include "RendererServices.h"
#include "debug/DebugParams.h"
#include "renderer/GeometryProducer/GeometryProducer.h"
#include "renderer/Passes/ImageRegistry.h"
#include "renderer/Passes/PassRegistry.h"
#include "renderer/WeatherController/WeatherController.h"
#include "scene/SceneTypes.h"
#include "scene/WeatherPreset.h"
#ifdef SWISH_DEBUG_UI
#include "debug/DebugUI.h"
#include "debug/SceneParamsUniform.h"
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
//  {#5c4,20} Forward Declerations
class Window;
class VulkanContext;
class Device;
class Swapchain;
class CommandManager;
class SyncObjects;
class Camera;
class TextureManager;
class PostProcessManager;
class CameraUniforms;
class MaterialDescriptors;
class IBLManager;

// The Renderer owns the Vulkan core and the shared subsystems, and drives the frame through the pass registry
// (renderer/Passes/: the explicit ordered pass list in PassList.def is the command-buffer recording order).
//
// Architecture:
//   App
//    ├── Window
//    ├── Renderer (Vulkan core + subsystem owner + frame driver)
//    │    ├── VulkanContext, Device, Swapchain, CommandManager, SyncObjects
//    │    ├── CameraUniforms              (set 0 — camera + lights UBOs)
//    │    ├── MaterialDescriptors         (set 1 — PBR textures)
//    │    ├── PostProcessManager          (offscreen images, render passes, fullscreen pipelines, descriptor sets)
//    │    ├── IBLManager                  (set 3) + WeatherController (sun/clarity/HDRI look, IBL rebake)
//    │    ├── GeometryProducer            (scene geometry; shadow atlas + G-buffer; the registry's first entry)
//    │    ├── PassRegistry                (Lighting, SSAO, SSR, god-rays, DoF, spray, rain, glass, windshield,
//    │    │                                TAA, HdrFinalize, bloom, composite, overlay: see PassList.def)
//    │    ├── ImageRegistry + FrameState  (named images and published CPU values the passes exchange)
//    │    └── Camera*                     (set by scene)
//    ├── TextureManager (owns all textures, ctor takes RendererServices)
//    ├── SceneManager   (owns scenes, handles switching)
//    └── ModelManager   (placeholder for cars)
//
class Renderer {
public:
    Renderer();
    ~Renderer();

    void init(Window& window);
    void cleanup();
    void drawFrame(float deltaTime);

    void register_texture_manager(TextureManager* mgr);

    TextureManager* get_texture_manager() const;

    RendererServices services() const;

    void upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void destroy_scene_geometry();

    void upload_chunk_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_road_chunks(float introOffsetZ, const std::vector<float>& slotOffsetsZ);

    void upload_interchange_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_interchanges(const std::vector<float>& slotOffsetsZ);

    // ── Real LIE corridor (Phase 3a) ──────────────────────────────────
    // Unlike the retired treadmill's ONE canonical chunk instanced at many
    // offsets, the corridor is a real road: every segment is a distinct mesh.
    // Each gets its own vertex/index buffer and its own per-frame render
    // offset (CorridorInstance), which is what keeps segment-local vertex
    // coordinates inside float32's precise range on an 18 mi road. Culling,
    // materials and the camera-relative rebase are unchanged — they come free
    // from SceneGeometry::record_draws.
    void add_corridor_segment(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_corridor_instances(const std::vector<CorridorInstance>& instances);

    void upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void update_dynamic_draw_calls(const std::vector<DrawCall>& draws);
    void destroy_dynamic_geometry();

    void rebuild_material_descriptors();

    void    set_camera(Camera* camera);
    Camera* get_camera() const;

    void set_scene_lights(const std::vector<LightDesc>& lights);

    void set_rain_intensity(float intensity);
    // RainSystem's INTEGRATED wetness [0,1] (asymmetric wet/dry ramp — NOT
    // raw rain intensity). 0 if the rain system hasn't been created yet.
    // Wired to CarEntity::set_wetness() so the wet-grip physics model
    // (CarPhysics.h::effective_mu) uses the same wetness the renderer's
    // wet-road/puddle shading uses.
    float get_wetness() const;
    void  set_car_velocity(Vec3 velocity);
    void  set_car_position(Vec3 position);
    void  set_wiper_enabled(bool enabled);

    void set_clear_day(bool clear);

    // HDRI + sun/clarity look for driving weather. Rebakes IBL from assets/Sky/.
    void          set_weather_preset(WeatherPreset preset);
    WeatherPreset weather_preset() const { return m_weather.preset(); }

#ifdef SWISH_DEBUG_UI

    void debug_init();
    void set_debug_edit_mode(bool edit);

    DebugParams& debug_params() { return m_debugParams; }
#endif

    void update_glass_draw_calls(const std::vector<DrawCall>& glassDCs);
    void update_windshield_draw_calls(const std::vector<DrawCall>& windshieldDCs);

#ifdef SWISH_DEBUG_UI
    // Per-object velocity buffer (todo.md): CarEntity::get_prev_draw_models(), paired
    // index-for-index with the dynamic geometry's current draw calls by VelocityPass.
    void update_dynamic_prev_draw_models(const std::vector<Mat4>& prevModels) {
        m_geometry.set_dynamic_prev_models(prevModels);
    }
#endif

    void wait_for_idle();

private:
    bool has_post_process() const { return m_postProcess != nullptr; }

    std::unique_ptr<VulkanContext>  m_context;
    std::unique_ptr<Device>         m_device;
    std::unique_ptr<Swapchain>      m_swapchain;
    std::unique_ptr<CommandManager> m_commandManager;
    std::unique_ptr<SyncObjects>    m_syncObjects;

    std::unique_ptr<PostProcessManager>  m_postProcess;
    std::unique_ptr<CameraUniforms>      m_cameraUniforms;
    std::unique_ptr<MaterialDescriptors> m_materialDescriptors;
    std::unique_ptr<IBLManager>          m_ibl;

    // Render-pass registry (renderer/Passes/): the explicit ordered list of frame passes (PassList.def), the named
    // images they exchange, the CPU values they publish for each other, and the subsystem pointers they may read.
    PassRegistry  m_passes;
    ImageRegistry m_images;
    FrameState    m_frameState;
    PassResources m_passRes;

    TextureManager* m_textureManager = nullptr;

    // Owns the scene geometry and draws it into the shadow atlas + G-buffer (R4); see GeometryProducer.h.
    GeometryProducer m_geometry;

    std::unique_ptr<Camera> m_camera;

    float m_rainIntensity = 0.0f;
    Vec3  m_carVelocity   = Vec3(0.0f, 0.0f, 0.0f);
    Vec3  m_carPosition   = Vec3(0.0f, 0.0f, 0.0f);
    bool  m_wiperEnabled  = false;

    // Sun / clarity / HDRI look + IBL rebake (R5). Bound to m_cameraUniforms / m_ibl / m_debugParams.
    WeatherController m_weather;

#ifdef SWISH_DEBUG_UI

    DebugUI     m_debugUI;
    DebugParams m_debugParams;

    SceneParamsUniform m_sceneParams;

#endif

#ifndef SWISH_DEBUG_UI
    const DebugParams m_defaultTune{};  // release: the shipped look (DebugParams defaults), read through tune()
#endif

    uint32_t m_currentFrame = 0;
    Window*  m_window       = nullptr;

    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);
    void recreateSwapchain();
    void init_passes(bool resize);  // (re)build the PassInitCtx and init / resize every registered pass
    // FrameCtx for one frame; `tune` is the live DebugParams in debug builds and DebugParams{} defaults in release.
    const DebugParams& tune() const;
    FrameUpdate        make_frame_update(float dt);
#ifdef SWISH_DEBUG_UI
    void apply_debug_params();
#endif
};

}  // namespace swish
