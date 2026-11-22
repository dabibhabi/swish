#pragma once

#include "MeshBuilder.h"
#include "RoadConfig.h"
#include "RoadParams.h"
#include "scene/SceneTypes.h"
#include "utils/units/Units.h"

#include <span>
#include <string_view>
#include <vector>

namespace swish {

// RoadScene — Configurable highway geometry generator.
//
// Create an instance, optionally configure dimensions via setters,
// then call generate() to produce the geometry. Defaults model the
// I-495 Long Island Expressway near Jericho, Nassau County.
// ══════════════════════════════════════════════════════════════════════

class RoadScene {
public:
    struct SceneData {
        MeshData               meshData;
        std::vector<DrawCall>  drawCalls;
        std::vector<LightDesc> lights;
    };

    RoadScene();
    explicit RoadScene(const RoadConfig& cfg);

    SceneData generate() const;

    // Build ONE canonical, tileable chunk of the uniform LIE cross-section in
    // chunk-local coords [0, -chunkLen] — grass, both carriageways, median,
    // shoulders, markings, guardrail, sound walls, lamp posts. Repeated at
    // chunkLen intervals by the ChunkManager to make the road endless past the
    // authored intro. Excludes the intro-only, whole-road-anchored features
    // (signs, exit ramp, overpass). The dashed-line cycle is snapped to an exact
    // divisor of chunkLen (road::snap_dash_to_chunk; members untouched) so lane
    // markings tile without a phase jump at seams. `chunkLen` MUST be a multiple of every
    // surface tile size (asphalt/grass/concrete/metal) for seamless UVs.
    SceneData generate_chunk(float chunkLen) const;

    // Build ONE elevated diamond interchange as a standalone mesh (local coords,
    // deck centred at z=0), for the endless region to instance at a sparse cadence.
    SceneData generate_interchange_scene() const;

    // Names of the road elements in the exact order generate() / generate_chunk()
    // emit them (the registry tables in RoadScene.cpp). Draw order is the
    // DrawCall index identity, so tests pin these lists against reordering.
    static std::vector<std::string_view> intro_element_names();
    static std::vector<std::string_view> chunk_element_names();

    // The interchange's DRIVABLE ribbons (local coords, deck at z=0) — the same
    // centrelines the geometry is built from, exposed so the physics can follow
    // them (car Y/pitch/heading from the graded curve). Index 0 = EB on-ramp,
    // 1 = deck, 2 = WB off-ramp, 3 = service-road off-ramp. See Ribbon::next/branch.
    std::vector<Ribbon> interchange_ribbons() const;

    // ── Road dimensions (getters + setters) ───────────────────────
    float get_road_length() const;
    void  set_road_length(float length);

    float get_lane_width() const;
    void  set_lane_width(float width);

    int  get_lane_count() const;
    void set_lane_count(int count);

    float get_shoulder_width_wb() const;
    void  set_shoulder_width_wb(float width);

    float get_shoulder_width_eb() const;
    void  set_shoulder_width_eb(float width);

    float get_grass_extent() const;
    void  set_grass_extent(float extent);

    // ── Barrier dimensions ────────────────────────────────────────
    float get_barrier_width() const;
    void  set_barrier_width(float width);

    float get_barrier_height() const;
    void  set_barrier_height(float height);

    // ── Guardrail dimensions ──────────────────────────────────────
    float get_rail_width() const;
    void  set_rail_width(float width);

    float get_rail_height() const;
    void  set_rail_height(float height);

    // ── Texture tiling ────────────────────────────────────────────
    float get_asphalt_tile() const;
    void  set_asphalt_tile(float tile);

    float get_grass_tile() const;
    void  set_grass_tile(float tile);

    float get_concrete_tile() const;
    void  set_concrete_tile(float tile);

    float get_metal_tile() const;
    void  set_metal_tile(float tile);

    // ── Marking parameters ────────────────────────────────────────
    float get_marking_y_offset() const;
    void  set_marking_y_offset(float offset);

    float get_dash_length() const;
    void  set_dash_length(float length);

    float get_dash_gap() const;
    void  set_dash_gap(float gap);

    // ── Colors ────────────────────────────────────────────────────
    const Vec4& get_shoulder_tint() const;
    void        set_shoulder_tint(const Vec4& tint);

    const Vec4& get_barrier_tint() const;
    void        set_barrier_tint(const Vec4& tint);

    const Vec4& get_rail_tint() const;
    void        set_rail_tint(const Vec4& tint);

    const Vec4& get_white_marking() const;
    void        set_white_marking(const Vec4& color);

    const Vec4& get_yellow_marking() const;
    void        set_yellow_marking(const Vec4& color);

    const Vec4& get_ground_tint() const;
    void        set_ground_tint(const Vec4& color);

    const Vec4& get_grass_tint() const;
    void        set_grass_tint(const Vec4& color);

    const Vec4& get_asphalt_tint() const;
    void        set_asphalt_tint(const Vec4& color);

    const Vec4& get_concrete_tint() const;
    void        set_concrete_tint(const Vec4& color);

    const Vec4& get_metal_tint() const;
    void        set_metal_tint(const Vec4& color);

    const Vec4& get_white_tint() const;
    void        set_white_tint(const Vec4& color);

    const Vec4& get_yellow_tint() const;
    void        set_yellow_tint(const Vec4& color);

    const Vec4& get_black_tint() const;
    void        set_black_tint(const Vec4& color);

private:
    // ── Unit conversion (static — doesn't change per instance) ────
    static constexpr float kFt = util::units::kWuPerFt;

