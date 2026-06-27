#include "RoadScene.h"

#include <limits>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// MeshBuilder implementation
// ══════════════════════════════════════════════════════════════════════

void MeshBuilder::pushDrawCall(uint32_t indexOffset, const Vec4& color, MaterialId material) {
    DrawCall dc{};
    dc.indexOffset = indexOffset;
    dc.indexCount  = 6;
    dc.color       = color;
    dc.model       = Mat4(1.0f);
    dc.material    = material;
    m_draws.push_back(dc);
}

void MeshBuilder::addHorizontalQuad(float leftX, float rightX, float y, float zStart, float zEnd, const Vec3& normal,
                                    const Vec4& color, MaterialId material, float tileSize) {
    uint32_t base        = m_mesh.getVertexCount();
    uint32_t indexOffset = m_mesh.getIndexCount();

    Vec2 uv0, uv1, uv2, uv3;
    if (tileSize > 0.0f) {
        uv0 = Vec2(leftX / tileSize, zStart / tileSize);
        uv1 = Vec2(rightX / tileSize, zStart / tileSize);
        uv2 = Vec2(leftX / tileSize, zEnd / tileSize);
        uv3 = Vec2(rightX / tileSize, zEnd / tileSize);
    } else {
        uv0 = Vec2(0.0f, 0.0f);
        uv1 = Vec2(1.0f, 0.0f);
        uv2 = Vec2(0.0f, 1.0f);
        uv3 = Vec2(1.0f, 1.0f);
    }

    // Tangent for horizontal surfaces: along +X direction
    Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_mesh.addVertex({Vec3(leftX, y, zStart), normal, uv0, hTan});
    m_mesh.addVertex({Vec3(rightX, y, zStart), normal, uv1, hTan});
    m_mesh.addVertex({Vec3(leftX, y, zEnd), normal, uv2, hTan});
    m_mesh.addVertex({Vec3(rightX, y, zEnd), normal, uv3, hTan});

    // CCW winding for upward-facing quads in Vulkan clip space
    m_mesh.addIndex(base + 0);
    m_mesh.addIndex(base + 2);
    m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 2);
    m_mesh.addIndex(base + 3);

    pushDrawCall(indexOffset, color, material);
}

void MeshBuilder::addVerticalFace(float x, float height, float zStart, float zEnd, const Vec3& normal,
                                  const Vec4& color, MaterialId material, float tileSize) {
    uint32_t base        = m_mesh.getVertexCount();
    uint32_t indexOffset = m_mesh.getIndexCount();

    Vec2 uvBotFar  = (tileSize > 0.0f) ? Vec2(zStart / tileSize, 0.0f) : Vec2(0.0f, 0.0f);
    Vec2 uvBotNear = (tileSize > 0.0f) ? Vec2(zEnd / tileSize, 0.0f) : Vec2(1.0f, 0.0f);
    Vec2 uvTopFar  = (tileSize > 0.0f) ? Vec2(zStart / tileSize, height / tileSize) : Vec2(0.0f, 1.0f);
    Vec2 uvTopNear = (tileSize > 0.0f) ? Vec2(zEnd / tileSize, height / tileSize) : Vec2(1.0f, 1.0f);

    // Tangent for vertical faces: along Z direction
    Vec4 vTan = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    m_mesh.addVertex({Vec3(x, 0.0f, zStart), normal, uvBotFar, vTan});
    m_mesh.addVertex({Vec3(x, 0.0f, zEnd), normal, uvBotNear, vTan});
    m_mesh.addVertex({Vec3(x, height, zStart), normal, uvTopFar, vTan});
    m_mesh.addVertex({Vec3(x, height, zEnd), normal, uvTopNear, vTan});

    // CCW winding for vertical faces in Vulkan clip space
    bool facesLeft = (normal.x < 0.0f);
    if (facesLeft) {
        m_mesh.addIndex(base + 0);
        m_mesh.addIndex(base + 2);
        m_mesh.addIndex(base + 1);
        m_mesh.addIndex(base + 1);
        m_mesh.addIndex(base + 2);
        m_mesh.addIndex(base + 3);
    } else {
        m_mesh.addIndex(base + 0);
        m_mesh.addIndex(base + 1);
        m_mesh.addIndex(base + 2);
        m_mesh.addIndex(base + 2);
        m_mesh.addIndex(base + 1);
        m_mesh.addIndex(base + 3);
    }

    pushDrawCall(indexOffset, color, material);
}

void MeshBuilder::addDashedLine(float leftX, float rightX, float y, float zStart, float zEnd, float dashLen,
                                float gapLen, const Vec3& normal, const Vec4& color, MaterialId material,
                                float tileSize) {
    float cycle = dashLen + gapLen;
    for (float z = zStart; z + dashLen <= zEnd; z += cycle) {
        addHorizontalQuad(leftX, rightX, y, z, z + dashLen, normal, color, material, tileSize);
    }
}

void MeshBuilder::addSlopedQuad(float leftX, float rightX, float yLeft, float yRight, float zStart, float zEnd,
                                const Vec3& normal, const Vec4& color, MaterialId material, float tileSize) {
    uint32_t base        = m_mesh.getVertexCount();
    uint32_t indexOffset = m_mesh.getIndexCount();

    Vec2 uv0, uv1, uv2, uv3;
    if (tileSize > 0.0f) {
        uv0 = Vec2(leftX / tileSize, zStart / tileSize);
        uv1 = Vec2(rightX / tileSize, zStart / tileSize);
        uv2 = Vec2(leftX / tileSize, zEnd / tileSize);
        uv3 = Vec2(rightX / tileSize, zEnd / tileSize);
    } else {
        uv0 = Vec2(0.0f, 0.0f);
        uv1 = Vec2(1.0f, 0.0f);
        uv2 = Vec2(0.0f, 1.0f);
        uv3 = Vec2(1.0f, 1.0f);
    }

    Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_mesh.addVertex({Vec3(leftX, yLeft, zStart), normal, uv0, hTan});
    m_mesh.addVertex({Vec3(rightX, yRight, zStart), normal, uv1, hTan});
    m_mesh.addVertex({Vec3(leftX, yLeft, zEnd), normal, uv2, hTan});
    m_mesh.addVertex({Vec3(rightX, yRight, zEnd), normal, uv3, hTan});

    // CCW winding for upward-facing quads in Vulkan clip space
    m_mesh.addIndex(base + 0);
    m_mesh.addIndex(base + 2);
    m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 2);
    m_mesh.addIndex(base + 3);

    pushDrawCall(indexOffset, color, material);
}

// ══════════════════════════════════════════════════════════════════════
// RoadScene constructors
// ══════════════════════════════════════════════════════════════════════

static Vec4 to_vec4(const float c[4]) {
    return {c[0], c[1], c[2], c[3]};
}

RoadScene::RoadScene() : RoadScene(load_road_config(CONFIG_DIR "road.bin")) {}

RoadScene::RoadScene(const RoadConfig& cfg)
    : m_road_length(cfg.road_length),
      m_lane_width(cfg.lane_width),
      m_lane_count(cfg.lane_count),
      m_shoulder_width_wb(cfg.shoulder_width_wb),
      m_shoulder_width_eb(cfg.shoulder_width_eb),
      m_grass_extent(cfg.grass_extent),
      m_barrier_width(cfg.barrier_width),
      m_barrier_height(cfg.barrier_height),
      m_rail_width(cfg.rail_width),
      m_rail_height(cfg.rail_height),
      m_curb_height(cfg.curb_height),
      m_curb_width(cfg.curb_width),
      m_asphalt_tile(cfg.asphalt_tile),
      m_grass_tile(cfg.grass_tile),
      m_concrete_tile(cfg.concrete_tile),
      m_metal_tile(cfg.metal_tile),
      m_marking_y_offset(cfg.marking_y_offset),
      m_dash_length(cfg.dash_length),
      m_dash_gap(cfg.dash_gap),
      m_shoulder_tint(to_vec4(cfg.shoulder_tint)),
      m_barrier_tint(to_vec4(cfg.barrier_tint)),
      m_rail_tint(to_vec4(cfg.rail_tint)),
      m_white_marking(to_vec4(cfg.white_marking)),
      m_yellow_marking(to_vec4(cfg.yellow_marking)),
      m_ground_tint(to_vec4(cfg.ground_tint)),
      m_grass_tint(to_vec4(cfg.grass_tint)),
      m_asphalt_tint(to_vec4(cfg.asphalt_tint)),
      m_concrete_tint(to_vec4(cfg.concrete_tint)),
      m_metal_tint(to_vec4(cfg.metal_tint)),
      m_white_tint(to_vec4(cfg.white_tint)),
      m_yellow_tint(to_vec4(cfg.yellow_tint)),
      m_black_tint(to_vec4(cfg.black_tint)) {}

