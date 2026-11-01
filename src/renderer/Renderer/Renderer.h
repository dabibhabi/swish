#pragma once
// scene headers
#include "RendererServices.h"
#include "debug/DebugParams.h"
#include "renderer/GeometryProducer/GeometryProducer.h"
#include "renderer/Passes/ImageRegistry.h"
#include "renderer/Passes/PassRegistry.h"
#include "renderer/WeatherController/WeatherController.h"
#include "scene/Entity/WiperBlade.h"  // WiperFrame
#include "scene/SceneTypes.h"
#include "scene/WeatherPreset.h"
#ifdef SWISH_DEBUG_UI
#include "debug/DebugUI.h"
#include "debug/SceneParamsUniform.h"
#include "renderer/FrameCapture/FrameCapture.h"        // S3: frame dump (debug)
#include "renderer/GpuProfiler/GpuProfiler.h"          // S2: GPU timestamp profiler (debug)
#include "renderer/Passes/PassDeclarationValidator.h"  // S4: pass resource declarations + validator (debug)
#include "renderer/ShaderHotReload/ShaderHotReload.h"  // S5: shader hot-reload (debug)
#endif
// vulkan headers
#include <vulkan/vulkan.h>
// stl headers
#include <array>
#include <cstdint>
#include <functional>
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
struct SprayEmitter;  // renderer/SpraySystem/WheelSpray.h (debug per-wheel spray)

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

    void add_corridor_segment(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_corridor_instances(const std::vector<CorridorInstance>& instances);

    void upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void update_dynamic_draw_calls(const std::vector<DrawCall>& draws);
    void destroy_dynamic_geometry();

    void rebuild_material_descriptors();

    void    set_camera(std::unique_ptr<Camera> camera);  // takes ownership
    Camera* get_camera() const;                          // non-owning view; the Renderer keeps ownership

    void set_scene_lights(const std::vector<LightDesc>& lights);
    void set_car_spot_lights(const std::vector<SpotLightDesc>& spots);

    void set_rain_intensity(float intensity);
  
    float get_wetness() const;
    void  set_car_velocity(Vec3 velocity);
    void  set_car_position(Vec3 position);

    void set_wiper(const WiperFrame& wiper);

    // HDRI + sun/clarity look for driving weather. Rebakes IBL from assets/Sky/.
    void          set_weather_preset(WeatherPreset preset);
    WeatherPreset weather_preset() const { return m_weather.preset(); }

#ifdef SWISH_DEBUG_UI

    void debug_init(std::function<void(int key, int action)> keyEvent);
    void debug_present();
    void toggle_debug_controls_window();
    void set_debug_edit_mode(bool edit);

    DebugParams& debug_params() { return m_debugParams; }

    // Per-wheel spray emitters -> SpraySystem::set_emitters (count 0 / nullptr = legacy single rear-axle origin).
    void set_spray_emitters(const SprayEmitter* emitters, uint32_t count);
    // S4/S5 status for the debug UI (plain data, read-only). Hot-reload: status(), log(), enabled(); toggle with
    // set_shader_hot_reload_enabled. Declarations: what each pass reads/writes (list order) + the validator's warnings
    // computed once after init_all.
    const ShaderHotReload&               shader_hot_reload() const { return m_shaderReload; }
    void                                 set_shader_hot_reload_enabled(bool e) { m_shaderReload.set_enabled(e); }
    const std::vector<NamedDeclaration>& pass_declarations() const { return m_passDecls; }
    const std::vector<std::string>&      pass_declaration_warnings() const { return m_declWarnings; }

    // S2: per-pass GPU timings (plain data; snapshot().supported == false when timestamps are unavailable).
    const GpuProfileSnapshot& gpu_profile() const { return m_gpuProfiler.snapshot(); }
    // S3: request a frame dump (F12). hdr also writes the float scene colour as .pfm. Status/last path for the UI.
    void                 request_capture(bool hdr) { m_frameCapture.request(hdr); }
    const CaptureStatus& capture_status() const { return m_frameCapture.status(); }
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

   
    PassRegistry  m_passes;
    ImageRegistry m_images;
    FrameState    m_frameState;
    PassResources m_passRes;

    TextureManager* m_textureManager = nullptr;


    GeometryProducer m_geometry;

    std::unique_ptr<Camera> m_camera;

    float      m_rainIntensity = 0.0f;
    Vec3       m_carVelocity   = Vec3(0.0f, 0.0f, 0.0f);
    Vec3       m_carPosition   = Vec3(0.0f, 0.0f, 0.0f);
    WiperFrame m_wiper{};


    WeatherController m_weather;

#ifdef SWISH_DEBUG_UI

    DebugUI     m_debugUI;
    DebugParams m_debugParams;

    SceneParamsUniform m_sceneParams;

    ShaderHotReload               m_shaderReload;  // S5: polled each frame; the Composite hook is registered in init
    std::vector<NamedDeclaration> m_passDecls;     // S4
    std::vector<std::string>      m_declWarnings;  // S4

    GpuProfiler  m_gpuProfiler;   // S2: registered as the PassRegistry observer in init
    FrameCapture m_frameCapture;  // S3: record after record_all, collect after the slot's fence wait

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
