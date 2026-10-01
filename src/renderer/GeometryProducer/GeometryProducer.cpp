#include "GeometryProducer.h"

#include "debug/DebugParams.h"
#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/MaterialDescriptors/MaterialDescriptors.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/Renderer/RendererServices.h"
#include "scene/Camera/Camera.h"
#include "utils/geometry/Frustum.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/RenderScope.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace swish {

// ── lifecycle ─────────────────────────────────────────────────────────

void GeometryProducer::init(const PassInitCtx& ctx) {
    const PostProcessManager& pp = *ctx.res.pp;
    VkDevice                  d  = ctx.services.device;

    // Sun shadow-map depth pass — fixed 2048² target owned by PostProcessManager.
    m_depthOnlyPipeline.init(d, {
                                    pp.get_shadow_render_pass(),
                                    {2048, 2048},
                                });

    // Scene pipeline uses dynamic viewport/scissor, so the init extent is cosmetic — pass the render extent
    // (its offscreen target size).
    m_scenePipeline.init(d, {
                                .cameraSetLayout   = ctx.cameraSetLayout,
                                .materialSetLayout = ctx.res.materials->get_layout(),
                                .targetRenderPass  = pp.get_gbuffer_render_pass(),
                                .extent            = ctx.renderExtent,
                                .pipelineCache     = ctx.services.pipelineCache,
                            });
}

void GeometryProducer::cleanup(VkDevice device) {
    m_depthOnlyPipeline.cleanup(device);
    m_scenePipeline.cleanup(device);
}

void GeometryProducer::record(const FrameCtx& c) {
    // Sun shadow map first — depth-only from the sun POV, sampled by the lighting pass. Must precede the
    // G-buffer / lighting passes.
    record_shadow(c);
    record_gbuffer(c);
    transition_gbuffer_for_lighting(c);
}

// ── Pass 0: sun shadow map — CSM depth atlas (NUM_CASCADES slices side by side) ─
// One depth-only render pass over the whole atlas (cleared once). Each cascade is
// drawn into its horizontal sub-rect with its own light-space matrix; the scene
// geometry is drawn once per cascade (viewport-scoped).
void GeometryProducer::record_shadow(const FrameCtx& c) {
    VkCommandBuffer           cmd         = c.cmd;
    const PostProcessManager& pp          = *c.res.pp;
    const VkExtent2D          atlasExtent = pp.get_shadow_atlas_extent();
    const uint32_t            cascadeDim  = pp.get_shadow_cascade_dim();

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};  // far = 1.0 (LESS compare, [0,1] clip-Z)

    vkx::RenderScope pass(cmd, pp.get_shadow_render_pass(), pp.get_shadow_framebuffer(c.frameIndex), atlasExtent,
                          clear);

    float biasConst = DepthOnlyPipeline::kDefaultDepthBiasConst;
    float biasSlope = DepthOnlyPipeline::kDefaultDepthBiasSlope;
#ifdef SWISH_DEBUG_UI
    biasConst = c.tune.depthBiasConst;
    biasSlope = c.tune.depthBiasSlope;
#endif
    m_depthOnlyPipeline.bind(cmd, biasConst, biasSlope);  // pipeline + dynamic depth bias

    // Distance-only cull from the CAMERA: casters beyond the shadow range can't
    // reach any cascade ortho volume, so they're pure waste. No frustum cull here
    // (an off-screen caster can still throw a shadow into view). See record_depth.
    // Params come from the live DebugParams in debug, the same struct's defaults
    // (the shipped look) in release: FrameCtx::tune is that single source.
    const DebugParams& dcull = c.tune;
    CullParams         shadowCull;
    shadowCull.cameraPos   = c.camera->get_position();
    shadowCull.enabled     = dcull.cullEnabled;
    shadowCull.maxDistance = dcull.cullShadowDist;
    shadowCull.useFrustum  = false;

    [[maybe_unused]] CullStats sstats;  // summed across cascades (ratio is per-cascade); read only in debug-UI builds
    CullStats* stats = nullptr;
#ifdef SWISH_DEBUG_UI
    stats = &sstats;
#endif

    for (uint32_t k = 0; k < NUM_CASCADES; ++k) {
        VkRect2D subRect{{static_cast<int32_t>(k * cascadeDim), 0}, {cascadeDim, cascadeDim}};
        m_depthOnlyPipeline.set_cascade(cmd, subRect, m_cascadeVP[k]);
        for_each_static([&](const SceneGeometry& g, const Vec3& offset) {
            g.record_depth(cmd, m_depthOnlyPipeline, shadowCull, offset, stats);
        });
        m_dynamic.record_depth(cmd, m_depthOnlyPipeline, shadowCull, Vec3(0.0f));
    }
#ifdef SWISH_DEBUG_UI
    m_lastShadowCull = sstats;
#endif
}

