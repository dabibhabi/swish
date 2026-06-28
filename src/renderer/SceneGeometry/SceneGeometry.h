#pragma once

#include "../../scene/SceneTypes.h"
#include "../Renderer/RendererServices.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

class MaterialDescriptors;
class ScenePipeline;

// Owns the scene's vertex + index buffers, their backing memory, and the
// per-mesh draw-call list. Replaces the staging-buffer dance and the bind /
// push / draw loop that used to live on Renderer.
//
// The public surface is intentionally narrow: upload (replace whatever's
// currently here), record_draws (encode the bind+draw loop into a command
// buffer), cleanup. No raw buffer getters — by convention, the bind loop
// stays inside this class so vkCmd* calls don't leak across the boundary.
class SceneGeometry {
public:
    SceneGeometry() = default;

    void cleanup(VkDevice device);

    // Uploads vertex + index data through staging buffers and stores the
    // draw list. Replaces any previously-uploaded geometry (cleanup is
    // called internally first).
    void upload(const RendererServices& services, const MeshData& mesh, const std::vector<DrawCall>& draws);

    // Bind vertex/index buffers, then loop over draw calls binding set 1
    // (material) and pushing per-draw constants. Caller must have already
    // bound the pipeline + set 0 (camera) via ScenePipeline::bind. No-op
    // if no geometry has been uploaded.
    void record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline, MaterialDescriptors& materials) const;

    bool has_geometry() const { return m_indexBuffer != VK_NULL_HANDLE; }

    // Raw handle accessors for passes that share this geometry but use a
    // different pipeline (e.g. GlassPass, WindshieldRainPass).
    VkBuffer get_vertex_buffer() const { return m_vertexBuffer; }
    VkBuffer get_index_buffer()  const { return m_indexBuffer; }

    // Replace the stored draw-call list without re-uploading GPU buffers.
    // Used every frame for dynamic objects (e.g. the car) whose vertex data
    // is static but whose model matrices change.
    void update_draw_calls(const std::vector<DrawCall>& draws) { m_drawCalls = draws; }

private:
    VkBuffer              m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory        m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer              m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory        m_indexBufferMemory  = VK_NULL_HANDLE;
    std::vector<DrawCall> m_drawCalls;
};

}  // namespace swish
