#include "CommandManager.h"

#include "../../utils/VulkanCheck.h"

// Implemented `CommandManager` to spin up command pools matching graphics queue family
// and gracefully allocate discrete main command buffers.

namespace swish {

void CommandManager::init(VkDevice device, uint32_t graphicsQueueFamily, uint32_t bufferCount) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_pool));

    m_buffers.resize(bufferCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = bufferCount;

    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, m_buffers.data()));
}

void CommandManager::cleanup(VkDevice device) {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    // Command buffers are automatically freed when their pool is destroyed
}

VkCommandBuffer CommandManager::getBuffer(uint32_t frameIndex) const {
    return m_buffers.empty() ? VK_NULL_HANDLE : m_buffers[frameIndex];
}

void CommandManager::resetBuffer(uint32_t frameIndex) {
    vkResetCommandBuffer(m_buffers[frameIndex], 0);
}

void CommandManager::beginRecording(uint32_t frameIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // We use this if we want to submit the same buffer multiple times concurrently = 0 implies false
    beginInfo.flags = 0;

    VK_CHECK(vkBeginCommandBuffer(m_buffers[frameIndex], &beginInfo));
}

void CommandManager::endRecording(uint32_t frameIndex) {
    VK_CHECK(vkEndCommandBuffer(m_buffers[frameIndex]));
}

VkCommandPool CommandManager::getPool() const {
    return m_pool;
}

}  // namespace swish
