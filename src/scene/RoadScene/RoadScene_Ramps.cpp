#include "RoadScene.h"
#include "scene/RoadGeometry/RoadGeometry.h"

#include <algorithm>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Service (frontage) roads — continuous, both sides
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_service_roads(MeshBuilder& builder, const RoadLayout& layout, float z_near,
                                       float z_far) const {
    const float laneW  = 12.0f * kFt;             // one lane each direction
    const float berm   = 55.0f * kFt;             // grass buffer beyond the mainline sound barriers
    const float line_w = 0.50f * kFt;             // marking width
    const float y      = 6.0f;                    // lift above grass (~6 mm) → no coplanar z-fight
    const float mark_y = y + m_marking_y_offset;  // markings sit just above the frontage asphalt

    // Small helper: 2-lane frontage with a double-yellow centre + white edge lines,
    // given the road's inner (mainline-facing) edge X and the outward +1/−1 sign.
    auto frontage = [&](float innerX, float sign) {
        float e0 = innerX;                 // inner edge (nearest the mainline)
        float e1 = innerX + sign * laneW;  // centre line
        float e2 = innerX + sign * 2.0f * laneW;
        float lo = std::min(e0, e2), hi = std::max(e0, e2);

        // Two lanes (single asphalt span each side of the centre).
        builder.addHorizontalQuad(lo, std::min(e1, hi), y, z_far, z_near, kUp, m_asphalt_tint, MAT_ASPHALT,
                                  m_asphalt_tile);
        builder.addHorizontalQuad(std::max(e1, lo), hi, y, z_far, z_near, kUp, m_asphalt_tint, MAT_ASPHALT,
                                  m_asphalt_tile);
        // Double-yellow centre line.
        builder.addHorizontalQuad(e1 - line_w / 2.0f - 0.1f * kFt, e1 - 0.1f * kFt, mark_y, z_far, z_near, kUp,
                                  m_yellow_marking);
        builder.addHorizontalQuad(e1 + 0.1f * kFt, e1 + line_w / 2.0f + 0.1f * kFt, mark_y, z_far, z_near, kUp,
                                  m_yellow_marking);
        // White edge lines (both outer edges of the pair of lanes).
        builder.addHorizontalQuad(lo, lo + line_w, mark_y, z_far, z_near, kUp, m_white_marking);
        builder.addHorizontalQuad(hi - line_w, hi, mark_y, z_far, z_near, kUp, m_white_marking);
    };

    // EB frontage: out past the right (+X) sound barrier.
    float eb_outer = layout.eb_start + layout.road_width + m_shoulder_width_eb + m_rail_width;
    frontage(eb_outer + berm, +1.0f);

    // WB frontage: out past the left (−X) sound barrier (mirror).
    float wb_outer = layout.wb_inner - m_shoulder_width_wb - m_rail_width;
    frontage(wb_outer - berm, -1.0f);
}

void RoadScene::generate_crossover(MeshBuilder& builder, const RoadLayout& layout, float zCenter, float halfLen) const {
    const float eb_inner = layout.eb_start + m_lane_width * 0.5f;  // EB innermost lane centre (+X)
    const float wb_inner = -m_lane_width * 0.5f;                   // WB innermost lane centre (−X)
    const float y        = 6.0f;                                   // lift above the road (avoid z-fight)

    // Smoothstep S-curve across the median: enters on the EB inner lane (+Z side of
    // the gap), sweeps across to the WB inner lane (−Z side).
    std::vector<Vec3> centerline;
    const int         N = 12;
    for (int i = 0; i <= N; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(N);
        float s = t * t * (3.0f - 2.0f * t);
        float x = eb_inner + (wb_inner - eb_inner) * s;
        float z = (zCenter + halfLen) + t * (-2.0f * halfLen);
        centerline.push_back(Vec3(x, y, z));
    }
    builder.emit_ribbon(centerline, m_lane_width * 1.2f, m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);
}

// ══════════════════════════════════════════════════════════════════════
// Elevated diamond interchange — deck on piers + four graded ramps
// ══════════════════════════════════════════════════════════════════════

