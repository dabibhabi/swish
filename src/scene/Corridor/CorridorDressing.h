#pragma once

#include "CorridorDiamonds.h"
#include "CorridorScene.h"
#include "scene/RoadScene/RoadParams.h"

#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// D5 roadside dressing (debug only; docs/endless-road.md D5). District kits
// (road::dressing::kKits, by exit number) pick the street lamps, the trees
// and the buildings beyond the edges; a skyline card stands in for Midtown.
// The release asset has no exit table, so none of this runs there.
// ══════════════════════════════════════════════════════════════════════

// Kit at station s: the last kit whose exit lies at or before s (the first kit before its exit).
const road::dressing::Kit& dress_kit(const CorridorAlignment& al, double s);

// Street lamps over [s0, s1) per kit: Queens median twin arms (60 m) + outer singles, Nassau / west Suffolk shoulder
// singles with shoebox or cobra heads, none east of exit 64. Steel, heads, lenses and pedestals are one draw each;
// one spot light per pole into `lamps`. `signs` (may be null): poles keep clear of gantry columns and roadside signs.
void emit_lamps(MeshBuilder& b, std::vector<DrawCall>& draws, std::vector<SpotLightDesc>& lamps,
                const CorridorAlignment& al, const std::vector<CorridorPath>& paths, const CorridorSigns* signs,
                double s0, double s1, double ox, double oz, const CorridorLook& look);

// Trees (crossed cards, no sun shadow) and buildings past the edges over tile [ts, te), kit at the tile middle.
// At most two draws per side (buildings, trees). Density al.edges().dress; out to the far ground.
void emit_dressing(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                   const std::vector<CorridorPath>& paths, double ts, double te, double ox, double oz);

// Skyline: one camera-facing card (draw 0) around a local origin at Midtown (originX/Z = its world position).
CorridorSegment skyline_segment(const CorridorAlignment& al);

// Per-frame card model for a camera at `cam` and the card origin at `anchor` (both render frame): scaled about the
// camera by k = kSkyDist / R and turned to face it, so bearing and angular size match a card at range R. False
// beyond kSkyMaxR (hide it).
bool skyline_model(const Vec3& cam, const Vec3& anchor, Mat4& model);

}  // namespace swish
