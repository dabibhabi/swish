#include "RoadScene.h"
#include "scene/RoadGeometry/RoadGeometry.h"

#include <algorithm>
#include <cmath>

namespace swish {

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

    // Intro: leave a grass hole through the exit opening so ramp asphalt isn't
    // buried under coplanar grass (Street View diverge needs a clear pavement path).
    const bool isIntro = (z_near == 0.0f) && (std::abs(z_far + m_road_length) < 1.0f);
    if (isIntro) {
        using RG          = RoadGeometry;
        const float gapHi = RG::intro_exit_decel_start_z(m_road_length);  // less negative
        const float gapLo = RG::intro_exit_ramp_end_z(m_road_length) - RG::kExitConnectFt * kFt;
        // Upstream of exit (toward start) + downstream of merge
        if (gapHi < z_near)
            builder.addHorizontalQuad(eb_right + m_rail_width, m_grass_extent, 0.0f, gapHi, z_near, kUp, m_grass_tint,
                                      MAT_GRASS, m_grass_tile);
        if (z_far < gapLo)
            builder.addHorizontalQuad(eb_right + m_rail_width, m_grass_extent, 0.0f, z_far, gapLo, kUp, m_grass_tint,
                                      MAT_GRASS, m_grass_tile);
        // Through the exit Z window: grass only beyond N Marginal (not over the ramp)
        const float past_marginal = RG::kIntroMarginalOuter + 15.0f * kFt;
        if (gapLo < gapHi)
            builder.addHorizontalQuad(past_marginal, m_grass_extent, 0.0f, gapLo, gapHi, kUp, m_grass_tint, MAT_GRASS,
                                      m_grass_tile);
    } else {
        builder.addHorizontalQuad(eb_right + m_rail_width, m_grass_extent, 0.0f, z_far, z_near, kUp, m_grass_tint,
                                  MAT_GRASS, m_grass_tile);
    }
}

void RoadScene::generate_road_surfaces(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                       float z_far) const {
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

void RoadScene::generate_solid_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                        float z_far) const {
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
    builder.addHorizontalQuad(layout.wb_inner, layout.wb_inner + line_w, eb_outer_y, z_far, z_near, kUp,
                              m_white_marking);
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

void RoadScene::generate_dashed_markings(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far,
                                         road::DashPattern dash) const {
    float eb_start    = layout.eb_start;
    float line_w      = 0.33f * kFt;
    float crown_slope = RoadConfig::kCrownSlope;

    // EB lane dividers — skip i=1 (HOV boundary is now solid double white)
    for (int i = 2; i < m_lane_count; i++) {
        float x = eb_start + static_cast<float>(i) * m_lane_width;
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near, dash.length, dash.gap, kUp,
                              m_white_marking);
    }

    // WB lane dividers — skip i=1 (HOV boundary)
    for (int i = 2; i < m_lane_count; i++) {
        float x = -(static_cast<float>(i) * m_lane_width);
        float y = crown_slope * static_cast<float>(m_lane_count - i) * m_lane_width + m_marking_y_offset;
        builder.addDashedLine(x - line_w / 2.0f, x + line_w / 2.0f, y, z_far, z_near, dash.length, dash.gap, kUp,
                              m_white_marking);
    }
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

void RoadScene::generate_rumble_strips(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                       float z_far) const {
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

void RoadScene::generate_ambient_occlusion(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                           float z_far) const {
    float ao_width = 0.5f * kFt;                   // 6 inches wide shadow strip
    float ao_y     = 0.5f;                         // tiny Y raise
    Vec4  ao_tint  = {0.15f, 0.15f, 0.18f, 1.0f};  // very dark blue-black

    // AO at base of jersey barrier (both sides)
    builder.addHorizontalQuad(-ao_width, 0.0f, ao_y, z_far, z_near, kUp, ao_tint, MAT_DEFAULT, 0.0f);
    builder.addHorizontalQuad(m_barrier_width, m_barrier_width + ao_width, ao_y, z_far, z_near, kUp, ao_tint,
                              MAT_DEFAULT, 0.0f);

    // AO at base of EB guardrail
    float eb_rail = layout.eb_start + layout.road_width + m_shoulder_width_eb;
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

}  // namespace swish
