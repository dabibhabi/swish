#pragma once

#include "../SceneTypes.h"
#include "RoadConfig.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// MeshBuilder — Encapsulates vertex/index buffer construction.
// ══════════════════════════════════════════════════════════════════════

class MeshBuilder {
public:
    MeshBuilder(MeshData& mesh, std::vector<DrawCall>& draws)
        : m_mesh(mesh), m_draws(draws) {}

    void addHorizontalQuad(float leftX, float rightX, float y, float zStart, float zEnd,
                           const Vec3& normal, const Vec4& color,
                           MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    void addVerticalFace(float x, float height, float zStart, float zEnd,
                         const Vec3& normal, const Vec4& color,
                         MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    void addDashedLine(float leftX, float rightX, float y, float zStart, float zEnd,
                       float dashLen, float gapLen, const Vec3& normal, const Vec4& color,
                       MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    MeshData&              m_mesh;
    std::vector<DrawCall>& m_draws;

    void pushDrawCall(uint32_t indexOffset, const Vec4& color, MaterialId material);

    void addSlopedQuad(float leftX, float rightX, float yLeft, float yRight,
                       float zStart, float zEnd, const Vec3& normal,
                       const Vec4& color, MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);
};

// ══════════════════════════════════════════════════════════════════════
// RoadScene — Configurable highway geometry generator.
//
// Create an instance, optionally configure dimensions via setters,
// then call generate() to produce the geometry. Defaults model the
// I-495 Long Island Expressway near Jericho, Nassau County.
// ══════════════════════════════════════════════════════════════════════

class RoadScene {
public:
    struct SceneData {
        MeshData              meshData;
        std::vector<DrawCall> drawCalls;
    };

    RoadScene();
    explicit RoadScene(const RoadConfig& cfg);

    SceneData generate() const;

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
    static constexpr float kFt = 0.3048f * WORLD_SCALE;

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

    // ── Section generators (const — read instance state only) ─────
    void generate_grass(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_road_surfaces(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_shoulders(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_jersey_barrier(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_guardrail(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_solid_markings(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_dashed_markings(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_curbs(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_rumble_strips(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_dirt_strips(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_trees(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_ambient_occlusion(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_hov_diamonds(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_sign_posts(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_overpass(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_sound_barriers(MeshBuilder& builder, float z_near, float z_far) const;
    void generate_exit_ramp(MeshBuilder& builder, float z_near, float z_far) const;
};

}  // namespace swish
