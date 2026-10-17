#include "RoadScene.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "utils/math/Hash.h"

#include <algorithm>
#include <cmath>

namespace swish {

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
    float rail_x = layout.eb_start + layout.road_width + m_shoulder_width_eb;

    // ── EB retaining wall (tall concrete wall like in LIE reference photo) ──
    float wall_height = 8.0f * kFt;                   // ~8ft tall retaining wall
    float wall_thick  = 1.0f * kFt;                   // 1ft thick
    Vec4  wall_tint   = {0.70f, 0.68f, 0.65f, 1.0f};  // weathered concrete
    Vec4  wall_dark   = {0.50f, 0.48f, 0.45f, 1.0f};  // darker base staining
    float fence_h     = wall_height + 4.0f * kFt;     // 4ft fence above wall
    Vec4  fence_tint  = {0.55f, 0.58f, 0.55f, 1.0f};

    auto emit_eb_wall = [&](float za, float zb) {
        if (!(za < zb)) return;
        builder.addVerticalFace(rail_x, wall_height, za, zb, kLeft, wall_tint, MAT_CONCRETE, m_concrete_tile);
        builder.addVerticalFace(rail_x + wall_thick, wall_height, za, zb, kRight, wall_dark, MAT_CONCRETE,
                                m_concrete_tile);
        builder.addHorizontalQuad(rail_x, rail_x + wall_thick, wall_height, za, zb, kUp, wall_tint, MAT_CONCRETE,
                                  m_concrete_tile);
        builder.addVerticalFace(rail_x, fence_h, za, zb, kLeft, fence_tint, MAT_METAL, m_metal_tile);
    };

    // Intro: open the EB wall through the exit diverge so the ramp is not blocked.
    const bool isIntro = (z_near == 0.0f) && (std::abs(z_far + m_road_length) < 1.0f);
    if (isIntro) {
        using RG          = RoadGeometry;
        const float gapHi = RG::intro_exit_decel_start_z(m_road_length);
        const float gapLo = RG::intro_exit_ramp_end_z(m_road_length) - RG::kExitConnectFt * kFt;
        emit_eb_wall(gapHi, z_near);
        emit_eb_wall(z_far, gapLo);
    } else {
        emit_eb_wall(z_far, z_near);
    }

    // ── WB guardrail: galvanized W-beam (evenly-spaced posts + corrugated rail) ──
    // The rail sits on the outer (−X) edge of the WB roadway, so its road-facing
    // plane is +X (toward WB traffic and toward the EB camera across the median).
    float wb_rail_x = layout.wb_inner - m_shoulder_width_wb - m_rail_width;
    float rail_x0   = wb_rail_x + m_rail_width;  // road-facing plane (+X)

    Vec4 post_tint = {0.42f, 0.44f, 0.46f, 1.0f};  // darker weathered steel post

