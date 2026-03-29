#include "RoadScene.h"

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

void MeshBuilder::addHorizontalQuad(float leftX, float rightX, float y, float zStart, float zEnd,
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

    // Tangent for horizontal surfaces: along +X direction
    Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_mesh.addVertex({Vec3(leftX, y, zStart), normal, uv0, hTan});
    m_mesh.addVertex({Vec3(rightX, y, zStart), normal, uv1, hTan});
    m_mesh.addVertex({Vec3(leftX, y, zEnd), normal, uv2, hTan});
    m_mesh.addVertex({Vec3(rightX, y, zEnd), normal, uv3, hTan});

    // CCW winding for upward-facing quads in Vulkan clip space
    m_mesh.addIndex(base + 0);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 1);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 3);

    pushDrawCall(indexOffset, color, material);
}

void MeshBuilder::addVerticalFace(float x, float height, float zStart, float zEnd,
                                  const Vec3& normal, const Vec4& color, MaterialId material, float tileSize) {
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
        m_mesh.addIndex(base + 0);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 1);
        m_mesh.addIndex(base + 1);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 3);
    } else {
        m_mesh.addIndex(base + 0);  m_mesh.addIndex(base + 1);  m_mesh.addIndex(base + 2);
        m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 1);  m_mesh.addIndex(base + 3);
    }

    pushDrawCall(indexOffset, color, material);
}

void MeshBuilder::addDashedLine(float leftX, float rightX, float y, float zStart, float zEnd,
                                float dashLen, float gapLen, const Vec3& normal, const Vec4& color,
                                MaterialId material, float tileSize) {
    float cycle = dashLen + gapLen;
    for (float z = zStart; z + dashLen <= zEnd; z += cycle) {
        addHorizontalQuad(leftX, rightX, y, z, z + dashLen, normal, color, material, tileSize);
    }
}

void MeshBuilder::addSlopedQuad(float leftX, float rightX, float yLeft, float yRight,
                                float zStart, float zEnd, const Vec3& normal,
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

    Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    m_mesh.addVertex({Vec3(leftX, yLeft, zStart), normal, uv0, hTan});
    m_mesh.addVertex({Vec3(rightX, yRight, zStart), normal, uv1, hTan});
    m_mesh.addVertex({Vec3(leftX, yLeft, zEnd), normal, uv2, hTan});
    m_mesh.addVertex({Vec3(rightX, yRight, zEnd), normal, uv3, hTan});

    // CCW winding for upward-facing quads in Vulkan clip space
    m_mesh.addIndex(base + 0);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 1);
    m_mesh.addIndex(base + 1);  m_mesh.addIndex(base + 2);  m_mesh.addIndex(base + 3);

    pushDrawCall(indexOffset, color, material);
}

// ══════════════════════════════════════════════════════════════════════
// RoadScene constructors
// ══════════════════════════════════════════════════════════════════════