// ── CSM cascade fit — per-frame light-space matrices + split distances ───────
void GeometryProducer::compute_cascades(const Camera& camera, const Vec3& sunDir, const DebugParams& tune) {
    const float camNear = camera.get_near();
    const float camFar  = camera.get_far();

    // Shadows only matter within a bounded range; cap the far cascade far short of
    // the ~2 km camera far so cascades pack resolution where shadows are visible.
    float shadowFar = 400000.0f;  // ~400 m
    float lambda    = 0.7f;       // practical-split blend (log vs uniform)
#ifdef SWISH_DEBUG_UI
    shadowFar = tune.csmShadowFar;
    lambda    = tune.csmLambda;
#else
    (void)tune;
#endif
    shadowFar              = std::min(shadowFar, camFar);
    const float shadowNear = camNear;

    // Practical split scheme (Zhang et al.): blend logarithmic and uniform splits.
    float splitFar[NUM_CASCADES];
    for (uint32_t i = 0; i < NUM_CASCADES; ++i) {
        float p     = static_cast<float>(i + 1) / static_cast<float>(NUM_CASCADES);
        float logS  = shadowNear * std::pow(shadowFar / shadowNear, p);
        float uniS  = shadowNear + (shadowFar - shadowNear) * p;
        splitFar[i] = lambda * logS + (1.0f - lambda) * uniS;
    }

    // Unproject the camera frustum's near/far corners to world. Reverse-Z: the near
    // plane is at NDC z = 1 and the far plane at z = 0 (swapped vs standard depth).
    const Mat4 invVP = glm::inverse(camera.get_projection_matrix() * camera.get_view_matrix());
    Vec3       nearC[4], farC[4];
    const Vec2 ndc[4] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
    for (int i = 0; i < 4; ++i) {
        Vec4 n   = invVP * Vec4(ndc[i], 1.0f, 1.0f);  // reverse-Z near plane (z = 1)
        Vec4 f   = invVP * Vec4(ndc[i], 0.0f, 1.0f);  // reverse-Z far plane  (z = 0)
        nearC[i] = Vec3(n) / n.w;
        farC[i]  = Vec3(f) / f.w;
    }

    float prevFar = shadowNear;
    for (uint32_t k = 0; k < NUM_CASCADES; ++k) {
        const float tNear = (prevFar - camNear) / (camFar - camNear);
        const float tFar  = (splitFar[k] - camNear) / (camFar - camNear);

        // This cascade's 8 world corners (linear interp along each frustum edge).
        Vec3 corners[8];
        for (int i = 0; i < 4; ++i) {
            const Vec3 edge = farC[i] - nearC[i];
            corners[i]      = nearC[i] + edge * tNear;
            corners[i + 4]  = nearC[i] + edge * tFar;
        }

        Vec3 center(0.0f);
        for (const Vec3& p : corners)
            center += p;
        center /= 8.0f;

        // Bounding-sphere fit → a stable square ortho (less shimmer than an AABB).
        float radius = 0.0f;
        for (const Vec3& p : corners)
            radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius);

        const float margin    = radius;  // pull the near plane back to catch occluders behind the slice
        const Vec3  eye       = center - sunDir * (radius + margin);
        const Mat4  lightView = glm::lookAt(eye, center, Vec3(0.0f, 1.0f, 0.0f));
        const Mat4  lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + margin);

        m_cascadeVP[k]     = lightProj * lightView;
        m_cascadeSplits[k] = splitFar[k];
        prevFar            = splitFar[k];
    }
}

