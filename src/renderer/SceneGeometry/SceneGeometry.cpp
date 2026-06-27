#include "SceneGeometry.h"

#include "../MaterialDescriptors/MaterialDescriptors.h"
#include "../ResourceManager/ResourceManager.h"
#include "../ScenePipeline/ScenePipeline.h"
#include "../Vertex.h"

#include <cstring>

namespace swish {

namespace {

// Boilerplate for a one-shot HOST_VISIBLE staging buffer that copies `srcBytes`
// of `src` data into a freshly-allocated DEVICE_LOCAL `dst` buffer of `usage`.
// Caller owns `dst` / `dstMem` and must destroy them. The staging buffer is
// fully cleaned up here.
void uploadViaStaging(const RendererServices& s, VkBufferUsageFlags dstUsage, VkDeviceSize bufferSize, const void* src,
                      VkBuffer& dst, VkDeviceMemory& dstMem) {
    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    ResourceManager::createBuffer(s.device, s.physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  stagingBuffer, stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(s.device, stagingMemory, 0, bufferSize, 0, &mapped);
    std::memcpy(mapped, src, static_cast<size_t>(bufferSize));
    vkUnmapMemory(s.device, stagingMemory);

    ResourceManager::createBuffer(s.device, s.physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | dstUsage,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dst, dstMem);

    ResourceManager::copyBuffer(s.device, s.commandPool, s.graphicsQueue, stagingBuffer, dst, bufferSize);

    vkDestroyBuffer(s.device, stagingBuffer, nullptr);
    vkFreeMemory(s.device, stagingMemory, nullptr);
}

// Uploads two buffers (vertex + index) through staging in a single command
// buffer submission — one vkQueueSubmit + wait instead of two.
void uploadTwoViaStaging(const RendererServices& s,
                         VkBufferUsageFlags vertUsage, VkDeviceSize vertSize, const void* vertSrc,
                         VkBuffer& vertDst, VkDeviceMemory& vertDstMem,
                         VkBufferUsageFlags idxUsage, VkDeviceSize idxSize, const void* idxSrc,
                         VkBuffer& idxDst, VkDeviceMemory& idxDstMem) {
    // Create and fill vertex staging buffer
    VkBuffer       vertStaging = VK_NULL_HANDLE;
    VkDeviceMemory vertStagingMem = VK_NULL_HANDLE;
    ResourceManager::createBuffer(s.device, s.physicalDevice, vertSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  vertStaging, vertStagingMem);
    void* mapped = nullptr;
    vkMapMemory(s.device, vertStagingMem, 0, vertSize, 0, &mapped);
    std::memcpy(mapped, vertSrc, static_cast<size_t>(vertSize));
    vkUnmapMemory(s.device, vertStagingMem);

    // Create and fill index staging buffer
    VkBuffer       idxStaging = VK_NULL_HANDLE;
    VkDeviceMemory idxStagingMem = VK_NULL_HANDLE;
    ResourceManager::createBuffer(s.device, s.physicalDevice, idxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  idxStaging, idxStagingMem);
    vkMapMemory(s.device, idxStagingMem, 0, idxSize, 0, &mapped);
    std::memcpy(mapped, idxSrc, static_cast<size_t>(idxSize));
    vkUnmapMemory(s.device, idxStagingMem);

    // Create destination (device-local) buffers
    ResourceManager::createBuffer(s.device, s.physicalDevice, vertSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | vertUsage,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertDst, vertDstMem);
    ResourceManager::createBuffer(s.device, s.physicalDevice, idxSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | idxUsage,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, idxDst, idxDstMem);

    // Record both copies into one command buffer and submit once
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = s.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(s.device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy vertRegion{0, 0, vertSize};
    vkCmdCopyBuffer(cmd, vertStaging, vertDst, 1, &vertRegion);

    VkBufferCopy idxRegion{0, 0, idxSize};
    vkCmdCopyBuffer(cmd, idxStaging, idxDst, 1, &idxRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(s.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(s.graphicsQueue);

    vkFreeCommandBuffers(s.device, s.commandPool, 1, &cmd);

    vkDestroyBuffer(s.device, vertStaging, nullptr);
    vkFreeMemory(s.device, vertStagingMem, nullptr);
    vkDestroyBuffer(s.device, idxStaging, nullptr);
    vkFreeMemory(s.device, idxStagingMem, nullptr);
}

}  // namespace

void SceneGeometry::cleanup(VkDevice device) {
    m_drawCalls.clear();

    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}

void SceneGeometry::upload(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws) {
    cleanup(s.device);
    m_drawCalls = draws;

    if (mesh.empty())
        return;

    uploadTwoViaStaging(s,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(Vertex) * mesh.getVertices().size(),
                        mesh.getVertices().data(), m_vertexBuffer, m_vertexBufferMemory,
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sizeof(uint32_t) * mesh.getIndices().size(),
                        mesh.getIndices().data(), m_indexBuffer, m_indexBufferMemory);
}

void SceneGeometry::record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline,
                                 MaterialDescriptors& materials) const {
    if (!has_geometry())
        return;

    VkBuffer     vbs[] = {m_vertexBuffer};
    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    VkPipelineLayout layout = pipeline.get_layout();

    for (const auto& dc : m_drawCalls) {
        VkDescriptorSet matSet = materials.get_set(dc.material);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &matSet, 0, nullptr);

        PushConstantData pushData{};
        pushData.model = dc.model;
        pushData.color = dc.color;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstantData), &pushData);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}

}  // namespace swish
