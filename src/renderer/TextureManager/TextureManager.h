#pragma once

#include "../Renderer/RendererServices.h"

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Texture — A single GPU texture with its Vulkan resources.
// ══════════════════════════════════════════════════════════════════════

struct Texture {
    std::string    name;
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    int            width  = 0;
    int            height = 0;
};

// ══════════════════════════════════════════════════════════════════════
// TextureManager — Loads, caches, and owns all textures.
//
// Takes raw Vulkan handles at construction (no Renderer dependency).
// Uses ResourceManager static methods for GPU operations.
// ══════════════════════════════════════════════════════════════════════

class TextureManager {
public:
    explicit TextureManager(const RendererServices& services);
    ~TextureManager();

    // Recursively load all textures from a directory.
    void load_directory(const std::string& dir);

    // Register a texture from raw RGBA pixels (e.g. 1x1 white default).
    void register_from_pixels(const std::string& name, const std::vector<uint8_t>& pixels,
                              uint32_t width, uint32_t height);

    // Retrieve a loaded texture by name. Returns nullptr if not found.
    Texture* get_texture(const std::string& name);

    // Shared sampler for all textures.
    VkSampler get_sampler() const;

    // Destroy all Vulkan resources.
    void cleanup();

private:
    VkDevice         m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool    m_commandPool;
    VkQueue          m_graphicsQueue;

    std::unordered_map<std::string, Texture> m_textures;
    VkSampler m_sampler = VK_NULL_HANDLE;

    void upload_texture(const std::string& name, const std::vector<uint8_t>& pixels,
                        uint32_t w, uint32_t h, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void create_sampler();
    static bool is_linear_data(const std::string& name);
};

}  // namespace swish
