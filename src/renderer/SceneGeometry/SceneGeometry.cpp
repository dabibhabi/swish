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

// True if this draw's bounding sphere is entirely outside the cull volume.
// Unbounded draws (negative radius — the car, loaded models) are never culled.
bool isCulled(const DrawCall& dc, const CullParams& cull, const Vec3& originOffset) {
    const float r = dc.boundsRadius;
    if (r < 0.0f)
        return false;

    // Center in render-frame world space: model translation (identity for the
    // authored road) plus the per-batch originOffset (chunk-slot / rebase shift),
    // matching how record_* place the geometry. No scale, so `r` needs none.
    const Vec3  c = Vec3(dc.model * Vec4(dc.boundsCenter, 1.0f)) + originOffset;
    const float d = glm::length(c - cull.cameraPos);
    if (d - r > cull.maxDistance)
        return true;

    if (cull.useFrustum) {
        for (const Vec4& pl : cull.planes) {
            // planes point inward; sphere is outside when signed distance < −r.
            if (glm::dot(Vec3(pl), c) + pl.w < -r)
                return true;
        }
    }
    return false;
}

}  // namespace

void SceneGeometry::cleanup(VkDevice /*device*/) {
    m_drawCalls.clear();
    m_indexBuffer.reset();  // RAII (VMA): frees buffer + sub-allocation
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

void SceneGeometry::record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline, MaterialDescriptors& materials,
                                 const MaterialOverride* overrides, const CullParams& cull, Vec3 originOffset,
                                 CullStats* stats) const {
    if (!has_geometry())
        return;

    // Camera-relative rebase (double precision): add the per-batch originOffset (chunk-slot
    // instance / intro rebase shift), then subtract the camera position, so basic.vert renders
    // with the eye at the origin. Done in double so the (objectPos + originOffset − cameraPos)
    // chain keeps full precision even at millions of WU; the small result stores in float32.
    const glm::dvec3 dCam(cull.cameraPos);
    const glm::dvec3 dOrigin(originOffset);

    VkBuffer     vbs[] = {m_vertexBuffer.handle()};
    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);

    VkPipelineLayout layout = pipeline.get_layout();

    for (const auto& dc : m_drawCalls) {
        const bool cullable = dc.boundsRadius >= 0.0f;
        if (stats && cullable)
            ++stats->total;
        if (cull.enabled && cullable && isCulled(dc, cull, originOffset))
            continue;
        if (stats && cullable)
            ++stats->submitted;

        VkDescriptorSet matSet = materials.get_set(dc.material);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &matSet, 0, nullptr);

        PushConstantData pushData{};
        // Rebase the translation column to camera-relative space in double precision.
        glm::dmat4 dModel = glm::dmat4(dc.model);
        dModel[3]         = glm::dvec4(glm::dvec3(dModel[3]) + dOrigin - dCam, dModel[3].w);
        pushData.model    = glm::mat4(dModel);
        pushData.color    = dc.color;
        // Per-material metalness (first pass: metal barriers/rails are metallic,
        // everything else is dielectric). Texture-driven metalness is a follow-up.
        pushData.material.x = (dc.material == MAT_METAL) ? 1.0f : 0.0f;
        // Wettable mask: 1 = rain-exposed road/world, 0 = dry (the whole car). Written
        // to outMaterial.b in gbuffer.frag and used by lighting.frag to keep wet-road
        // effects (darkening, sheen, halos) off the car — they wash the cabin out.
        pushData.material.y = dc.dry ? 0.0f : 1.0f;
        // Roughness multiplier (gbuffer.frag: roughness *= material.z). 1 = no change.
        pushData.material.z = 1.0f;
        // Road tag (gbuffer.frag: outMaterial.a = material.w). 1 = asphalt → drives
        // screen-space puddles in lighting.frag/ssr.frag; everything else (car, grass,
        // barriers, signs) stays 0 so only the road pools water. Release writes it too,
        // but reads it with puddle coverage 0 → no effect (byte-identical).
        pushData.material.w = (dc.material == MAT_ASPHALT) ? 1.0f : 0.0f;

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

void SceneGeometry::record_depth(VkCommandBuffer cmd, const DepthOnlyPipeline& pipe, const CullParams& cull,
                                 Vec3 originOffset, CullStats* stats) const {
    if (!has_geometry())
        return;

    VkBuffer     vbs[] = {m_vertexBuffer.handle()};
    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);

    // Depth-only: no material or descriptor binds — just the per-object model
    // matrix pushed into the depth pipeline's 128-byte block (bytes [64,128)).
    // `cull` here is distance-only (useFrustum should be false — see header).
    // originOffset shifts the model translation (render-frame, kept small by the
    // origin rebase, so plain float32 here stays precise for the shadow projection).
    for (const auto& dc : m_drawCalls) {
        const bool cullable = dc.boundsRadius >= 0.0f;
        if (stats && cullable)
            ++stats->total;
        if (cull.enabled && cullable && isCulled(dc, cull, originOffset))
            continue;
        if (stats && cullable)
            ++stats->submitted;

        Mat4 model = dc.model;
        model[3]   = Vec4(Vec3(model[3]) + originOffset, model[3].w);
        pipe.push_model(cmd, model);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}

}  // namespace swish
