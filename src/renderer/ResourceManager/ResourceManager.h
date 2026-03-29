#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Static utility class — no instances, no state. Just helper functions.
// You will call createBuffer 20+ times across the project.
// Write it once here, never duplicate.
// Modeled after DownPour's ResourceManager (all static, pure utility).
class ResourceManager {
public:
    // Creates a VkBuffer and allocates its device memory.
    // Used for: vertex buffers, index buffers, uniform buffers, staging buffers,
    // instance buffers. Key: choose the right memory property flags:
    //   - HOST_VISIBLE | HOST_COHERENT for CPU-writable (uniform buffers, staging)
    //   - DEVICE_LOCAL for GPU-only (vertex/index buffers after staging copy)
    static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                             VkDeviceMemory& memory);

    // Copies data between two buffers (staging → device-local)
    // Uses a temporary command buffer — submit and wait immediately.
    static void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
                           VkBuffer dstBuffer, VkDeviceSize size);

    // Creates a VkImage and allocates its device memory.
    // Used for: depth images, textures, G-Buffer attachments, offscreen targets.
    static void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                            VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory);

    // Finds a memory type that satisfies both the type filter AND the
    // property flags. The GPU has different memory heaps (VRAM, system RAM, etc.)
    // — this picks the right one.
    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);

    // Transitions a VkImage between layouts using a pipeline barrier.
    // Used during texture upload: UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY
    static void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkImage image,
                                      VkImageLayout oldLayout, VkImageLayout newLayout);

    // Copies pixel data from a staging buffer into a VkImage.
    static void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer buffer,
                                  VkImage image, uint32_t width, uint32_t height);

    // Finds the best depth buffer format the GPU supports.
    // Try D32_SFLOAT first, then D32_SFLOAT_S8_UINT, then D24_UNORM_S8_UINT.
    static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

    // Finds a format from candidates that supports the required features.
    static VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates,
                                        VkImageTiling tiling, VkFormatFeatureFlags features);
};

}  // namespace swish
