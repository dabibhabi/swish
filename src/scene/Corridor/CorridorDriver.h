#pragma once

#include "CorridorAlignment.h"
#include "CorridorScene.h"

namespace swish {

class Renderer;
class CarEntity;

// ══════════════════════════════════════════════════════════════════════
// CorridorDriver — finite-corridor mechanics (Phase 3a).
//
// The Treadmill's replacement. What carries over, what doesn't:
//
//  KEPT — the origin rebase, though it turned out NOT to be load-bearing here.
//    Measured (spec §7 asked for this rather than an assumption): 18 mi at
//    WORLD_SCALE = 1000 is 2.897e7 WU, where float32's ULP is 2.0 WU = 2 mm.
//    That is fine. The design note's "~3e10 WU, far beyond float32" applied
//    WORLD_SCALE twice — one mile is 1.609e6 WU, so the corridor is 1000x
//    smaller than that estimate, and the 2048 WU (2 m) quantisation it feared
//    does not occur. Being finite, the corridor also has none of the endless
//    treadmill's unbounded accumulation.
//
//    It is kept because it is nearly free and strictly better: it holds the
//    car and camera under kRebaseThreshold (ULP 0.125 WU), so there is no
//    millimetre-scale shimmer at the east end, and it keeps the scheme valid
//    if the corridor is ever lengthened.
//
//    One difference from the old scheme, and it is what makes a rebase legal
//    on a NON-periodic road: the treadmill could only shift by whole chunk
//    lengths, because it relied on the tile looking identical afterwards. Here
//    every segment's render offset is recomputed from its double-precision
//    origin every frame, so a shift of any size moves *all* geometry together
//    — nothing moves relative to anything else, and the shift is invisible by
//    construction rather than by periodicity.
//
//  KEPT — the idea that the drivable surface, not world X, defines the bounds.
//
//  DROPPED — chunk instancing, sparse interchange slots, the intro/endless
//    split, and the world-X lateral clamp. A real alignment curves, so "how
//    far along" and "how far across" are (station, lateral) in the corridor's
//    own frame, not (z, x). CarEntity's own X-clamp is disabled (bounds set
//    wide open) and the lateral clamp happens here, in corridor space.
//
//  NEW — the corridor is finite. The car is stopped at each end, and the
//    median barrier is omitted over a short pad there so the player can U-turn
//    (spec decision #7). There is no other way off the corridor.
// ══════════════════════════════════════════════════════════════════════
class CorridorDriver {
public:
    // `alignment` is borrowed and must outlive this object (App owns it).
    void set_alignment(const CorridorAlignment* alignment);
    void set_segments(std::vector<CorridorPlacement> placements);

    // Puts the car at the west end (Queens-Midtown Tunnel portal), in the
    // right-hand eastbound lane, pointing down-corridor.
    void place_at_start(CarEntity& car);

    // Per-frame: rebase the origin, refresh the visible-segment window, clamp
    // the car into its carriageway, and stop it at the ends. (App snaps the car's Y.)
    void update(CarEntity& car, Renderer& renderer, float deltaTime, bool debugEdit);

    // renderZ = trueZ + origin_shift().
    double origin_shift() const { return m_originShift; }

    // Live state, for the debug readout and for tests.
    double station() const { return m_station; }
    float  lateral() const { return m_lateral; }
    float  lanes() const { return m_lanes; }
    bool   eastbound() const { return m_side >= 0; }

    // The lateral window the car is confined to at station `s` for a car
    // committed to carriageway `side` (+1 eastbound / -1 westbound). Inside a
    // turn-around pad the median opens and `side` is ignored. Pure function of
    // the alignment — unit-tested directly.
    void carriageway_bounds(double s, int side, float& minLat, float& maxLat) const;

private:
    const CorridorAlignment*       m_al = nullptr;
    std::vector<CorridorPlacement> m_segments;

    // Accumulated origin-rebase shift (WU, double; render frame = true + this).
    double m_originShift = 0.0;

    // Corridor-frame tracking of the car. m_hint is the station index the
    // incremental nearest-point search starts from.
    size_t m_hint    = 0;
    double m_station = 0.0;
    float  m_lateral = 0.0f;
    float  m_lanes   = 3.0f;
    bool   m_seeded  = false;
    // Committed carriageway: +1 eastbound (lateral > 0), -1 westbound. Changes
    // ONLY inside a turn-around pad, which is what makes the median barrier
    // impassable — see carriageway_bounds in CorridorBounds.cpp.
    int m_side = +1;

#ifdef SWISH_DEBUG_UI
    // Throttle for the ~1 Hz corridor telemetry line (see update()).
    float m_logTimer = 0.0f;
#endif
};

}  // namespace swish
