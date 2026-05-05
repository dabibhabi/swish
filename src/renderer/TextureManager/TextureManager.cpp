#include "TextureManager.h"

#include "../../utils/VulkanCheck.h"
#include "../ResourceManager/ResourceManager.h"

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <stb_image.h>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════════════

TextureManager::TextureManager(const RendererServices& services)
    : m_device(services.device)
    , m_physicalDevice(services.physicalDevice)
    , m_commandPool(services.commandPool)
    , m_graphicsQueue(services.graphicsQueue) {
    create_sampler();
}

TextureManager::~TextureManager() { cleanup(); }

// ══════════════════════════════════════════════════════════════════════
// load_directory — Scan a directory and load all image files.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::load_directory(const std::string& dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(dir)) {
        throw std::runtime_error("TextureManager: directory not found: " + dir);
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") continue;

        std::string name = entry.path().stem().string();
        std::string path = entry.path().string();

        int            w, h, channels;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data) {
            throw std::runtime_error("TextureManager: failed to load " + path +
                                     " (" + stbi_failure_reason() + ")");
        }

        std::vector<uint8_t> pixels(data, data + (w * h * 4));
        stbi_image_free(data);

        VkFormat fmt = is_linear_data(name) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        upload_texture(name, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), fmt);
    }
}

bool TextureManager::is_linear_data(const std::string& name) {
    return name.find("normal") != std::string::npos ||
           name.find("roughness") != std::string::npos ||
           name.find("metallic") != std::string::npos ||
           name.find("ao") != std::string::npos ||
           name.find("height") != std::string::npos;
}

// ══════════════════════════════════════════════════════════════════════
// register_from_pixels — Register a texture from raw RGBA data.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::register_from_pixels(const std::string& name,
                                          const std::vector<uint8_t>& pixels,
                                          uint32_t width, uint32_t height) {
    upload_texture(name, pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB);
}

// ══════════════════════════════════════════════════════════════════════
// get_texture — Look up a texture by name.
// ══════════════════════════════════════════════════════════════════════

Texture* TextureManager::get_texture(const std::string& name) {
    auto it = m_textures.find(name);
    return (it != m_textures.end()) ? &it->second : nullptr;
}

VkSampler TextureManager::get_sampler() const { return m_sampler; }

// ══════════════════════════════════════════════════════════════════════
// cleanup — Destroy all Vulkan resources.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::cleanup() {
    for (auto& [name, tex] : m_textures) {
        if (tex.view != VK_NULL_HANDLE) vkDestroyImageView(m_device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE) vkDestroyImage(m_device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE) vkFreeMemory(m_device, tex.memory, nullptr);
    }
    m_textures.clear();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

// ══════════════════════════════════════════════════════════════════════
// upload_texture — Upload RGBA pixels to a VkImage via staging buffer.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::upload_texture(const std::string& name, const std::vector<uint8_t>& pixels,
                                    uint32_t w, uint32_t h, VkFormat format) {
    Texture tex;
    tex.name   = name;
    tex.width  = static_cast<int>(w);
    tex.height = static_cast<int>(h);

    VkDeviceSize imageSize = w * h * 4;

    // Staging buffer
    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingMemory;
    ResourceManager::createBuffer(m_device, m_physicalDevice, imageSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingMemory);

    // Create VkImage in device-local memory
    ResourceManager::createImage(m_device, m_physicalDevice, w, h,
                                 format, VK_IMAGE_TILING_OPTIMAL,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);

    // Transitions + copy
    ResourceManager::transitionImageLayout(m_device, m_commandPool, m_graphicsQueue,
                                           tex.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    ResourceManager::copyBufferToImage(m_device, m_commandPool, m_graphicsQueue,
                                       stagingBuffer, tex.image, w, h);

    ResourceManager::transitionImageLayout(m_device, m_commandPool, m_graphicsQueue,
                                           tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view));

    m_textures[name] = tex;
}

// ══════════════════════════════════════════════════════════════════════
// create_sampler — Shared linear-repeat sampler for all textures.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::create_sampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter               = VK_FILTER_LINEAR;
    samplerInfo.minFilter               = VK_FILTER_LINEAR;
    samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable        = VK_FALSE;
    samplerInfo.maxAnisotropy           = 1.0f;
    samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable           = VK_FALSE;
    samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler));
}

}  // namespace swish
