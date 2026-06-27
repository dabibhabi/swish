#include "SyncObjects.h"

#include "../../utils/VulkanCheck.h"

#include <cassert>

namespace swish {

namespace {
VkSemaphore createBinarySemaphore(VkDevice device) {
    VkSemaphoreCreateInfo info{};
    info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore sem = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &info, nullptr, &sem));
    return sem;
}
}  // namespace

void SyncObjects::init(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount) {
    m_imageAvailable.resize(framesInFlight);
    m_inFlightFences.resize(framesInFlight);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_imageAvailable[i] = createBinarySemaphore(device);
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]));
    }

    m_renderFinished.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; i++) {
        m_renderFinished[i] = createBinarySemaphore(device);
    }
}

void SyncObjects::cleanup(VkDevice device) {
    for (VkSemaphore sem : m_renderFinished) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    for (VkSemaphore sem : m_imageAvailable) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    for (VkFence fence : m_inFlightFences) {
        vkDestroyFence(device, fence, nullptr);
    }
    m_renderFinished.clear();
    m_imageAvailable.clear();
    m_inFlightFences.clear();
}

void SyncObjects::recreateRenderFinishedSemaphores(VkDevice device, uint32_t swapchainImageCount) {
    for (VkSemaphore sem : m_renderFinished) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    m_renderFinished.clear();
    m_renderFinished.reserve(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; i++) {
        m_renderFinished.push_back(createBinarySemaphore(device));
    }
}

void SyncObjects::waitForFence(VkDevice device, uint32_t frameIndex) {
    constexpr uint64_t kNoFenceTimeout = UINT64_MAX;  // disable timeout
    VK_CHECK(vkWaitForFences(device, 1, &m_inFlightFences[frameIndex], VK_TRUE, kNoFenceTimeout));
}

void SyncObjects::resetFence(VkDevice device, uint32_t frameIndex) {
    VK_CHECK(vkResetFences(device, 1, &m_inFlightFences[frameIndex]));
}

VkSemaphore SyncObjects::getImageAvailable(uint32_t frameIndex) const {
    assert(!m_imageAvailable.empty() && "SyncObjects::init not called");
    return m_imageAvailable[frameIndex];
}

VkSemaphore SyncObjects::getRenderFinished(uint32_t imageIndex) const {
    assert(!m_renderFinished.empty() && "SyncObjects::init not called");
    return m_renderFinished[imageIndex];
}

VkFence SyncObjects::getInFlightFence(uint32_t frameIndex) const {
    assert(!m_inFlightFences.empty() && "SyncObjects::init not called");
    return m_inFlightFences[frameIndex];
}

}  // namespace swish
