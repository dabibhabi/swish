#include "SceneGeometry.h"

#include "../DepthOnlyPipeline/DepthOnlyPipeline.h"
#include "../MaterialDescriptors/MaterialDescriptors.h"
#include "../ScenePipeline/ScenePipeline.h"
#include "../Vertex.h"

#include <cstring>

namespace swish {

namespace {

// Uploads vertex + index data through host-visible staging buffers into two
// device-local buffers, with a single command-buffer submission (one
// vkQueueSubmit + wait). All four buffers are VMA-allocated; the two staging
// buffers free themselves (RAII) when this function returns.
void uploadTwoViaStaging(const RendererServices& s, VkBufferUsageFlags vertUsage, VkDeviceSize vertSize,
                         const void* vertSrc, GpuBuffer& vertDst, VkBufferUsageFlags idxUsage, VkDeviceSize idxSize,
                         const void* idxSrc, GpuBuffer& idxDst) {
    GpuBuffer vertStaging = gpu::hostVisibleBuffer(s.allocator, vertSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(vertStaging.mapped(), vertSrc, static_cast<size_t>(vertSize));

    GpuBuffer idxStaging = gpu::hostVisibleBuffer(s.allocator, idxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(idxStaging.mapped(), idxSrc, static_cast<size_t>(idxSize));

    // Device-local destinations (out-params).
    vertDst = gpu::deviceLocalBuffer(s.allocator, vertSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | vertUsage);
    idxDst  = gpu::deviceLocalBuffer(s.allocator, idxSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | idxUsage);

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
    vkCmdCopyBuffer(cmd, vertStaging.handle(), vertDst.handle(), 1, &vertRegion);

    VkBufferCopy idxRegion{0, 0, idxSize};
    vkCmdCopyBuffer(cmd, idxStaging.handle(), idxDst.handle(), 1, &idxRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(s.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(s.graphicsQueue);

    vkFreeCommandBuffers(s.device, s.commandPool, 1, &cmd);
    // vertStaging / idxStaging free themselves (RAII) on return.
}

}  // namespace

void SceneGeometry::cleanup(VkDevice /*device*/) {
    m_drawCalls.clear();
    m_indexBuffer.reset();   // RAII (VMA): frees buffer + sub-allocation
    m_vertexBuffer.reset();
}

void SceneGeometry::upload(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws) {
    cleanup(s.device);
    m_drawCalls = draws;

    if (mesh.empty())
        return;

    uploadTwoViaStaging(s, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(Vertex) * mesh.getVertices().size(),
                        mesh.getVertices().data(), m_vertexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        sizeof(uint32_t) * mesh.getIndices().size(), mesh.getIndices().data(), m_indexBuffer);
}

void SceneGeometry::record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline,
                                 MaterialDescriptors& materials, const MaterialOverride* overrides) const {
    if (!has_geometry())
        return;

    VkBuffer     vbs[] = {m_vertexBuffer.handle()};
    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);

    VkPipelineLayout layout = pipeline.get_layout();

    for (const auto& dc : m_drawCalls) {
        VkDescriptorSet matSet = materials.get_set(dc.material);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &matSet, 0, nullptr);

        PushConstantData pushData{};
        pushData.model = dc.model;
        pushData.color = dc.color;
        // Per-material metalness (first pass: metal barriers/rails are metallic,
        // everything else is dielectric). Texture-driven metalness is a follow-up.
        pushData.material.x = (dc.material == MAT_METAL) ? 1.0f : 0.0f;
        // Wettable mask: 1 = rain-exposed road/world, 0 = dry (the whole car). Written
        // to outMaterial.b in gbuffer.frag and used by lighting.frag to keep wet-road
        // effects (darkening, sheen, halos) off the car — they wash the cabin out.
        pushData.material.y = dc.dry ? 0.0f : 1.0f;
        // Roughness multiplier (gbuffer.frag: roughness *= material.z). 1 = no change.
        pushData.material.z = 1.0f;

        // Debug material override for this slot (metalness / roughness / colour).
        if (overrides != nullptr) {
            const MaterialOverride& o = overrides[dc.material];
            if (o.enabled) {
                pushData.color      = Vec4(o.color, dc.color.a);  // keep .a (wash sentinel)
                pushData.material.x = o.metalness;
                pushData.material.z = o.roughnessMul;
            }
        }
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstantData), &pushData);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}

void SceneGeometry::record_depth(VkCommandBuffer cmd, const DepthOnlyPipeline& pipe) const {
    if (!has_geometry())
        return;

    VkBuffer     vbs[] = {m_vertexBuffer.handle()};
    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);

    // Depth-only: no material or descriptor binds — just the per-object model
    // matrix pushed into the depth pipeline's 128-byte block (bytes [64,128)).
    for (const auto& dc : m_drawCalls) {
        pipe.push_model(cmd, dc.model);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}

}  // namespace swish
