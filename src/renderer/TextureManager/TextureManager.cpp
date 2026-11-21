#include "TextureManager.h"

#include "renderer/ResourceManager/ResourceManager.h"
#include "utils/vulkan/Check.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════════════

TextureManager::TextureManager(const RendererServices& services)
    : m_device(services.device),
      m_commandPool(services.commandPool),
      m_graphicsQueue(services.graphicsQueue),
      m_allocator(services.allocator) {
    create_sampler();
}

TextureManager::~TextureManager() { cleanup(); }

// ══════════════════════════════════════════════════════════════════════
// load_directory — Scan a directory and load all image files.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::load_directory(const std::string& dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(dir)) { throw std::runtime_error("TextureManager: directory not found: " + dir); }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") continue;

        std::string name = entry.path().stem().string();
        std::string path = entry.path().string();

        int            w, h, channels;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data) {
            throw std::runtime_error("TextureManager: failed to load " + path + " (" + stbi_failure_reason() + ")");
        }

        std::vector<uint8_t> pixels(data, data + (w * h * 4));
        stbi_image_free(data);

        VkFormat fmt = is_linear_data(name) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        upload_texture(name, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), fmt);
    }
}

bool TextureManager::is_linear_data(const std::string& name) {
    return name.find("normal") != std::string::npos || name.find("roughness") != std::string::npos ||
           name.find("metallic") != std::string::npos || name.find("ao") != std::string::npos ||
           name.find("height") != std::string::npos;
}

// ══════════════════════════════════════════════════════════════════════
// register_from_pixels — Register a texture from raw RGBA data.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::register_from_pixels(const std::string& name, const std::vector<uint8_t>& pixels, uint32_t width,
                                          uint32_t height) {
    // Match load_directory: albedo = sRGB, normal/roughness/metallic/ao = linear UNORM.
    // (Previously everything went through sRGB — car ORM/normal maps were gamma-warped
    // and sampled too glossy, which showed up as faceted IBL on the steering wheel.)
    VkFormat fmt = is_linear_data(name) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
    upload_texture(name, pixels, width, height, fmt);
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
    }
    m_textures.clear();  // GpuImage RAII frees each image + sub-allocation

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
}

// ══════════════════════════════════════════════════════════════════════
// upload_texture — Upload RGBA pixels to a VkImage via staging buffer.
// ══════════════════════════════════════════════════════════════════════

void TextureManager::upload_texture(const std::string& name, const std::vector<uint8_t>& pixels, uint32_t w, uint32_t h,
                                    VkFormat format) {
    Texture tex;
    tex.name   = name;
    tex.width  = static_cast<int>(w);
    tex.height = static_cast<int>(h);

    uint32_t                    levels = 1;
    const std::vector<uint8_t>* chain  = &pixels;
#ifdef SWISH_DEBUG_UI
    // D2: box-filtered mip chain; without it a 1 m brick tile (and every road texture) aliases into moire at range.
    std::vector<uint8_t> mips = pixels;
    for (uint32_t lw = w, lh = h; lw > 1 || lh > 1; ++levels) {
        const uint32_t nw = std::max(1u, lw / 2), nh = std::max(1u, lh / 2);
        const size_t   src = mips.size() - size_t{lw} * lh * 4;
        for (uint32_t y = 0; y < nh; ++y)
            for (uint32_t x = 0; x < nw; ++x)
                for (uint32_t c = 0; c < 4; ++c) {
                    const auto px = [&](uint32_t i, uint32_t j) {
                        return mips[src + (size_t{std::min(j, lh - 1)} * lw + std::min(i, lw - 1)) * 4 + c];
                    };
                    mips.push_back(static_cast<uint8_t>((px(2 * x, 2 * y) + px(2 * x + 1, 2 * y) +
                                                         px(2 * x, 2 * y + 1) + px(2 * x + 1, 2 * y + 1) + 2) /
                                                        4));
                }
        lw = nw;
        lh = nh;
    }
    chain = &mips;
#endif
    VkDeviceSize imageSize = chain->size();

    // Staging buffer (host-visible, persistently mapped; freed by RAII at scope end).
    GpuBuffer staging = gpu::hostVisibleBuffer(m_allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memcpy(staging.mapped(), chain->data(), static_cast<size_t>(imageSize));

    // Create VkImage in device-local memory (VMA).
    tex.image = gpu::deviceLocalImage(m_allocator, w, h, format, VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, levels);

    // Transitions + copy
    resource::transitionImageLayout(m_device, m_commandPool, m_graphicsQueue, tex.image.handle(),
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels);

    resource::copyBufferToImage(m_device, m_commandPool, m_graphicsQueue, staging.handle(), tex.image.handle(), w, h,
                                levels);

    resource::transitionImageLayout(m_device, m_commandPool, m_graphicsQueue, tex.image.handle(),
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    levels);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image.handle();
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = levels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view));

    m_textures[name] = std::move(tex);  // Texture is move-only (owns a GpuImage)
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
#ifdef SWISH_DEBUG_UI
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;  // D2: sample the mip chain (release keeps level 0 only)
#endif

    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler));
}

}  // namespace swish
