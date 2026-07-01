#include "DepthBuffer.h"

#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../ResourceManager/ResourceManager.h"

namespace swish {

void DepthBuffer::init(const RendererServices& s, VkFormat format) {
    m_format = (format == VK_FORMAT_UNDEFINED) ? ResourceManager::findDepthFormat(s.physicalDevice) : format;

    m_image = gpu::deviceLocalImage(s.allocator, s.swapchainExtent.width, s.swapchainExtent.height, m_format,
                                    VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    auto viewInfo                            = vk::makeImageViewCreateInfo();
    viewInfo.image                           = m_image.handle();
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = m_format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(s.device, &viewInfo, nullptr, &m_view));
}

void DepthBuffer::recreate(const RendererServices& s) {
    cleanup(s.device);
    init(s, m_format);  // preserve previously-selected format
}

void DepthBuffer::cleanup(VkDevice device) {
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    m_image.reset();  // VMA-backed: frees image + sub-allocation
}

}  // namespace swish
