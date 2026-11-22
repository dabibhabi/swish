#pragma once

#include "CorridorAsset.h"
#include "utils/types/Types.h"

#include <cmath>
#include <utility>
#include <vector>

namespace swish {

// C2b: one crossroad per exit number. The mainline dips `depth` under it; ramp lengths either side (WU).
struct CorridorCrossroad {
    double      s         = 0.0;
    float       depth     = 0.0f;
    float       lenBefore = 0.0f, lenAfter = 0.0f;
    int         exitNo = 0;                   // D3: the exit number it was built from (sign text)
    BridgeStyle style  = BridgeStyle::Steel;  // D4: from the OSM crossing it absorbed, else the district
};

// D4: an OSM crossing kept after the merge with the crossroads (corridor_bridges, CorridorBridges.h). All WU.
// Deck frame: a along s, t across, a point at (a, t) sits at station s + a + t * tan(skew).
struct CorridorBridge {
    double       s         = 0.0;
    float        skew      = 0.0f;
    float        halfA     = 0.0f;          // deck half-extent in a (real half width / cos skew)
    float        reach[2]  = {0.0f, 0.0f};  // |t| of the abutment face (over) or trench end (LIE over); [0] t < 0
    float        half      = 0.0f;          // footprint half-length along s inside the corridor (skew included)
    float        yLow      = 0.0f;          // over: girder soffit / arch spring; LIE over: under-road surface
    CrossingKind kind      = CrossingKind::RoadOver;
    BridgeStyle  style     = BridgeStyle::Steel;
    bool         railUnder = false;
    uint8_t      lanes     = 0;
};

// C4 route ends (debug; never set in release, so every query below falls back to the old behaviour). All WU.
// West: Queens-Midtown Tunnel over [0, portal), plaza widening and a U-turn pad past it. East: a lane drop, a
// splitter island, then a roundabout centred on the centreline at ringC. Filled by corridor_ends (CorridorEnds.h).
struct CorridorEnds {
    double portal = 0.0;                                            // 0 = no tunnel
    double plaza0 = 0.0, plaza1 = 0.0, plaza2 = 0.0, plaza3 = 0.0;  // widening eases in [0,1], holds, eases out [2,3]
    float  plazaLanes = 0.0f;
    double padW0 = 0.0, padW1 = 0.0;         // U-turn opening
    double open0  = 0.0;                     // diamonds and service roads start here
    double taper0 = 0.0, taper1 = 0.0;       // east lane drop (one lane), bank eased to 0
    double barrierEnd = 0.0, mainEnd = 0.0;  // median barrier stops; mainline pavement stops (ring takes over)
    double ringC  = 0.0;                     // 0 = no roundabout
    float  ringRo = 0.0f, ringRi = 0.0f;
};

// D2 edges (debug; empty in release, so the median and bounds stay as before). Station ranges per side,
// [0] = WB (-1), [1] = EB (+1). Filled by corridor_edges (CorridorEdges.h).
struct CorridorEdges {
    using Spans  = std::vector<std::pair<double, double>>;
    double wide0 = 0.0, wide1 = 0.0;  // wide grass median holds over [wide0, wide1], eases over `ease` outside
    float  wideExtra = 0.0f, ease = 0.0f;
    float  ditch = 0.0f, wallH = 0.0f;  // roadside ditch depth, sound-wall height (WU)
    float  far = 0.0f, dress = 0.0f;    // D5: far ground past the grass (WU), tree / building density (0 = none)
    float  graves    = 0.0f;            // D6: headstone density x (0 = none)
    bool   landmarks = false;           // D6: one-off landmark structures (CorridorLandmarks.h)
    bool   jersey    = false;           // NJ median barrier: clamp at its toe (release: the painted line)
    Spans  wall[2], rail[2];            // sound walls (top of the cut), W-beam runs (rail face = clamp)
};

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
        float  y     = 0.0f;           // centreline height (WU), relative to the start station
        float  bank  = 0.0f;           // cross slope: height(t) = y - bank * t, t positive right
        float  med   = 0.0f;           // D2: median half-width (header value unless the median widens)
    };

    // Throws (see load_corridor_asset) if the baked asset is missing/stale.
    void load(const std::string& path);
    bool valid() const { return m_asset.stations.size() >= 2; }

    const CorridorHeader&                header() const { return m_asset.header; }
    const std::vector<CorridorLandmark>& landmarks() const { return m_asset.landmarks; }
    double                               total_length() const { return m_asset.header.totalLength; }

    // Exit table and districts (empty for the flat v1 release asset).
    const std::vector<CorridorExit>&     exits() const { return m_asset.exits; }
    const std::vector<CorridorDistrict>& districts() const { return m_asset.districts; }
    // Service-road stretches, sorted by s0, both sides (empty without the SRVR block).
    const std::vector<CorridorServiceRoad>& service_roads() const { return m_asset.serviceRoads; }
    // The next exit ahead of `s` for a car going east (stations rising) or
    // west (stations falling); nullptr when there is none.
    const CorridorExit* next_exit(double s, bool westbound) const;
    // District containing `s`; nullptr when the asset has no districts.
    const CorridorDistrict* district_at(double s) const;
    // D4: OSM crossings (empty without an XING block) and the ones kept after the crossroad merge.
    const std::vector<CorridorCrossing>& crossings() const { return m_asset.crossings; }
    void                                 set_bridges(std::vector<CorridorBridge> b) { m_bridges = std::move(b); }
    const std::vector<CorridorBridge>&   bridges() const { return m_bridges; }
    // True when `s` is within `pad` of a kept bridge's footprint.
    bool near_bridge(double s, double pad) const {
        for (const CorridorBridge& b : m_bridges)
            if (std::abs(s - b.s) < static_cast<double>(b.half) + pad) return true;
        return false;
    }
    // D1: pavement at `s` (Legacy without a SURF block). B5b bumps read it too.
    Pavement surface_at(double s) const;

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
    // D2: the same at a sample, with its median half-width (equal to the above unless the median widens).
    float outer_edge(const Sample& sm) const;
    float paint_inner(const Sample& sm) const;
    float paint_outer(const Sample& sm) const;

    float road_y() const { return m_asset.header.roadY; }
    // Carriageway surface height at station `s`, lateral `t` (positive right of
    // travel): the profile height plus the cross slope, minus the cut under a crossroad (C2b).
    // Equals road_y() on a flat asset.
    float surface_y(double s, float lateral) const {
        const Sample sm = sample(s);
        return m_asset.header.roadY + sm.y - sm.bank * lateral - dip(s);
    }
    // Level ground at `s` (no bank, no dip): service roads, crossroads and ramp ends sit here.
    float ground_y(double s) const { return m_asset.header.roadY + sample(s).y; }

    // ── C2b grading (debug; never set in release, so dip() is 0 there) ──
    // `crossroads` sorted by s; `dipHalf` = half-length of each cut; `srStrip` = grass from the
    // mainline outer edge to the service road's inner curb.
    void set_grading(std::vector<CorridorCrossroad> crossroads, float dipHalf, float srStrip) {
        m_cross   = std::move(crossroads);
        m_dipHalf = dipHalf;
        m_srStrip = srStrip;
    }
    const std::vector<CorridorCrossroad>& crossroads() const { return m_cross; }
    float                                 sr_strip() const { return m_srStrip; }
    // C3a: cross-street stub past each service road (WU); 0 = crossroad ends at the grass edge (C2b).
    void  set_cross_stub(float len) { m_stub = len; }
    float cross_stub() const { return m_stub; }
    // Mainline cut: depth * (1 - smoothstep(|s - c| / dipHalf)), deepest neighbour wins. The one query C3 needs.
    float dip(double s) const;
    // Slope of the surface at `s` in world axes: (dy/dx, dy/dz).
    void surface_slope(double s, float& dydx, float& dydz) const {
        const double sp = static_cast<double>(m_asset.header.stationSpacing);
        const Sample sm = sample(s);
        const float  grade =
            ((sample(s + sp).y - dip(s + sp)) - (sample(s - sp).y - dip(s - sp))) / static_cast<float>(2.0 * sp);
        // right-of-travel = (-tz, tx); height falls by bank per unit to the right.
        dydx = grade * sm.tx + sm.bank * sm.tz;
        dydz = grade * sm.tz - sm.bank * sm.tx;
    }

    // ── D2 edges ──
    void                 set_edges(CorridorEdges e) { m_edges = std::move(e); }
    const CorridorEdges& edges() const { return m_edges; }
    // Extra median half-width at `s` (0 in release): smoothstep in and out of the wide stretch.
    float median_extra(double s) const;
    // W-beam face |lateral| on `side` at `s`, or 0 where no rail stands.
    float rail_face(double s, int side) const;

    // ── C4 route ends ──
    void                set_ends(const CorridorEnds& e) { m_ends = e; }
    const CorridorEnds& ends() const { return m_ends; }
    bool                has_ends() const { return m_ends.ringC > 0.0; }
    bool                in_tunnel(double s) const { return s < m_ends.portal; }
    // Last station of mainline pavement (the roundabout takes over past it); total length without ends.
    double main_end() const { return has_ends() ? m_ends.mainEnd : total_length(); }

    // Inside a turn-around pad the median barrier is omitted and asphalt is
    // laid across it, so the player can U-turn (spec decision #7). The pads
    // are the only place the corridor is laterally continuous.
    bool in_turnaround(double s) const;

private:
    CorridorAsset                  m_asset;
    std::vector<CorridorCrossroad> m_cross;
    std::vector<CorridorBridge>    m_bridges;
    float                          m_dipHalf = 0.0f;
    float                          m_srStrip = 10000.0f;  // 10 m until set_grading
    float                          m_stub    = 0.0f;
    CorridorEnds                   m_ends;
    CorridorEdges                  m_edges;
};

}  // namespace swish