// ══════════════════════════════════════════════════════════════════════
// Getters + Setters
// ══════════════════════════════════════════════════════════════════════

float RoadScene::get_road_length() const {
    return m_road_length;
}
void RoadScene::set_road_length(float length) {
    m_road_length = length;
}

float RoadScene::get_lane_width() const {
    return m_lane_width;
}
void RoadScene::set_lane_width(float width) {
    m_lane_width = width;
}

int RoadScene::get_lane_count() const {
    return m_lane_count;
}
void RoadScene::set_lane_count(int count) {
    m_lane_count = count;
}

float RoadScene::get_shoulder_width_wb() const {
    return m_shoulder_width_wb;
}
void RoadScene::set_shoulder_width_wb(float width) {
    m_shoulder_width_wb = width;
}

float RoadScene::get_shoulder_width_eb() const {
    return m_shoulder_width_eb;
}
void RoadScene::set_shoulder_width_eb(float width) {
    m_shoulder_width_eb = width;
}

float RoadScene::get_grass_extent() const {
    return m_grass_extent;
}
void RoadScene::set_grass_extent(float extent) {
    m_grass_extent = extent;
}

float RoadScene::get_barrier_width() const {
    return m_barrier_width;
}
void RoadScene::set_barrier_width(float width) {
    m_barrier_width = width;
}

float RoadScene::get_barrier_height() const {
    return m_barrier_height;
}
void RoadScene::set_barrier_height(float height) {
    m_barrier_height = height;
}

float RoadScene::get_rail_width() const {
    return m_rail_width;
}
void RoadScene::set_rail_width(float width) {
    m_rail_width = width;
}

float RoadScene::get_rail_height() const {
    return m_rail_height;
}
void RoadScene::set_rail_height(float height) {
    m_rail_height = height;
}

float RoadScene::get_asphalt_tile() const {
    return m_asphalt_tile;
}
void RoadScene::set_asphalt_tile(float tile) {
    m_asphalt_tile = tile;
}

float RoadScene::get_grass_tile() const {
    return m_grass_tile;
}
void RoadScene::set_grass_tile(float tile) {
    m_grass_tile = tile;
}

float RoadScene::get_concrete_tile() const {
    return m_concrete_tile;
}
void RoadScene::set_concrete_tile(float tile) {
    m_concrete_tile = tile;
}

float RoadScene::get_metal_tile() const {
    return m_metal_tile;
}
void RoadScene::set_metal_tile(float tile) {
    m_metal_tile = tile;
}

float RoadScene::get_marking_y_offset() const {
    return m_marking_y_offset;
}
void RoadScene::set_marking_y_offset(float offset) {
    m_marking_y_offset = offset;
}

float RoadScene::get_dash_length() const {
    return m_dash_length;
}
void RoadScene::set_dash_length(float length) {
    m_dash_length = length;
}

float RoadScene::get_dash_gap() const {
    return m_dash_gap;
}
void RoadScene::set_dash_gap(float gap) {
    m_dash_gap = gap;
}

const Vec4& RoadScene::get_shoulder_tint() const {
    return m_shoulder_tint;
}
void RoadScene::set_shoulder_tint(const Vec4& tint) {
    m_shoulder_tint = tint;
}

const Vec4& RoadScene::get_barrier_tint() const {
    return m_barrier_tint;
}
void RoadScene::set_barrier_tint(const Vec4& tint) {
    m_barrier_tint = tint;
}

const Vec4& RoadScene::get_rail_tint() const {
    return m_rail_tint;
}
void RoadScene::set_rail_tint(const Vec4& tint) {
    m_rail_tint = tint;
}

const Vec4& RoadScene::get_white_marking() const {
    return m_white_marking;
}
void RoadScene::set_white_marking(const Vec4& color) {
    m_white_marking = color;
}

const Vec4& RoadScene::get_yellow_marking() const {
    return m_yellow_marking;
}
void RoadScene::set_yellow_marking(const Vec4& color) {
    m_yellow_marking = color;
}

// ══════════════════════════════════════════════════════════════════════
// Section generators
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_grass(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    // WB road left edge is at X = -(lane_count * lane_width + shoulder_width_wb)
    float wb_left = layout.wb_inner - m_shoulder_width_wb;

    // EB road right edge is at X = barrier_width + gap(3ft) + lane_count * lane_width + shoulder_width_eb
    float eb_right = layout.eb_start + layout.road_width + m_shoulder_width_eb;

    builder.addHorizontalQuad(-m_grass_extent, wb_left, 0.0f, z_far, z_near, kUp, m_grass_tint, MAT_GRASS,
                              m_grass_tile);
    builder.addHorizontalQuad(eb_right + m_rail_width, m_grass_extent, 0.0f, z_far, z_near, kUp, m_grass_tint,
                              MAT_GRASS, m_grass_tile);
}

void RoadScene::generate_road_surfaces(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start    = layout.eb_start;
    float crown_slope = RoadConfig::kCrownSlope;

    // HOV lane (i=0) has lighter asphalt (newer repaving) vs darker GP lanes
    Vec4 hov_tint = {m_asphalt_tint.x * 1.25f, m_asphalt_tint.y * 1.25f, m_asphalt_tint.z * 1.25f, 1.0f};

    // ── Eastbound lanes (crown: inside lane highest, slopes down outward) ──
    for (int i = 0; i < m_lane_count; i++) {
        float left  = eb_start + static_cast<float>(i) * m_lane_width;
        float right = left + m_lane_width;

        float y_left  = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width;
        float y_right = crown_slope * static_cast<float>(m_lane_count - i - 1) * m_lane_width;

        Vec4 tint = (i == 0) ? hov_tint : m_asphalt_tint;
        builder.addSlopedQuad(left, right, y_left, y_right, z_far, z_near, kUp, tint, MAT_ASPHALT, m_asphalt_tile);
    }

    // ── Westbound lanes (mirror: inside lane highest, slopes down outward) ──
    for (int i = 0; i < m_lane_count; i++) {
        float right = -(static_cast<float>(i) * m_lane_width);
        float left  = right - m_lane_width;

        float y_right = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width;
        float y_left  = crown_slope * static_cast<float>(m_lane_count - i - 1) * m_lane_width;

        Vec4 tint = (i == 0) ? hov_tint : m_asphalt_tint;
        builder.addSlopedQuad(left, right, y_left, y_right, z_far, z_near, kUp, tint, MAT_ASPHALT, m_asphalt_tile);
    }
}

void RoadScene::generate_shoulders(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start    = layout.eb_start;
    float eb_width    = layout.road_width;
    float crown_slope = RoadConfig::kCrownSlope;

    // WB outer shoulder — slopes away from road
    float wb_inner = layout.wb_inner;
    float wb_outer = wb_inner - m_shoulder_width_wb;
    builder.addSlopedQuad(wb_outer, wb_inner, -crown_slope * m_shoulder_width_wb, 0.0f, z_far, z_near, kUp,
                          m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);

    // EB outer shoulder — split into clean inner zone + dirtier outer zone
    float eb_sh_start = eb_start + eb_width;
    float eb_inner_w  = 6.0f * kFt;                        // inner 6ft (clean)
    float eb_outer_w  = m_shoulder_width_eb - eb_inner_w;  // rest (dirtier)
    Vec4  dirty_tint  = {1.05f, 1.00f, 0.95f, 1.0f};

    if (eb_outer_w > 0.0f) {
        // Inner zone: clean shoulder, slopes away
        builder.addSlopedQuad(eb_sh_start, eb_sh_start + eb_inner_w, 0.0f, -crown_slope * eb_inner_w, z_far, z_near,
                              kUp, m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);
        // Outer zone: dirtier, continues slope
        float y_mid = -crown_slope * eb_inner_w;
        float y_out = -crown_slope * m_shoulder_width_eb;
        builder.addSlopedQuad(eb_sh_start + eb_inner_w, eb_sh_start + m_shoulder_width_eb, y_mid, y_out, z_far, z_near,
                              kUp, dirty_tint, MAT_ASPHALT, m_asphalt_tile);
    } else {
        // Shoulder is <= 6ft, single zone
        builder.addSlopedQuad(eb_sh_start, eb_sh_start + m_shoulder_width_eb, 0.0f, -crown_slope * m_shoulder_width_eb,
                              z_far, z_near, kUp, m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);
    }
}