// ── Pass 1: scene → G-buffer ─────────────────────────────────────────
void GeometryProducer::record_gbuffer(const FrameCtx& c) {
    VkCommandBuffer           cmd    = c.cmd;
    const PostProcessManager& pp     = *c.res.pp;
    const VkExtent2D          extent = c.renderExtent;

    std::array<VkClearValue, 5> clear{};
    clear[0].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // albedo
    clear[1].color        = {{0.5f, 0.5f, 1.0f, 0.0f}};  // normal (up = 0,0,1 encoded)
    clear[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};  // material
    clear[3].color        = {{0.5f, 0.5f, 0.0f, 0.0f}};  // clearcoat+flake (oct-encoded up normal, 0 strength/mask)
    clear[4].depthStencil = {0.0f, 0};                   // reverse-Z: far = 0.0 (sky), near = 1.0; GREATER compare

    vkx::RenderScope pass(cmd, pp.get_gbuffer_render_pass(), pp.get_gbuffer_framebuffer(c.frameIndex), extent,
                          clear.data(), static_cast<uint32_t>(clear.size()));

    m_scenePipeline.bind(cmd, extent, c.res.cameraUniforms->get_set(c.frameIndex));

    const MaterialOverride* overrides = nullptr;
#ifdef SWISH_DEBUG_UI
    overrides = c.tune.matOverrides;  // debug per-material editor
#endif
    const Vec3 camPos = c.camera->get_position();

    // Distance + frustum cull. Frustum culling the G-buffer pass never changes the
    // rendered image (off-screen draws contribute no pixels); with the default view
    // distance = camera far, distance culling is a no-op until pulled in for Layer 1.
    // Params: live DebugParams in debug, its defaults (the shipped look) in release.
    const DebugParams& dcull = c.tune;
    CullParams         cull;
    cull.cameraPos   = camPos;
    cull.enabled     = dcull.cullEnabled;
    cull.maxDistance = dcull.cullMainViewDist;
    cull.useFrustum  = dcull.cullFrustum;
    if (cull.useFrustum)
        util::geometry::extract_frustum_planes(c.camera->get_projection_matrix() * c.camera->get_view_matrix(),
                                               cull.planes);

    // Clearcoat + flake global tuning (debug UI global multiplier over the
    // per-material authored defaults; identity in release — see SceneTypes.h
    // PaintTuning / DebugParams::clearcoatMul etc).
    PaintTuning paint;
    float       flakeScale  = 8.0f;
    float       flakeJitter = 0.35f;
#ifdef SWISH_DEBUG_UI
    paint.clearcoatMul    = c.tune.clearcoatMul;
    paint.flakeDensityMul = c.tune.flakeDensityMul;
    flakeScale            = c.tune.flakeScale;
    flakeJitter           = c.tune.flakeJitter;
#endif

    [[maybe_unused]] CullStats stats;  // read only in debug-UI builds
    CullStats* sp = nullptr;
#ifdef SWISH_DEBUG_UI
    sp = &stats;
#endif

    // Authored intro (shifted by the origin rebase), then the endless-road chunks
    // (one canonical mesh instanced at each active slot), interchanges, corridor, then the dynamic car.
    MaterialDescriptors& materials = *c.res.materials;
    for_each_static([&](const SceneGeometry& g, const Vec3& offset) {
        g.record_draws(cmd, m_scenePipeline, materials, overrides, cull, offset, sp, paint, flakeScale, flakeJitter);
    });
    m_dynamic.record_draws(cmd, m_scenePipeline, materials, overrides, cull, Vec3(0.0f), sp, paint, flakeScale,
                           flakeJitter);
#ifdef SWISH_DEBUG_UI
    m_lastMainCull = stats;
#endif
}

// ── G-buffer attachments → SHADER_READ_ONLY for the lighting pass ───
void GeometryProducer::transition_gbuffer_for_lighting(const FrameCtx& c) const {
    const uint32_t f = c.frameIndex;
    for (Img id : {Img::GbAlbedo, Img::GbNormal, Img::GbMaterial, Img::GbClearcoat})
        vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            c.cmd, c.images.image(id, f), VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL>(c.cmd, c.images.image(Img::HdrDepth, f),
                                                                        VK_IMAGE_ASPECT_DEPTH_BIT);
}

// ── scene data ───────────────────────────────────────────────────────

void GeometryProducer::upload_scene(const RendererServices& s, const MeshData& mesh,
                                    const std::vector<DrawCall>& draws) {
    m_scene.upload(s, mesh, draws);
}

void GeometryProducer::destroy_scene(VkDevice device) {
    m_scene.cleanup(device);
    m_chunk.cleanup(device);
    m_interchange.cleanup(device);
    for (SceneGeometry& seg : m_corridorSegments)
        seg.cleanup(device);
    m_corridorSegments.clear();
    m_corridorInstances.clear();
    m_chunkSlotOffsetsZ.clear();
    m_interchangeSlotOffsetsZ.clear();
    m_introOffsetZ = 0.0f;
}

void GeometryProducer::upload_chunk(const RendererServices& s, const MeshData& mesh,
                                    const std::vector<DrawCall>& draws) {
    m_chunk.upload(s, mesh, draws);
}

void GeometryProducer::set_road_chunks(float introOffsetZ, const std::vector<float>& slotOffsetsZ) {
    m_introOffsetZ      = introOffsetZ;
    m_chunkSlotOffsetsZ = slotOffsetsZ;
}

void GeometryProducer::upload_interchange(const RendererServices& s, const MeshData& mesh,
                                          const std::vector<DrawCall>& draws) {
    m_interchange.upload(s, mesh, draws);
}

void GeometryProducer::set_interchanges(const std::vector<float>& slotOffsetsZ) {
    m_interchangeSlotOffsetsZ = slotOffsetsZ;
}

void GeometryProducer::add_corridor_segment(const RendererServices& s, const MeshData& mesh,
                                            const std::vector<DrawCall>& draws) {
    m_corridorSegments.emplace_back();
    m_corridorSegments.back().upload(s, mesh, draws);
}

void GeometryProducer::set_corridor_instances(const std::vector<CorridorInstance>& instances) {
    m_corridorInstances = instances;
}

void GeometryProducer::upload_dynamic(const RendererServices& s, const MeshData& mesh,
                                      const std::vector<DrawCall>& draws) {
    m_dynamic.upload(s, mesh, draws);
}

}  // namespace swish
