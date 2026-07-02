#pragma once

#include "../../scene/SceneTypes.h"
#include "../GpuResource/GpuResource.h"
#include "../Renderer/RendererServices.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

class MaterialDescriptors;
class ScenePipeline;
class DepthOnlyPipeline;

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
    // `overrides` (optional) is a table indexed by MaterialId; when a draw's material
    // has an enabled entry, its metalness / roughness-mult / colour replace the
    // asset values (debug material editor). Pass nullptr for none (release path).
    void record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline, MaterialDescriptors& materials,
                      const MaterialOverride* overrides = nullptr) const;

    // Depth-only pass (shadow map): bind vertex/index buffers, then for each
    // draw call push only the per-object model matrix (no material/descriptor
    // binds) and draw. Caller must have already bound the depth pipeline +
    // pushed lightViewProj via DepthOnlyPipeline::bind. No-op if no geometry.
    void record_depth(VkCommandBuffer cmd, const DepthOnlyPipeline& pipe) const;

    bool has_geometry() const { return static_cast<bool>(m_indexBuffer); }

    // Raw handle accessors for passes that share this geometry but use a
    // different pipeline (e.g. GlassPass, WindshieldRainPass).
    VkBuffer get_vertex_buffer() const { return m_vertexBuffer.handle(); }
    VkBuffer get_index_buffer() const { return m_indexBuffer.handle(); }

    // Replace the stored draw-call list without re-uploading GPU buffers.
    // Used every frame for dynamic objects (e.g. the car) whose vertex data
    // is static but whose model matrices change.
    void update_draw_calls(const std::vector<DrawCall>& draws) { m_drawCalls = draws; }

private:
    GpuBuffer             m_vertexBuffer;  // RAII (VMA)
    GpuBuffer             m_indexBuffer;   // RAII (VMA)
    std::vector<DrawCall> m_drawCalls;
};

}  // namespace swish
