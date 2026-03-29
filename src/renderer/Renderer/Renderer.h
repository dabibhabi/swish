#pragma once

#include <vulkan/vulkan.h>

#include "../../scene/SceneTypes.h"

#include <cstdint>
#include <vector>

namespace swish {

// Forward declarations
class Window;
class VulkanContext;
class Device;
class Swapchain;
class RenderPass;
class CommandManager;
class SyncObjects;
class Camera;
class TextureManager;
class SceneManager;
class ModelManager;

// The Renderer orchestrates the Vulkan draw loop and acts as a
// central registry for managers (rind-style architecture).
//
// Architecture:
//   App
//    ├── Window
//    ├── Renderer (Vulkan core + manager registry)
//    │    ├── VulkanContext, Device, Swapchain, RenderPass
//    │    ├── Pipeline, CommandManager, SyncObjects
//    │    ├── UBOs, descriptors, scene geometry
//    │    └── Camera* (set by scene)
//    ├── TextureManager (owns all textures)
//    ├── SceneManager (owns scenes, handles switching)
//    └── ModelManager (placeholder for cars)
//
class Renderer {
public:
    Renderer();
    ~Renderer();

    // ── Core lifecycle ────────────────────────────────────────────────
    void init(Window& window);
    void cleanup();
    void drawFrame();

    // ── Manager registration (called by App after init) ───────────────
    void register_texture_manager(TextureManager* mgr);
    void register_scene_manager(SceneManager* mgr);
    void register_model_manager(ModelManager* mgr);

    // ── Manager getters ───────────────────────────────────────────────
    TextureManager* get_texture_manager() const;
    SceneManager*   get_scene_manager() const;
    ModelManager*   get_model_manager() const;

    // ── Vulkan handle getters (for managers) ──────────────────────────
    VkDevice         get_vk_device() const;
    VkPhysicalDevice get_vk_physical_device() const;
    VkCommandPool    get_command_pool() const;
    VkQueue          get_graphics_queue() const;
    VkExtent2D       get_swapchain_extent() const;

    // ── Scene geometry (called by Scene lambdas) ──────────────────────
    void upload_scene_geometry(const MeshData& mesh, const std::vector<DrawCall>& draws);
    void destroy_scene_geometry();

    // ── Material descriptors (called after TextureManager loads) ──────
    void rebuild_material_descriptors();

    // ── Camera (owned by Renderer, set by Scene) ──────────────────────
    void    set_camera(Camera* camera);
    Camera* get_camera() const;

    // ── GPU synchronization ───────────────────────────────────────────
    void wait_for_idle();

private:
    // ── Owned subsystems ───────────────────────────────────────────
    VulkanContext*  m_context        = nullptr;
    Device*         m_device         = nullptr;
    Swapchain*      m_swapchain      = nullptr;
    RenderPass*     m_renderPass     = nullptr;
    CommandManager* m_commandManager = nullptr;
    SyncObjects*    m_syncObjects    = nullptr;

    // ── Manager pointers (NOT owned — App owns these) ─────────────
    TextureManager* m_textureManager = nullptr;
    SceneManager*   m_sceneManager   = nullptr;
    ModelManager*   m_modelManager   = nullptr;

    // ── Depth resources ───────────────────────────────────────────
    VkImage        m_depthImage     = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory    = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    // ── Pipeline ──────────────────────────────────────────────────
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialSetLayout   = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_pipeline            = VK_NULL_HANDLE;

    // ── Uniform buffers (one per frame-in-flight) ─────────────────
    std::vector<VkBuffer>       m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*>          m_uniformBuffersMapped;

    // ── Descriptor pool + sets for camera UBOs ────────────────────
    VkDescriptorPool             m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // ── Material descriptor pool + sets (rebuilt on texture load) ──
    VkDescriptorPool             m_materialPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_materialSets;

    // ── Scene geometry ────────────────────────────────────────────
    VkBuffer              m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory        m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer              m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory        m_indexBufferMemory  = VK_NULL_HANDLE;
    std::vector<DrawCall> m_drawCalls;

    // ── Camera ────────────────────────────────────────────────────
    Camera* m_camera = nullptr;

    // ── Frame tracking ────────────────────────────────────────────
    uint32_t m_currentFrame = 0;

    // ── Pointer back to window ────────────────────────────────────
    Window* m_window = nullptr;

    // ── Private helpers ───────────────────────────────────────────
    void recordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);
    void recreateSwapchain();
    void createDepthResources();
    void destroyDepthResources();
    void createPipeline();
    void destroyPipeline();
    void createUniformBuffers();
    void destroyUniformBuffers();
    void createCameraDescriptorPool();
    void createCameraDescriptorSets();
    void updateUniformBuffer(uint32_t frameIndex);
};

}  // namespace swish
