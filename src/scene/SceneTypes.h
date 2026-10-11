#pragma once

#include "renderer/Vertex.h"
#include "utils/types/Types.h"

#include <vector>

namespace swish {

// ── Material IDs ──────────────────────────────────────────────────────
enum MaterialId : uint32_t {
    MAT_ASPHALT  = 0,
    MAT_GRASS    = 1,
    MAT_CONCRETE = 2,
    MAT_METAL    = 3,
    MAT_DEFAULT  = 4,
    MAT_RUMBLE   = 5,
    MAT_DIRT     = 6,
    MAT_TREE     = 7,
    MAT_SIGN_0   = 8,
    MAT_SIGN_1   = 9,
    MAT_SIGN_2   = 10,
    MAT_SIGN_3   = 11,
    MAT_SIGN_4   = 12,
    MAT_SIGN_5   = 13,
    MAT_SIGN_6   = 14,
    MAT_SIGN_7   = 15,
    // ── Car material slots (loaded from GLB, one per glTF material) ──
    MAT_CAR_0    = 16,
    MAT_CAR_1    = 17,
    MAT_CAR_2    = 18,
    MAT_CAR_3    = 19,
    MAT_CAR_4    = 20,
    MAT_CAR_5    = 21,
    MAT_CAR_6    = 22,
    MAT_CAR_7    = 23,
    MAT_CAR_8    = 24,
    MAT_CAR_9    = 25,
    MAT_CAR_10   = 26,
    MAT_CAR_11   = 27,
    MAT_CAR_12   = 28,
    MAT_CAR_13   = 29,
    MAT_CAR_14   = 30,
    MAT_CAR_15   = 31,
    MAT_CAR_16   = 32,
    MAT_CAR_17   = 33,
    MAT_CAR_18   = 34,
    MAT_CAR_19   = 35,
    MAT_CAR_20   = 36,
    MAT_CAR_21   = 37,
    MAT_CAR_22   = 38,
    MAT_CAR_23   = 39,
    MAT_CAR_24   = 40,
    MAT_CAR_25   = 41,
    MAT_CAR_26   = 42,
    MAT_CAR_27   = 43,
    MAT_CAR_28   = 44,
    MAT_CAR_29   = 45,
    MAT_CAR_30   = 46,
    MAT_CAR_31   = 47,
    MAT_CAR_LAST = MAT_CAR_31,  // car slots are contiguous MAT_CAR_0..MAT_CAR_LAST
    MAT_COUNT
};

// Number of sun shadow cascades (CSM). Must match NUM_CASCADES in lighting.frag
// and kNumCascades in PostProcessManager. 3 covers near→~400 m with crisp near
// shadows on the 4.2 km road.
static constexpr uint32_t NUM_CASCADES = 3;

// Matches the UBO in basic.vert/basic.frag (set=0, binding=0). Everything past
// `sunColor` is appended at the END so vertex shaders that declare only the prefix
// (basic.vert, rain.vert, glass.vert, windshield_rain.vert stop at sunColor) stay
// layout-compatible; only lighting.frag declares + reads the tail.
struct CameraUBO {
    Mat4 view;
    Mat4 proj;
    Vec4 camPos;    // xyz = camera world position, w = wetness (set_wetness)
    Vec4 sunDir;    // xyz = normalized sun direction, w = intensity
    Vec4 sunColor;  // rgb = sun color, a = ambient strength
    Vec4 weather;   // x = clarity (0 overcast .. 1 clear day), yzw reserved
    // ── CSM: per-cascade light-space view*proj + split far-distances ──
    // cascadeViewProj[i] maps world → that cascade's [0,1] shadow-atlas clip.
    // cascadeSplits.xyz = the view-space far distance (positive, +Z forward) of
    // cascades 0/1/2; a fragment picks the first cascade whose split ≥ its depth.
    // All 16-aligned; only lighting.frag reads these.
    Mat4 cascadeViewProj[NUM_CASCADES];
    Vec4 cascadeSplits;
};

// Per-material debug override (debug UI). One entry per MaterialId; when `enabled`
// the draw for that material uses these instead of its asset values. Defaults are
// no-ops (metalness/roughnessMul applied only when enabled), so release — which
// never passes an override table — is unaffected.
struct MaterialOverride {
    bool  enabled      = false;
    float metalness    = 0.0f;
    float roughnessMul = 1.0f;
    Vec3  color        = Vec3(1.0f);
    // Clearcoat + flake (see PushConstantData.paint / gbuffer.frag / lighting.frag).
    // Same "enabled" gate as the fields above — the material editor's per-slot
    // panel replaces the authored default() below for this slot when on.
    float clearcoat    = 0.0f;
    float flakeDensity = 0.0f;
};

// ── Clearcoat + flake authored defaults (per-MaterialId) ────────────────
// A second, tight near-mirror GGX lobe (clearcoat) plus a stochastic per-pixel
// normal-perturbation sparkle (flake) — see docs/car-asset-pipeline.md §3 and
// tasks/todo.md's "Clearcoat + flake car paint" item. The current GLB loader
// (ModelManager::load_car) assigns MAT_CAR_0..19 by raw glTF material index with
// no semantic tag (paint vs rubber vs interior vs chrome — see car-asset-pipeline.md),
// so every car material slot gets the same modest default; the per-material debug
// editor (DebugParams::matOverrides) is how an individual slot gets tuned once its
// identity is known, and non-car materials (road, signs, etc.) default to zero so
// nothing outside the car is affected. flake defaults to 0 (opt-in) — clearcoat is
// the default "paint" look; flake is a deliberate per-car dial.
inline constexpr float kDefaultClearcoatStrength = 0.35f;
inline constexpr float kDefaultFlakeDensity      = 0.0f;

inline float default_clearcoat_strength(MaterialId mat) {
    return (mat >= MAT_CAR_0 && mat <= MAT_CAR_LAST) ? kDefaultClearcoatStrength : 0.0f;
}
inline float default_flake_density(MaterialId mat) {
    return (mat >= MAT_CAR_0 && mat <= MAT_CAR_LAST) ? kDefaultFlakeDensity : 0.0f;
}

// Global clearcoat/flake multipliers applied on top of the per-material defaults
// above (debug UI, same "global multiplier over an authored table" pattern as
// SSAO/IBL). Identity (1,1) in release, so release output is unaffected; setting
// flakeDensityMul to 0 dials flake off entirely to tune the clearcoat lobe alone
// (the todo.md requirement).
struct PaintTuning {
    float clearcoatMul    = 1.0f;
    float flakeDensityMul = 1.0f;
};

// ── Point Light System (inspired by rind/Light.cpp) ──────────────────
static constexpr uint32_t MAX_POINT_LIGHTS = 32;

struct PointLightData {
    Vec4 positionRadius;  // xyz = world position, w = influence radius
    Vec4 colorIntensity;  // rgb = color, a = intensity multiplier
};

struct LightsUBO {
    PointLightData pointLights[MAX_POINT_LIGHTS];
    glm::uvec4     numPointLights;  // x = count, yzw = 0
};

struct LightDesc {
    Vec3  position;
    Vec3  color;
    float intensity;
    float radius;
};

// ── Ribbon ────────────────────────────────────────────────────────────
// A drivable surface swept along a 3D centreline (interchange-local coords).
// Shared by the geometry builder (emit_ribbon renders it) and the physics
// follower (the car tracks arc-length along `pts`, taking Y/pitch/heading from
// the curve). `next`/`branch` are successor ribbon indices at the far end:
// `next` by default, `branch` when the driver steers toward it; −1 = leave the
// interchange (resume free driving).
struct Ribbon {
    std::vector<Vec3> pts;
    float             halfWidth = 0.0f;
    int               next      = -1;
    int               branch    = -1;
};

// ── SweepFrame ────────────────────────────────────────────────────────
// One station of a swept cross-section, for MeshBuilder::emit_sweep_band /
// emit_sweep_wall (the real-corridor mesh generator, Phase 3a). `right` is
// the unit lateral direction = cross(tangent, up), so a lateral offset t
// lands at `center + right * t`; `arc` is cumulative arc length for UV
// tiling. The frames are computed ONCE per tile and shared by every band in
// the cross-section — if each band re-derived its own tangents, rounding
// would open hairline slivers between adjacent bands.
struct SweepFrame {
    Vec3  center;
    Vec3  right;
    float arc = 0.0f;
};

// ── CorridorInstance ──────────────────────────────────────────────────
// One visible corridor segment for this frame: which uploaded segment mesh to
// bind, and the render-frame translation to draw it at. The scene layer
// (CorridorDriver) computes `offset` = segment origin + origin-rebase shift in
// DOUBLE and hands over the small float result. Segment-local vertices then sit
// under ~6e5 WU (float32 ULP 0.06 WU) instead of the 2.9e7 WU absolute
// coordinates at the corridor's east end (ULP 2 WU).
struct CorridorInstance {
    uint32_t segment = 0;
    Vec3     offset  = Vec3(0.0f);
};

// ── MeshData ──────────────────────────────────────────────────────────
// Encapsulates vertex and index buffers with controlled access.
// Vertices and indices are only added through push methods, ensuring
// the caller always gets correct base indices for quad construction.
class MeshData {
public:
    uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(m_indices.size()); }

