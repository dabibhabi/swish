#pragma once

#include "../renderer/Vertex.h"
#include "../utils/Types.h"

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
    MAT_CAR_0  = 16,
    MAT_CAR_1  = 17,
    MAT_CAR_2  = 18,
    MAT_CAR_3  = 19,
    MAT_CAR_4  = 20,
    MAT_CAR_5  = 21,
    MAT_CAR_6  = 22,
    MAT_CAR_7  = 23,
    MAT_CAR_8  = 24,
    MAT_CAR_9  = 25,
    MAT_CAR_10 = 26,
    MAT_CAR_11 = 27,
    MAT_CAR_12 = 28,
    MAT_CAR_13 = 29,
    MAT_CAR_14 = 30,
    MAT_CAR_15 = 31,
    MAT_CAR_16 = 32,
    MAT_CAR_17 = 33,
    MAT_CAR_18 = 34,
    MAT_CAR_19 = 35,
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

    // Mutable access for load-time mesh fixups (e.g. grounding a model).
    std::vector<Vertex>& getVertices() { return m_vertices; }

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
    bool       dry = false;
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
    Vec4 material;  // 16 B — x = metalness [0,1], yzw reserved  (total 96 B)
};

// Passes that don't consume `material` (glass / windshield) size their push
// range + push to just the model+color prefix (still 16-aligned at 80 B).
inline constexpr uint32_t kPushConstantModelColorSize =
    static_cast<uint32_t>(sizeof(Mat4) + sizeof(Vec4));

}  // namespace swish