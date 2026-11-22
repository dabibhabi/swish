#pragma once

#include "CorridorAlignment.h"
#include "CorridorDiamonds.h"
#include "scene/RoadScene/RoadParams.h"

#include <array>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// N3 network-road structures (debug-only; docs/endless-road.md "BQE and its
// junctions (N3)"). A road baked with a GRND block (its DEM ground per
// station) stands on a viaduct where its profile is kViaductMinH over that
// ground and runs in a trench where it is kTrenchMinD under it. The graded
// verge is cut there (CorridorAlignment::verge_holes) and this file draws the
// structure and the ground instead. Pure geometry, no Vulkan.
// ══════════════════════════════════════════════════════════════════════

struct RoadStructure {
    double s0 = 0.0, s1 = 0.0;
    bool   trench = false;
};

// Spans in station order; empty for a road without a GRND block. Spans shorter than kMinSpan are dropped.
std::vector<RoadStructure> road_structures(const CorridorAlignment& al);

struct StructureParams {
    float pierSpacing = 30.0f;  // m between pier bents (slider)
    float wallAbove   = 1.07f;  // m of retaining wall above the ground at the top of a trench (42 in, slider)
    bool  signs       = true;   // network guide signs (gantries, gores; slider)
    bool  landmarks   = true;   // N8 network landmarks (slider)
    float signNits    = road::netmarks::kPriceNits;  // N8 lit landmark signs, cd/m2 (slider)
};

// [from, to) of road `al`. Viaduct: NJ parapets, fascia, soffit, three-column pier bents, the ground under it;
// where the deck spans the pavement of a road in `under` below it (N5: any network road), green steel plate girders
// and no piers or ground. Trench:
// retaining walls on an NJ toe, coping, the ground at the top.
// `keepOut` = world xz boxes (x0, z0, x1, z1) with no ground band: interchange footprints, where the ramps and the
// other road's verge own the ground.
using XzBox = std::array<double, 4>;
void emit_road_structures(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                          const std::vector<RoadStructure>& st, const std::vector<const CorridorAlignment*>& under,
                          const std::vector<XzBox>& keepOut, double from, double to, double ox, double oz,
                          const CorridorLook& look, const StructureParams& p);

// NJ median barrier along mainline frames (N3 network roads with a profile; the BQE's "low NJ median" [SV-2]).
void emit_nj_median(MeshBuilder& b, const std::vector<SweepFrame>& f, float ry, const CorridorLook& look);

// A street, footway or track crossing over the road (XING RoadOver / RailOver): a deck spanning both edges at
// max(ground + wall, road + MUTCD 16 ft clearance), girders, parapets and a wall pier each side; a track gets
// ballast, rails and through girders (an IRT elevated line rides higher).
void emit_overpass(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorCrossing& x,
                   double ox, double oz, const CorridorLook& look, const StructureParams& p);

}  // namespace swish
