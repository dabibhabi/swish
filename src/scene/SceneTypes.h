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

static constexpr uint32_t NUM_CASCADES = 3;

struct CameraUBO {
    Mat4 view;
    Mat4 proj;
    Vec4 camPos;    // xyz = camera world position, w = wetness (set_wetness)
    Vec4 sunDir;    // xyz = normalized sun direction, w = sun illuminance (lux)
    Vec4 sunColor;  // rgb = sun tint, a = ambient gain
    Vec4
        weather;  // x = clarity (0 overcast .. 1 clear day), y = sky scale (cd/m²), z = pre-exposure, w = metres per WU

    Mat4 cascadeViewProj[NUM_CASCADES];
    Vec4 cascadeSplits;
};

struct MaterialOverride {
    bool  enabled      = false;
    float metalness    = 0.0f;
    float roughnessMul = 1.0f;
    Vec3  color        = Vec3(1.0f);

    float clearcoat    = 0.0f;
    float flakeDensity = 0.0f;
};

inline constexpr float kDefaultClearcoatStrength = 0.35f;
inline constexpr float kDefaultFlakeDensity      = 0.0f;

inline float default_clearcoat_strength(MaterialId mat) {
    return (mat >= MAT_CAR_0 && mat <= MAT_CAR_LAST) ? kDefaultClearcoatStrength : 0.0f;
}
inline float default_flake_density(MaterialId mat) {
    return (mat >= MAT_CAR_0 && mat <= MAT_CAR_LAST) ? kDefaultFlakeDensity : 0.0f;
}

struct PaintTuning {
    float clearcoatMul      = 1.0f;
    float flakeDensityMul   = 1.0f;
    float paintFlakeDensity = 0.0f;  // flake density for body paint (Submesh::is_paint); 0 = off
    // Rain P3 (debug): asphalt UV tile in WU, pushed as extra.w on asphalt draws so gbuffer.frag can recover the
    // lateral position for wheel-track ruts (shaders/common/ruts.glsl). 0 = ruts off (release never sets it).
    float rutUvTile = 0.0f;
};

// ── Point Light System (inspired by rind/Light.cpp) ──────────────────
static constexpr uint32_t MAX_POINT_LIGHTS = 32;

struct PointLightData {
    Vec4 positionRadius;  // xyz = world position, w = influence radius
    Vec4 colorIntensity;  // rgb = color, a = intensity multiplier
};

#ifdef SWISH_DEBUG_UI
// ── Car spot lights (headlights / tail / brake; tasks/todo.md "Car lights") ──
// Debug-only for now: the release LightsUBO layout (and the release lighting.frag)
// is unchanged until the feature is turned on for release as its own step.
static constexpr uint32_t MAX_SPOT_LIGHTS = 40;  // 7 car lights + the nearest street lamps (lighting L6.3)

struct SpotLightData {
    Vec4 positionRange;      // xyz = world position (WU), w = range (WU)
    Vec4 directionCosOuter;  // xyz = unit beam direction, w = cos(outer half-angle)
    Vec4 colorIntensity;     // rgb = color, a = intensity multiplier
    Vec4 params;             // x = cos(inner half-angle), y = 1 for a low-beam cutoff (L6.4), zw = 0
};

// ── Rectangle area lights (car showcase; docs/car-showcase.md) ──
static constexpr uint32_t MAX_RECT_LIGHTS = 16;
struct RectLightData {
    Vec4 centerRange;  // xyz = centre (WU), w = range (WU)
    Vec4 rightHalfW;   // xyz = unit right, w = half width (WU)
    Vec4 upHalfH;      // xyz = unit up, w = half height (WU)
    Vec4 colorNits;    // rgb = colour, a = luminance (cd/m²)
};
#endif

struct LightsUBO {
    PointLightData pointLights[MAX_POINT_LIGHTS];
    glm::uvec4     numPointLights;  // x = count, yzw = 0
#ifdef SWISH_DEBUG_UI
    SpotLightData spotLights[MAX_SPOT_LIGHTS];
    glm::uvec4    numSpotLights;  // x = count, yzw = 0
    RectLightData rectLights[MAX_RECT_LIGHTS];
    glm::uvec4    numRectLights;  // x = count
    Vec4          carBoxCenter;   // xyz = the car body's box centre (WU), w = yaw (radians)
    Vec4          carBoxHalf;     // xyz = half extents along forward / up / right (WU), w = 1 when it occludes
#endif
};

struct LightDesc {
    Vec3  position;
    Vec3  color;
    float intensity;
    float radius;
};

struct SpotLightDesc {
    Vec3  position;   // WU
    Vec3  direction;  // unit beam direction
    Vec3  color;
    float intensity;
    float range;           // WU; the windowed falloff reaches 0 here
    float cosInner;        // full intensity inside this half-angle
    float cosOuter;        // zero outside this half-angle
    bool  cutoff = false;  // low beam: almost no light above the beam axis (headlights, lighting L6.4)
};

