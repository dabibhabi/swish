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

// Per-pass culling inputs. A draw is kept if its bounding sphere (see DrawCall)
// is (a) within `maxDistance` of the camera AND (b) inside the frustum (when
// `frustum` is set). Draws with a negative bounds radius are always kept. With
// `enabled == false` the record loops submit every draw (the pre-cull behavior).
struct CullParams {
    Vec3  cameraPos   = Vec3(0.0f);
    float maxDistance = 1e30f;  // skip draws whose (dist − radius) exceeds this
    bool  enabled     = true;
    bool  useFrustum  = false;  // when true, `planes` are valid (G-buffer pass only)
    Vec4  planes[6]{};          // world-space inward frustum planes (xyz = unit normal, w)
};

// Optional last-frame cull tally for the debug readout. `total` counts every
// cull-eligible draw (negative-radius draws are excluded from both counts).
struct CullStats {
    uint32_t submitted = 0;
    uint32_t total     = 0;
};

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
    // `cull.cameraPos` rebases each per-draw model translation to camera-relative space (in
    // double precision) so basic.vert can render with the eye at the origin — avoids the
    // float32 cancellation that made fine geometry swim at large world coords. See basic.vert.
    // The same `cull` also distance/frustum-culls per-draw bounding spheres; pass an optional
    // `stats` to tally submitted-vs-total for the debug readout.
    // `originOffset` is added (in double) to every draw's model translation before the
    // camera-relative rebase — this is how ONE canonical chunk mesh is instanced at many
    // world offsets (the endless road) and how the authored intro is shifted on origin rebase.
    void record_draws(VkCommandBuffer cmd, const ScenePipeline& pipeline, MaterialDescriptors& materials,
                      const MaterialOverride* overrides, const CullParams& cull, Vec3 originOffset = Vec3(0.0f),
                      CullStats* stats = nullptr) const;

    // Depth-only pass (shadow map): bind vertex/index buffers, then for each
    // draw call push only the per-object model matrix (no material/descriptor
    // binds) and draw. Caller must have already bound the depth pipeline +
    // pushed lightViewProj via DepthOnlyPipeline::bind. No-op if no geometry.
    // `cull` distance-culls shadow casters by distance from the CAMERA (not the
    // light): props far beyond the shadow range can't reach any cascade, so they
    // are skipped. Frustum culling is intentionally NOT used here — a caster just
    // outside the view can still cast a shadow into it. Pass optional `stats`.
    // `originOffset` instances the canonical chunk / shifts the intro, as in record_draws.
    void record_depth(VkCommandBuffer cmd, const DepthOnlyPipeline& pipe, const CullParams& cull,
                      Vec3 originOffset = Vec3(0.0f), CullStats* stats = nullptr) const;

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
