#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Free functions, no state (was an all-static `class ResourceManager`; a namespace is the honest shape, see
// docs/DESIGN_PATTERNS.md section 1).
//
// Buffer/image *allocation* now lives in the VMA-backed RAII wrappers
// (`GpuResource.h`: `gpu::deviceLocalBuffer` / `hostVisibleBuffer` / `deviceLocalImage`),
// so the old raw `createBuffer`/`createImage`/`copyBuffer`/`findMemoryType` helpers
// were removed. What remains here are the command-buffer and format utilities that
// operate on already-allocated handles.
namespace resource {

// Transitions a VkImage between layouts using a pipeline barrier.
// Used during texture upload: UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY
void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout);

// Copies pixel data from a staging buffer into a VkImage.
void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height);

// (Inline mid-frame image transitions live in swish::vkx::image_barrier / image_barrier2,
// utils/vulkan/Barrier.h. The legacy insertImageBarrier was deleted 2026-09-29: no callers left.)

// Finds the best depth buffer format the GPU supports.
// Try D32_SFLOAT first, then D32_SFLOAT_S8_UINT, then D24_UNORM_S8_UINT.
VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

// Finds a format from candidates that supports the required features.
VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates,
                             VkImageTiling tiling, VkFormatFeatureFlags features);

}  // namespace resource

}  // namespace swish