// A rectangle area light: a glowing panel. It emits along cross(right, up), lights surfaces softly and appears
// in glossy paint as a rectangle highlight. Luminance in cd/m² (a studio softbox is a few thousand).
struct RectLightDesc {
    Vec3  center;      // WU
    Vec3  right;       // unit
    Vec3  up;          // unit, perpendicular to right
    float halfWidth;   // WU, along right
    float halfHeight;  // WU, along up
    Vec3  color;
    float nits;
    float range;  // WU; the light fades to zero here
};

// The car body as a box, for the grounding shadow under area lights.
struct CarOccluder {
    Vec3  center{0.0f};  // WU
    Vec3  half{0.0f};    // WU, along the car's forward / up / right
    float yawRad  = 0.0f;
    bool  enabled = false;
};

struct Ribbon {
    std::vector<Vec3> pts;
    float             halfWidth = 0.0f;
    int               next      = -1;
    int               branch    = -1;
};

struct SweepFrame {
    Vec3  center;
    Vec3  right;
    float arc = 0.0f;
};

struct CorridorInstance {
    uint32_t segment = 0;
    Vec3     offset  = Vec3(0.0f);
};

//

class MeshData {
public:
    uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(m_indices.size()); }

    const std::vector<Vertex>&   getVertices() const { return m_vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    template <class F>
    void forEachVertex(F&& fn) {
        for (Vertex& v : m_vertices) fn(v);
    }

    bool empty() const { return m_vertices.empty() || m_indices.empty(); }

    void addVertex(const Vertex& v) { m_vertices.push_back(v); }
    void addIndex(uint32_t i) { m_indices.push_back(i); }

private:
    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
};

enum class RigPart : uint8_t { None, NeedleRpm, NeedleSpeed, WiperDriver, WiperPassenger };

struct Submesh {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    MaterialId material = MAT_DEFAULT;
    Vec4       color    = Vec4(1.f, 1.f, 1.f, 1.f);

    // glTF metallicFactor of the source material, in [0,1]. Negative = no asset value
    // (non-glTF geometry), so SceneGeometry falls back to the per-material default.
    float metalness = -1.0f;

    // Lighting L4.3 / L5.2 (debug shaders): body paint gets metallic flakes; a lamp lens or emitter glows when
    // its lamp is on. lens: 0 none, 1 headlight, 2 tail, 3 brake (from the node name, ModelManager).
    bool    is_paint = false;
    uint8_t lens     = 0;
    // Corrected values for this material by name (scene/Entity/CarLooks.h); used only when the car is asked to.
    float fixMetalness    = -1.0f;  // < 0 = no correction
    float fixRoughnessMul = 1.0f;   // multiplier on the roughness texture that reaches the corrected roughness

    bool is_steering_wheel = false;

    Mat4 sw_pivot_frame = Mat4(1.f);

    bool is_glass      = false;
    bool is_windshield = false;

    bool is_interior = false;

    int8_t wheel_corner = -1;
    bool   wheel_spins  = false;

    RigPart rig_part        = RigPart::None;
    Mat4    rig_pivot_frame = Mat4(1.f);
};

struct WheelFrame {
    Mat4  frame     = Mat4(1.f);
    float radius    = 0.34f;
    float spin_sign = 1.f;
};

struct DrawCall {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    Vec4       color;
    Mat4       model;
    MaterialId material;

    bool dry = false;

    Vec3  boundsCenter = Vec3(0.0f);
    float boundsRadius = -1.0f;

    float metalness = -1.0f;  // copied from Submesh::metalness; negative = use the per-material default

    // Lighting L5 / L8 / L4.3. Read only by the debug-build shaders (see PushConstantData::extra).
    float emissiveNits = 0.0f;   // surface luminance in cd/m² (a lamp lens, a street-lamp head); 0 = not emissive
    float retro        = 0.0f;   // 1 = retroreflective (lane markings)
    bool  isPaint      = false;  // car body paint: gets the metallic-flake default
    float roughnessMul = 1.0f;   // per-draw roughness multiplier (material fixes); 1 = the texture's value
    float surfaceWet   = 0.0f;   // 1 = a rain-wet car surface with beaded drops (car showcase)
};

struct PushConstantData {
    Mat4 model;     // 64 B
    Vec4 color;     // 16 B
    Vec4 material;  // 16 B — x = metalness, y = wettable mask, z = roughness mul, w = road tag
    Vec4 paint;
    Vec4 extra;  // x = emissive strength, already pre-exposed; y = retroreflective (0/1); z = wet car surface (0..1).
                 // Debug shaders only
};
static_assert(sizeof(PushConstantData) % 16 == 0, "PushConstantData: push-constant block must be a 16-byte multiple");

inline constexpr uint32_t kPushConstantModelColorSize = static_cast<uint32_t>(sizeof(Mat4) + sizeof(Vec4));
static_assert(kPushConstantModelColorSize % 16 == 0, "model+color push range must be a 16-byte multiple");

}  // namespace swish