void RoadScene::generate_jersey_barrier(MeshBuilder& builder, float z_near, float z_far) const {
    // NJ F-shape barrier profile: sloped base, break at 13in, near-vertical upper
    float base_w  = m_barrier_width;   // full width at base (2.67ft)
    float mid_w   = 2.0f * kFt;        // width at break point
    float top_w   = 0.5f * kFt;        // width at top (6in)
    float break_h = 1.08f * kFt;       // break point at 13 inches
    float full_h  = m_barrier_height;  // full height (2.67ft)

    float cx = base_w / 2.0f;  // center X of barrier

    // Lower slope left side: base edge → break point
    float base_left = cx - base_w / 2.0f;  // 0.0
    float mid_left  = cx - mid_w / 2.0f;
    builder.addSlopedQuad(base_left, mid_left, 0.0f, break_h, z_far, z_near, kLeft, m_barrier_tint, MAT_CONCRETE,
                          m_concrete_tile);

    // Lower slope right side
    float base_right = cx + base_w / 2.0f;  // barrier_width
    float mid_right  = cx + mid_w / 2.0f;
    builder.addSlopedQuad(mid_right, base_right, break_h, 0.0f, z_far, z_near, kRight, m_barrier_tint, MAT_CONCRETE,
                          m_concrete_tile);

    // Upper slope left side: break point → top
    float top_left = cx - top_w / 2.0f;
    builder.addSlopedQuad(mid_left, top_left, break_h, full_h, z_far, z_near, kLeft, m_barrier_tint, MAT_CONCRETE,
                          m_concrete_tile);

    // Upper slope right side
    float top_right = cx + top_w / 2.0f;
    builder.addSlopedQuad(top_right, mid_right, full_h, break_h, z_far, z_near, kRight, m_barrier_tint, MAT_CONCRETE,
                          m_concrete_tile);

    // Top cap
    builder.addHorizontalQuad(top_left, top_right, full_h, z_far, z_near, kUp, m_barrier_tint, MAT_CONCRETE,
                              m_concrete_tile);

    // Dark weathering stain at base (road grime/water staining)
    Vec4  stain_tint = {0.45f, 0.42f, 0.38f, 1.0f};
    float stain_h    = 0.5f * kFt;  // 6-inch dark strip at ground level
    builder.addSlopedQuad(base_left, mid_left * 0.7f + base_left * 0.3f, 0.0f, stain_h, z_far, z_near, kLeft,
                          stain_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addSlopedQuad(mid_right * 0.7f + base_right * 0.3f, base_right, stain_h, 0.0f, z_far, z_near, kRight,
                          stain_tint, MAT_CONCRETE, m_concrete_tile);
}

void RoadScene::generate_guardrail(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float rail_x   = layout.eb_start + layout.road_width + m_shoulder_width_eb;

    // ── EB retaining wall (tall concrete wall like in LIE reference photo) ──
    float wall_height = 8.0f * kFt;                   // ~8ft tall retaining wall
    float wall_thick  = 1.0f * kFt;                   // 1ft thick
    Vec4  wall_tint   = {0.70f, 0.68f, 0.65f, 1.0f};  // weathered concrete
    Vec4  wall_dark   = {0.50f, 0.48f, 0.45f, 1.0f};  // darker base staining

    // Main wall face (facing road, -X direction)
    builder.addVerticalFace(rail_x, wall_height, z_far, z_near, kLeft, wall_tint, MAT_CONCRETE, m_concrete_tile);
    // Back face
    builder.addVerticalFace(rail_x + wall_thick, wall_height, z_far, z_near, kRight, wall_dark, MAT_CONCRETE,
                            m_concrete_tile);
    // Top
    builder.addHorizontalQuad(rail_x, rail_x + wall_thick, wall_height, z_far, z_near, kUp, wall_tint, MAT_CONCRETE,
                              m_concrete_tile);

    // Metal chain-link fence on top of wall (simple thin rail)
    float fence_h    = wall_height + 4.0f * kFt;  // 4ft fence above wall
    Vec4  fence_tint = {0.55f, 0.58f, 0.55f, 1.0f};
    builder.addVerticalFace(rail_x, fence_h, z_far, z_near, kLeft, fence_tint, MAT_METAL, m_metal_tile);

    // ── WB guardrail (standard metal W-beam) ──────────────────────
    float wb_rail_x = layout.wb_inner - m_shoulder_width_wb - m_rail_width;

    builder.addHorizontalQuad(wb_rail_x, wb_rail_x + m_rail_width, m_rail_height, z_far, z_near, kUp, m_rail_tint,
                              MAT_METAL, m_metal_tile);
    builder.addVerticalFace(wb_rail_x, m_rail_height, z_far, z_near, kLeft, m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addVerticalFace(wb_rail_x + m_rail_width, m_rail_height, z_far, z_near, kRight, m_rail_tint, MAT_METAL,
                            m_metal_tile);
}

void RoadScene::generate_solid_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start    = layout.eb_start;
    float eb_width    = layout.road_width;
    float line_w      = 0.50f * kFt;  // 6-inch marking width
    float crown_slope = RoadConfig::kCrownSlope;

    // Crown height at each edge
    float eb_inner_y = crown_slope * static_cast<float>(m_lane_count) * m_lane_width + m_marking_y_offset;
    float eb_outer_y = m_marking_y_offset;
    float wb_inner_y = eb_inner_y;

    // Yellow median lines (both sides of barrier)
    builder.addHorizontalQuad(-line_w / 2.0f, line_w / 2.0f, wb_inner_y, z_far, z_near, kUp, m_yellow_marking);
    builder.addHorizontalQuad(m_barrier_width - line_w / 2.0f, m_barrier_width + line_w / 2.0f, eb_inner_y, z_far,
                              z_near, kUp, m_yellow_marking);

    // EB white edge lines
    builder.addHorizontalQuad(eb_start, eb_start + line_w, eb_inner_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(eb_start + eb_width - line_w, eb_start + eb_width, eb_outer_y, z_far, z_near, kUp,
                              m_white_marking);

    // WB white edge lines
    builder.addHorizontalQuad(layout.wb_inner, layout.wb_inner + line_w, eb_outer_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(-line_w, 0.0f, wb_inner_y, z_far, z_near, kUp, m_white_marking);

    // ── HOV double solid white lines (lane 0 / lane 1 boundary) ──
    float hov_gap = 0.33f * kFt;  // 4-inch gap between double lines

    // EB HOV boundary: between lane 0 (HOV) and lane 1 (first GP lane)
    float eb_hov_x = eb_start + m_lane_width;
    float eb_hov_y = crown_slope * static_cast<float>(m_lane_count - 1) * m_lane_width + m_marking_y_offset;
    builder.addHorizontalQuad(eb_hov_x - line_w - hov_gap / 2.0f, eb_hov_x - hov_gap / 2.0f, eb_hov_y, z_far, z_near,
                              kUp, m_white_marking);
    builder.addHorizontalQuad(eb_hov_x + hov_gap / 2.0f, eb_hov_x + hov_gap / 2.0f + line_w, eb_hov_y, z_far, z_near,
                              kUp, m_white_marking);

    // WB HOV boundary: between lane 0 (HOV) and lane 1
    float wb_hov_x = -m_lane_width;
    float wb_hov_y = eb_hov_y;  // symmetric crown height
    builder.addHorizontalQuad(wb_hov_x - line_w - hov_gap / 2.0f, wb_hov_x - hov_gap / 2.0f, wb_hov_y, z_far, z_near,
                              kUp, m_white_marking);
    builder.addHorizontalQuad(wb_hov_x + hov_gap / 2.0f, wb_hov_x + hov_gap / 2.0f + line_w, wb_hov_y, z_far, z_near,
                              kUp, m_white_marking);
}

void RoadScene::generate_dashed_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start    = layout.eb_start;
    float line_w      = 0.33f * kFt;
    float crown_slope = RoadConfig::kCrownSlope;

    // EB lane dividers — skip i=1 (HOV boundary is now solid double white)
    for (int i = 2; i < m_lane_count; i++) {
        float x = eb_start + static_cast<float>(i) * m_lane_width;
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near, m_dash_length, m_dash_gap, kUp,
                              m_white_marking);
    }

    // WB lane dividers — skip i=1 (HOV boundary)
    for (int i = 2; i < m_lane_count; i++) {
        float x = -(static_cast<float>(i) * m_lane_width);
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near, m_dash_length, m_dash_gap, kUp,
                              m_white_marking);
    }
}

