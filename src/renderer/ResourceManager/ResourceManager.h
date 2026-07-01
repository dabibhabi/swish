#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Static utility class — no instances, no state. Just helper functions.
//
// Buffer/image *allocation* now lives in the VMA-backed RAII wrappers
// (`GpuResource.h`: `gpu::deviceLocalBuffer` / `hostVisibleBuffer` / `deviceLocalImage`),
// so the old raw `createBuffer`/`createImage`/`copyBuffer`/`findMemoryType` helpers
// were removed. What remains here are the command-buffer and format utilities that
// operate on already-allocated handles.
class ResourceManager {
public:
    // Transitions a VkImage between layouts using a pipeline barrier.
    // Used during texture upload: UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY
    static void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkImage image,
                                      VkImageLayout oldLayout, VkImageLayout newLayout);

    // Copies pixel data from a staging buffer into a VkImage.
    static void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer buffer,
                                  VkImage image, uint32_t width, uint32_t height);

    // Records an inline image layout transition into an existing command buffer.
    // Used for multi-pass rendering where transitions happen mid-frame.
    static void insertImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    // Finds the best depth buffer format the GPU supports.
    // Try D32_SFLOAT first, then D32_SFLOAT_S8_UINT, then D24_UNORM_S8_UINT.
    static VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

    // Finds a format from candidates that supports the required features.
    static VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates,
                                        VkImageTiling tiling, VkFormatFeatureFlags features);
};

}  // namespace swish
