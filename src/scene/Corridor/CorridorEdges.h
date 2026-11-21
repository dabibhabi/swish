#pragma once

#include "CorridorDiamonds.h"

#include <array>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// D2 corridor edges (debug only; docs/endless-road.md D2). NJ-shape median
// barrier, wide grass median with a cable barrier east of exit 64, W-beam
// shields at crossroad piers, sound walls between diamonds. Sizes in
// RoadParams road::edges. The release asset gets none of it (no exit table).
// ══════════════════════════════════════════════════════════════════════

// Wide-median stretch (exit table, `wideExtra` WU extra half-width), sound-wall and W-beam station ranges clear of
// `paths`' ramps and the crossroad decks. Call once before the paths exist (the wide median moves them), then again.
CorridorEdges corridor_edges(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, float wideExtra);

// Station of exit number n (lowest record, or highest with `last`, either direction); -1 when absent.
double exit_s(const CorridorAlignment& al, int n, bool last = false);

// NJ barrier cross-section, toe to toe over the top, CCW: (signed lateral, height above the toe), WU.
std::array<Vec2, 8> nj_profile();

// Median over [ts, te): NJ barrier (end caps, U-turn opening paved, none in the tube), or the wide grass median
// with a swale and a three-cable barrier. Replaces the C4 box barrier.
void emit_median(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double ts, double te,
                 double ox, double oz, const CorridorLook& look);

// Sound walls and W-beam runs clipped to [from, to), batched per 300 m tile and side.
void emit_edges(MeshBuilder& b, const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double from,
                double to, double ox, double oz, const CorridorLook& look);

}  // namespace swish
