#pragma once

#include "../GpuResource/GpuResource.h"
#include "../Renderer/RendererServices.h"

#include <vulkan/vulkan.h>

namespace swish {

// Owns the depth-buffer image, its backing memory, and the view that
// render passes attach. The format is picked once at init() via
// ResourceManager::findDepthFormat (or accepted as an explicit override)
// and stays stable across recreate.
//
// Design follows the CameraUniforms / MaterialDescriptors convention:
// blank ctor, init/cleanup, no setters, narrow getters for the two
// values (view + format) that render-pass attachment plumbing needs.
class DepthBuffer {
public:
    DepthBuffer() = default;

    // Allocates image + memory + view sized to services.swapchainExtent.
    // If format is VK_FORMAT_UNDEFINED, picks one via findDepthFormat.
    void init(const RendererServices& services, VkFormat format = VK_FORMAT_UNDEFINED);

    // Tear down + re-init at the new swapchain extent. Format does not
    // change (depends only on the physical device, which is stable).
    void recreate(const RendererServices& services);

    void cleanup(VkDevice device);

    VkImageView get_view() const { return m_view; }
    VkFormat    get_format() const { return m_format; }

private:
    GpuImage    m_image;  // RAII (VMA-backed); replaces raw VkImage + VkDeviceMemory
    VkImageView m_view   = VK_NULL_HANDLE;
    VkFormat    m_format = VK_FORMAT_UNDEFINED;
};

}  // namespace swish