    const std::vector<Vertex>&   getVertices() const { return m_vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    // Load-time mesh fixups (e.g. grounding a model): apply `fn(Vertex&)` to every vertex in place. Unlike a mutable
    // vector getter this cannot add/remove vertices, so the indices stay valid.
    template <class F>
    void forEachVertex(F&& fn) {
        for (Vertex& v : m_vertices)
            fn(v);
    }

    bool empty() const { return m_vertices.empty() || m_indices.empty(); }

    void addVertex(const Vertex& v) { m_vertices.push_back(v); }
    void addIndex(uint32_t i) { m_indices.push_back(i); }

private:
    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
};

// ── Submesh ───────────────────────────────────────────────────────────
// One primitive within a ModelEntity. Like DrawCall but without the model
// matrix — that is injected at draw-call generation time from the entity's
// current transform.
struct Submesh {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    MaterialId material = MAT_DEFAULT;
    Vec4       color    = Vec4(1.f, 1.f, 1.f, 1.f);

    bool is_steering_wheel = false;
    // Normalized SteeringWheel_Pivot frame (RootNode-rel, +90° Y, grounded).
    // Steer = sw_pivot_frame * R_local(angle) * inverse(sw_pivot_frame).
    Mat4 sw_pivot_frame = Mat4(1.f);

