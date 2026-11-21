#pragma once

#include "CorridorDiamonds.h"
#include "scene/RoadScene/RoadParams.h"

#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// D6 landmarks (debug only; docs/endless-road.md D6). One-off structures from
// road::landmarks::kSpots (Unisphere, NYS Pavilion towers, Queens vent building),
// placed off an exit's records at their real side and distance. Boards live in
// CorridorSigns, headstones and street-tree rows in the D5 kits, deck styles here.
// The release asset has no exit table, so none of this runs there.
// ══════════════════════════════════════════════════════════════════════

// Station of a spot: mean of the exit's first and last record plus `along`; < 0 when the exit is missing.
double landmark_s(const CorridorAlignment& al, const road::landmarks::Spot& sp);

// Crossroad deck styles from road::landmarks::kStyles (by exit number), after D4 restyled them.
void landmark_styles(std::vector<CorridorCrossroad>& cross);

// Spots whose station is in [from, to): one batch per material.
void emit_landmarks(MeshBuilder& b, const CorridorAlignment& al, double from, double to, double ox, double oz,
                    const CorridorLook& look);

}  // namespace swish
