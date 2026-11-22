#pragma once

#include "CorridorDiamonds.h"

#include <functional>
#include <utility>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// D4 overpasses (debug only; docs/endless-road.md D4). OSM crossings from the
// XING asset block become bridges: road / rail over the LIE (girders, arches,
// through-girders on spill-through abutments) and the LIE over a road (deck,
// trench, U-walls). C2b crossroad decks get the same parts in their style.
// Sizes in RoadParams road::bridges. Release has no XING block: nothing here runs.
// ══════════════════════════════════════════════════════════════════════

// Kept bridges, sorted by s. A crossing whose footprint overlaps a crossroad's diamond is the same structure:
// the nearest road-over gives the crossroad its style, the rest are dropped. Overlapping crossings keep the
// wider one. Also dropped: tunnel, plaza and east end; LIE-over in a dip or the wide median. `cross` is
// restyled in place (district style where no crossing joins it). `clearFt` = clearance over the mainline.
std::vector<CorridorBridge> corridor_bridges(const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
                                             std::vector<CorridorCrossroad>& cross, float clearFt);

// One bridge, whole (the segment holding its station). Over: deck, girders, bearings, caps, piers, spill slopes,
// approach embankments, road and paint. LIE over: deck under the mainline and service roads, trench, under-road.
void emit_bridge(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorBridge& br,
                 const std::vector<CorridorPath>& paths, double ox, double oz, const CorridorLook& look);

// N4: |a| (a across the LIE) of the flat under-road at yLow beneath LIE-over bridge `br`; the D4 under-road then
// climbs to the LIE's ground over rb::kLieClimb, a network road under it (br.net) back to its own profile.
float lie_over_flat(const CorridorAlignment& al, const CorridorBridge& br);

// Station ranges on `side` cut out of the verge for LIE-over trenches (emit_bridge fills the skew wedges).
std::vector<std::pair<double, double>> bridge_verge_holes(const CorridorAlignment& al, int side);

// Deck frame: girders run along et, supports along ea (both may be skewed, not unit); y absolute, plus slopes
// gr along a and gt along t (crossroad grade, LIE bank and grade).
struct DeckFrame {
    Vec3  O, ea, et;
    float gr = 0.0f, gt = 0.0f;
    Vec3  P(float a, float t, float y) const { return O + ea * a + et * t + Vec3(0.0f, y + gr * a + gt * t, 0.0f); }
};

// Superstructure and supports in `style` over t in [t0, t1]: slab (top `top`, girders down to `soffit`), drip
// notch, bearings, pier caps at `piers` (median wall where t = 0, bents / walls elsewhere), abutment seats at
// t0 / t1 when `seats`, F-shape (or stone) parapets, fence frame when `fence`. `base(t)` = pier foot height.
struct DeckSpec {
    DeckFrame          F;
    BridgeStyle        style = BridgeStyle::Steel;
    float              A = 0.0f, top = 0.0f, soffit = 0.0f, t0 = 0.0f, t1 = 0.0f;
    std::vector<float> piers;
    bool               seats = true, fence = false, nassau = false;
    float              medianHalf = 0.0f;  // median wall pier half thickness
};
void emit_deck(MeshBuilder& b, std::vector<DrawCall>& draws, const DeckSpec& d, const std::function<float(float)>& base,
               const CorridorLook& look);

// Segmental stone arches over the spans between `sup` (t, ascending), spring at `spring`, deck top `top`: ashlar
// fascia on both edges a = +-A, concrete vault, stone parapets over [p0, p1]. N4: also the GCP's 1930s overpasses.
void emit_arch(MeshBuilder& b, std::vector<DrawCall>& draws, const DeckFrame& F, float A, const std::vector<float>& sup,
               float spring, float top, float p0, float p1, const CorridorLook& look);
// Stone arch rise for a span: rb::kArchRise of it, capped at rb::kArchRiseMax.
float arch_rise(float span);

}  // namespace swish
