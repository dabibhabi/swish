#pragma once

#include "CorridorDiamonds.h"
#include "SignAtlas.h"

#include <cstdint>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// D3 guide signs (debug only; docs/endless-road.md D3). Per exit ramp: overhead
// galvanised box-truss gantries at 1 mile and 1/2 mile before the gore, an E5-1a
// gore sign on a breakaway post pair, and D10 mile markers every 0.2 real mile.
// Text lives in the baked atlases (tools/sign_corridor.py -> SignAtlas.h); sizes in
// RoadParams road::signs. Distances on the faces are REAL miles (the HUD's unit),
// placed at the baked equivalent, so the compacted route squeezes them: see
// advance_gantries for the collapse rule.
// ══════════════════════════════════════════════════════════════════════

// Distance strip under a panel, in sign_atlas::kStrips order.
enum class SignDist : uint8_t { OneMile, Half, Quarter };

struct AdvanceGantry {
    double   dist  = 0.0;  // travel distance before the gore (WU)
    SignDist label = SignDist::OneMile;
};

// Collapse rule. `room` = free travel distance before the gore (past the previous gore + kAfterGore), `wuPerMile`
// = baked WU per real mile. Both 1 mile and 1/2 mile fit: two gantries. Only 1/2 mile fits: one. Neither: one
// at 1/4 mile if that fits, else none. A face always shows the distance it stands at (corridor_signs then steps
// a gantry off a crossroad deck, upstream when there is room, so the deck never hides it).
std::vector<AdvanceGantry> advance_gantries(double room, double wuPerMile);

struct SignGantry {
    double   s     = 0.0;
    int      side  = 1;   // +1 EB (right of rising stations), -1 WB
    int      panel = -1;  // sign_atlas::kPanels index
    SignDist label = SignDist::OneMile;
};
struct SignGore {
    double s     = 0.0;
    int    side  = 1;
    int    panel = -1;
};
struct SignMarker {
    double s    = 0.0;
    int    side = 1;
    int    mile = 0, tenth = 0;  // real mileage mile.tenth (tenth even)
};
// D6 landmark board (road::landmarks::kBoards) on two posts; lat = |lateral| on its side, foot = ground y.
struct SignBoard {
    double s    = 0.0;
    int    side = 1;
    float  lat = 0.0f, foot = 0.0f, w = 0.0f, h = 0.0f, lift = 0.0f;
    int    cell = 0;  // sign_atlas::kLandmarkBoards index
};
struct CorridorSigns {
    std::vector<SignGantry> gantries;
    std::vector<SignGore>   gores;
    std::vector<SignMarker> markers;
    std::vector<SignBoard>  boards;                       // D6
    float                   sheen = road::signs::kSheen;  // face roughness multiplier
};

// Baked WU per real mile (the HUD's real/baked ratio).
double sign_wu_per_mile(const CorridorAlignment& al);
// sign_atlas::kPanels index for an exit number and direction, -1 when the atlas has none.
int sign_panel(int exitNo, bool westbound);
// Sites from the diamonds' exit ramps (paths) and crossroads, plus the D6 boards; empty without an exit table.
CorridorSigns corridor_signs(const CorridorAlignment& al, const std::vector<CorridorPath>& paths);

// Textured quad: centre c, half extents r (viewer's right) and u (up), facing n; uv = pixel rect px of a W x H atlas.
void sign_quad(MeshBuilder& b, const Vec3& c, const Vec3& r, const Vec3& u, const Vec3& n, const sign_atlas::Rect& px,
               int W, int H);

// Gantries, gore signs, markers and boards whose station is in [from, to). Faces are retroreflective.
void emit_signs(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                const std::vector<CorridorPath>& paths, const CorridorSigns& signs, double from, double to, double ox,
                double oz, const CorridorLook& look);

}  // namespace swish