// ══════════════════════════════════════════════════════════════════════
// generate()
// ══════════════════════════════════════════════════════════════════════

RoadScene::SceneData RoadScene::generate() const {
    SceneData   scene;
    MeshBuilder builder(scene.meshData, scene.drawCalls);

    float z_near = 0.0f;
    float z_far  = -m_road_length;

    RoadLayout layout{
        m_barrier_width + 3.0f * kFt,
        static_cast<float>(m_lane_count) * m_lane_width,
        -(static_cast<float>(m_lane_count) * m_lane_width),
    };

    generate_grass(builder, layout, z_near, z_far);
    generate_road_surfaces(builder, layout, z_near, z_far);
    generate_shoulders(builder, layout, z_near, z_far);
    generate_jersey_barrier(builder, z_near, z_far);
    generate_guardrail(builder, layout, z_near, z_far);
    generate_solid_markings(builder, layout, z_near, z_far);
    generate_dashed_markings(builder, layout, z_near, z_far);
    generate_curbs(builder, layout, z_near, z_far);
    generate_rumble_strips(builder, layout, z_near, z_far);
    generate_dirt_strips(builder, layout, z_near, z_far);
    generate_ambient_occlusion(builder, layout, z_near, z_far);
    generate_hov_diamonds(builder, layout, z_near, z_far);
    generate_sign_posts(builder, layout, z_near, z_far);
    generate_overpass(builder, layout, z_near, z_far);
    generate_sound_barriers(builder, layout, z_near, z_far);
    generate_exit_ramp(builder, layout, z_near, z_far);
    generate_street_lamps(builder, layout, scene.lights, z_near, z_far);

    return scene;
}

