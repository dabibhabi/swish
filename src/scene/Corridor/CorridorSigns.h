#pragma once

#include "CorridorDiamonds.h"
#include "SignAtlas.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
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
    int      board = -1, board2 = -1;  // N3: sign_atlas::kNetBoards instead of a panel; board2 left of board
    // N6: post = parkway roadside board on two posts (road::network::kPostScale); strip = the D3 distance strip under
    // a board without one; arrow = the exit-direction strip; crop = drop an N3-N5 board's distance line; closed = the
    // EXIT CLOSED panel over its lower part; left = a left exit (over the left lane, arrow mirrored); clear = lateral
    // the outer column / post must stand past (a ramp alongside), 0 = none.
    bool  post = false, strip = false, arrow = false, crop = false, closed = false, left = false;
    float clear = 0.0f;
};
struct SignGore {
    double s     = 0.0;
    int    side  = 1;
    int    panel = -1;
    int    cell  = -1;  // N3: sign_atlas::kNetGores instead of the panel's gore
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

// N3: road `road`'s rows of road::network::kGuideSigns on `al`. goreAt(recordStation, side) = the gore of the exit at
// that record; junctions = (name, station on `al`) for junction anchors. Gantries on a deck are kept (network roads).
void add_guide_signs(CorridorSigns& out, const CorridorAlignment& al, int road,
                     const std::function<double(double, int)>&          goreAt,
                     const std::vector<std::pair<std::string, double>>& junctions);

// N6: every exit of network road `road` (sign_atlas::kNetExits) in its drivable range [lo, hi]: where no kGuideSigns
// row has it, advance boards at 1 mile / 1/2 mile (advance_gantries) and its gore sign; an exit-direction sign
// kDirBefore before the gore; an EXIT CLOSED panel on every board of a closed exit. exitAt(record station, side) = its
// ramp. Parkways (kParkways) get roadside boards. clearOf(s, side) = lateral a sign there must stand past (ramps
// alongside).
struct ExitSite {
    double gore   = -1.0;  // -1: no ramp leaves the road there
    bool   closed = false, left = false;
};
void add_exit_signs(CorridorSigns& out, const CorridorAlignment& al, int road, double lo, double hi,
                    const std::function<ExitSite(double, int)>& exitAt,
                    const std::function<float(double, int)>&    clearOf);
// N6: kParkways holds road.
bool is_parkway(int road);

// Textured quad: centre c, half extents r (viewer's right) and u (up), facing n; uv = pixel rect px of a W x H atlas.
void sign_quad(MeshBuilder& b, const Vec3& c, const Vec3& r, const Vec3& u, const Vec3& n, const sign_atlas::Rect& px,
               int W, int H);

// Gantries, gore signs, markers and boards whose station is in [from, to). Faces are retroreflective.
void emit_signs(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                const std::vector<CorridorPath>& paths, const CorridorSigns& signs, double from, double to, double ox,
                double oz, const CorridorLook& look);

}  // namespace swish