// Interchange dimensions (local coords, deck at z=0) — shared by the ribbon
// definition and the geometry builder so physics and visuals stay in sync.
RoadScene::IxDims RoadScene::ix_dims() const {
    IxDims d{};
    d.clearance  = 16.5f * kFt;
    d.deck_depth = 3.0f * kFt;
    d.deck_top   = d.clearance + d.deck_depth;
    d.deck_half  = 22.0f * kFt;
    d.eb_edge = (m_barrier_width + 3.0f * kFt) + static_cast<float>(m_lane_count) * m_lane_width + m_shoulder_width_eb;
    d.wb_edge = -(static_cast<float>(m_lane_count) * m_lane_width) - m_shoulder_width_wb;
    d.span_right   = d.eb_edge + 70.0f * kFt;
    d.span_left    = d.wb_edge - 70.0f * kFt;
    d.rampLen      = 420.0f * kFt;
    d.rw           = m_lane_width * 0.6f;
    d.eb_service_x = d.eb_edge + m_rail_width + 55.0f * kFt + m_lane_width;  // ≈ EB frontage-road centre
    return d;
}

std::vector<Ribbon> RoadScene::interchange_ribbons() const {
    std::vector<Ribbon> ribbons;
    if (m_lane_count <= 0)
        return ribbons;
    const IxDims d = ix_dims();

    auto curve = [](Vec3 p0, Vec3 p1, int N) {
        std::vector<Vec3> cl;
        for (int i = 0; i <= N; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(N);
            float s = t * t * (3.0f - 2.0f * t);  // smoothstep X + Y (vertical curve), linear Z
            cl.push_back(Vec3(p0.x + (p1.x - p0.x) * s, p0.y + (p1.y - p0.y) * s, p0.z + (p1.z - p0.z) * t));
        }
        return cl;
    };

    Ribbon r0;  // EB on-ramp: mainline edge (ground, +Z) → deck east end (z=0)
    r0.pts       = curve(Vec3(d.eb_edge, 6.0f, d.rampLen), Vec3(d.span_right - d.rw, d.deck_top, 0.0f), 14);
    r0.halfWidth = d.rw;
    r0.next      = 1;  // onto the deck
    r0.branch    = 3;  // …or steer onto the service off-ramp
    Ribbon r1;         // deck: east end → west end (straight, at deck level)
    r1.pts       = {Vec3(d.span_right - d.rw, d.deck_top, 0.0f), Vec3(d.span_left + d.rw, d.deck_top, 0.0f)};
    r1.halfWidth = d.deck_half * 0.8f;
    r1.next      = 2;  // onto the WB off-ramp (reverses direction)
    Ribbon r2;         // WB off-ramp: deck west end → WB mainline, exits +Z (opposite direction)
    r2.pts       = curve(Vec3(d.span_left + d.rw, d.deck_top, 0.0f), Vec3(d.wb_edge, 6.0f, d.rampLen), 14);
    r2.halfWidth = d.rw;
    Ribbon r3;  // service off-ramp: deck east end → EB frontage road, exits +Z
    r3.pts       = curve(Vec3(d.span_right - d.rw, d.deck_top, 0.0f), Vec3(d.eb_service_x, 6.0f, d.rampLen), 14);
    r3.halfWidth = d.rw;

    ribbons = {r0, r1, r2, r3};
    return ribbons;
}

void RoadScene::generate_interchange(MeshBuilder& builder, const RoadLayout& /*layout*/, float /*zCenter*/) const {
    const IxDims d = ix_dims();

    const Vec4 concrete    = {0.70f, 0.68f, 0.65f, 1.0f};
    const Vec4 concrete_dk = {0.52f, 0.50f, 0.47f, 1.0f};

    // Deck: drivable top slab (visual; physics follows ribbon 1 at the same height)
    // + underside + fascias + parapets.
    const float zl = -d.deck_half, zr = d.deck_half;
    builder.addHorizontalQuad(d.span_left, d.span_right, d.deck_top, zl, zr, kUp, concrete, MAT_CONCRETE,
                              m_concrete_tile);
    builder.addHorizontalQuad(d.span_left, d.span_right, d.clearance, zl, zr, kUp, concrete_dk, MAT_CONCRETE,
                              m_concrete_tile);
    builder.addZFace(d.span_left, d.span_right, d.clearance, d.deck_top, zr, 1.0f, concrete, MAT_CONCRETE);
    builder.addZFace(d.span_left, d.span_right, d.clearance, d.deck_top, zl, -1.0f, concrete, MAT_CONCRETE);
    builder.addZFace(d.span_left, d.span_right, d.deck_top, d.deck_top + 3.5f * kFt, zr, 1.0f, concrete,
                     MAT_CONCRETE);  // parapets
    builder.addZFace(d.span_left, d.span_right, d.deck_top, d.deck_top + 3.5f * kFt, zl, -1.0f, concrete, MAT_CONCRETE);

    // Piers: median + one inboard of each embankment.
    auto pier = [&](float cx) {
        float hw = 2.0f * kFt, hz = 2.5f * kFt;
        builder.addVerticalFace(cx - hw, d.clearance, -hz, hz, kLeft, concrete_dk, MAT_CONCRETE, m_concrete_tile);
        builder.addVerticalFace(cx + hw, d.clearance, -hz, hz, kRight, concrete_dk, MAT_CONCRETE, m_concrete_tile);
        builder.addZFace(cx - hw, cx + hw, 0.0f, d.clearance, hz, 1.0f, concrete_dk, MAT_CONCRETE);
        builder.addZFace(cx - hw, cx + hw, 0.0f, d.clearance, -hz, -1.0f, concrete_dk, MAT_CONCRETE);
    };
    pier(m_barrier_width * 0.5f);
    pier(d.span_left + 8.0f * kFt);
    pier(d.span_right - 8.0f * kFt);

    // Drivable ramps: emit the on-ramp, WB off-ramp, and service off-ramp ribbons
    // (the deck top slab above is ribbon 1's visual, so it isn't re-emitted).
    const std::vector<Ribbon> ribbons = interchange_ribbons();
    for (int i : {0, 2, 3})
        builder.emit_ribbon(ribbons[i].pts, ribbons[i].halfWidth, m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);
}