    // Glass (alphaMode=BLEND) — rendered in the forward transparent pass,
    // not in the G-buffer. is_windshield further marks the main front/side
    // window panes that receive the windshield rain trail effect.
    bool is_glass      = false;
    bool is_windshield = false;

    // Interior cabin geometry (node name contains "Interior") — tinted light gray
    // by CarEntity::get_draw_calls as rain rises, via the gbuffer.frag wash sentinel.
    bool is_interior = false;

    // ── Road-wheel articulation ───────────────────────────────────────
    // Corner index into CarEntity's WheelFrame table (0 FL · 1 FR · 2 BL ·
    // 3 BR, −1 = not wheel-related), assigned by the loader from the piece
    // node's translation. wheel_spins marks tire/rim/disc pieces (rotate
    // with speed); caliper pieces get a corner but wheel_spins = false —
    // they steer with the front uprights and never spin.
    int8_t wheel_corner = -1;
    bool   wheel_spins  = false;
};

// ── WheelFrame ────────────────────────────────────────────────────────
// One road-wheel corner's pivot frame, recovered by the loader (pieces are
// grouped by node ancestry + the four exact piece translations) and given
// the same normalization as sw_pivot_frame: RootNode-relative, +90° Y,
// grounded, scale-stripped. The frame's rotation is the corner's as-baked
// pose — camber tilt (≈1° front / 2° rear) plus the 180° side flip — so
// spin conjugated through it turns about the true axle (no rim wobble).
struct WheelFrame {
    Mat4  frame     = Mat4(1.f);
    float radius    = 0.34f;  // rolling radius (m) = wheel-center height above ground
    float spin_sign = 1.f;    // ±1 so +θ at forward speed rolls the wheel forward
};

// ── DrawCall ──────────────────────────────────────────────────────────
// One draw call = one material applied to a range of indices.
struct DrawCall {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    Vec4       color;
    Mat4       model;
    MaterialId material;
    // Excluded from wet-road (asphalt) effects in lighting.frag. Set for the WHOLE
    // car — the wet-road BRDF (porosity darkening, sky sheen) is tuned for the
    // horizontal road, not car paint or the enclosed cabin; applying it there
    // washed the interior out. Road/world geometry leaves this false (wettable).
    bool dry = false;
    // ── Per-draw bounding sphere (object space), for culling ──────────────
    // Filled by MeshBuilder::pushDrawCall from the draw's index range. A negative
    // radius is the "unbounded — never cull" sentinel: dynamic draws that skip
    // MeshBuilder (the car, loaded models) keep the default and always render.
    // The full-length road surface quads get a huge radius, so they too are never
    // culled — only the thousands of small props are. Center is transformed by
    // `model` at cull time; the sphere is conservative (encloses all 4 verts).
    Vec3  boundsCenter = Vec3(0.0f);
    float boundsRadius = -1.0f;
};

// Matches the push constant block in basic.vert + gbuffer.frag.
// `material.x` is the real per-material metalness [0,1] consumed by gbuffer.frag
// (dielectric = 0); yzw are reserved. A full Vec4 is used (not a bare float) so
// the struct is 96 bytes = a multiple of 16: MoltenVK mis-maps push constants
// whose total size isn't 16-byte aligned (an 80→84 byte change rendered geometry
// black). basic.vert declares the same 96-byte block so vertex/fragment agree.
struct PushConstantData {
    Mat4 model;     // 64 B
    Vec4 color;     // 16 B
    Vec4 material;  // 16 B — x = metalness, y = wettable mask, z = roughness mul, w = road tag
    // Clearcoat + flake (car paint BRDF). x = clearcoat lobe strength [0,1],
    // y = flake density [0,1] (probability a world-space flake cell sparkles),
    // z = flake cell scale (world units), w = flake normal-jitter strength [0,1].
    // z/w are global debug tunables broadcast into every draw's push constants
    // (not per-material) — see SceneGeometry::record_draws. 112 B total (still a
    // 16-byte multiple; MoltenVK requires this).
    Vec4 paint;
};
static_assert(sizeof(PushConstantData) % 16 == 0, "PushConstantData: push-constant block must be a 16-byte multiple");

// Passes that don't consume `material` (glass / windshield) size their push
// range + push to just the model+color prefix (still 16-aligned at 80 B).
inline constexpr uint32_t kPushConstantModelColorSize = static_cast<uint32_t>(sizeof(Mat4) + sizeof(Vec4));
static_assert(kPushConstantModelColorSize % 16 == 0, "model+color push range must be a 16-byte multiple");

}  // namespace swish