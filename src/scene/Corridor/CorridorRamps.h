#pragma once

#include "CorridorAlignment.h"
#include "scene/RoadScene/RoadParams.h"
#include "scene/SceneTypes.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace swish {

class MeshBuilder;

// C2b diamond dimensions live in RoadParams.h (road::diamond); short alias for the corridor code.
namespace diamond = road::diamond;

// ══════════════════════════════════════════════════════════════════════
// Ramps, service roads and median crossovers in corridor frames (C1).
// Ported from RoadScene_Ramps.cpp (generate_exit_ramp / generate_service_roads /
// generate_crossover), which stays as-is for the golden mesh test and the lab.
//
// One shape covers all three: a band whose centreline offset and height ease
// with smoothstep from (lat0, dy0) at s0 to (lat1, dy1) at s1. Bank 0: the band
// is level at the highest point of the mainline surface plane under it,
// surface_y(s, lat) + |bank| * halfWidth, plus dy.
// Not drivable yet (C3 adds the bounds).
// ══════════════════════════════════════════════════════════════════════
struct CorridorPath {
    enum class Kind : uint8_t { Plain, Service, Ramp };
    double s0 = 0.0, s1 = 0.0;        // station range (WU)
    float  lat0 = 0.0f, lat1 = 0.0f;  // centreline offset, positive right of travel (WU)
    float  dy0 = 0.0f, dy1 = 0.0f;    // height above the base (WU)
    float  halfWidth  = 0.0f;
    bool   centreLine = false;  // two-way road: double yellow down the middle
    // C2b. side +-1: lat is measured outward from the mainline outer edge on that side (0 = absolute, C1).
    Kind kind  = Kind::Plain;
    int  side  = 0;
    int  lanes = 1;
    // Base height: 0 = mainline surface plane, 1 = level ground; eased over u in [h0, h1] with dy.
    float g0 = 0.0f, g1 = 0.0f;
    float h0 = 0.0f, h1 = 1.0f;
};

// Signed lateral, surface height and d(lat)/ds of `p` at station `s` (clamped to the path).
struct PathPoint {
    float lat = 0.0f, y = 0.0f, dlat = 0.0f;
};
PathPoint corridor_path_at(const CorridorAlignment& al, const CorridorPath& p, double s);

// Eastbound exit ramp from the outer shoulder edge to the service road's near lane; lengths from RoadParams kExit.
CorridorPath corridor_exit_ramp(const CorridorAlignment& al, double s0, float dy);
// Eastbound two-lane service road past the ramp's landing (constant offset, height dy).
CorridorPath corridor_service_road(const CorridorAlignment& al, double s0, double s1, float dy);
// S-curve from the EB inner lane to the WB inner lane across the median.
CorridorPath corridor_crossover(const CorridorAlignment& al, double sCenter, double halfLen);

// Crossroad pavement half-width along s: two 12 ft lanes each way plus the gutter.
float corridor_cross_half(const CorridorAlignment& al);
// C2b: one crossroad per exit number. EB/WB records paired by the ref's leading digits; the crossroad sits
// midway between the paired diverges (lone diverge: kUnpairedLead past it), crossroads closer than
// kMinSpacing to the previous one are dropped. Depth is raised where bank would eat the 16 ft clearance.
std::vector<CorridorCrossroad> corridor_crossroads(const CorridorAlignment& al, float depth);
// C2b: continuous service roads (split at each crossroad, stubs where OSM has none) and four ramps per
// crossroad, from al.service_roads() / al.crossroads() / al.sr_strip().
std::vector<CorridorPath> corridor_diamond_paths(const CorridorAlignment& al);
// Inverse smoothstep: u in [0, 1] along `p` where its centre offset (lat) equals `offset`. Finds a ramp's
// nose (inner edge leaves the shoulder) and touch (outer edge reaches the service road's inner curb).
float ramp_u_from_offset(const CorridorPath& p, float offset);

// ── C3a: cross-street stubs, signals, HOV lane ──
// Stub of crossroad `s` on side +-1: |t| from the service road's outer edge (t0) to the stub end (t1).
struct CrossStub {
    double s    = 0.0;
    int    side = 0;
    float  t0 = 0.0f, t1 = 0.0f;
};
// Outer edge |t| of the service-road band carried across crossroad `cs` on `side`; 0 when none meets it.
float crossroad_sr_outer(const CorridorAlignment& al, double cs, const std::vector<CorridorPath>& paths, int side);
// One stub per crossroad side where a service road meets it; empty while al.cross_stub() is 0.
std::vector<CrossStub> corridor_cross_stubs(const CorridorAlignment& al, const std::vector<CorridorPath>& paths);

// Span-wire signal lenses: group 0 = service-road approach, 1 = crossroad approaches; colour 0 red, 1 amber, 2 green.
struct SignalLens {
    uint32_t draw   = 0;
    uint8_t  group  = 0;
    uint8_t  colour = 0;
};
// Two-phase fixed cycle (seconds); group 1 runs half a cycle behind group 0. Exactly one colour lit per group.
bool  signal_lit(int group, int colour, double t, float cycle);
Vec4  signal_lens_color(int colour, bool lit);
float signal_lens_nits(int colour);

// HOV lane: median-side lane both ways from exit kHovFromExit to kHovToExit. [0] = EB, [1] = WB.
struct CorridorHov {
    double                                 s0 = 0.0, s1 = 0.0;  // empty when s1 <= s0 (no exit table)
    std::vector<std::pair<double, double>> zones[2];            // access zones: buffer lines broken
    std::vector<double>                    diamonds[2];         // diamond centres
};
CorridorHov corridor_hov(const CorridorAlignment& al);

// Sweep frames for `p` clipped to [from, to], local to (ox, oz); empty when they do not overlap.
// Clipping is how a ramp crossing a segment edge is split: both halves sample the same curve.
std::vector<SweepFrame> corridor_path_frames(const CorridorAlignment& al, const CorridorPath& p, double from, double to,
                                             double ox, double oz);

// Asphalt, white edge lines and (centreLine) a double yellow.
void emit_corridor_path(MeshBuilder& b, const std::vector<SweepFrame>& f, const CorridorPath& p, const Vec4& asphalt,
                        const Vec4& white, const Vec4& yellow, float tile);

}  // namespace swish