static Vec4 to_vec4(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

RoadScene::RoadScene()
    : RoadScene(load_road_config(CONFIG_DIR "road.bin")) {}

RoadScene::RoadScene(const RoadConfig& cfg)
    : m_road_length(cfg.road_length)
    , m_lane_width(cfg.lane_width)
    , m_lane_count(cfg.lane_count)
    , m_shoulder_width_wb(cfg.shoulder_width_wb)
    , m_shoulder_width_eb(cfg.shoulder_width_eb)
    , m_grass_extent(cfg.grass_extent)
    , m_barrier_width(cfg.barrier_width)
    , m_barrier_height(cfg.barrier_height)
    , m_rail_width(cfg.rail_width)
    , m_rail_height(cfg.rail_height)
    , m_curb_height(cfg.curb_height)
    , m_curb_width(cfg.curb_width)
    , m_asphalt_tile(cfg.asphalt_tile)
    , m_grass_tile(cfg.grass_tile)
    , m_concrete_tile(cfg.concrete_tile)
    , m_metal_tile(cfg.metal_tile)
    , m_marking_y_offset(cfg.marking_y_offset)
    , m_dash_length(cfg.dash_length)
    , m_dash_gap(cfg.dash_gap)
    , m_shoulder_tint(to_vec4(cfg.shoulder_tint))
    , m_barrier_tint(to_vec4(cfg.barrier_tint))
    , m_rail_tint(to_vec4(cfg.rail_tint))
    , m_white_marking(to_vec4(cfg.white_marking))
    , m_yellow_marking(to_vec4(cfg.yellow_marking))
    , m_ground_tint(to_vec4(cfg.ground_tint))
    , m_grass_tint(to_vec4(cfg.grass_tint))
    , m_asphalt_tint(to_vec4(cfg.asphalt_tint))
    , m_concrete_tint(to_vec4(cfg.concrete_tint))
    , m_metal_tint(to_vec4(cfg.metal_tint))
    , m_white_tint(to_vec4(cfg.white_tint))
    , m_yellow_tint(to_vec4(cfg.yellow_tint))
    , m_black_tint(to_vec4(cfg.black_tint)) {}

// ══════════════════════════════════════════════════════════════════════
// Getters + Setters
// ══════════════════════════════════════════════════════════════════════

float RoadScene::get_road_length() const { return m_road_length; }
void  RoadScene::set_road_length(float length) { m_road_length = length; }

float RoadScene::get_lane_width() const { return m_lane_width; }
void  RoadScene::set_lane_width(float width) { m_lane_width = width; }

int  RoadScene::get_lane_count() const { return m_lane_count; }
void RoadScene::set_lane_count(int count) { m_lane_count = count; }

float RoadScene::get_shoulder_width_wb() const { return m_shoulder_width_wb; }
void  RoadScene::set_shoulder_width_wb(float width) { m_shoulder_width_wb = width; }

float RoadScene::get_shoulder_width_eb() const { return m_shoulder_width_eb; }
void  RoadScene::set_shoulder_width_eb(float width) { m_shoulder_width_eb = width; }

float RoadScene::get_grass_extent() const { return m_grass_extent; }
void  RoadScene::set_grass_extent(float extent) { m_grass_extent = extent; }

float RoadScene::get_barrier_width() const { return m_barrier_width; }
void  RoadScene::set_barrier_width(float width) { m_barrier_width = width; }

float RoadScene::get_barrier_height() const { return m_barrier_height; }
void  RoadScene::set_barrier_height(float height) { m_barrier_height = height; }

float RoadScene::get_rail_width() const { return m_rail_width; }
void  RoadScene::set_rail_width(float width) { m_rail_width = width; }

float RoadScene::get_rail_height() const { return m_rail_height; }
void  RoadScene::set_rail_height(float height) { m_rail_height = height; }

float RoadScene::get_asphalt_tile() const { return m_asphalt_tile; }
void  RoadScene::set_asphalt_tile(float tile) { m_asphalt_tile = tile; }

float RoadScene::get_grass_tile() const { return m_grass_tile; }
void  RoadScene::set_grass_tile(float tile) { m_grass_tile = tile; }

float RoadScene::get_concrete_tile() const { return m_concrete_tile; }
void  RoadScene::set_concrete_tile(float tile) { m_concrete_tile = tile; }

float RoadScene::get_metal_tile() const { return m_metal_tile; }
void  RoadScene::set_metal_tile(float tile) { m_metal_tile = tile; }

float RoadScene::get_marking_y_offset() const { return m_marking_y_offset; }
void  RoadScene::set_marking_y_offset(float offset) { m_marking_y_offset = offset; }

float RoadScene::get_dash_length() const { return m_dash_length; }
void  RoadScene::set_dash_length(float length) { m_dash_length = length; }

float RoadScene::get_dash_gap() const { return m_dash_gap; }
void  RoadScene::set_dash_gap(float gap) { m_dash_gap = gap; }

const Vec4& RoadScene::get_shoulder_tint() const { return m_shoulder_tint; }
void        RoadScene::set_shoulder_tint(const Vec4& tint) { m_shoulder_tint = tint; }

const Vec4& RoadScene::get_barrier_tint() const { return m_barrier_tint; }
void        RoadScene::set_barrier_tint(const Vec4& tint) { m_barrier_tint = tint; }

const Vec4& RoadScene::get_rail_tint() const { return m_rail_tint; }
void        RoadScene::set_rail_tint(const Vec4& tint) { m_rail_tint = tint; }

const Vec4& RoadScene::get_white_marking() const { return m_white_marking; }
void        RoadScene::set_white_marking(const Vec4& color) { m_white_marking = color; }

const Vec4& RoadScene::get_yellow_marking() const { return m_yellow_marking; }
void        RoadScene::set_yellow_marking(const Vec4& color) { m_yellow_marking = color; }

// ══════════════════════════════════════════════════════════════════════
// Section generators
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_grass(MeshBuilder& builder, float z_near, float z_far) const {
    // WB road left edge is at X = -(lane_count * lane_width + shoulder_width_wb)
    float wb_left = -(static_cast<float>(m_lane_count) * m_lane_width + m_shoulder_width_wb);

    // EB road right edge is at X = barrier_width + gap(3ft) + lane_count * lane_width + shoulder_width_eb
    float eb_right = m_barrier_width + 3.0f * kFt + static_cast<float>(m_lane_count) * m_lane_width + m_shoulder_width_eb;

    builder.addHorizontalQuad(-m_grass_extent, wb_left, 0.0f, z_far, z_near, kUp,
                              m_grass_tint, MAT_GRASS, m_grass_tile);
    builder.addHorizontalQuad(eb_right + m_rail_width, m_grass_extent, 0.0f, z_far, z_near, kUp,
                              m_grass_tint, MAT_GRASS, m_grass_tile);
}

void RoadScene::generate_road_surfaces(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start    = m_barrier_width + 3.0f * kFt;  // 3ft clearance after barrier
    float crown_slope = RoadConfig::m_crown_slope;

    // HOV lane (i=0) has lighter asphalt (newer repaving) vs darker GP lanes
    Vec4 hov_tint = {m_asphalt_tint.x * 1.25f, m_asphalt_tint.y * 1.25f,
                     m_asphalt_tint.z * 1.25f, 1.0f};

    // ── Eastbound lanes (crown: inside lane highest, slopes down outward) ──
    for (int i = 0; i < m_lane_count; i++) {
        float left  = eb_start + static_cast<float>(i) * m_lane_width;
        float right = left + m_lane_width;

        float y_left  = crown_slope * static_cast<float>(m_lane_count - i)     * m_lane_width;
        float y_right = crown_slope * static_cast<float>(m_lane_count - i - 1) * m_lane_width;

        Vec4 tint = (i == 0) ? hov_tint : m_asphalt_tint;
        builder.addSlopedQuad(left, right, y_left, y_right, z_far, z_near, kUp,
                              tint, MAT_ASPHALT, m_asphalt_tile);
    }

    // ── Westbound lanes (mirror: inside lane highest, slopes down outward) ──
    for (int i = 0; i < m_lane_count; i++) {
        float right = -(static_cast<float>(i) * m_lane_width);
        float left  = right - m_lane_width;

        float y_right = crown_slope * static_cast<float>(m_lane_count - i)     * m_lane_width;
        float y_left  = crown_slope * static_cast<float>(m_lane_count - i - 1) * m_lane_width;

        Vec4 tint = (i == 0) ? hov_tint : m_asphalt_tint;
        builder.addSlopedQuad(left, right, y_left, y_right, z_far, z_near, kUp,
                              tint, MAT_ASPHALT, m_asphalt_tile);
    }
}

void RoadScene::generate_shoulders(MeshBuilder& builder, float z_near, float z_far) const {
    float wb_width    = static_cast<float>(m_lane_count) * m_lane_width;
    float eb_start    = m_barrier_width + 3.0f * kFt;
    float eb_width    = static_cast<float>(m_lane_count) * m_lane_width;
    float crown_slope = RoadConfig::m_crown_slope;

    // WB outer shoulder — slopes away from road
    float wb_inner = -wb_width;
    float wb_outer = wb_inner - m_shoulder_width_wb;
    builder.addSlopedQuad(wb_outer, wb_inner, -crown_slope * m_shoulder_width_wb, 0.0f,
                          z_far, z_near, kUp, m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);

    // EB outer shoulder — split into clean inner zone + dirtier outer zone
    float eb_sh_start = eb_start + eb_width;
    float eb_inner_w  = 6.0f * kFt;   // inner 6ft (clean)
    float eb_outer_w  = m_shoulder_width_eb - eb_inner_w;  // rest (dirtier)
    Vec4  dirty_tint  = {1.05f, 1.00f, 0.95f, 1.0f};

    if (eb_outer_w > 0.0f) {
        // Inner zone: clean shoulder, slopes away
        builder.addSlopedQuad(eb_sh_start, eb_sh_start + eb_inner_w,
                              0.0f, -crown_slope * eb_inner_w,
                              z_far, z_near, kUp, m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);
        // Outer zone: dirtier, continues slope
        float y_mid = -crown_slope * eb_inner_w;
        float y_out = -crown_slope * m_shoulder_width_eb;
        builder.addSlopedQuad(eb_sh_start + eb_inner_w, eb_sh_start + m_shoulder_width_eb,
                              y_mid, y_out,
                              z_far, z_near, kUp, dirty_tint, MAT_ASPHALT, m_asphalt_tile);
    } else {
        // Shoulder is <= 6ft, single zone
        builder.addSlopedQuad(eb_sh_start, eb_sh_start + m_shoulder_width_eb,
                              0.0f, -crown_slope * m_shoulder_width_eb,
                              z_far, z_near, kUp, m_shoulder_tint, MAT_ASPHALT, m_asphalt_tile);
    }
}

void RoadScene::generate_jersey_barrier(MeshBuilder& builder, float z_near, float z_far) const {
    // NJ F-shape barrier profile: sloped base, break at 13in, near-vertical upper
    float base_w  = m_barrier_width;            // full width at base (2.67ft)
    float mid_w   = 2.0f * kFt;                // width at break point
    float top_w   = 0.5f * kFt;                // width at top (6in)
    float break_h = 1.08f * kFt;               // break point at 13 inches
    float full_h  = m_barrier_height;           // full height (2.67ft)

    float cx      = base_w / 2.0f;             // center X of barrier

    // Lower slope left side: base edge → break point
    float base_left = cx - base_w / 2.0f;      // 0.0
    float mid_left  = cx - mid_w / 2.0f;
    builder.addSlopedQuad(base_left, mid_left, 0.0f, break_h, z_far, z_near, kLeft,
                          m_barrier_tint, MAT_CONCRETE, m_concrete_tile);

    // Lower slope right side
    float base_right = cx + base_w / 2.0f;     // barrier_width
    float mid_right  = cx + mid_w / 2.0f;
    builder.addSlopedQuad(mid_right, base_right, break_h, 0.0f, z_far, z_near, kRight,
                          m_barrier_tint, MAT_CONCRETE, m_concrete_tile);

    // Upper slope left side: break point → top
    float top_left = cx - top_w / 2.0f;
    builder.addSlopedQuad(mid_left, top_left, break_h, full_h, z_far, z_near, kLeft,
                          m_barrier_tint, MAT_CONCRETE, m_concrete_tile);

    // Upper slope right side
    float top_right = cx + top_w / 2.0f;
    builder.addSlopedQuad(top_right, mid_right, full_h, break_h, z_far, z_near, kRight,
                          m_barrier_tint, MAT_CONCRETE, m_concrete_tile);

    // Top cap
    builder.addHorizontalQuad(top_left, top_right, full_h, z_far, z_near, kUp,
                              m_barrier_tint, MAT_CONCRETE, m_concrete_tile);

    // Dark weathering stain at base (road grime/water staining)
    Vec4 stain_tint = {0.45f, 0.42f, 0.38f, 1.0f};
    float stain_h   = 0.5f * kFt;  // 6-inch dark strip at ground level
    builder.addSlopedQuad(base_left, mid_left * 0.7f + base_left * 0.3f,
                          0.0f, stain_h, z_far, z_near, kLeft,
                          stain_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addSlopedQuad(mid_right * 0.7f + base_right * 0.3f, base_right,
                          stain_h, 0.0f, z_far, z_near, kRight,
                          stain_tint, MAT_CONCRETE, m_concrete_tile);
}

void RoadScene::generate_guardrail(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start = m_barrier_width + 3.0f * kFt;
    float eb_width = static_cast<float>(m_lane_count) * m_lane_width;
    float rail_x   = eb_start + eb_width + m_shoulder_width_eb;

    // ── EB retaining wall (tall concrete wall like in LIE reference photo) ──
    float wall_height = 8.0f * kFt;    // ~8ft tall retaining wall
    float wall_thick  = 1.0f * kFt;    // 1ft thick
    Vec4  wall_tint   = {0.70f, 0.68f, 0.65f, 1.0f};  // weathered concrete
    Vec4  wall_dark   = {0.50f, 0.48f, 0.45f, 1.0f};  // darker base staining

    // Main wall face (facing road, -X direction)
    builder.addVerticalFace(rail_x, wall_height, z_far, z_near, kLeft,
                            wall_tint, MAT_CONCRETE, m_concrete_tile);
    // Back face
    builder.addVerticalFace(rail_x + wall_thick, wall_height, z_far, z_near, kRight,
                            wall_dark, MAT_CONCRETE, m_concrete_tile);
    // Top
    builder.addHorizontalQuad(rail_x, rail_x + wall_thick, wall_height, z_far, z_near, kUp,
                              wall_tint, MAT_CONCRETE, m_concrete_tile);

    // Metal chain-link fence on top of wall (simple thin rail)
    float fence_h = wall_height + 4.0f * kFt;  // 4ft fence above wall
    Vec4 fence_tint = {0.55f, 0.58f, 0.55f, 1.0f};
    builder.addVerticalFace(rail_x, fence_h, z_far, z_near, kLeft,
                            fence_tint, MAT_METAL, m_metal_tile);

    // ── WB guardrail (standard metal W-beam) ──────────────────────
    float wb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_rail_x = -(wb_width + m_shoulder_width_wb + m_rail_width);

    builder.addHorizontalQuad(wb_rail_x, wb_rail_x + m_rail_width, m_rail_height, z_far, z_near, kUp,
                              m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addVerticalFace(wb_rail_x, m_rail_height, z_far, z_near, kLeft,
                            m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addVerticalFace(wb_rail_x + m_rail_width, m_rail_height, z_far, z_near, kRight,
                            m_rail_tint, MAT_METAL, m_metal_tile);
}

void RoadScene::generate_solid_markings(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start    = m_barrier_width + 3.0f * kFt;
    float eb_width    = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_width    = static_cast<float>(m_lane_count) * m_lane_width;
    float line_w      = 0.50f * kFt;  // 6-inch marking width
    float crown_slope = RoadConfig::m_crown_slope;

    // Crown height at each edge
    float eb_inner_y = crown_slope * static_cast<float>(m_lane_count) * m_lane_width + m_marking_y_offset;
    float eb_outer_y = m_marking_y_offset;
    float wb_inner_y = eb_inner_y;

    // Yellow median lines (both sides of barrier)
    builder.addHorizontalQuad(-line_w / 2.0f, line_w / 2.0f, wb_inner_y, z_far, z_near, kUp, m_yellow_marking);
    builder.addHorizontalQuad(m_barrier_width - line_w / 2.0f, m_barrier_width + line_w / 2.0f, eb_inner_y,
                              z_far, z_near, kUp, m_yellow_marking);

    // EB white edge lines
    builder.addHorizontalQuad(eb_start, eb_start + line_w, eb_inner_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(eb_start + eb_width - line_w, eb_start + eb_width, eb_outer_y, z_far, z_near, kUp,
                              m_white_marking);

    // WB white edge lines
    builder.addHorizontalQuad(-wb_width, -wb_width + line_w, eb_outer_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(-line_w, 0.0f, wb_inner_y, z_far, z_near, kUp, m_white_marking);

    // ── HOV double solid white lines (lane 0 / lane 1 boundary) ──
    float hov_gap = 0.33f * kFt;  // 4-inch gap between double lines

    // EB HOV boundary: between lane 0 (HOV) and lane 1 (first GP lane)
    float eb_hov_x = eb_start + m_lane_width;
    float eb_hov_y = crown_slope * static_cast<float>(m_lane_count - 1) * m_lane_width + m_marking_y_offset;
    builder.addHorizontalQuad(eb_hov_x - line_w - hov_gap / 2.0f, eb_hov_x - hov_gap / 2.0f,
                              eb_hov_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(eb_hov_x + hov_gap / 2.0f, eb_hov_x + hov_gap / 2.0f + line_w,
                              eb_hov_y, z_far, z_near, kUp, m_white_marking);

    // WB HOV boundary: between lane 0 (HOV) and lane 1
    float wb_hov_x = -m_lane_width;
    float wb_hov_y = eb_hov_y;  // symmetric crown height
    builder.addHorizontalQuad(wb_hov_x - line_w - hov_gap / 2.0f, wb_hov_x - hov_gap / 2.0f,
                              wb_hov_y, z_far, z_near, kUp, m_white_marking);
    builder.addHorizontalQuad(wb_hov_x + hov_gap / 2.0f, wb_hov_x + hov_gap / 2.0f + line_w,
                              wb_hov_y, z_far, z_near, kUp, m_white_marking);
}

void RoadScene::generate_dashed_markings(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start    = m_barrier_width + 3.0f * kFt;
    float wb_width    = static_cast<float>(m_lane_count) * m_lane_width;
    float line_w      = 0.33f * kFt;
    float crown_slope = RoadConfig::m_crown_slope;

    // EB lane dividers — skip i=1 (HOV boundary is now solid double white)
    for (int i = 2; i < m_lane_count; i++) {
        float x = eb_start + static_cast<float>(i) * m_lane_width;
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near,
                              m_dash_length, m_dash_gap, kUp, m_white_marking);
    }

    // WB lane dividers — skip i=1 (HOV boundary)
    for (int i = 2; i < m_lane_count; i++) {
        float x = -(static_cast<float>(i) * m_lane_width);
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near,
                              m_dash_length, m_dash_gap, kUp, m_white_marking);
    }
}

// ══════════════════════════════════════════════════════════════════════
// generate()
// ══════════════════════════════════════════════════════════════════════

RoadScene::SceneData RoadScene::generate() const {
    SceneData scene;
    MeshBuilder builder(scene.meshData, scene.drawCalls);

    float z_near = 0.0f;
    float z_far  = -m_road_length;

    generate_grass(builder, z_near, z_far);
    generate_road_surfaces(builder, z_near, z_far);
    generate_shoulders(builder, z_near, z_far);
    generate_jersey_barrier(builder, z_near, z_far);
    generate_guardrail(builder, z_near, z_far);
    generate_solid_markings(builder, z_near, z_far);
    generate_dashed_markings(builder, z_near, z_far);
    generate_curbs(builder, z_near, z_far);
    generate_rumble_strips(builder, z_near, z_far);
    generate_dirt_strips(builder, z_near, z_far);
    generate_ambient_occlusion(builder, z_near, z_far);
    generate_hov_diamonds(builder, z_near, z_far);
    generate_sign_posts(builder, z_near, z_far);
    generate_overpass(builder, z_near, z_far);

    return scene;
}


void RoadScene::generate_curbs(MeshBuilder& builder, float z_near, float z_far) const {
    float curb_height = m_curb_height;
    float curb_width  = m_curb_width;

    // ── WB outer curb (at shoulder-grass boundary) ────────────────
    float wb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_curb_x = -(wb_width + m_shoulder_width_wb);

    builder.addHorizontalQuad(wb_curb_x - curb_width, wb_curb_x, curb_height,
                              z_far, z_near, kUp, m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(wb_curb_x, curb_height, z_far, z_near, kRight,
                            m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(wb_curb_x - curb_width, curb_height, z_far, z_near, kLeft,
                            m_concrete_tint, MAT_CONCRETE, m_concrete_tile);

    // ── EB outer curb (at shoulder-grass boundary, positive X) ────
    float eb_start  = m_barrier_width + 3.0f * kFt;
    float eb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float eb_curb_x = eb_start + eb_width + m_shoulder_width_eb;

    builder.addHorizontalQuad(eb_curb_x, eb_curb_x + curb_width, curb_height,
                              z_far, z_near, kUp, m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(eb_curb_x, curb_height, z_far, z_near, kLeft,
                            m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(eb_curb_x + curb_width, curb_height, z_far, z_near, kRight,
                            m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
}

void RoadScene::generate_rumble_strips(MeshBuilder& builder, float z_near, float z_far) const {
    float strip_width = 1.0f * kFt;   // 1 ft wide
    float y_offset    = 1.0f;         // tiny Y raise for z-fighting
    Vec4  rumble_tint = {0.85f, 0.85f, 0.85f, 1.0f};  // slightly dark

    // WB rumble strip (at inner edge of WB shoulder, next to travel lane)
    float wb_width = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_x     = -wb_width;
    builder.addHorizontalQuad(wb_x - strip_width, wb_x, y_offset, z_far, z_near, kUp,
                              rumble_tint, MAT_RUMBLE, 300.0f);

    // EB rumble strip (at inner edge of EB shoulder, next to travel lane)
    float eb_start = m_barrier_width + 3.0f * kFt;
    float eb_right = eb_start + static_cast<float>(m_lane_count) * m_lane_width;
    builder.addHorizontalQuad(eb_right, eb_right + strip_width, y_offset, z_far, z_near, kUp,
                              rumble_tint, MAT_RUMBLE, 300.0f);
}

// ══════════════════════════════════════════════════════════════════════
// Ambient Occlusion — dark shadow strips at base of vertical structures
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_ambient_occlusion(MeshBuilder& builder, float z_near, float z_far) const {
    float ao_width = 0.5f * kFt;  // 6 inches wide shadow strip
    float ao_y     = 0.5f;        // tiny Y raise
    Vec4  ao_tint  = {0.15f, 0.15f, 0.18f, 1.0f};  // very dark blue-black

    // AO at base of jersey barrier (both sides)
    builder.addHorizontalQuad(-ao_width, 0.0f, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);
    builder.addHorizontalQuad(m_barrier_width, m_barrier_width + ao_width, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of EB guardrail
    float eb_start = m_barrier_width + 3.0f * kFt;
    float eb_width = static_cast<float>(m_lane_count) * m_lane_width;
    float eb_rail  = eb_start + eb_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_rail - ao_width, eb_rail, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of WB guardrail
    float wb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_rail_x = -(wb_width + m_shoulder_width_wb);
    builder.addHorizontalQuad(wb_rail_x, wb_rail_x + ao_width, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of WB curb (inner side)
    builder.addHorizontalQuad(wb_rail_x - m_curb_width, wb_rail_x - m_curb_width + ao_width, ao_y,
                              z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);

    // AO at base of EB curb (inner side)
    float eb_curb_x = eb_start + eb_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_curb_x - ao_width, eb_curb_x, ao_y, z_far, z_near, kUp,
                              ao_tint, MAT_DEFAULT, 0.0f);
}

// ══════════════════════════════════════════════════════════════════════
// HOV Diamond Markings — white diamonds on HOV lane pavement
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_hov_diamonds(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start    = m_barrier_width + 3.0f * kFt;
    float crown_slope = RoadConfig::m_crown_slope;

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
        builder.addHorizontalQuad(eb_cx - line_thick / 2.0f, eb_cx + line_thick / 2.0f,
                                  eb_y, z - diamond_len / 2.0f, z + diamond_len / 2.0f,
                                  kUp, m_white_marking);
        // EB diamond — horizontal bar (short, wide)
        builder.addHorizontalQuad(eb_cx - diamond_w / 2.0f, eb_cx + diamond_w / 2.0f,
                                  eb_y, z - line_thick / 2.0f, z + line_thick / 2.0f,
                                  kUp, m_white_marking);

        // WB diamond
        builder.addHorizontalQuad(wb_cx - line_thick / 2.0f, wb_cx + line_thick / 2.0f,
                                  wb_y, z - diamond_len / 2.0f, z + diamond_len / 2.0f,
                                  kUp, m_white_marking);
        builder.addHorizontalQuad(wb_cx - diamond_w / 2.0f, wb_cx + diamond_w / 2.0f,
                                  wb_y, z - line_thick / 2.0f, z + line_thick / 2.0f,
                                  kUp, m_white_marking);
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
        builder.m_mesh.addVertex({Vec3(eb_cx - text_w / 2.0f, eb_y, z - text_h / 2.0f),
                                  kUp, Vec2(0.0f, 0.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx + text_w / 2.0f, eb_y, z - text_h / 2.0f),
                                  kUp, Vec2(1.0f, 0.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx - text_w / 2.0f, eb_y, z + text_h / 2.0f),
                                  kUp, Vec2(0.0f, 1.0f), hTan});
        builder.m_mesh.addVertex({Vec3(eb_cx + text_w / 2.0f, eb_y, z + text_h / 2.0f),
                                  kUp, Vec2(1.0f, 1.0f), hTan});

        builder.m_mesh.addIndex(base + 0);  builder.m_mesh.addIndex(base + 2);  builder.m_mesh.addIndex(base + 1);
        builder.m_mesh.addIndex(base + 1);  builder.m_mesh.addIndex(base + 2);  builder.m_mesh.addIndex(base + 3);
        builder.pushDrawCall(idx, white_tint, MAT_SIGN_6);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Sign Posts — Textured highway signs with FHWA text
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_sign_posts(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start   = m_barrier_width + 3.0f * kFt;
    float eb_width   = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_width   = static_cast<float>(m_lane_count) * m_lane_width;
    float roadside_x = eb_start + eb_width + m_shoulder_width_eb + 5.0f * kFt;

    float post_width  = 0.25f * kFt;
    Vec4  post_tint   = {0.45f, 0.48f, 0.45f, 1.0f};
    Vec4  white_tint  = {1.0f, 1.0f, 1.0f, 1.0f};  // texture provides color

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
    float gantry_left   = -(wb_width + m_shoulder_width_wb + 3.0f * kFt);
    float gantry_right  = eb_start + eb_width + m_shoulder_width_eb + 3.0f * kFt;
    Vec4  gantry_tint   = {0.50f, 0.52f, 0.50f, 1.0f};

    for (const auto& s : signs) {
        if (s.overhead) {
            // ── Steel truss gantry structure ──
            float beam_h    = 3.0f * kFt;   // truss beam depth (top chord to bottom chord)
            float chord_w   = 0.25f * kFt;  // chord member thickness
            float strut_w   = 0.20f * kFt;  // diagonal strut width
            float top_y     = gantry_height;
            float bottom_y  = gantry_height - beam_h;

            // Top chord (horizontal beam at gantry_height)
            builder.addHorizontalQuad(gantry_left, gantry_right, top_y,
                                      s.z - chord_w, s.z + chord_w, kUp,
                                      gantry_tint, MAT_METAL, m_metal_tile);
            // Bottom chord
            builder.addHorizontalQuad(gantry_left, gantry_right, bottom_y,
                                      s.z - chord_w, s.z + chord_w, kUp,
                                      Vec4(0.40f, 0.42f, 0.40f, 1.0f), MAT_METAL, m_metal_tile);
            // Front face of truss (facing +Z toward driver)
            builder.addVerticalFace(gantry_left, beam_h,
                                    s.z + chord_w, s.z + chord_w, kRight,
                                    gantry_tint, MAT_METAL, m_metal_tile);

            // Vertical web members along the truss span (every ~15ft)
            float web_spacing = 15.0f * kFt;
            for (float x = gantry_left; x <= gantry_right; x += web_spacing) {
                // Vertical strut
                builder.addVerticalFace(x, top_y,
                                        s.z - strut_w, s.z + strut_w, kLeft,
                                        gantry_tint, MAT_METAL, m_metal_tile);
                builder.addVerticalFace(x, top_y,
                                        s.z - strut_w, s.z + strut_w, kRight,
                                        gantry_tint, MAT_METAL, m_metal_tile);
            }

            // Support posts (thicker — two parallel vertical faces per post for depth)
            float post_thick = 1.0f * kFt;
            // Left post
            builder.addVerticalFace(gantry_left, gantry_height,
                                    s.z - post_thick, s.z + post_thick, kLeft,
                                    gantry_tint, MAT_METAL, m_metal_tile);
            builder.addVerticalFace(gantry_left, gantry_height,
                                    s.z - post_thick, s.z + post_thick, kRight,
                                    gantry_tint, MAT_METAL, m_metal_tile);
            // Left post front/back faces
            builder.addVerticalFace(gantry_left - post_width, gantry_height,
                                    s.z - post_thick, s.z + post_thick, kLeft,
                                    gantry_tint, MAT_METAL, m_metal_tile);

            // Right post
            builder.addVerticalFace(gantry_right, gantry_height,
                                    s.z - post_thick, s.z + post_thick, kLeft,
                                    gantry_tint, MAT_METAL, m_metal_tile);
            builder.addVerticalFace(gantry_right, gantry_height,
                                    s.z - post_thick, s.z + post_thick, kRight,
                                    gantry_tint, MAT_METAL, m_metal_tile);

            // Diagonal cross-bracing on support posts (X-pattern on each post)
            // Left post diagonal: sloped quad from bottom-front to top-back
            float diag_w = 0.15f * kFt;
            float post_h = gantry_height;
            // Left post X-brace front
            builder.addSlopedQuad(gantry_left - diag_w, gantry_left + diag_w,
                                  0.0f, post_h * 0.5f,
                                  s.z + post_thick, s.z + post_thick, kRight,
                                  gantry_tint, MAT_METAL, m_metal_tile);
            builder.addSlopedQuad(gantry_left - diag_w, gantry_left + diag_w,
                                  post_h * 0.5f, post_h,
                                  s.z - post_thick, s.z - post_thick, kRight,
                                  gantry_tint, MAT_METAL, m_metal_tile);
            // Right post X-brace
            builder.addSlopedQuad(gantry_right - diag_w, gantry_right + diag_w,
                                  0.0f, post_h * 0.5f,
                                  s.z + post_thick, s.z + post_thick, kLeft,
                                  gantry_tint, MAT_METAL, m_metal_tile);
            builder.addSlopedQuad(gantry_right - diag_w, gantry_right + diag_w,
                                  post_h * 0.5f, post_h,
                                  s.z - post_thick, s.z - post_thick, kLeft,
                                  gantry_tint, MAT_METAL, m_metal_tile);

            // ── Hanging sign panel with texture ──
            float panel_bottom = gantry_height - 0.5f * kFt - s.h;
            float panel_cx     = eb_start + eb_width / 2.0f;

            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();

            Vec3 panel_normal = Vec3(0.0f, 0.0f, 1.0f);
            Vec4 pTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            builder.m_mesh.addVertex({Vec3(panel_cx - s.w / 2.0f, panel_bottom, s.z + gantry_depth),
                                      panel_normal, Vec2(0.0f, 1.0f), pTan});
            builder.m_mesh.addVertex({Vec3(panel_cx + s.w / 2.0f, panel_bottom, s.z + gantry_depth),
                                      panel_normal, Vec2(1.0f, 1.0f), pTan});
            builder.m_mesh.addVertex({Vec3(panel_cx - s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth),
                                      panel_normal, Vec2(0.0f, 0.0f), pTan});
            builder.m_mesh.addVertex({Vec3(panel_cx + s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth),
                                      panel_normal, Vec2(1.0f, 0.0f), pTan});

            builder.m_mesh.addIndex(base + 0);  builder.m_mesh.addIndex(base + 1);  builder.m_mesh.addIndex(base + 2);
            builder.m_mesh.addIndex(base + 2);  builder.m_mesh.addIndex(base + 1);  builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, white_tint, s.mat);

        } else {
            // ── Roadside sign with post ──
            float post_h = s.bottom + s.h;
            builder.addVerticalFace(roadside_x, post_h,
                                    s.z - post_width, s.z + post_width, kRight,
                                    post_tint, MAT_METAL, m_metal_tile);

            // Sign face — facing left toward driver
            float sign_left  = s.z - s.w / 2.0f;
            float sign_right = s.z + s.w / 2.0f;

            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();

            Vec4 hTan = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            Vec3 sign_normal = Vec3(-1.0f, 0.0f, 0.0f);

            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom, sign_left), sign_normal,
                                      Vec2(0.0f, 1.0f), hTan});
            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom, sign_right), sign_normal,
                                      Vec2(1.0f, 1.0f), hTan});
            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom + s.h, sign_left), sign_normal,
                                      Vec2(0.0f, 0.0f), hTan});
            builder.m_mesh.addVertex({Vec3(roadside_x, s.bottom + s.h, sign_right), sign_normal,
                                      Vec2(1.0f, 0.0f), hTan});

            builder.m_mesh.addIndex(base + 0);  builder.m_mesh.addIndex(base + 2);  builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1);  builder.m_mesh.addIndex(base + 2);  builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, white_tint, s.mat);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// Overpass — bridge deck crossing over the highway
//
// Places a concrete bridge slab every ~3000ft (roughly every 0.5 mile)
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_overpass(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start     = m_barrier_width + 3.0f * kFt;
    float eb_width     = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_width     = static_cast<float>(m_lane_count) * m_lane_width;

    float bridge_clearance = 16.5f * kFt;   // 16.5ft clearance (FHWA minimum)
    float bridge_depth     = 3.0f * kFt;    // 3ft thick deck slab
    float bridge_width     = 30.0f * kFt;   // 30ft wide bridge road
    float bridge_span_left = -(wb_width + m_shoulder_width_wb + 10.0f * kFt);
    float bridge_span_right = eb_start + eb_width + m_shoulder_width_eb + 10.0f * kFt;
    float bridge_spacing   = 3000.0f * kFt; // every ~0.5 mile

    Vec4 bridge_top_tint    = {0.75f, 0.73f, 0.70f, 1.0f};  // light concrete
    Vec4 bridge_bottom_tint = {0.55f, 0.53f, 0.50f, 1.0f};  // darker underside
    Vec4 bridge_side_tint   = {0.65f, 0.63f, 0.60f, 1.0f};  // medium sides
    Vec4 railing_tint       = {0.60f, 0.62f, 0.60f, 1.0f};  // concrete railing

    for (float z = z_far + bridge_spacing; z < z_near - bridge_spacing; z += bridge_spacing) {
        float z_left  = z - bridge_width / 2.0f;
        float z_right = z + bridge_width / 2.0f;

        // ── Bridge deck top surface ───────────────────────────────
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right,
                                  bridge_clearance + bridge_depth,
                                  z_left, z_right, kUp,
                                  bridge_top_tint, MAT_CONCRETE, m_concrete_tile);

        // ── Bridge deck bottom surface (visible from below) ───────
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right,
                                  bridge_clearance,
                                  z_left, z_right, kUp,
                                  bridge_bottom_tint, MAT_CONCRETE, m_concrete_tile);

        // ── Bridge side walls (near and far faces) ────────────────
        // Near face (facing +Z toward camera)
        builder.addVerticalFace(bridge_span_left, bridge_depth,
                                z_right, z_right, kRight,
                                bridge_side_tint, MAT_CONCRETE, m_concrete_tile);

        // Far face (facing -Z away from camera)
        builder.addVerticalFace(bridge_span_right, bridge_depth,
                                z_left, z_left, kLeft,
                                bridge_side_tint, MAT_CONCRETE, m_concrete_tile);

        // ── Bridge railings (short walls on edges) ────────────────
        float railing_height = 3.5f * kFt;   // 42-inch railing
        float railing_top    = bridge_clearance + bridge_depth + railing_height;

        // Left railing
        builder.addVerticalFace(bridge_span_left, railing_top,
                                z_left, z_right, kLeft,
                                railing_tint, MAT_CONCRETE, m_concrete_tile);
        // Right railing
        builder.addVerticalFace(bridge_span_right, railing_top,
                                z_left, z_right, kRight,
                                railing_tint, MAT_CONCRETE, m_concrete_tile);
    }
}

void RoadScene::generate_dirt_strips(MeshBuilder& builder, float z_near, float z_far) const {
    // Brown dirt/gravel strip between the shoulder curb and grass (like the real LIE)
    float dirt_width = 5.0f * kFt;  // 5 ft wide
    Vec4 dirt_tint   = {0.85f, 0.75f, 0.60f, 1.0f};  // warm gravel tint

    // WB dirt strip (between curb and grass)
    float wb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_curb_x = -(wb_width + m_shoulder_width_wb);
    builder.addHorizontalQuad(wb_curb_x - dirt_width, wb_curb_x, 0.0f, z_far, z_near, kUp,
                              dirt_tint, MAT_DIRT, 2000.0f);

    // EB dirt strip (between curb and grass)
    float eb_start  = m_barrier_width + 3.0f * kFt;
    float eb_width  = static_cast<float>(m_lane_count) * m_lane_width;
    float eb_curb_x = eb_start + eb_width + m_shoulder_width_eb;
    builder.addHorizontalQuad(eb_curb_x, eb_curb_x + dirt_width, 0.0f, z_far, z_near, kUp,
                              dirt_tint, MAT_DIRT, 2000.0f);
}

// ══════════════════════════════════════════════════════════════════════
// Trees — cross-billboard vegetation along both sides of the road
//
// Uses two perpendicular quads per tree (cross-billboard pattern)
// for a 3D appearance from any angle. Alpha testing in the shader
// discards transparent pixels.
// ══════════════════════════════════════════════════════════════════════

// Simple hash for pseudo-random variation per tree
static float tree_hash(float z) {
    return std::fmod(std::abs(std::sin(z * 0.0001f) * 43758.5453f), 1.0f);
}

void RoadScene::generate_trees(MeshBuilder& builder, float z_near, float z_far) const {
    float eb_start   = m_barrier_width + 3.0f * kFt;
    float eb_width   = static_cast<float>(m_lane_count) * m_lane_width;
    float wb_width   = static_cast<float>(m_lane_count) * m_lane_width;

    // Tree placement parameters
    float tree_spacing  = 20.0f * kFt;    // every 20 feet
    float tree_setback  = 25.0f * kFt;    // 25ft behind guardrail
    float base_height   = 35.0f * kFt;    // 35ft base tree height
    float height_var    = 15.0f * kFt;    // ±15ft height variation
    float base_width    = 20.0f * kFt;    // 20ft canopy width
    float width_var     = 8.0f * kFt;     // ±8ft width variation

    // EB tree line position (right side of road)
    float eb_rail_x    = eb_start + eb_width + m_shoulder_width_eb + m_rail_width;
    float eb_tree_x    = eb_rail_x + tree_setback;

    // WB tree line position (left side of road)
    float wb_rail_x    = -(wb_width + m_shoulder_width_wb + m_rail_width);
    float wb_tree_x    = wb_rail_x - tree_setback;

    Vec4  tree_tint    = {1.0f, 1.0f, 1.0f, 1.0f};  // full texture color
    Vec3  face_normal  = Vec3(0.0f, 0.0f, 1.0f);

    for (float z = z_far + tree_spacing; z < z_near - tree_spacing; z += tree_spacing) {
        float rnd = tree_hash(z);
        float h   = base_height + height_var * (rnd - 0.5f) * 2.0f;
        float w   = base_width + width_var * (rnd * 0.7f - 0.35f) * 2.0f;

        // Slight X jitter so trees don't look grid-aligned
        float x_jitter = (tree_hash(z + 1000.0f) - 0.5f) * 10.0f * kFt;

        // ── EB side tree (cross-billboard: 2 perpendicular quads) ──
        float ex = eb_tree_x + x_jitter;

        // Quad 1: facing along Z axis (visible from road)
        {
            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();
            Vec4 tan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            builder.m_mesh.addVertex({Vec3(ex - w / 2.0f, 0.0f, z), face_normal, Vec2(0.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex + w / 2.0f, 0.0f, z), face_normal, Vec2(1.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex - w / 2.0f, h, z), face_normal, Vec2(0.0f, 0.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex + w / 2.0f, h, z), face_normal, Vec2(1.0f, 0.0f), tan});
            // Both winding orders so visible from both sides
            builder.m_mesh.addIndex(base + 0); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, tree_tint, MAT_TREE);
        }

        // Quad 2: perpendicular (facing along X axis)
        {
            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();
            Vec3 side_normal = Vec3(1.0f, 0.0f, 0.0f);
            Vec4 tan = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            builder.m_mesh.addVertex({Vec3(ex, 0.0f, z - w / 2.0f), side_normal, Vec2(0.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex, 0.0f, z + w / 2.0f), side_normal, Vec2(1.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex, h, z - w / 2.0f), side_normal, Vec2(0.0f, 0.0f), tan});
            builder.m_mesh.addVertex({Vec3(ex, h, z + w / 2.0f), side_normal, Vec2(1.0f, 0.0f), tan});
            builder.m_mesh.addIndex(base + 0); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, tree_tint, MAT_TREE);
        }

        // ── WB side tree ──
        float wx = wb_tree_x + x_jitter;

        // Quad 1
        {
            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();
            Vec4 tan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            builder.m_mesh.addVertex({Vec3(wx - w / 2.0f, 0.0f, z), face_normal, Vec2(0.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx + w / 2.0f, 0.0f, z), face_normal, Vec2(1.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx - w / 2.0f, h, z), face_normal, Vec2(0.0f, 0.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx + w / 2.0f, h, z), face_normal, Vec2(1.0f, 0.0f), tan});
            builder.m_mesh.addIndex(base + 0); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, tree_tint, MAT_TREE);
        }

        // Quad 2
        {
            uint32_t base = builder.m_mesh.getVertexCount();
            uint32_t idx  = builder.m_mesh.getIndexCount();
            Vec3 side_normal = Vec3(1.0f, 0.0f, 0.0f);
            Vec4 tan = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            builder.m_mesh.addVertex({Vec3(wx, 0.0f, z - w / 2.0f), side_normal, Vec2(0.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx, 0.0f, z + w / 2.0f), side_normal, Vec2(1.0f, 1.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx, h, z - w / 2.0f), side_normal, Vec2(0.0f, 0.0f), tan});
            builder.m_mesh.addVertex({Vec3(wx, h, z + w / 2.0f), side_normal, Vec2(1.0f, 0.0f), tan});
            builder.m_mesh.addIndex(base + 0); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 1);
            builder.m_mesh.addIndex(base + 1); builder.m_mesh.addIndex(base + 2); builder.m_mesh.addIndex(base + 3);
            builder.pushDrawCall(idx, tree_tint, MAT_TREE);
        }
    }
}

}  // namespace swish
