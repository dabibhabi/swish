#include "SyncObjects.h"

#include "../../utils/VulkanCheck.h"

// Implement GPU-GPU syncing via Semaphores and CPU-GPU syncing via Fences

namespace swish {

void SyncObjects::init(VkDevice device, uint32_t framesInFlight) {
    m_imageAvailable.resize(framesInFlight);
    m_renderFinished.resize(framesInFlight);
    m_inFlightFences.resize(framesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Signaled by default so first frame doesn't wait infinitely
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailable[i]));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinished[i]));
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]));
    }
}

void SyncObjects::cleanup(VkDevice device) {
    for (size_t i = 0; i < m_inFlightFences.size(); i++) {
        vkDestroySemaphore(device, m_renderFinished[i], nullptr);
        vkDestroySemaphore(device, m_imageAvailable[i], nullptr);
        vkDestroyFence(device, m_inFlightFences[i], nullptr);
    }
    m_inFlightFences.clear();
    m_imageAvailable.clear();
    m_renderFinished.clear();
}

void SyncObjects::waitForFence(VkDevice device, uint32_t frameIndex) {
    VK_CHECK(vkWaitForFences(device, 1, &m_inFlightFences[frameIndex], VK_TRUE, UINT64_MAX));
}

void SyncObjects::resetFence(VkDevice device, uint32_t frameIndex) {
    VK_CHECK(vkResetFences(device, 1, &m_inFlightFences[frameIndex]));
}

void SyncObjects::recreateRenderFinishedSemaphore(VkDevice device, uint32_t frameIndex) {
    vkDestroySemaphore(device, m_renderFinished[frameIndex], nullptr);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinished[frameIndex]));
}

VkSemaphore SyncObjects::getImageAvailable(uint32_t frameIndex) const {
    return m_imageAvailable.empty() ? VK_NULL_HANDLE : m_imageAvailable[frameIndex];
}

VkSemaphore SyncObjects::getRenderFinished(uint32_t frameIndex) const {
    return m_renderFinished.empty() ? VK_NULL_HANDLE : m_renderFinished[frameIndex];
}

VkFence SyncObjects::getInFlightFence(uint32_t frameIndex) const {
    return m_inFlightFences.empty() ? VK_NULL_HANDLE : m_inFlightFences[frameIndex];
}

}  // namespace swish