RoadScene::SceneData RoadScene::generate_interchange_scene() const {
    SceneData   scene;
    MeshBuilder builder(scene.meshData, scene.drawCalls);
    if (m_lane_count <= 0)
        return scene;
    RoadLayout layout{
        m_barrier_width + 3.0f * kFt,
        static_cast<float>(m_lane_count) * m_lane_width,
        -(static_cast<float>(m_lane_count) * m_lane_width),
    };
    generate_interchange(builder, layout, 0.0f);
    return scene;
}

// ══════════════════════════════════════════════════════════════════════
// Exit Ramp — EB exit to N Marginal Road (Street View–style diverge)
//
// Decel lane → diverge with chevron gore → connector → 2-lane N Marginal.
// Z/X constants live in RoadGeometry so drivability + EXIT ONLY gantry stay
// locked to this mesh.
// ══════════════════════════════════════════════════════════════════════

void RoadScene::generate_exit_ramp(MeshBuilder& builder, const RoadLayout& layout, float z_near, float z_far) const {
    (void)z_near;
    using RG = RoadGeometry;

    float eb_right = layout.eb_start + layout.road_width + m_shoulder_width_eb;
    float line_w   = 0.50f * kFt;
    // Lifted like endless service roads so asphalt sits above grass (was y=0 → buried).
    const float y      = RG::kIntroExitAsphaltY;
    const float mark_y = y + m_marking_y_offset;

    const float introLen      = -z_far;
    const float ramp_end_z    = RG::intro_exit_ramp_end_z(introLen);
    const float ramp_start_z  = RG::intro_exit_ramp_start_z(introLen);
    const float decel_start_z = RG::intro_exit_decel_start_z(introLen);

    const float ramp_offset = RG::kExitRampOffsetFt * kFt;
    const float ramp_width  = RG::kExitRampWidthFt * kFt;
    const float gore_len    = ramp_start_z - ramp_end_z;

    // ── Deceleration lane (extra lane width that tapers in) ──────
    int   num_decel_segs = 8;
    float decel_len      = decel_start_z - ramp_start_z;
    float seg_len        = decel_len / static_cast<float>(num_decel_segs);

    for (int i = 0; i < num_decel_segs; i++) {
        float z0 = decel_start_z - static_cast<float>(i) * seg_len;
        float z1 = z0 - seg_len;
        float t0 = static_cast<float>(i) / static_cast<float>(num_decel_segs);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_decel_segs);

        float left   = eb_right;
        float right0 = eb_right + t0 * ramp_width;
        float right1 = eb_right + t1 * ramp_width;

        Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.add_quad({Vec3(left, y, z0), kUp, Vec2(left / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(right0, y, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(left, y, z1), kUp, Vec2(left / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         {Vec3(right1, y, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         QuadWinding::Cross, m_asphalt_tint, MAT_ASPHALT);
    }

    // Helper: short W-beam segment along +X outer edge (galvanized, matches WB rail).
    auto emit_wbeam_seg = [&](float rail_x, float z0, float z1) {
        float d        = m_rail_width;
        float beam_top = m_rail_height;
        float y0 = beam_top, y1 = beam_top * 0.72f, y2 = beam_top * 0.55f, y3 = beam_top * 0.38f, y4 = beam_top * 0.15f;
        Vec3  nOut(1.f, 0.f, 0.f);
        Vec3  nIn(-1.f, 0.f, 0.f);
        float x0 = rail_x;
        builder.addSlopedQuad(x0, x0 + d, y0, y1, z1, z0, nOut, m_rail_tint, MAT_METAL, m_metal_tile);
        builder.addSlopedQuad(x0 + d, x0, y1, y2, z1, z0, nIn, m_rail_tint, MAT_METAL, m_metal_tile);
        builder.addSlopedQuad(x0, x0 + d, y2, y3, z1, z0, nOut, m_rail_tint, MAT_METAL, m_metal_tile);
        builder.addSlopedQuad(x0 + d, x0, y3, y4, z1, z0, nIn, m_rail_tint, MAT_METAL, m_metal_tile);
        float post_x = x0 + d + 0.06f * kFt;
        float post_w = 0.15f * kFt;
        builder.addVerticalFace(post_x, beam_top, z0 - post_w, z0 + post_w, kRight, m_rail_tint, MAT_METAL,
                                m_metal_tile);
    };

    // ── Ramp diverging section + chevron gore ────────────────────
    int   num_ramp_segs = 12;
    float ramp_seg_len  = gore_len / static_cast<float>(num_ramp_segs);

    for (int i = 0; i < num_ramp_segs; i++) {
        float z0 = ramp_start_z - static_cast<float>(i) * ramp_seg_len;
        float z1 = z0 - ramp_seg_len;
        float t0 = static_cast<float>(i) / static_cast<float>(num_ramp_segs);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_ramp_segs);

        float off0 = ramp_offset * t0 * t0;
        float off1 = ramp_offset * t1 * t1;

        float left0  = eb_right + off0;
        float right0 = left0 + ramp_width;
        float left1  = eb_right + off1;
        float right1 = left1 + ramp_width;

        Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.add_quad({Vec3(left0, y, z0), kUp, Vec2(left0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(right0, y, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(left1, y, z1), kUp, Vec2(left1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         {Vec3(right1, y, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         QuadWinding::Cross, m_asphalt_tint, MAT_ASPHALT);

        // White edge lines on ramp
        builder.addHorizontalQuad(left0, left0 + line_w, mark_y + 1.0f, z1, z0, kUp, m_white_marking);
        builder.addHorizontalQuad(right0 - line_w, right0, mark_y + 1.0f, z1, z0, kUp, m_white_marking);

        // Gore outline: mainline right edge + ramp left edge
        builder.addHorizontalQuad(eb_right - line_w, eb_right, mark_y, z1, z0, kUp, m_white_marking);
        builder.addHorizontalQuad(left0, left0 + line_w, mark_y, z1, z0, kUp, m_white_marking);

        // Diagonal chevrons in the triangular gore (mainline edge → ramp left).
        const float gore_gap = left0 - eb_right;
        if (gore_gap > 4.0f * line_w) {
            const float stripe_w = 1.0f * kFt;
            const float inset    = 2.0f * kFt;
            float       x_lo     = eb_right + inset;
            float       x_hi     = left0 - inset;
            if (x_hi > x_lo + stripe_w * 2.0f) {
                const int n_stripes = std::max(2, static_cast<int>((x_hi - x_lo) / (8.0f * kFt)));
                for (int s = 0; s < n_stripes; ++s) {
                    float u       = (static_cast<float>(s) + 0.35f) / static_cast<float>(n_stripes);
                    float band_lo = x_lo + u * (x_hi - x_lo - stripe_w);
                    float band_hi = band_lo + stripe_w;
                    // Slight Z stagger per stripe for a diagonal / read
                    float z_a = z0 - u * (z0 - z1) * 0.35f;
                    float z_b = z1 + (1.0f - u) * (z0 - z1) * 0.35f;
                    if (z_a > z_b)
                        std::swap(z_a, z_b);
                    builder.addHorizontalQuad(band_lo, band_hi, mark_y + 1.0f, z_a, z_b, kUp, m_white_marking);
                }
            }
        }

        emit_wbeam_seg(right0, z0, z1);
    }

    // ── N Marginal Road (runs parallel to LIE, offset to the right) ──
    float marginal_offset  = ramp_offset + ramp_width + RG::kExitMarginalGapFt * kFt;
    float marginal_width   = RG::kExitMarginalLaneFt * kFt;
    float marginal_left    = eb_right + marginal_offset;
    float marginal_start_z = ramp_end_z;
    float marginal_end_z   = z_far;

    builder.addHorizontalQuad(marginal_left, marginal_left + marginal_width, y, marginal_end_z, marginal_start_z, kUp,
                              m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);
    builder.addHorizontalQuad(marginal_left + marginal_width, marginal_left + 2.0f * marginal_width, y, marginal_end_z,
                              marginal_start_z, kUp, m_asphalt_tint, MAT_ASPHALT, m_asphalt_tile);

    float center_x = marginal_left + marginal_width;
    builder.addHorizontalQuad(center_x - line_w / 2.0f - 0.1f * kFt, center_x - 0.1f * kFt, mark_y, marginal_end_z,
                              marginal_start_z, kUp, m_yellow_marking);
    builder.addHorizontalQuad(center_x + 0.1f * kFt, center_x + line_w / 2.0f + 0.1f * kFt, mark_y, marginal_end_z,
                              marginal_start_z, kUp, m_yellow_marking);

    builder.addHorizontalQuad(marginal_left, marginal_left + line_w, mark_y, marginal_end_z, marginal_start_z, kUp,
                              m_white_marking);
    builder.addHorizontalQuad(marginal_left + 2.0f * marginal_width - line_w, marginal_left + 2.0f * marginal_width,
                              mark_y, marginal_end_z, marginal_start_z, kUp, m_white_marking);

    builder.addHorizontalQuad(eb_right + m_rail_width, marginal_left, 0.0f, marginal_end_z, marginal_start_z, kUp,
                              m_grass_tint, MAT_GRASS, m_grass_tile);

    builder.addHorizontalQuad(marginal_left + 2.0f * marginal_width,
                              marginal_left + 2.0f * marginal_width + 100.0f * kFt, 0.0f, marginal_end_z,
                              marginal_start_z, kUp, m_grass_tint, MAT_GRASS, m_grass_tile);

    float mr_curb_x = marginal_left + 2.0f * marginal_width;
    builder.addHorizontalQuad(mr_curb_x, mr_curb_x + m_curb_width, y + m_curb_height, marginal_end_z, marginal_start_z,
                              kUp, m_concrete_tint, MAT_CONCRETE, m_concrete_tile);
    builder.addVerticalFace(mr_curb_x, y + m_curb_height, marginal_end_z, marginal_start_z, kLeft, m_concrete_tint,
                            MAT_CONCRETE, m_concrete_tile);

    // ── Connecting ramp to Marginal Road + merge gore flare ──────
    float connect_start_z = ramp_end_z;
    float connect_end_z   = ramp_end_z - RG::kExitConnectFt * kFt;
    int   num_connect     = 6;
    float connect_seg     = (connect_start_z - connect_end_z) / static_cast<float>(num_connect);

    float ramp_end_left = eb_right + ramp_offset;

    for (int i = 0; i < num_connect; i++) {
        float z0 = connect_start_z - static_cast<float>(i) * connect_seg;
        float z1 = z0 - connect_seg;
        float t0 = static_cast<float>(i) / static_cast<float>(num_connect);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(num_connect);

        float left0  = ramp_end_left + t0 * (marginal_left - ramp_end_left);
        float right0 = left0 + ramp_width;
        float left1  = ramp_end_left + t1 * (marginal_left - ramp_end_left);
        float right1 = left1 + ramp_width;

        Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);

        builder.add_quad({Vec3(left0, y, z0), kUp, Vec2(left0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(right0, y, z0), kUp, Vec2(right0 / m_asphalt_tile, z0 / m_asphalt_tile), hTan},
                         {Vec3(left1, y, z1), kUp, Vec2(left1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         {Vec3(right1, y, z1), kUp, Vec2(right1 / m_asphalt_tile, z1 / m_asphalt_tile), hTan},
                         QuadWinding::Cross, m_asphalt_tint, MAT_ASPHALT);

        builder.addHorizontalQuad(left0, left0 + line_w, mark_y + 1.0f, z1, z0, kUp, m_white_marking);
        builder.addHorizontalQuad(right0 - line_w, right0, mark_y + 1.0f, z1, z0, kUp, m_white_marking);

        // Merge flare: white edge opening toward the marginal (photo 2 style)
        if (t0 > 0.35f) {
            float flare_w = (1.0f - t0) * 4.0f * kFt + line_w;
            builder.addHorizontalQuad(right0, right0 + flare_w, mark_y, z1, z0, kUp, m_white_marking);
        }

        emit_wbeam_seg(right0, z0, z1);
    }

    // Continue W-beam along N Marginal outer edge
    float mr_rail_x = marginal_left + 2.0f * marginal_width;
    float post_sp   = 12.5f * kFt;
    for (float z = marginal_end_z; z < marginal_start_z; z += post_sp) {
        float z1 = std::min(z + post_sp, marginal_start_z);
        emit_wbeam_seg(mr_rail_x, z1, z);
    }
}

}  // namespace swish
