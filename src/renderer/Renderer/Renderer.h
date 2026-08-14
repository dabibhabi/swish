#pragma once
// scene headers
#include "../../scene/SceneTypes.h"
#include "../DeferredLightingPipeline/DeferredLightingPipeline.h"
#include "../DepthOnlyPipeline/DepthOnlyPipeline.h"
#include "../SceneGeometry/SceneGeometry.h"
#include "../ScenePipeline/ScenePipeline.h"
#include "RendererServices.h"

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
//  {#5c4,20} Forward Declerations
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
class DofPass;

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

    
    void init(Window& window);
    void cleanup();
    void drawFrame(float deltaTime);

    
    void register_texture_manager(TextureManager* mgr);
    void register_scene_manager(SceneManager* mgr);
    void register_model_manager(ModelManager* mgr);
    
    TextureManager* get_texture_manager() const;
    SceneManager*   get_scene_manager() const;
    ModelManager*   get_model_manager() const;
    
    RendererServices services() const;

    void upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void destroy_scene_geometry();

    void upload_chunk_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_road_chunks(float introOffsetZ, const std::vector<float>& slotOffsetsZ);

    void upload_interchange_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_interchanges(const std::vector<float>& slotOffsetsZ);

    
    void upload_dynamic_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void update_dynamic_draw_calls(const std::vector<DrawCall>& draws);
    void destroy_dynamic_geometry();

    void rebuild_material_descriptors();
    
    void    set_camera(Camera* camera);
    Camera* get_camera() const;
    
    void set_scene_lights(const std::vector<LightDesc>& lights);
    
    void set_rain_intensity(float intensity);  
    void set_car_velocity(Vec3 velocity);      
    void set_car_position(Vec3 position);      
    void set_wiper_enabled(bool enabled);      
    
    void set_clear_day(bool clear);

#ifdef SWISH_DEBUG_UI

    void debug_init();
    void set_debug_edit_mode(bool edit);
    
    DebugParams& debug_params() { return m_debugParams; }
#endif

    
    
    void update_glass_draw_calls(const std::vector<DrawCall>& glassDCs);
    void update_windshield_draw_calls(const std::vector<DrawCall>& windshieldDCs);

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
    std::unique_ptr<RainSystem>          m_rainSystem;
    std::unique_ptr<SpraySystem>         m_spraySystem;  
    std::unique_ptr<GlassPass>           m_glassPass;
    std::unique_ptr<WindshieldRainPass>  m_windshieldRainPass;
    std::unique_ptr<IBLManager>          m_ibl;  

    
    TextureManager* m_textureManager = nullptr;
    SceneManager*   m_sceneManager   = nullptr;
    ModelManager*   m_modelManager   = nullptr;

    ScenePipeline            m_scenePipeline;
    DeferredLightingPipeline m_deferredLighting;
    DepthOnlyPipeline        m_depthOnlyPipeline;  
    
    SceneGeometry m_sceneGeometry;
    SceneGeometry m_dynamicGeometry;

    SceneGeometry      m_chunkGeometry;
    float              m_introOffsetZ = 0.0f;
    std::vector<float> m_chunkSlotOffsetsZ;

    
    SceneGeometry      m_interchangeGeometry;
    std::vector<float> m_interchangeSlotOffsetsZ;

    
    std::unique_ptr<Camera> m_camera;

    
    float m_rainIntensity = 0.0f;
    Vec3  m_rainWind      = Vec3(4500.0f, 0.0f, 1200.0f);  
    Vec3  m_carVelocity   = Vec3(0.0f, 0.0f, 0.0f);        
    Vec3  m_carPosition   = Vec3(0.0f, 0.0f, 0.0f);        
    bool  m_wiperEnabled  = false;                         
    float m_windTime      = 0.0f;                          
    bool  m_clearDay      = false;                         

    Vec3 m_sunDir = glm::normalize(Vec3(0.3f, 0.6f, 0.15f));
    
    std::array<Mat4, NUM_CASCADES> m_cascadeVP{};
    Vec3                           m_cascadeSplits{0.0f};

    float m_clarity       = 0.0f;
    Vec3  m_sunColor      = Vec3(1.0f, 0.95f, 0.85f);
    Vec3  m_bakedSunDir   = Vec3(1e9f);  
    float m_bakedClarity  = -1.0f;
    Vec3  m_bakedSunColor = Vec3(-1.0f);

#ifdef SWISH_DEBUG_UI
    
    DebugUI     m_debugUI;
    DebugParams m_debugParams;
        
    SceneParamsUniform m_sceneParams;
    
    float m_aeAdaptedLum = 0.5f;
    float m_aeExposure   = 0.45f;

    std::unique_ptr<TaaPass> m_taa;
    Mat4                     m_prevViewProjUnjit = Mat4(1.0f);
    uint32_t                 m_taaFrameCounter   = 0;

    
    
    std::unique_ptr<DofPass> m_dof;
#endif
    
    std::vector<DrawCall> m_glassDrawCalls;
    std::vector<DrawCall> m_windshieldDrawCalls;

    uint32_t m_currentFrame = 0;
    Window* m_window = nullptr;

    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex);  
    void computeCascades();
    void recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void transitionGBufferForLighting(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordLightingPass(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent);
    void recordRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordSprayPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordGlassPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordWindshieldRainPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordBloomExtract(VkCommandBuffer cmd, VkExtent2D extent);
    void recordBloomBlur(VkCommandBuffer cmd, VkExtent2D extent, bool horizontal);
    void recordGodRaysPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordSsrPass(VkCommandBuffer cmd, uint32_t frameIndex);
#ifdef SWISH_DEBUG_UI
    void recordSsaoPasses(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordLuminancePyramid(VkCommandBuffer cmd, uint32_t frameIndex);
    void updateAutoExposure(float dt);
#endif
    void recordCompositePass(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex, VkExtent2D extent);
    void maybeRebakeIBL();
    void recreateSwapchain();
#ifdef SWISH_DEBUG_UI
    void apply_debug_params();
#endif
};

}  
