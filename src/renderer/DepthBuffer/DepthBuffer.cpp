#include "DepthBuffer.h"

#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../ResourceManager/ResourceManager.h"

namespace swish {

void DepthBuffer::init(const RendererServices& s, VkFormat format) {
    m_format = (format == VK_FORMAT_UNDEFINED) ? ResourceManager::findDepthFormat(s.physicalDevice) : format;

    ResourceManager::createImage(s.device, s.physicalDevice, s.swapchainExtent.width, s.swapchainExtent.height,
                                 m_format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_image, m_memory);

    auto viewInfo                            = vk::makeImageViewCreateInfo();
    viewInfo.image                           = m_image;
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
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

}  // namespace swish
