#pragma once

// GeometryProducer (R4): the one place the scene's geometry is drawn into an offscreen target. The pass registry
// calls it first (PassList.def, PRODUCER(Geometry)); its record() produces, in order,
//   1. the CSM shadow atlas   (depth-only, one draw of the scene per cascade),
//   2. the G-buffer           (albedo / normal / material / clearcoat+flake + depth),
//   3. the G-buffer -> SHADER_READ / depth -> DEPTH_STENCIL_READ_ONLY barriers the lighting pass expects.
// It is NOT an ordinary pass: it owns the scene geometry (intro, road chunks, interchanges, corridor segments, the
// dynamic car), the two pipelines that draw it and the per-frame cascade fit, and the Renderer's scene-upload API
// forwards here. Ordinary passes reach the car's buffers (glass, windshield, velocity) through dynamic().

#include "renderer/DepthOnlyPipeline/DepthOnlyPipeline.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/SceneGeometry/SceneGeometry.h"
#include "renderer/ScenePipeline/ScenePipeline.h"
#include "scene/SceneTypes.h"
#include "utils/types/Types.h"

#include <array>
#include <vector>

namespace swish {

struct DebugParams;

class GeometryProducer final : public IRenderPass {
public:
    const char* name() const override { return "Geometry"; }
    // Scene draws into the G-buffer MRTs + scene depth (the shadow atlas is not an `Img`).
    void declare(PassDeclaration& d) const override {
        d.write({Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat, Img::HdrDepth});
    }
    void        init(const PassInitCtx& ctx) override;  // depth-only + G-buffer pipelines
    void        record(const FrameCtx& ctx) override;   // shadow atlas, G-buffer, lighting barriers
    void        cleanup(VkDevice device) override;      // pipelines (scene buffers: destroy_scene / destroy_dynamic)

    // ── CSM ──────────────────────────────────────────────────────────
    // Fit the per-cascade sun light-space matrices + split distances to the camera frustum. Call after the camera
    // jitter is set (the fit unprojects the jittered projection) and before CameraUniforms::set_cascades.
    void compute_cascades(const Camera& camera, const Vec3& sunDir, const DebugParams& tune);
    const std::array<Mat4, NUM_CASCADES>& cascade_vp() const { return m_cascadeVP; }
    const Vec3&                           cascade_splits() const { return m_cascadeSplits; }

    // ── Scene data (the Renderer's upload_* / set_* / destroy_* API forwards here) ──
    void upload_scene(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws);
    // Frees intro, chunk, interchange and corridor buffers and forgets every offset / instance.
    void destroy_scene(VkDevice device);
    void upload_chunk(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_road_chunks(float introOffsetZ, const std::vector<float>& slotOffsetsZ);
    void upload_interchange(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_interchanges(const std::vector<float>& slotOffsetsZ);
    void add_corridor_segment(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws);
    void set_corridor_instances(const std::vector<CorridorInstance>& instances);
    void upload_dynamic(const RendererServices& s, const MeshData& mesh, const std::vector<DrawCall>& draws);
    void update_dynamic_draw_calls(const std::vector<DrawCall>& draws) { m_dynamic.update_draw_calls(draws); }
    void destroy_dynamic(VkDevice device) { m_dynamic.cleanup(device); }

    // The car: shared vertex/index buffers for the glass, windshield and velocity passes.
    const SceneGeometry&         dynamic() const { return m_dynamic; }
    void                         set_glass_draws(const std::vector<DrawCall>& d) { m_glassDraws = d; }
    void                         set_windshield_draws(const std::vector<DrawCall>& d) { m_windshieldDraws = d; }
    const std::vector<DrawCall>& glass_draws() const { return m_glassDraws; }
    const std::vector<DrawCall>& windshield_draws() const { return m_windshieldDraws; }
#ifdef SWISH_DEBUG_UI
    // CarEntity's per-submesh previous-frame model matrices, paired index-for-index with dynamic()'s draw list.
    void                     set_dynamic_prev_models(const std::vector<Mat4>& m) { m_dynamicPrevModels = m; }
    const std::vector<Mat4>& dynamic_prev_models() const { return m_dynamicPrevModels; }
    // Last frame's cull tallies for the debug readout (the Renderer copies them into DebugParams).
    const CullStats& last_shadow_cull() const { return m_lastShadowCull; }
    const CullStats& last_main_cull() const { return m_lastMainCull; }
#endif

private:
    void record_shadow(const FrameCtx& c);
    void record_gbuffer(const FrameCtx& c);
    void transition_gbuffer_for_lighting(const FrameCtx& c) const;

    // Visit every static piece in draw order: intro (shifted by the origin rebase), each chunk slot, each
    // interchange slot, each corridor instance. The dynamic car is drawn separately, after these.
    template <class F>
    void for_each_static(F&& f) const {
        f(m_scene, Vec3(0.0f, 0.0f, m_introOffsetZ));
        for (float slotZ : m_chunkSlotOffsetsZ)
            f(m_chunk, Vec3(0.0f, 0.0f, slotZ));
        for (float slotZ : m_interchangeSlotOffsetsZ)
            f(m_interchange, Vec3(0.0f, 0.0f, slotZ));
        for (const CorridorInstance& ci : m_corridorInstances)
            f(m_corridorSegments[ci.segment], ci.offset);
    }

    ScenePipeline     m_scenePipeline;      // G-buffer (basic.vert + gbuffer.frag)
    DepthOnlyPipeline m_depthOnlyPipeline;  // shadow cascades (depth_only)

    SceneGeometry                 m_scene;    // authored intro
    SceneGeometry                 m_dynamic;  // the car
    SceneGeometry                 m_chunk;    // one canonical road chunk, instanced per slot
    SceneGeometry                 m_interchange;
    std::vector<SceneGeometry>    m_corridorSegments;
    std::vector<CorridorInstance> m_corridorInstances;
    float                         m_introOffsetZ = 0.0f;
    std::vector<float>            m_chunkSlotOffsetsZ;
    std::vector<float>            m_interchangeSlotOffsetsZ;

    std::vector<DrawCall> m_glassDraws;
    std::vector<DrawCall> m_windshieldDraws;

    std::array<Mat4, NUM_CASCADES> m_cascadeVP{};
    Vec3                           m_cascadeSplits{0.0f};

#ifdef SWISH_DEBUG_UI
    std::vector<Mat4> m_dynamicPrevModels;
    CullStats         m_lastShadowCull;
    CullStats         m_lastMainCull;
#endif
};

}  // namespace swish