void RoadScene::generate_curbs(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float curb_height = m_curb_height;
    float curb_width  = m_curb_width;

    // ── WB outer curb (at shoulder-grass boundary) ────────────────
    float wb_curb_x = layout.wb_inner - m_shoulder_width_wb;

    builder.addHorizontalQuad(wb_curb_x - curb_width, wb_curb_x, curb_height, z_far, z_near, kUp, m_concrete_tint,
                              MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(wb_curb_x, curb_height, z_far, z_near, kRight, m_concrete_tint, MAT_CONCRETE,
                            m_concrete_tile);
    builder.addVerticalFace(wb_curb_x - curb_width, curb_height, z_far, z_near, kLeft, m_concrete_tint, MAT_CONCRETE,
                            m_concrete_tile);

    // ── EB outer curb (at shoulder-grass boundary, positive X) ────
    float eb_curb_x = layout.eb_start + layout.road_width + m_shoulder_width_eb;

    builder.addHorizontalQuad(eb_curb_x, eb_curb_x + curb_width, curb_height, z_far, z_near, kUp, m_concrete_tint,
                              MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(eb_curb_x, curb_height, z_far, z_near, kLeft, m_concrete_tint, MAT_CONCRETE,
                            m_concrete_tile);
    builder.addVerticalFace(eb_curb_x + curb_width, curb_height, z_far, z_near, kRight, m_concrete_tint, MAT_CONCRETE,
                            m_concrete_tile);
}

void RoadScene::generate_rumble_strips(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float strip_width = 1.0f * kFt;                   // 1 ft wide
    float y_offset    = 1.0f;                         // tiny Y raise for z-fighting
    Vec4  rumble_tint = {0.85f, 0.85f, 0.85f, 1.0f};  // slightly dark

    // WB rumble strip (at inner edge of WB shoulder, next to travel lane)
    float wb_x = layout.wb_inner;
    builder.addHorizontalQuad(wb_x - strip_width, wb_x, y_offset, z_far, z_near, kUp, rumble_tint, MAT_RUMBLE, 300.0f);

    // EB rumble strip (at inner edge of EB shoulder, next to travel lane)
    float eb_right = layout.eb_start + layout.road_width;
    builder.addHorizontalQuad(eb_right, eb_right + strip_width, y_offset, z_far, z_near, kUp, rumble_tint, MAT_RUMBLE,
                              300.0f);
}

// ══════════════════════════════════════════════════════════════════════
// Ambient Occlusion — dark shadow strips at base of vertical structures
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_ambient_occlusion(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float ao_width = 0.5f * kFt;                   // 6 inches wide shadow strip
    float ao_y     = 0.5f;                         // tiny Y raise
    Vec4  ao_tint  = {0.15f, 0.15f, 0.18f, 1.0f};  // very dark blue-black

    // AO at base of jersey barrier (both sides)
    builder.addHorizontalQuad(-ao_width, 0.0f, ao_y, z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);
    builder.addHorizontalQuad(m_barrier_width, m_barrier_width + ao_width, ao_y, z_far, z_near, kUp, ao_tint,
                              MAT_DEFAULT, 0.0f);

    // AO at base of EB guardrail
    float eb_rail  = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_rail - ao_width, eb_rail, ao_y, z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of WB guardrail
    float wb_rail_x = layout.wb_inner - m_shoulder_width_wb;
    builder.addHorizontalQuad(wb_rail_x, wb_rail_x + ao_width, ao_y, z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of WB curb (inner side)
    builder.addHorizontalQuad(wb_rail_x - m_curb_width, wb_rail_x - m_curb_width + ao_width, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of EB curb (inner side)
    float eb_curb_x = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_curb_x - ao_width, eb_curb_x, ao_y, z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════
// HOV Diamond Markings — white diamonds on HOV lane pavement
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_hov_diamonds(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start    = layout.eb_start;
    float crown_slope = RoadConfig::kCrownSlope;

    // Diamond dimensions (MUTCD: ~12ft long x ~4ft wide)
    float diamond_len = 12.0f * kFt;
    float diamond_w   = 4.0f * kFt;
    float line_thick  = 0.50f * kFt;
    float spacing     = 500.0f * kFt;

    // EB HOV lane center (lane 0)
    float eb_cx = eb_start + m_lane_width / 2.0f;
    float eb_y  = crown_slope * (static_cast<float>(m_lane_count) - 0.5f) * m_lane_width + m_marking_y_offset + 1.0f;

    // WB HOV lane center (lane 0)
    float wb_cx = -m_lane_width / 2.0f;
    float wb_y  = eb_y;

    for (float z = z_far + spacing; z < z_near; z += spacing) {
        // EB diamond — vertical bar (tall, thin)
        builder.addHorizontalQuad(eb_cx - line_thick / 2.0f, eb_cx + line_thick / 2.0f, eb_y, z - diamond_len / 2.0f,
                                  z + diamond_len / 2.0f, kUp, m_white_marking);
        // EB diamond — horizontal bar (short, wide)
        builder.addHorizontalQuad(eb_cx - diamond_w / 2.0f, eb_cx + diamond_w / 2.0f, eb_y, z - line_thick / 2.0f,
                                  z + line_thick / 2.0f, kUp, m_white_marking);

        // WB diamond
        builder.addHorizontalQuad(wb_cx - line_thick / 2.0f, wb_cx + line_thick / 2.0f, wb_y, z - diamond_len / 2.0f,
                                  z + diamond_len / 2.0f, kUp, m_white_marking);
        builder.addHorizontalQuad(wb_cx - diamond_w / 2.0f, wb_cx + diamond_w / 2.0f, wb_y, z - line_thick / 2.0f,
                                  z + line_thick / 2.0f, kUp, m_white_marking);
    }

    // ── "I-495" pavement text in EB HOV lane ──
    // Place one every 1000ft, offset from diamonds
    float text_spacing = 1000.0f * kFt;
    float text_w       = 8.0f * kFt;   // 8ft wide
    float text_h       = 16.0f * kFt;  // 16ft long (along Z)
    Vec4  white_tint   = {1.0f, 1.0f, 1.0f, 1.0f};

    for (float z = z_far + text_spacing + spacing / 2.0f; z < z_near; z += text_spacing) {
        // Horizontal quad on road surface with sign_06 texture
        uint32_t base = builder.m_mesh.getVertexCount();
        uint32_t idx  = builder.m_mesh.getIndexCount();

        Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        builder.m_mesh.addVertex({Vec3(eb_cx - text_w / 2.0f, eb_y, z - text_h / 2.0f), kUp, Vec2(0.0f, 0.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx + text_w / 2.0f, eb_y, z - text_h / 2.0f), kUp, Vec2(1.0f, 0.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx - text_w / 2.0f, eb_y, z + text_h / 2.0f), kUp, Vec2(0.0f, 1.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx + text_w / 2.0f, eb_y, z + text_h / 2.0f), kUp, Vec2(1.0f, 1.0f), hTan});

        builder.m_mesh.addIndex(base + 0);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 3);
        builder.pushDrawCall(idx, white_tint, MAT_SIGN_6);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Sign Posts — Textured highway signs with FHWA text
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_sign_posts(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_start   = layout.eb_start;
    float eb_width   = layout.road_width;
    float roadside_x = eb_start + eb_width + m_shoulder_width_eb + 5.0f * kFt;

    float post_width = 0.25f * kFt;
    Vec4  post_tint  = {0.45f, 0.48f, 0.45f, 1.0f};
    Vec4  white_tint = {1.0f, 1.0f, 1.0f, 1.0f};  // texture provides color

    // ── Sign definitions: {z_pos, width, height, bottom, material, is_overhead} ──
    struct SignDef {
        float      z;
        float      w;
        float      h;
        float      bottom;
        MaterialId mat;
        bool       overhead;
    };

    SignDef signs[] = {
        // Roadside: speed limit (first thing driver sees)
        {-150000.0f, 3.0f * kFt, 4.0f * kFt, 6.0f * kFt, MAT_SIGN_4, false},
        // Roadside: mile marker
        {-250000.0f, 2.0f * kFt, 3.0f * kFt, 5.0f * kFt, MAT_SIGN_5, false},
        // Overhead gantry: I-495 EAST
        {-350000.0f, 20.0f * kFt, 6.0f * kFt, 0.0f, MAT_SIGN_0, true},
        // Roadside: EXIT 41B 1 MILE
        {-500000.0f, 8.0f * kFt, 5.0f * kFt, 7.0f * kFt, MAT_SIGN_1, false},
        // Roadside: blue service sign
        {-650000.0f, 6.0f * kFt, 4.0f * kFt, 6.0f * kFt, MAT_SIGN_3, false},
        // Overhead gantry: EXIT 41B 1/2 MILE
        {-800000.0f, 20.0f * kFt, 6.0f * kFt, 0.0f, MAT_SIGN_2, true},
    };

    float gantry_height = 20.0f * kFt;
    float gantry_depth  = 1.0f * kFt;
    float gantry_left   = layout.wb_inner - m_shoulder_width_wb - 3.0f * kFt;
    float gantry_right  = eb_start + eb_width + m_shoulder_width_eb + 3.0f * kFt;
    Vec4  gantry_tint   = {0.50f, 0.52f, 0.50f, 1.0f};

    for (const auto& s : signs) {
        if (s.overhead) {
            // ── Steel truss gantry structure ──
            float beam_h   = 3.0f * kFt;   // truss beam depth (top chord to bottom chord)
            float chord_w  = 0.25f * kFt;  // chord member thickness
            float strut_w  = 0.20f * kFt;  // diagonal strut width
            float top_y    = gantry_height;
            float bottom_y = gantry_height - beam_h;

            // Top chord (horizontal beam at gantry_height)
            builder.addHorizontalQuad(gantry_left, gantry_right, top_y, s.z - chord_w, s.z + chord_w, kUp, gantry_tint,
                                      MAT_METAL, m_metal_tile);
            // Bottom chord
            builder.addHorizontalQuad(gantry_left, gantry_right, bottom_y, s.z - chord_w, s.z + chord_w, kUp,
                                      Vec4(0.40f, 0.42f, 0.40f, 1.0f), MAT_METAL, m_metal_tile);
            // Front face of truss (facing +Z toward driver)
            builder.addVerticalFace(gantry_left, beam_h, s.z + chord_w, s.z + chord_w, kRight, gantry_tint, MAT_METAL,
                                    m_metal_tile);

            // Vertical web members along the truss span (every ~15ft)
            float web_spacing = 15.0f * kFt;
            for (float x = gantry_left; x <= gantry_right; x += web_spacing) {
                // Vertical strut
                builder.addVerticalFace(x, top_y, s.z - strut_w, s.z + strut_w, kLeft, gantry_tint, MAT_METAL,
                                        m_metal_tile);
                builder.addVerticalFace(x, top_y, s.z - strut_w, s.z + strut_w, kRight, gantry_tint, MAT_METAL,
                                        m_metal_tile);
            }

            // Support posts (thicker — two parallel vertical faces per post for depth)
            float post_thick = 1.0f * kFt;
            // Left post
            builder.addVerticalFace(gantry_left, gantry_height, s.z - post_thick, s.z + post_thick, kLeft, gantry_tint,
                                    MAT_METAL, m_metal_tile);
            builder.addVerticalFace(gantry_left, gantry_height, s.z - post_thick, s.z + post_thick, kRight, gantry_tint,
                                    MAT_METAL, m_metal_tile);
            // Left post front/back faces
            builder.addVerticalFace(gantry_left - post_width, gantry_height, s.z - post_thick, s.z + post_thick, kLeft,
                                    gantry_tint, MAT_METAL, m_metal_tile);

            // Right post
            builder.addVerticalFace(gantry_right, gantry_height, s.z - post_thick, s.z + post_thick, kLeft, gantry_tint,
                                    MAT_METAL, m_metal_tile);
            builder.addVerticalFace(gantry_right, gantry_height, s.z - post_thick, s.z + post_thick, kRight,
                                    gantry_tint, MAT_METAL, m_metal_tile);

            // Diagonal cross-bracing on support posts (X-pattern on each post)
            // Left post diagonal: sloped quad from bottom-front to top-back
            float diag_w = 0.15f * kFt;
            float post_h = gantry_height;
            // Left post X-brace front
            builder.addSlopedQuad(gantry_left - diag_w, gantry_left + diag_w, 0.0f, post_h * 0.5f, s.z + post_thick,
                                  s.z + post_thick, kRight, gantry_tint, MAT_METAL, m_metal_tile);
            builder.addSlopedQuad(gantry_left - diag_w, gantry_left + diag_w, post_h * 0.5f, post_h, s.z - post_thick,
                                  s.z - post_thick, kRight, gantry_tint, MAT_METAL, m_metal_tile);
            // Right post X-brace
            builder.addSlopedQuad(gantry_right - diag_w, gantry_right + diag_w, 0.0f, post_h * 0.5f, s.z + post_thick,
                                  s.z + post_thick, kLeft, gantry_tint, MAT_METAL, m_metal_tile);
            builder.addSlopedQuad(gantry_right - diag_w, gantry_right + diag_w, post_h * 0.5f, post_h, s.z - post_thick,
                                  s.z - post_thick, kLeft, gantry_tint, MAT_METAL, m_metal_tile);

            // ── Hanging sign panel with texture ──
            float panel_bottom = gantry_height - 0.5f * kFt - s.h;
            float panel_cx     = eb_start + eb_width / 2.0f;

            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();

            Vec3 panel_normal = Vec3(0.0f, 0.0f, 1.0f);
            Vec4 pTan         = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            builder.m_mesh.addVertex(
                {Vec3(panel_cx - s.w / 2.0f, panel_bottom, s.z + gantry_depth), panel_normal, Vec2(0.0f, 1.0f), pTan});
            builder.m_mesh.addVertex(
                {Vec3(panel_cx + s.w / 2.0f, panel_bottom, s.z + gantry_depth), panel_normal, Vec2(1.0f, 1.0f), pTan});
            builder.m_mesh.addVertex({Vec3(panel_cx - s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth), panel_normal,
                                      Vec2(0.0f, 0.0f), pTan});
            builder.m_mesh.addVertex({Vec3(panel_cx + s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth), panel_normal,
                                      Vec2(1.0f, 0.0f), pTan});

            builder.m_mesh.addIndex(base + 0);
            builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 2);
            builder.m_mesh.addIndex(base + 2);
            builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, white_tint, s.mat);

        } else {
            // ── Roadside sign with post ──
            float post_h = s.bottom + s.h;
            builder.addVerticalFace(roadside_x, post_h, s.z - post_width, s.z + post_width, kRight, post_tint,
                                    MAT_METAL, m_metal_tile);

            // Sign face — facing left toward driver
            float sign_left  = s.z - s.w / 2.0f;
            float sign_right = s.z + s.w / 2.0f;

            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();

            Vec4 hTan        = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            Vec3 sign_normal = Vec3(-1.0f, 0.0f, 0.0f);

            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom, sign_left), sign_normal, Vec2(0.0f, 1.0f), hTan});
            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom, sign_right), sign_normal, Vec2(1.0f, 1.0f), hTan});
            builder.m_mesh.addVertex(
                {Vec3(roadside_x, s.bottom + s.h, sign_left), sign_normal, Vec2(0.0f, 0.0f), hTan});
            builder.m_mesh.addVertex(
                {Vec3(roadside_x, s.bottom + s.h, sign_right), sign_normal, Vec2(1.0f, 0.0f), hTan});

            builder.m_mesh.addIndex(base + 0);
            builder.m_mesh.addIndex(base + 2);
            builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 2);
            builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, white_tint, s.mat);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Overpass — bridge deck crossing over the highway
