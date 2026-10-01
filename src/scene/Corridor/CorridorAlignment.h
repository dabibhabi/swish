#pragma once

#include "CorridorAsset.h"
#include "utils/types/Types.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorAlignment — the corridor's curve, in double precision.
//
// The retired treadmill could treat "along the road" as world Z and
// "across the road" as world X, because the fictional road was axis-aligned.
// A real alignment curves, so every query the game needs — surface height,
// drivable bounds, how far along you are — has to be phrased in the
// corridor's own frame:
//
//     (station s, lateral t)  <->  (world x, z)
//
// with s measured along the centreline and t positive to the right of
// travel (eastbound). This class is the only thing that knows how to convert
// between the two, and both the mesh generator and the driving code use it,
// so geometry and physics cannot drift apart (the lesson Layer 4b of the old
// system learned the hard way — see docs/endless-road.md).
//
// Stations are uniformly spaced (header.stationSpacing), so s -> index is
// arithmetic rather than a search.
// ══════════════════════════════════════════════════════════════════════
class CorridorAlignment {
public:
    struct Sample {
        double x = 0.0, z = 0.0;       // world position on the centreline
        float  tx = 0.0f, tz = -1.0f;  // unit tangent (direction of travel)
        float  lanes = 3.0f;           // fractional lanes per carriageway
    };

    // Throws (see load_corridor_asset) if the baked asset is missing/stale.
    void load(const std::string& path);
    bool valid() const { return m_asset.stations.size() >= 2; }

    const CorridorHeader&                header() const { return m_asset.header; }
    const std::vector<CorridorLandmark>& landmarks() const { return m_asset.landmarks; }
    double                               total_length() const { return m_asset.header.totalLength; }

    // Centreline position/tangent/lanes at arc length `s` (clamped to the ends).
    Sample sample(double s) const;

    // Nearest point on the centreline to world (x, z). `hint` is an in/out
    // station index: the search only scans a window around it, so tracking
    // the car costs O(1) per frame instead of O(stations). Pass the previous
    // frame's value; on the first call pass any index (a cold `hint` just
    // means the window may miss, so seed it with `project_global`).
    void project(double x, double z, size_t& hint, double& s, float& lateral) const;

    // Full scan — for initial placement / teleports, where no hint exists.
    void project_global(double x, double z, size_t& hint, double& s, float& lateral) const;

    // Station index bracketing `s` (clamped), for callers walking the asset.
    size_t index_at(double s) const;

    // ── Cross-section at a station (all WU, |lateral| from the centreline) ──
    // inner edge of the travelled way (median side, inside the shoulder)
    float inner_edge(float lanes) const;
    // outer edge of the travelled way (shoulder side)
    float outer_edge(float lanes) const;
    // Lane-edge line offsets: the painted white/yellow edge lines.
    float paint_inner(float lanes) const;
    float paint_outer(float lanes) const;

    float road_y() const { return m_asset.header.roadY; }

    // Inside a turn-around pad the median barrier is omitted and asphalt is
    // laid across it, so the player can U-turn (spec decision #7). The pads
    // are the only place the corridor is laterally continuous.
    bool in_turnaround(double s) const;

private:
    CorridorAsset m_asset;
};

}  // namespace swish