    // ── Normals (static — universal constants) ────────────────────
    static inline const Vec3 kUp    = {0.0f, 1.0f, 0.0f};
    static inline const Vec3 kLeft  = {-1.0f, 0.0f, 0.0f};
    static inline const Vec3 kRight = {1.0f, 0.0f, 0.0f};

    // ── Instance state ────────────────────────────────────────────
    // Road geometry
    float m_road_length;
    float m_lane_width;
    int   m_lane_count;
    float m_shoulder_width_wb;
    float m_shoulder_width_eb;
    float m_grass_extent;

    // Barrier
    float m_barrier_width;
    float m_barrier_height;

    // Guardrail
    float m_rail_width;
    float m_rail_height;

    // Curb
    float m_curb_height;
    float m_curb_width;

    // Texture tiling
    float m_asphalt_tile;
    float m_grass_tile;
    float m_concrete_tile;
    float m_metal_tile;

    // Lane markings
    float m_marking_y_offset;
    float m_dash_length;
    float m_dash_gap;

    // Colors
    Vec4 m_shoulder_tint;
    Vec4 m_barrier_tint;
    Vec4 m_rail_tint;
    Vec4 m_white_marking;
    Vec4 m_yellow_marking;
    Vec4 m_ground_tint;
    Vec4 m_grass_tint;
    Vec4 m_asphalt_tint;
    Vec4 m_concrete_tint;
    Vec4 m_metal_tint;
    Vec4 m_white_tint;
    Vec4 m_yellow_tint;
    Vec4 m_black_tint;

    // ── Precomputed per-generate() layout values ──────────────────
    struct RoadLayout {
        float eb_start;    // eastbound start X (barrier + clearance)
        float road_width;  // total road width (lane_count * lane_width)
        float wb_inner;    // westbound inner edge (-road_width)
    };

    // ── Road-element registry ─────────────────────────────────────
    // generate() and generate_chunk() each walk one ordered table of Elements
    // (RoadScene.cpp). The two orders differ on purpose (intro-only signs /
    // overpass / exit ramp vs the chunk's gapped barrier + service roads) and
    // draw order is the DrawCall identity, so they are two tables, not one
    // table with flags. Adding an element = write generate_x in the topic file,
    // add one line to the table(s).
    struct GenCtx {
        MeshBuilder&            b;
        const RoadLayout&       layout;
        float                   z_near;
        float                   z_far;
        road::DashPattern       dash;
        std::vector<LightDesc>& lights;
        float                   chunk_len;  // 0 for the intro pass
    };
    struct Element {
        std::string_view name;
        void (*fn)(const RoadScene&, const GenCtx&);
    };
    // Adapter for the common (builder, layout, z_near, z_far) generator signature.
    template <void (RoadScene::*Fn)(MeshBuilder&, const RoadLayout&, float, float) const>
    static void layout_gen(const RoadScene& s, const GenCtx& c) {
        (s.*Fn)(c.b, c.layout, c.z_near, c.z_far);
    }
    static std::span<const Element> intro_elements();
    static std::span<const Element> chunk_elements();

    // ── Section generators (const — read instance state only) ─────
    void generate_grass(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_road_surfaces(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_shoulders(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_jersey_barrier(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_guardrail(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_solid_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_dashed_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far,
                                  road::DashPattern dash) const;
    void generate_curbs(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_rumble_strips(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_dirt_strips(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    // Dense tree wall along intro N Marginal (far side + berm) — Street View stand-in
    // for suburban houses. Cross-billboard MAT_TREE quads (alpha-tested in the shader).
    void generate_marginal_trees(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_ambient_occlusion(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_hov_diamonds(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_sign_posts(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_overpass(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    // Continuous 2-lane frontage ("service") roads flanking the mainline on BOTH
    // sides, out beyond the sound barriers in the grass. Full-length spans (asphalt
    // + double-yellow center + white edges), lifted a few WU above the grass to
    // avoid coplanar reverse-Z z-fighting. Used by generate_chunk (endless region);
    // the authored intro keeps its one-off exit-ramp marginal road.
    void generate_service_roads(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;

    // Flat EB↔WB median crossover (Layer 3): a curved connector ribbon spanning the
    // median between the innermost lanes, centred at chunk-local z=zCenter with a
    // ±halfLen footprint. Pairs with a matching gap in the jersey barrier so the car
    // can (guided) cross to the opposite carriageway. Built with emit_ribbon.
    void generate_crossover(MeshBuilder& builder, const RoadLayout& layout, float zCenter, float halfLen) const;

    // Elevated diamond interchange (Layer 4): an over-the-mainline cross-street deck
    // (16.5 ft clearance) on piers, plus four graded on/off ramps built with
    // emit_ribbon that climb from the mainline (y≈0) up to deck level. Built in
    // local coords centred at zCenter; instanced at a sparse cadence in the endless
    // region. Geometry only for now — the ramps aren't drivable yet (needs the car
    // to track a ribbon with grade-driven Y/pitch).
    void generate_interchange(MeshBuilder& builder, const RoadLayout& layout, float zCenter) const;

    // Shared interchange dimensions (local coords, deck at z=0) so the ribbon
    // definition and the geometry builder agree.
    struct IxDims {
        float clearance, deck_depth, deck_top, deck_half, eb_edge, wb_edge, span_right, span_left, rampLen, rw,
            eb_service_x;
    };
    IxDims ix_dims() const;

    void generate_sound_barriers(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_exit_ramp(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const;
    void generate_street_lamps(MeshBuilder& builder, const RoadLayout& layout, std::vector<LightDesc>& lights,
                               float z_near, float z_far) const;
};

}  // namespace swish