//
// Places a concrete bridge slab every ~3000ft (roughly every 0.5 mile)
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_overpass(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float bridge_clearance  = 16.5f * kFt;  // 16.5ft clearance (FHWA minimum)
    float bridge_depth      = 3.0f * kFt;   // 3ft thick deck slab
    float bridge_width      = 30.0f * kFt;  // 30ft wide bridge road
    float bridge_span_left  = layout.wb_inner - m_shoulder_width_wb - 10.0f * kFt;
    float bridge_span_right = layout.eb_start + layout.road_width + m_shoulder_width_eb + 10.0f * kFt;
    float bridge_spacing    = 3000.0f * kFt;  // every ~0.5 mile

    Vec4 bridge_top_tint    = {0.75f, 0.73f, 0.70f, 1.0f};  // light concrete
    Vec4 bridge_bottom_tint = {0.55f, 0.53f, 0.50f, 1.0f};  // darker underside
    Vec4 bridge_side_tint   = {0.65f, 0.63f, 0.60f, 1.0f};  // medium sides
    Vec4 railing_tint       = {0.60f, 0.62f, 0.60f, 1.0f};  // concrete railing

    for (float z = z_far + bridge_spacing; z < z_near - bridge_spacing; z += bridge_spacing) {
        float z_left  = z - bridge_width / 2.0f;
        float z_right = z + bridge_width / 2.0f;

        // ── Bridge deck top surface ───────────────────────────────
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right, bridge_clearance + bridge_depth, z_left, z_right,
                                  kUp, bridge_top_tint, MAT_CONCRETE, m_concrete_tile);

        // ── Bridge deck bottom surface (visible from below) ───────
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right, bridge_clearance, z_left, z_right, kUp,
                                  bridge_bottom_tint, MAT_CONCRETE, m_concrete_tile);

        // ── Bridge side walls (near and far faces) ────────────────
        // Near face (facing +Z toward camera)
        builder.addVerticalFace(bridge_span_left, bridge_depth, z_right, z_right, kRight, bridge_side_tint,
                                MAT_CONCRETE, m_concrete_tile);

        // Far face (facing -Z away from camera)
        builder.addVerticalFace(bridge_span_right, bridge_depth, z_left, z_left, kLeft, bridge_side_tint, MAT_CONCRETE,
                                m_concrete_tile);

        // ── Bridge railings (short walls on edges) ────────────────
        float railing_height = 3.5f * kFt;  // 42-inch railing
        float railing_top    = bridge_clearance + bridge_depth + railing_height;

        // Left railing
        builder.addVerticalFace(bridge_span_left, railing_top, z_left, z_right, kLeft, railing_tint, MAT_CONCRETE,
                                m_concrete_tile);
        // Right railing
        builder.addVerticalFace(bridge_span_right, railing_top, z_left, z_right, kRight, railing_tint, MAT_CONCRETE,
                                m_concrete_tile);
    }
}

void RoadScene::generate_dirt_strips(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    // Brown dirt/gravel strip between the shoulder curb and grass (like the real LIE)
    float dirt_width = 5.0f * kFt;                   // 5 ft wide
    Vec4  dirt_tint  = {0.85f, 0.75f, 0.60f, 1.0f};  // warm gravel tint

    // WB dirt strip (between curb and grass)
    float wb_curb_x = layout.wb_inner - m_shoulder_width_wb;
    builder.addHorizontalQuad(wb_curb_x - dirt_width, wb_curb_x, 0.0f, z_far, z_near, kUp, dirt_tint, MAT_DIRT,
                              2000.0f);

    // EB dirt strip (between curb and grass)
    float eb_curb_x = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_curb_x, eb_curb_x + dirt_width, 0.0f, z_far, z_near, kUp, dirt_tint, MAT_DIRT,
                              2000.0f);
}

// ══════════════════════════════════════════════════════════════════════
// Trees — cross-billboard vegetation along both sides of the road
//
// Uses two perpendicular quads per tree (cross-billboard pattern)
// for a 3D appearance from any angle. Alpha testing in the shader
// discards transparent pixels.
// ══════════════════════════════════════════════════════════════════════

// Simple hash for pseudo-random variation per tree (Wang integer hash)
static float tree_hash(int slot) {
    uint32_t x = static_cast<uint32_t>(slot) * 2654435761u;
    x ^= x >> 16;
    return static_cast<float>(x) / static_cast<float>(std::numeric_limits<uint32_t>::max());
}

