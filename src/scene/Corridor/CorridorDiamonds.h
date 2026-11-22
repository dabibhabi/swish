#pragma once

#include "CorridorRamps.h"

#include <array>
#include <utility>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// C2b mesh for the diamonds: service roads, ramps, crossroads and the
// graded verge between them. Paths come from corridor_diamond_paths; every
// emitter is clipped to one segment's station range, as C1's ramps are.
// Not drivable yet (C3).
// ══════════════════════════════════════════════════════════════════════

// Tints and texture tiles from config/road.toml (one source of truth for the road's look).
struct CorridorLook {
    Vec4  asphalt, grass, concrete, shoulder, white, yellow;
    float asphaltTile, grassTile, concreteTile;
};

// Shared C2b / C4 mesh helpers (CorridorDiamonds.cpp).
namespace cmesh {
// Corridor (s, signed lateral, y) to segment-local.
Vec3 at(const CorridorAlignment& al, double s, float lat, float y, double ox, double oz);
// One quad of a batch, front face along n (mesh_shapes::quad convention), UVs every `tile`.
void face(MeshBuilder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, const Vec3& n, float tile = 4000.0f);
void hexa(MeshBuilder& b, const std::array<Vec3, 8>& c,
          float tile = 4000.0f);  // six outward faces; corner bits 1 = a1, 2 = t1, 4 = top
// [lo, hi] minus `holes`.
std::vector<std::pair<double, double>> minus(double lo, double hi, std::vector<std::pair<double, double>> holes);
void obox(MeshBuilder& b, const Vec3& c, const Vec3& hx, const Vec3& hy, const Vec3& hz);  // centre + half extents
void rod(MeshBuilder& b, const Vec3& p, const Vec3& q, float w);                           // square bar p to q
void pole(MeshBuilder& b, const Vec3& base, const Vec3& top, float rad);                   // octagonal, no cap
}  // namespace cmesh

// Graded verge on side +-1 over [ts, te]: gore slope (mainline edge to ramp inner edge), D2 ditch and rounded cut
// slope (ramp outer edge or mainline edge up to the strip top at ground level), then level grass. One draw.
void emit_corridor_verge(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                         const std::vector<CorridorPath>& paths, int side, double ts, double te, double ox, double oz,
                         const CorridorLook& look);
// D2: verge height at station s, |lateral| `lat` from the centreline on side +-1 (what emit_corridor_verge draws).
float corridor_verge_y(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, int side, double s,
                       float lat);

// D5: far-ground width past the grass edge on side +-1 at s: al.edges().far, cut back on the inside of a curve (no
// fold-over) and to 0 near an LIE-over trench (its under-road runs out there).
float corridor_far_ground(const CorridorAlignment& al, int side, double s);

// A service road (curbs, MUTCD lines, stop bar + crosswalk at a crossroad) or a ramp (edge lines, gore
// chevrons), clipped to [from, to). Paint draws get the retroreflective tag.
void emit_diamond_path(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorPath& p,
                       const std::vector<CorridorPath>& paths, double from, double to, double ox, double oz,
                       const CorridorLook& look);

// Crossroad at level ground across the whole corridor: deck with parapets over the cut, piers at the median and
// the mainline edges, curbs, double yellow, lane lines, crosswalks outside each service road. C3a: stubs past each
// service road (walks, grass apron, crest hiding the end), stop bars, span-wire signals; lens draws go to `lenses`.
void emit_crossroad(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                    const CorridorCrossroad& c, const std::vector<CorridorPath>& paths, double ox, double oz,
                    const CorridorLook& look, std::vector<SignalLens>& lenses);

// C3a: mainline frames over [a, b] on the tiles' own stations (dip and bank included), for paint on part of a tile.
std::vector<SweepFrame> corridor_mainline_frames(const CorridorAlignment& al, double a, double b, double ox, double oz);
// C3a: HOV buffer lines (solid, broken in access zones) and diamonds, clipped to [from, to), one draw set per tile.
void emit_hov(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorHov& hov,
              double from, double to, double ox, double oz, const CorridorLook& look);

}  // namespace swish