    // Corrugated "W" beam: a ~12in-tall band with two ridges bulging toward the
    // road, built as four X-Y facets extruded the full length. Each facet carries
    // its own normal (the `kFt` scale cancels, so the unit normals are literals:
    // normalize(0.25, ±0.22) = (0.751, ∓0.661)) so the corrugation reads under light.
    float beam_top = m_rail_height;
    float beam_h   = 1.0f * kFt;  // 12in beam
    float beam_bot = beam_top - beam_h;
    float d        = 0.22f * kFt;  // ridge depth toward road
    float y0 = beam_bot, y1 = beam_bot + 0.25f * beam_h, y2 = beam_bot + 0.50f * beam_h, y3 = beam_bot + 0.75f * beam_h,
          y4   = beam_top;
    Vec3  nOut = {0.751f, -0.661f, 0.0f};  // out+up facet → faces road, tilted down
    Vec3  nIn  = {0.751f, 0.661f, 0.0f};   // in+up facet  → faces road, tilted up
    builder.addSlopedQuad(rail_x0, rail_x0 + d, y0, y1, z_far, z_near, nOut, m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addSlopedQuad(rail_x0 + d, rail_x0, y1, y2, z_far, z_near, nIn, m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addSlopedQuad(rail_x0, rail_x0 + d, y2, y3, z_far, z_near, nOut, m_rail_tint, MAT_METAL, m_metal_tile);
    builder.addSlopedQuad(rail_x0 + d, rail_x0, y3, y4, z_far, z_near, nIn, m_rail_tint, MAT_METAL, m_metal_tile);

    // Evenly-spaced posts behind the beam (ground → beam top). Spacing is wider
    // than the real 6.25ft so the ~4.2km run stays draw-call-bounded (no
    // instancing/culling here); at highway distance the cadence still reads right.
    float post_spacing = 12.5f * kFt;
    float post_zw      = 0.18f * kFt;            // ~4in post seen edge-on
    float post_x       = rail_x0 - 0.06f * kFt;  // a hair behind the beam plane (no z-fight)
    for (float z = z_far; z <= z_near; z += post_spacing) {
        builder.addVerticalFace(post_x, beam_top, z - post_zw, z + post_zw, kRight, post_tint, MAT_METAL, m_metal_tile);
    }
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
        Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.add_quad({Vec3(eb_cx - text_w / 2.0f, eb_y, z - text_h / 2.0f), kUp, Vec2(0.0f, 0.0f), hTan},
                         {Vec3(eb_cx + text_w / 2.0f, eb_y, z - text_h / 2.0f), kUp, Vec2(1.0f, 0.0f), hTan},
                         {Vec3(eb_cx - text_w / 2.0f, eb_y, z + text_h / 2.0f), kUp, Vec2(0.0f, 1.0f), hTan},
                         {Vec3(eb_cx + text_w / 2.0f, eb_y, z + text_h / 2.0f), kUp, Vec2(1.0f, 1.0f), hTan},
                         QuadWinding::Cross, white_tint, MAT_SIGN_6);
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

    // Sign Z positions: most are fractions of z_far; EXIT ONLY gantry locks to
    // RoadGeometry::intro_exit_gantry_z so it sits just upstream of the diverge.
    const float introLen   = -z_far;  // m_road_length
    const float exitGantry = RoadGeometry::intro_exit_gantry_z(introLen);

    SignDef signs[] = {
        // Roadside: SPEED LIMIT 55 (first thing driver sees)
        {0.10f * z_far, 3.0f * kFt, 4.0f * kFt, 6.0f * kFt, MAT_SIGN_4, false},
        // Overhead gantry: I-495 EAST / Long Island Expwy (mainline reassurance)
        {0.24f * z_far, 20.0f * kFt, 6.0f * kFt, 0.0f, MAT_SIGN_0, true},
        // Roadside: EXIT 39 Glen Cove Rd 1 MILE (advance guide)
        {0.40f * z_far, 8.0f * kFt, 5.0f * kFt, 7.0f * kFt, MAT_SIGN_1, false},
        // Overhead gantry: EXIT 39 Glen Cove Rd / Northern Blvd (EXIT ONLY) — at diverge
        {exitGantry, 20.0f * kFt, 6.0f * kFt, 0.0f, MAT_SIGN_2, true},
        // Roadside: MILE 39 marker
        {0.70f * z_far, 2.0f * kFt, 3.0f * kFt, 5.0f * kFt, MAT_SIGN_5, false},
        // Overhead gantry: S Oyster Bay Rd / Syosset · Bethpage (pull-through)
        {0.82f * z_far, 20.0f * kFt, 6.0f * kFt, 0.0f, MAT_SIGN_7, true},
        // Roadside: blue service sign
        {0.92f * z_far, 6.0f * kFt, 4.0f * kFt, 6.0f * kFt, MAT_SIGN_3, false},
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

            Vec3 panel_normal = Vec3(0.0f, 0.0f, 1.0f);
            Vec4 pTan         = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

            builder.add_quad(
                {Vec3(panel_cx - s.w / 2.0f, panel_bottom, s.z + gantry_depth), panel_normal, Vec2(0.0f, 1.0f), pTan},
                {Vec3(panel_cx + s.w / 2.0f, panel_bottom, s.z + gantry_depth), panel_normal, Vec2(1.0f, 1.0f), pTan},
                {Vec3(panel_cx - s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth), panel_normal, Vec2(0.0f, 0.0f),
                 pTan},
                {Vec3(panel_cx + s.w / 2.0f, panel_bottom + s.h, s.z + gantry_depth), panel_normal, Vec2(1.0f, 0.0f),
                 pTan},
                QuadWinding::Fan, white_tint, s.mat);

        } else {
            // ── Roadside sign with post ──
            float post_h = s.bottom + s.h;
            builder.addVerticalFace(roadside_x, post_h, s.z - post_width, s.z + post_width, kRight, post_tint,
                                    MAT_METAL, m_metal_tile);

            // Sign face — facing left toward driver
            float sign_left  = s.z - s.w / 2.0f;
            float sign_right = s.z + s.w / 2.0f;

            Vec4 hTan        = Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            Vec3 sign_normal = Vec3(-1.0f, 0.0f, 0.0f);

            builder.add_quad({Vec3(roadside_x, s.bottom, sign_left), sign_normal, Vec2(0.0f, 1.0f), hTan},
                             {Vec3(roadside_x, s.bottom, sign_right), sign_normal, Vec2(1.0f, 1.0f), hTan},
                             {Vec3(roadside_x, s.bottom + s.h, sign_left), sign_normal, Vec2(0.0f, 0.0f), hTan},
                             {Vec3(roadside_x, s.bottom + s.h, sign_right), sign_normal, Vec2(1.0f, 0.0f), hTan},
                             QuadWinding::Cross, white_tint, s.mat);
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

    Vec4  pier_tint      = {0.58f, 0.56f, 0.53f, 1.0f};  // pier concrete
    float railing_height = 3.5f * kFt;                   // 42-inch parapet

    // A rectangular support pier from ground to deck underside (±X sides via the
    // helper, ±Z faces hand-built).
    auto addPier = [&](float cx, float half_x, float z, float half_z) {
        builder.addVerticalFace(cx - half_x, bridge_clearance, z - half_z, z + half_z, kLeft, pier_tint, MAT_CONCRETE,
                                m_concrete_tile);
        builder.addVerticalFace(cx + half_x, bridge_clearance, z - half_z, z + half_z, kRight, pier_tint, MAT_CONCRETE,
                                m_concrete_tile);
        builder.addZFace(cx - half_x, cx + half_x, 0.0f, bridge_clearance, z + half_z, 1.0f, pier_tint, MAT_CONCRETE);
        builder.addZFace(cx - half_x, cx + half_x, 0.0f, bridge_clearance, z - half_z, -1.0f, pier_tint, MAT_CONCRETE);
    };

    for (float z = z_far + bridge_spacing; z < z_near - bridge_spacing; z += bridge_spacing) {
        float z_left   = z - bridge_width / 2.0f;  // far edge (−Z)
        float z_right  = z + bridge_width / 2.0f;  // near edge (+Z, toward driver)
        float deck_bot = bridge_clearance;
        float deck_top = bridge_clearance + bridge_depth;

        // Deck top + underside
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right, deck_top, z_left, z_right, kUp, bridge_top_tint,
                                  MAT_CONCRETE, m_concrete_tile);
        builder.addHorizontalQuad(bridge_span_left, bridge_span_right, deck_bot, z_left, z_right, kUp,
                                  bridge_bottom_tint, MAT_CONCRETE, m_concrete_tile);

        // Deck fascia (the concrete beam face the driver sees) — near (+Z) and far (−Z)
        builder.addZFace(bridge_span_left, bridge_span_right, deck_bot, deck_top, z_right, 1.0f, bridge_side_tint,
                         MAT_CONCRETE);
        builder.addZFace(bridge_span_left, bridge_span_right, deck_bot, deck_top, z_left, -1.0f, bridge_side_tint,
                         MAT_CONCRETE);

        // Parapets run along the deck's long edges (constant Z), on top of the deck
        builder.addZFace(bridge_span_left, bridge_span_right, deck_top, deck_top + railing_height, z_right, 1.0f,
                         railing_tint, MAT_CONCRETE);
        builder.addZFace(bridge_span_left, bridge_span_right, deck_top, deck_top + railing_height, z_left, -1.0f,
                         railing_tint, MAT_CONCRETE);

        // Support piers: one in the median, one inboard of each embankment
        addPier(m_barrier_width * 0.5f, 2.0f * kFt, z, 2.5f * kFt);
        addPier(bridge_span_left + 6.0f * kFt, 2.0f * kFt, z, 2.5f * kFt);
        addPier(bridge_span_right - 6.0f * kFt, 2.0f * kFt, z, 2.5f * kFt);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Trees — cross-billboard vegetation along both sides of the road
//
// Uses two perpendicular quads per tree (cross-billboard pattern)
// for a 3D appearance from any angle. Alpha testing in the shader
// discards transparent pixels.
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_marginal_trees(MeshBuilder& builder, const RoadLayout& layout, float /*z_near*/,
                                        float z_far) const {
    // Tree wall beside intro N Marginal only (Z from ramp end → z_far). Replaces
    // suburban houses with a dense green line matching the Street View refs.
    using RG = RoadGeometry;

    const float introLen    = -z_far;
    const float z_tree_near = RG::intro_exit_ramp_end_z(introLen);  // start of marginal
    const float z_tree_far  = z_far;

    float tree_spacing = 14.0f * kFt;  // denser than mainline billboards
    float base_height  = 38.0f * kFt;
    float height_var   = 12.0f * kFt;
    float base_width   = 22.0f * kFt;
    float width_var    = 8.0f * kFt;

    // Far side of N Marginal + berm between LIE and marginal
    float marginal_outer = RG::kIntroMarginalOuter;
    float far_tree_x     = marginal_outer + 18.0f * kFt;
    float berm_tree_x    = RG::kEbRoadRight + 28.0f * kFt;

    Vec4 tree_tint   = {1.0f, 1.0f, 1.0f, 1.0f};
    Vec3 face_normal = Vec3(0.0f, 0.0f, 1.0f);

    auto emit_billboard_tree = [&](float cx, float tree_h, float tree_w, float z_pos) {
        {
            Vec4 tan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

            builder.add_quad({Vec3(cx - tree_w / 2.0f, 0.0f, z_pos), face_normal, Vec2(0.0f, 1.0f), tan},
                             {Vec3(cx + tree_w / 2.0f, 0.0f, z_pos), face_normal, Vec2(1.0f, 1.0f), tan},
                             {Vec3(cx - tree_w / 2.0f, tree_h, z_pos), face_normal, Vec2(0.0f, 0.0f), tan},
                             {Vec3(cx + tree_w / 2.0f, tree_h, z_pos), face_normal, Vec2(1.0f, 0.0f), tan},
                             QuadWinding::Cross, tree_tint, MAT_TREE);
        }
        {
            Vec3 side_normal = Vec3(1.0f, 0.0f, 0.0f);
            Vec4 tan         = Vec4(0.0f, 0.0f, 1.0f, 1.0f);

            builder.add_quad({Vec3(cx, 0.0f, z_pos - tree_w / 2.0f), side_normal, Vec2(0.0f, 1.0f), tan},
                             {Vec3(cx, 0.0f, z_pos + tree_w / 2.0f), side_normal, Vec2(1.0f, 1.0f), tan},
                             {Vec3(cx, tree_h, z_pos - tree_w / 2.0f), side_normal, Vec2(0.0f, 0.0f), tan},
                             {Vec3(cx, tree_h, z_pos + tree_w / 2.0f), side_normal, Vec2(1.0f, 0.0f), tan},
                             QuadWinding::Cross, tree_tint, MAT_TREE);
        }
    };

    (void)layout;
    int slot = 0;
    for (float z = z_tree_far + tree_spacing; z < z_tree_near - tree_spacing; z += tree_spacing, ++slot) {
        float rnd      = util::math::hash01(slot);
        float h        = base_height + height_var * (rnd - 0.5f) * 2.0f;
        float w        = base_width + width_var * (rnd * 0.7f - 0.35f) * 2.0f;
        float x_jitter = (util::math::hash01(slot + 10000) - 0.5f) * 8.0f * kFt;

        emit_billboard_tree(far_tree_x + x_jitter, h, w, z);
        // Second staggered row on the berm between LIE and marginal
        float rnd2 = util::math::hash01(slot + 5000);
        float h2   = base_height * 0.85f + height_var * (rnd2 - 0.5f) * 2.0f;
        float w2   = base_width * 0.9f + width_var * (rnd2 * 0.7f - 0.35f) * 2.0f;
        emit_billboard_tree(berm_tree_x - x_jitter, h2, w2, z + tree_spacing * 0.5f);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Sound Barriers — noise reduction panels along the roadside
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_sound_barriers(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                        float z_far) const {
    // EB sound barrier sits on top of the retaining wall
    float wall_x    = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    float wall_top  = 8.0f * kFt;  // retaining wall height
    float panel_h   = 6.0f * kFt;  // 6ft tall panels on top of wall
    float panel_top = wall_top + panel_h;
    float panel_w   = 0.5f * kFt;  // panel thickness

    Vec4 panel_tint_front = {0.65f, 0.55f, 0.42f, 1.0f};  // weathered wood tone
    Vec4 panel_tint_back  = {0.55f, 0.48f, 0.38f, 1.0f};  // darker backside
    Vec4 panel_top_tint   = {0.60f, 0.52f, 0.40f, 1.0f};

    float post_spacing = 10.0f * kFt;
    float post_w       = 0.75f * kFt;
    Vec4  post_tint    = {0.65f, 0.63f, 0.60f, 1.0f};  // concrete post

    auto emit_eb_panels = [&](float za, float zb) {
        if (!(za < zb)) return;
        builder.addVerticalFace(wall_x, panel_top, za, zb, kLeft, panel_tint_front, MAT_CONCRETE, m_concrete_tile);
        builder.addVerticalFace(wall_x + panel_w, panel_top, za, zb, kRight, panel_tint_back, MAT_CONCRETE,
                                m_concrete_tile);
        builder.addHorizontalQuad(wall_x, wall_x + panel_w, panel_top, za, zb, kUp, panel_top_tint, MAT_CONCRETE,
                                  m_concrete_tile);
        for (float z = za + post_spacing; z < zb; z += post_spacing) {
            builder.addVerticalFace(wall_x - 0.1f * kFt, panel_top, z - post_w / 2.0f, z + post_w / 2.0f, kLeft,
                                    post_tint, MAT_CONCRETE, m_concrete_tile);
        }
    };

    const bool isIntro = (z_near == 0.0f) && (std::abs(z_far + m_road_length) < 1.0f);
    if (isIntro) {
        using RG          = RoadGeometry;
        const float gapHi = RG::intro_exit_decel_start_z(m_road_length);
        const float gapLo = RG::intro_exit_ramp_end_z(m_road_length) - RG::kExitConnectFt * kFt;
        emit_eb_panels(gapHi, z_near);
        emit_eb_panels(z_far, gapLo);
    } else {
        emit_eb_panels(z_far, z_near);
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
// Street Lamps — highway light poles with point light sources
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_street_lamps(MeshBuilder& builder, const RoadLayout& layout, std::vector<LightDesc>& lights,
                                      float z_near, float z_far) const {
    // Lamp placement parameters
    float lamp_spacing = 500.0f * kFt;  // every 500ft (typical highway)
    float pole_height  = 30.0f * kFt;   // 30ft tall highway lamp
    float pole_width   = 0.25f * kFt;   // 3-inch square pole
    float arm_len      = 8.0f * kFt;    // 8ft davit arm reaching over road
    float arm_rise     = 5.0f * kFt;    // arm sweeps up ~5ft over its reach (davit curve)
    float arm_h        = 0.20f * kFt;   // arm thickness
    int   arm_seg      = 4;             // segments approximating the arc
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

    // A davit (mast-arm) lamp: tall pole + a curved arm that sweeps up and over the
    // road + a cobra-head fixture at the raised end + a warm point light. The arm is
    // an arc approximated by `arm_seg` short sloped segments following
    // y(t) = pole_height + arm_rise·(1−(1−t)²)   (rises steeply then flattens).
    auto davit = [&](float pole_x, float light_x, float z) {
        // Pole (paired ±X faces for a hint of depth)
        builder.addVerticalFace(pole_x, pole_height, z - pole_width, z + pole_width, kRight, pole_tint, MAT_METAL,
                                m_metal_tile);
        builder.addVerticalFace(pole_x, pole_height, z - pole_width, z + pole_width, kLeft, pole_tint, MAT_METAL,
                                m_metal_tile);
        // Curved arm
        float px = pole_x, py = pole_height;
        for (int i = 1; i <= arm_seg; i++) {
            float t  = static_cast<float>(i) / static_cast<float>(arm_seg);
            float nx = pole_x + (light_x - pole_x) * t;
            float ny = pole_height + arm_rise * (1.0f - (1.0f - t) * (1.0f - t));
            if (px <= nx)
                builder.addSlopedQuad(px, nx, py, ny, z - arm_h, z + arm_h, kUp, pole_tint, MAT_METAL, m_metal_tile);
            else
                builder.addSlopedQuad(nx, px, ny, py, z - arm_h, z + arm_h, kUp, pole_tint, MAT_METAL, m_metal_tile);
            px = nx;
            py = ny;
        }
        // Cobra-head fixture hanging just under the raised arm end
        float fy = py;
        builder.addHorizontalQuad(light_x - fixture_w / 2.0f, light_x + fixture_w / 2.0f, fy - fixture_h,
                                  z - fixture_w / 2.0f, z + fixture_w / 2.0f, kUp, fixture_tint, MAT_DEFAULT, 0.0f);
        // Warm sodium-vapor point light (intensity, ~50m radius). CameraUniforms
        // uploads only the N nearest the camera each frame, so all lamps can emit.
        lights.push_back({Vec3(light_x, fy - fixture_h, z), Vec3(1.0f, 0.9f, 0.7f), 5.0f, 50000.0f});
    };

    // Emit lamps along the full road (both carriageways, mirrored).
    for (float z = z_far + lamp_spacing; z < z_near; z += lamp_spacing) {
        davit(eb_pole_x, eb_light_x, z);
        davit(wb_pole_x, wb_light_x, z);
    }
}

}  // namespace swish