void RoadScene::generate_trees(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {

    // Tree placement parameters
    float tree_spacing = 20.0f * kFt;  // every 20 feet
    float tree_setback = 25.0f * kFt;  // 25ft behind guardrail
    float base_height  = 35.0f * kFt;  // 35ft base tree height
    float height_var   = 15.0f * kFt;  // ±15ft height variation
    float base_width   = 20.0f * kFt;  // 20ft canopy width
    float width_var    = 8.0f * kFt;   // ±8ft width variation

    // EB tree line position (right side of road)
    float eb_rail_x = layout.eb_start + layout.road_width + m_shoulder_width_eb + m_rail_width;
    float eb_tree_x = eb_rail_x + tree_setback;

    // WB tree line position (left side of road)
    float wb_rail_x = layout.wb_inner - m_shoulder_width_wb - m_rail_width;
    float wb_tree_x = wb_rail_x - tree_setback;

    Vec4 tree_tint   = {1.0f, 1.0f, 1.0f, 1.0f};  // full texture color
    Vec3 face_normal = Vec3(0.0f, 0.0f, 1.0f);

    int slot = 0;
    for (float z = z_far + tree_spacing; z < z_near - tree_spacing; z += tree_spacing, ++slot) {
        float rnd = tree_hash(slot);
        float h   = base_height + height_var * (rnd - 0.5f) * 2.0f;
        float w   = base_width + width_var * (rnd * 0.7f - 0.35f) * 2.0f;

        // Slight X jitter so trees don't look grid-aligned
        float x_jitter = (tree_hash(slot + 10000) - 0.5f) * 10.0f * kFt;

        // Emit two crossed billboard quads centered at (cx, 0, z_pos)
        auto emit_billboard_tree = [&](float cx, float tree_h, float tree_w, float z_pos) {
            // Quad 1: facing along Z axis (visible from road)
            {
                uint32_t base = builder.m_mesh.getVertexCount();
                uint32_t idx  = builder.m_mesh.getIndexCount();
                Vec4     tan  = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
                builder.m_mesh.addVertex({Vec3(cx - tree_w / 2.0f, 0.0f, z_pos), face_normal, Vec2(0.0f, 1.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx + tree_w / 2.0f, 0.0f, z_pos), face_normal, Vec2(1.0f, 1.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx - tree_w / 2.0f, tree_h, z_pos), face_normal, Vec2(0.0f, 0.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx + tree_w / 2.0f, tree_h, z_pos), face_normal, Vec2(1.0f, 0.0f), tan});
                // Both winding orders so visible from both sides
                builder.m_mesh.addIndex(base + 0);
                builder.m_mesh.addIndex(base + 2);
                builder.m_mesh.addIndex(base + 1);
                builder.m_mesh.addIndex(base + 1);
                builder.m_mesh.addIndex(base + 2);
                builder.m_mesh.addIndex(base + 3);
                builder.pushDrawCall(idx, tree_tint, MAT_TREE);
            }

            // Quad 2: perpendicular (facing along X axis)
            {
                uint32_t base        = builder.m_mesh.getVertexCount();
                uint32_t idx         = builder.m_mesh.getIndexCount();
                Vec3     side_normal = Vec3(1.0f, 0.0f, 0.0f);
                Vec4     tan         = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
                builder.m_mesh.addVertex({Vec3(cx, 0.0f, z_pos - tree_w / 2.0f), side_normal, Vec2(0.0f, 1.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx, 0.0f, z_pos + tree_w / 2.0f), side_normal, Vec2(1.0f, 1.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx, tree_h, z_pos - tree_w / 2.0f), side_normal, Vec2(0.0f, 0.0f), tan});
                builder.m_mesh.addVertex({Vec3(cx, tree_h, z_pos + tree_w / 2.0f), side_normal, Vec2(1.0f, 0.0f), tan});
                builder.m_mesh.addIndex(base + 0);
                builder.m_mesh.addIndex(base + 2);
                builder.m_mesh.addIndex(base + 1);
                builder.m_mesh.addIndex(base + 1);
                builder.m_mesh.addIndex(base + 2);
                builder.m_mesh.addIndex(base + 3);
                builder.pushDrawCall(idx, tree_tint, MAT_TREE);
            }
        };

        emit_billboard_tree(eb_tree_x + x_jitter, h, w, z);
        emit_billboard_tree(wb_tree_x + x_jitter, h, w, z);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Sound Barriers — noise reduction panels along the roadside
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_sound_barriers(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    // EB sound barrier sits on top of the retaining wall
    float wall_x    = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    float wall_top  = 8.0f * kFt;  // retaining wall height
    float panel_h   = 6.0f * kFt;  // 6ft tall panels on top of wall
    float panel_top = wall_top + panel_h;
    float panel_w   = 0.5f * kFt;  // panel thickness

    Vec4 panel_tint_front = {0.65f, 0.55f, 0.42f, 1.0f};  // weathered wood tone
    Vec4 panel_tint_back  = {0.55f, 0.48f, 0.38f, 1.0f};  // darker backside
    Vec4 panel_top_tint   = {0.60f, 0.52f, 0.40f, 1.0f};

    // EB panels — concrete posts every 10ft with wood panels between
    float post_spacing = 10.0f * kFt;
    float post_w       = 0.75f * kFt;
    Vec4  post_tint    = {0.65f, 0.63f, 0.60f, 1.0f};  // concrete post

    // Continuous panel face (road side)
    builder.addVerticalFace(wall_x, panel_top, z_far, z_near, kLeft, panel_tint_front, MAT_CONCRETE, m_concrete_tile);
    // Back face
    builder.addVerticalFace(wall_x + panel_w, panel_top, z_far, z_near, kRight, panel_tint_back, MAT_CONCRETE,
                            m_concrete_tile);
    // Top cap
    builder.addHorizontalQuad(wall_x, wall_x + panel_w, panel_top, z_far, z_near, kUp, panel_top_tint, MAT_CONCRETE,
                              m_concrete_tile);

    // Concrete posts at intervals
    for (float z = z_far + post_spacing; z < z_near; z += post_spacing) {
        builder.addVerticalFace(wall_x - 0.1f * kFt, panel_top, z - post_w / 2.0f, z + post_w / 2.0f, kLeft, post_tint,
                                MAT_CONCRETE, m_concrete_tile);
    }

    // WB sound barrier (on the grass side past WB guardrail)
    float wb_rail_x   = layout.wb_inner - m_shoulder_width_wb - m_rail_width;
    float wb_wall_x   = wb_rail_x - 2.0f * kFt;  // 2ft behind WB guardrail
    float wb_wall_top = 10.0f * kFt;

    builder.addVerticalFace(wb_wall_x, wb_wall_top, z_far, z_near, kRight, panel_tint_front, MAT_CONCRETE,
                            m_concrete_tile);
    builder.addVerticalFace(wb_wall_x - panel_w, wb_wall_top, z_far, z_near, kLeft, panel_tint_back, MAT_CONCRETE,
                            m_concrete_tile);
    builder.addHorizontalQuad(wb_wall_x - panel_w, wb_wall_x, wb_wall_top, z_far, z_near, kUp, panel_top_tint,
                              MAT_CONCRETE, m_concrete_tile);

    for (float z = z_far + post_spacing; z < z_near; z += post_spacing) {
        builder.addVerticalFace(wb_wall_x + 0.1f * kFt, wb_wall_top, z - post_w / 2.0f, z + post_w / 2.0f, kRight,
                                post_tint, MAT_CONCRETE, m_concrete_tile);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Exit Ramp — EB exit to N Marginal Road
//
// The ramp branches off the rightmost EB lane, diverges at a shallow
// angle, and connects to a 2-lane surface street (N Marginal Road)
// that runs parallel to the expressway.
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_exit_ramp(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    float eb_right = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    float line_w   = 0.50f * kFt;

    // ── Ramp geometry parameters ─────────────────────────────────
    float ramp_start_z  = -2132.5f * kFt;   // ramp begins diverging (~650m)
    float ramp_end_z    = -2804.0f * kFt;   // ramp fully separated (~855m)
    float decel_start_z = -1804.0f * kFt;   // deceleration lane begins (~550m)

    float ramp_offset = 40.0f * kFt;                // how far right the ramp shifts at the end
    float ramp_width  = 14.0f * kFt;                // single ramp lane width
    float gore_len    = ramp_start_z - ramp_end_z;  // length of diverge

    // ── Deceleration lane (extra lane width that tapers in) ──────
    // From decel_start_z to ramp_start_z: shoulder widens into a full lane
    int   num_decel_segs = 8;
    float decel_len      = decel_start_z - ramp_start_z;
    float seg_len        = decel_len / static_cast<float>(num_decel_segs);

    for (int i = 0; i < num_decel_segs; i++) {
        float z0 = decel_start_z - static_cast<float>(i) * seg_len;
        float z1 = z0 - seg_len;
        float t0 = static_cast<float>(i) / static_cast<float>(num_decel_segs);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_decel_segs);

        // Taper: left edge stays at road edge, right edge widens
        float left   = eb_right;
        float right0 = eb_right + t0 * ramp_width;
        float right1 = eb_right + t1 * ramp_width;

        // Approximate with a quad per segment (right edge shifts outward)
        uint32_t base = builder.m_mesh.getVertexCount();
        uint32_t idx  = builder.m_mesh.getIndexCount();
        Vec4     hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.m_mesh.addVertex({Vec3(left, 0.0f, z0), kUp, Vec2(left / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right0, 0.0f, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex({Vec3(left, 0.0f, z1), kUp, Vec2(left / m_asphalt_tile, z1 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right1, 0.0f, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan});

        builder.m_mesh.addIndex(base + 0);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 3);
        builder.pushDrawCall(idx, m_asphalt_tint, MAT_ASPHALT);
    }

    // ── Ramp diverging section ───────────────────────────────────
    // From ramp_start_z to ramp_end_z: ramp curves away from mainline
    int   num_ramp_segs = 12;
    float ramp_seg_len  = gore_len / static_cast<float>(num_ramp_segs);

    for (int i = 0; i < num_ramp_segs; i++) {
        float z0 = ramp_start_z - static_cast<float>(i) * ramp_seg_len;
        float z1 = z0 - ramp_seg_len;
        float t0 = static_cast<float>(i) / static_cast<float>(num_ramp_segs);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_ramp_segs);

        // Smooth ease-out curve for the offset
        float off0 = ramp_offset * t0 * t0;  // quadratic ease-in
        float off1 = ramp_offset * t1 * t1;

        float left0  = eb_right + off0;
        float right0 = left0 + ramp_width;
        float left1  = eb_right + off1;
        float right1 = left1 + ramp_width;

        uint32_t base = builder.m_mesh.getVertexCount();
        uint32_t idx  = builder.m_mesh.getIndexCount();
        Vec4     hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.m_mesh.addVertex({Vec3(left0, 0.0f, z0), kUp, Vec2(left0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right0, 0.0f, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex({Vec3(left1, 0.0f, z1), kUp, Vec2(left1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right1, 0.0f, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan});

        builder.m_mesh.addIndex(base + 0);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 3);
        builder.pushDrawCall(idx, m_asphalt_tint, MAT_ASPHALT);

        // White edge lines on ramp
        builder.addHorizontalQuad(left0, left0 + line_w, m_marking_y_offset + 1.0f, z1, z0, kUp, m_white_marking);
        builder.addHorizontalQuad(right0 - line_w, right0, m_marking_y_offset + 1.0f, z1, z0, kUp, m_white_marking);

        // ── Gore area (triangular painted chevron zone) ──────────
        // White solid line separating ramp from mainline
        float gore_x = eb_right + off0;
        builder.addHorizontalQuad(gore_x - line_w, gore_x, m_marking_y_offset + 2.0f, z1, z0, kUp, m_white_marking);
    }

    // ── N Marginal Road (runs parallel to LIE, offset to the right) ──
    // 2-lane local road from ramp_end_z continuing to z_far
    float marginal_offset  = ramp_offset + ramp_width + 10.0f * kFt;  // gap after ramp merges
    float marginal_width   = 12.0f * kFt;                             // one lane each direction
    float marginal_left    = eb_right + marginal_offset;
    float marginal_start_z = ramp_end_z;
    float marginal_end_z   = z_far;

    // Road surface — 2 lanes
    builder.addHorizontalQuad(marginal_left, marginal_left + marginal_width, 0.0f, marginal_end_z, marginal_start_z,
                              kUp, m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);
    builder.addHorizontalQuad(marginal_left + marginal_width, marginal_left + 2.0f * marginal_width, 0.0f,
                              marginal_end_z, marginal_start_z, kUp, m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);

    // Double yellow center line
    float center_x = marginal_left + marginal_width;
    builder.addHorizontalQuad(center_x - line_w / 2.0f - 0.1f * kFt, center_x - 0.1f * kFt, m_marking_y_offset,
                              marginal_end_z, marginal_start_z, kUp, m_yellow_marking);
    builder.addHorizontalQuad(center_x + 0.1f * kFt, center_x + line_w / 2.0f + 0.1f * kFt, m_marking_y_offset,
                              marginal_end_z, marginal_start_z, kUp, m_yellow_marking);

    // White edge lines
    builder.addHorizontalQuad(marginal_left, marginal_left + line_w, m_marking_y_offset, marginal_end_z,
                              marginal_start_z, kUp, m_white_marking);
    builder.addHorizontalQuad(marginal_left + 2.0f * marginal_width - line_w, marginal_left + 2.0f * marginal_width,
                              m_marking_y_offset, marginal_end_z, marginal_start_z, kUp, m_white_marking);

    // Grass between LIE and Marginal Road
    builder.addHorizontalQuad(eb_right + m_rail_width, marginal_left, 0.0f, marginal_end_z, marginal_start_z, kUp,
                              m_grass_tint, MAT_GRASS, m_grass_tile);

    // Grass on far side of Marginal Road
    builder.addHorizontalQuad(marginal_left + 2.0f * marginal_width,
                              marginal_left + 2.0f * marginal_width + 100.0f * kFt, 0.0f, marginal_end_z,
                              marginal_start_z, kUp, m_grass_tint, MAT_GRASS, m_grass_tile);

    // Curb on Marginal Road right edge
    float mr_curb_x = marginal_left + 2.0f * marginal_width;
    builder.addHorizontalQuad(mr_curb_x, mr_curb_x + m_curb_width, m_curb_height, marginal_end_z, marginal_start_z, kUp,
                              m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(mr_curb_x, m_curb_height, marginal_end_z, marginal_start_z, kLeft, m_concrete_tint,
                            MAT_CONCRETE, m_concrete_tile);

    // ── Connecting ramp to Marginal Road ─────────────────────────
    // Short curved connection from ramp end to Marginal Road
    float connect_start_z = ramp_end_z;
    float connect_end_z   = ramp_end_z - 200.0f * kFt;  // 200ft merge
    int   num_connect     = 6;
    float connect_seg     = (connect_start_z - connect_end_z) / static_cast<float>(num_connect);

    float ramp_end_left = eb_right + ramp_offset;

    for (int i = 0; i < num_connect; i++) {
        float z0 = connect_start_z - static_cast<float>(i) * connect_seg;
        float z1 = z0 - connect_seg;
        float t0 = static_cast<float>(i) / static_cast<float>(num_connect);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_connect);

        // Smooth transition from ramp right edge to marginal road left edge
        float left0  = ramp_end_left + t0 * (marginal_left - ramp_end_left);
        float right0 = left0 + ramp_width;
        float left1  = ramp_end_left + t1 * (marginal_left - ramp_end_left);
        float right1 = left1 + ramp_width;

        uint32_t base = builder.m_mesh.getVertexCount();
        uint32_t idx  = builder.m_mesh.getIndexCount();
        Vec4     hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.m_mesh.addVertex({Vec3(left0, 0.0f, z0), kUp, Vec2(left0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right0, 0.0f, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex({Vec3(left1, 0.0f, z1), kUp, Vec2(left1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan});
        builder.m_mesh.addVertex(
            {Vec3(right1, 0.0f, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan});

        builder.m_mesh.addIndex(base + 0);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 2);
        builder.m_mesh.addIndex(base + 3);
        builder.pushDrawCall(idx, m_asphalt_tint, MAT_ASPHALT);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Street Lamps — highway light poles with point light sources
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_street_lamps(MeshBuilder& builder, const RoadLayout& layout, std::vector<LightDesc>& lights,
                                      float z_near, float z_far) const {

    // Lamp placement parameters
    float lamp_spacing = 500.0f * kFt;  // every 500ft (typical highway)
    float pole_height  = 30.0f * kFt;   // 30ft tall highway lamp
    float pole_width   = 0.25f * kFt;   // 3-inch square pole
    float arm_len      = 6.0f * kFt;    // 6ft arm reaching over road
    float arm_h        = 0.20f * kFt;   // arm thickness
    float fixture_w    = 2.0f * kFt;    // fixture width
    float fixture_h    = 0.5f * kFt;    // fixture depth

    Vec4 pole_tint    = {0.55f, 0.58f, 0.55f, 1.0f};  // galvanized steel
    Vec4 fixture_tint = {0.70f, 0.72f, 0.70f, 1.0f};  // lighter fixture

    // EB lamp positions (right side, past retaining wall)
    float eb_pole_x = layout.eb_start + layout.road_width + m_shoulder_width_eb + 2.0f * kFt;
    // Light hangs over the road: arm reaches back toward lanes
    float eb_light_x = eb_pole_x - arm_len;

    // WB lamp positions (left side, past guardrail)
    float wb_pole_x  = layout.wb_inner - m_shoulder_width_wb - m_rail_width - 2.0f * kFt;
    float wb_light_x = wb_pole_x + arm_len;

    uint32_t light_count = 0;

    for (float z = z_far + lamp_spacing; z < z_near; z += lamp_spacing) {
        if (light_count >= MAX_POINT_LIGHTS)
            break;

        // ── EB lamp pole ──
        builder.addVerticalFace(eb_pole_x, pole_height, z - pole_width, z + pole_width, kRight, pole_tint, MAT_METAL,
                                m_metal_tile);
        builder.addVerticalFace(eb_pole_x, pole_height, z - pole_width, z + pole_width, kLeft, pole_tint, MAT_METAL,
                                m_metal_tile);

        // Horizontal arm (reaching back over road)
        builder.addHorizontalQuad(eb_light_x, eb_pole_x, pole_height, z - arm_h, z + arm_h, kUp, pole_tint, MAT_METAL,
                                  m_metal_tile);

        // Light fixture (box at end of arm)
        builder.addHorizontalQuad(eb_light_x - fixture_w / 2.0f, eb_light_x + fixture_w / 2.0f, pole_height - fixture_h,
                                  z - fixture_w / 2.0f, z + fixture_w / 2.0f, kUp, fixture_tint, MAT_DEFAULT, 0.0f);

        // EB point light
        lights.push_back({
            Vec3(eb_light_x, pole_height - fixture_h, z), Vec3(1.0f, 0.9f, 0.7f),  // warm sodium-vapor color
            5.0f,                                                                  // intensity
            50000.0f                                                               // radius (~50m coverage)
        });
        light_count++;

        // ── WB lamp pole (every other one to stay under MAX_POINT_LIGHTS) ──
        if (light_count < MAX_POINT_LIGHTS) {
            builder.addVerticalFace(wb_pole_x, pole_height, z - pole_width, z + pole_width, kRight, pole_tint,
                                    MAT_METAL, m_metal_tile);
            builder.addVerticalFace(wb_pole_x, pole_height, z - pole_width, z + pole_width, kLeft, pole_tint, MAT_METAL,
                                    m_metal_tile);

            builder.addHorizontalQuad(wb_pole_x, wb_light_x, pole_height, z - arm_h, z + arm_h, kUp, pole_tint,
                                      MAT_METAL, m_metal_tile);

            builder.addHorizontalQuad(wb_light_x - fixture_w / 2.0f, wb_light_x + fixture_w / 2.0f,
                                      pole_height - fixture_h, z - fixture_w / 2.0f, z + fixture_w / 2.0f, kUp,
                                      fixture_tint, MAT_DEFAULT, 0.0f);

            lights.push_back({Vec3(wb_light_x, pole_height - fixture_h, z), Vec3(1.0f, 0.9f, 0.7f), 5.0f, 50000.0f});
            light_count++;
        }
    }
}

}  // namespace swish
