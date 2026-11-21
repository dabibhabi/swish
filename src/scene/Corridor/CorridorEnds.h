#pragma once

#include "CorridorDiamonds.h"
#include "CorridorSurface.h"
#include "utils/light/SkyVisibility.h"

#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// C4 route ends (debug only; docs/endless-road.md C4).
// West: the Queens-Midtown Tunnel's Queens tube end (tiled walls, walks, ceiling, luminaire rows, portal,
// embankment) and the old toll plaza footprint past it (D6: no gantry; cashless tolls are at the Manhattan end).
// East: a lane drop, splitter island and a single-lane roundabout (fictional) that turns the car onto the westbound
// side. Sizes in RoadParams road::ends.
// Queries (CorridorEnds.cpp) are GPU-free for the tests; mesh lives in CorridorEndsMesh.cpp.
// ══════════════════════════════════════════════════════════════════════

// Ends for this alignment: portal kPortalLead before the EB exit 13 diverge, roundabout at the last station.
// No ends (ringC 0) when the asset has no exit table.
CorridorEnds corridor_ends(const CorridorAlignment& al);

// Roundabout windows at station s (both sides, no inset); empty off the ring.
std::vector<DriveSpan> ring_spans(const CorridorAlignment& al, double s);

// Luminaire spots in tube `side` over [s0, s1), true world frame. `lux` = interior road illuminance; boosted near
// the portal (CIE 88 threshold zone for the WB tube entering, exit zone for the EB tube leaving).
std::vector<SpotLightDesc> corridor_tunnel_lights(const CorridorAlignment& al, int side, double s0, double s1,
                                                  float lux);

// Sky-occlusion boxes over the tube chunks nearest station s, render frame (z + zShift); empty past the plaza.
std::vector<util::light::RoofVolume> corridor_tunnel_roofs(const CorridorAlignment& al, double s, double zShift);

// ── Mesh, clipped to one segment [from, to), local to (ox, oz) ──
// Tubes, west end cap, portal headwall, embankment and wing walls (D6: Queens toll gantry removed).
void emit_tunnel(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double from, double to,
                 double ox, double oz, const CorridorLook& look);
// Median: D2 emit_median in CorridorEdges.h (NJ barrier, U-turn opening paved, wide grass median).
// Approach signs (blank boards for D3), splitter island, roundabout, island, outer barrier, grass past the end.
void emit_road_end(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double from, double to,
                   double ox, double oz, const CorridorLook& look);

}  // namespace swish
