#pragma once

#include "CorridorDiamonds.h"

#include <cstdint>
#include <vector>

namespace swish {

class CorridorNetwork;

// ══════════════════════════════════════════════════════════════════════
// N2b work zones (debug only; docs/endless-road.md "Work zones (N2b)").
// A freeway full closure at each network wall, laid out from MUTCD Part 6 and
// NYSDOT 619 (docs/queens-network-reference/workzone.md): advance signs at
// the freeway A / B / C spacing, one merging taper L = W S per closed lane with
// a 2L tangent between them and an arrow board at each, the buffer, a median
// U-turn gap, Type III barricades with R11-2, a sand-barrel cushion and a
// full-width portable concrete barrier wall. Layout is pure (tested); the
// mesh lives in CorridorWorkZoneMesh.cpp.
// ══════════════════════════════════════════════════════════════════════

enum class WzKind : uint8_t { Drum, DrumDown, Cone, ConeDown, Barricade, Sign, Arrow, Wall, Barrel, Pile };
// Sign faces in sign_atlas::kWorkZone order; None = a barricade without a board.
enum class WzFace : uint8_t {
    RoadWork,     // W20-1 ROAD WORK 1 MILE
    LaneClosed1,  // W20-5 RIGHT LANE CLOSED 1/2 MILE (+1, +2: W20-5a RIGHT 2 / 3 LANES)
    LaneClosed2,
    LaneClosed3,
    LaneEnds,     // W4-2R
    RoadClosed,   // R11-2
    ExitClosed,   // E5-2a style panel
    EndRoadWork,  // G20-2
    Speeding,     // NYR9-11
    None
};

struct WzItem {
    WzKind  kind = WzKind::Drum;
    WzFace  face = WzFace::None;
    double  s    = 0.0;   // station along its piece (road or ramp arc length)
    float   lat  = 0.0f;  // lateral, positive right of rising s
    double  x = 0.0, z = 0.0;
    float   y  = 0.0f;             // ground under it
    float   fx = 0.0f, fz = 0.0f;  // unit facing: toward the traffic it faces
    float   len   = 0.0f;          // barricade / wall segment width across the facing
    float   dy    = 0.0f;          // wall segment: height change over len (bank)
    float   spin  = 0.0f;          // debris yaw (rad)
    uint8_t taper = 0;             // wall segment: bit 0 / 1 = tapered end at -len / +len side
};

struct WorkZoneParams {
    float spacing = 1.0f;     // advance-sign spacing x (MUTCD = 1)
    float drums   = 1.0f;     // channelizer count x (spacing / drums)
    float tailM   = 2400.0f;  // horizon tail past the wall (m): data, else extruded along the end tangent
};

struct WorkZone {
    int    road = 0, end = 1, dir = 1, lanes = 0;  // dir = travel direction of the approach (+1 = rising s)
    double wall = 0.0, face = 0.0;                 // hold station, barrier face station
    double taper0 = 0.0, gap0 = 0.0, gap1 = 0.0;   // first taper start; median gap [gap0, gap1] (sorted)
    float  L = 0.0f, buffer = 0.0f, k = 1.0f;      // merging taper and buffer as built, squeeze factor
    double tailFrom = 0.0;                         // data end the tail extrudes from (tailLen 0 = none)
    float  tailLen  = 0.0f;
    std::vector<WzItem> items;
};

// MUTCD Table 6B-2 buffer (ft) at speed S (mph), interpolated, clamped to 45..70.
float wz_buffer_ft(float mph);
// Median gap [g0, g1] before a wall at station `wall` on end `end` (0 = low-s end).
void wz_gap(double wall, int end, double& g0, double& g1);
// Barrier face station for a wall.
double wz_face(double wall, int end);

// The closure at a wall. `limit` = station the transition must stay clear of (last ramp touchdown or the other
// drivable end), `far` = other drivable end (signs past it are dropped).
WorkZone work_zone(const CorridorAlignment& al, int road, double wall, int end, double limit, double far,
                   const WorkZoneParams& p);

// N6 kit: a short EXIT CLOSED zone kExitGore past ramp `ramp`'s gore (edge drums, barricades with the board, cones).
WorkZone exit_closure(const CorridorNetwork& net, int ramp, const WorkZoneParams& p);

// ── Mesh (CorridorWorkZoneMesh.cpp): batched per 300 m cluster; arrow lamps -> `flash` (group 2), warning
// lights (group 3); the caller toggles their glow.
void emit_work_zone(MeshBuilder& b, std::vector<DrawCall>& draws, std::vector<SignalLens>& flash, const WorkZone& wz,
                    double ox, double oz, const CorridorLook& look);
// Unpainted road past the data end along its end tangent (asphalt, median barrier, grass).
void emit_work_zone_tail(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const WorkZone& wz,
                         double ox, double oz, const CorridorLook& look);

}  // namespace swish
