#include "scene/Corridor/CorridorAlignment.h"
#include "scene/Corridor/CorridorNetworkAsset.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace swish;
using swish::util::units::m;

// N1 Queens network bake (tools/queens_network.py): the road assets parse, the junction table is complete, and
// every link endpoint lands on its placed junction on both roads.

namespace {

const CorridorNetworkAsset& network() {
    static const CorridorNetworkAsset n = load_corridor_network(ASSET_DIR "corridor/queens_network_full.bin");
    return n;
}

const std::vector<CorridorAlignment>& roads() {
    static const std::vector<CorridorAlignment> r = [] {
        std::vector<CorridorAlignment> out(network().roads.size());
        for (const NetworkRoad& rd : network().roads) out.at(rd.id).load(std::string(ASSET_DIR) + rd.asset);
        return out;
    }();
    return r;
}

// Signed heading change over +/- `half` stations: the bake's curve_radii, on the asset.
double radius_at(const CorridorAlignment& al, size_t i, size_t half) {
    const auto&  st = al.header().stationCount;
    const size_t lo = i > half ? i - half : 0, hi = std::min<size_t>(i + half, st - 1);
    const double sp = al.header().stationSpacing;
    const auto   a = al.sample(static_cast<double>(lo) * sp), b = al.sample(static_cast<double>(hi) * sp);
    const double dth = std::abs(std::remainder(std::atan2(b.tz, b.tx) - std::atan2(a.tz, a.tx), 2.0 * M_PI));
    return dth < 1e-12 ? 1e18 : static_cast<double>(hi - lo) * sp / dth;
}

}  // namespace

TEST_CASE("network: the road table and every road asset parse", "[corridor][network]") {
    REQUIRE(network().roads.size() == 6);
    for (size_t i = 0; i < roads().size(); ++i) {
        CHECK(network().roads[i].id == i);
        CHECK(roads()[i].valid());
        CHECK(roads()[i].header().version == CorridorHeader::VERSION);
    }
}

TEST_CASE("network: nine junctions, nine walls, ramps in range", "[corridor][network]") {
    CHECK(network().junctions.size() == 9);
    CHECK(network().walls.size() == 9);  // two per new road, except the BQE north end (it merges into the GCP)
    for (const NetworkWall& w : network().walls) {
        REQUIRE(w.road < roads().size());
        CHECK(w.road != 0);
        CHECK(w.s > 0.0);
        CHECK(w.s < roads()[w.road].total_length());
    }
    CHECK(!network().ramps.empty());
    for (const NetworkRamp& r : network().ramps) {
        CHECK(r.junction < network().junctions.size());
        CHECK(r.count >= 2);
        CHECK((r.from < roads().size() || r.from == 255));
        CHECK((r.to < roads().size() || r.to == 255));
    }
}

TEST_CASE("network: every link endpoint is within 0.5 m of its junction on both roads", "[corridor][network]") {
    for (const NetworkJunction& j : network().junctions) {
        const uint8_t r[2] = {j.roadA, j.roadB};
        const double  s[2] = {j.sA, j.sB};
        const float   t[2] = {j.tA, j.tB};
        for (int k = 0; k < 2; ++k) {
            INFO(j.name << " road " << int(r[k]));
            REQUIRE(r[k] < roads().size());
            const auto sm = roads()[r[k]].sample(s[k]);
            // right of travel = (-tz, tx)
            const double px = sm.x - static_cast<double>(t[k] * sm.tz), pz = sm.z + static_cast<double>(t[k] * sm.tx);
            CHECK(std::hypot(px - j.x, pz - j.z) < m(0.5));
            // A crossing sits on both centrelines (t = the real gap, warped). bqe_gcp is the Astoria merge: the
            // point is the BQE's end, and the GCP centreline is the real ~105 m away.
            CHECK(std::abs(t[k]) < (std::string(j.name) == "bqe_gcp" && k == 1 ? m(150.0f) : m(3.0f)));
        }
    }
}

TEST_CASE("network: drivable mainline radius stays above the bake's reported minimum", "[corridor][network]") {
    // Per road, the tightest compact radius between the walls (50 m baseline, as curve_radii). Real curves
    // tighter than 500 m are exempt from the 500 m floor (as on the LIE); these pins are the bake's measured
    // minima rounded down, so a re-bake that tightens a curve fails here. See docs/corridor/tuning-notes.md N1.
    // 2026-11-21 bake: BQE 273, GCP 234, I-678 411, I-295 278, CIP 437 m; all are real tight curves (exempt).
    const double        kMinRadius[6] = {0.0, m(260.0f), m(220.0f), m(400.0f), m(260.0f), m(420.0f)};
    std::vector<double> lo(6, 0.0), hi(6, 1e30);  // no wall: the drivable range runs to the data end
    for (const NetworkWall& w : network().walls) (w.end ? hi : lo)[w.road] = w.s;
    for (size_t r = 1; r < roads().size(); ++r) {
        const CorridorAlignment& al   = roads()[r];
        const double             sp   = al.header().stationSpacing;
        const size_t             half = static_cast<size_t>(m(50.0) / sp);
        double                   rmin = 1e30;
        const size_t             last = std::min<size_t>(static_cast<size_t>(hi[r] / sp), al.header().stationCount - 1);
        for (size_t i = static_cast<size_t>(lo[r] / sp); i <= last; ++i) rmin = std::min(rmin, radius_at(al, i, half));
        INFO(network().roads[r].key << " min radius " << rmin / m(1.0) << " m");
        CHECK(rmin >= kMinRadius[r]);
    }
}

// ── N2a runtime: CorridorNetwork ramps and the height-continuity handoff ──

#include "scene/Corridor/CorridorNetwork.h"

namespace {

const CorridorNetwork& net() {
    static const CorridorAlignment lie = [] {
        CorridorAlignment a;
        a.load(ASSET_DIR "corridor/lie_corridor_full.bin");
        return a;
    }();
    static const CorridorNetwork n = [] {
        CorridorNetwork c;
        c.load(ASSET_DIR "corridor/queens_network_full.bin", ASSET_DIR, &lie);
        return c;
    }();
    return n;
}

constexpr float kInset = 300.0f;  // CorridorDriver::kLaneInset

// Point on a ramp's centreline at arc length u, extended along the end tangents past either end.
void ramp_xz(const NetRamp& rp, double u, double& x, double& z) {
    const auto& P = rp.pts;
    size_t      i = 1;
    while (i + 1 < P.size() && P[i].u < u) ++i;
    const double l = std::max(1e-3, static_cast<double>(P[i].u - P[i - 1].u));
    const double f = (u - static_cast<double>(P[i - 1].u)) / l;
    x              = P[i - 1].x + (P[i].x - P[i - 1].x) * f;
    z              = P[i - 1].z + (P[i].z - P[i - 1].z) * f;
}

// A car on road r at world (x, z), committed to the side it is on.
NetPos on_road(int r, double x, double z) {
    NetPos p;
    p.road = r;
    net().road(r).project_global(x, z, p.hint, p.s, p.lat);
    p.side = p.lat >= 0.0f ? +1 : -1;
    return p;
}

struct Drive {
    NetPos pos;
    float  y = 0.0f, maxStep = 0.0f, maxHandoff = 0.0f;  // largest height step overall / at a piece change
    bool   held   = false;
    int    pieces = 0;  // piece changes
};

// Steer along a ramp's centreline from u0 to u1 in 1 m steps, as a car would (y fed back each step), until a ramp
// hands the car to a road.
Drive drive_ramp(const NetRamp& rp, NetPos start, double u0, double u1) {
    Drive d;
    d.pos = start;
    d.y   = start.ramp >= 0 ? net().ramp_y(net().ramps()[static_cast<size_t>(start.ramp)], start.s)
                            : net().road(start.road).surface_y(start.s, start.lat);
    for (double u = u0; u <= u1; u += m(1.0)) {
        double x = 0.0, z = 0.0;
        ramp_xz(rp, u, x, z);
        const NetStep st  = net().step(d.pos, x, z, d.y, kInset);
        d.held            = d.held || st.held;
        d.maxStep         = std::max(d.maxStep, std::abs(st.y - d.y));
        const bool change = st.pos.ramp != d.pos.ramp || (st.pos.ramp < 0 && st.pos.road != d.pos.road);
        d.pieces += change ? 1 : 0;
        if (change) d.maxHandoff = std::max(d.maxHandoff, std::abs(st.y - d.y));
        const bool landed = change && st.pos.ramp < 0 && d.pos.ramp >= 0;  // a ramp handed the car to a road
        d.pos             = st.pos;
        d.y               = st.y;
        if (landed) break;
    }
    return d;
}

}  // namespace

// Both ends inside their roads' drivable ranges (ends past a work-zone wall lead into the closed zone, N2b).
bool both_drivable(const NetRamp& rp) {
    for (int e = 0; e < 2; ++e) {
        const int r = e ? rp.to : rp.from;
        if (r == 255) return false;
        double lo = 0.0, hi = 0.0;
        net().drivable(r, lo, hi);
        if (rp.endS[e] < lo || rp.endS[e] > hi) return false;
    }
    return true;
}

TEST_CASE("network runtime: every ramp is built and drivable end to end between its two roads", "[corridor][network]") {
    REQUIRE(net().valid());
    CHECK(net().ramps().size() == network().ramps.size());  // all 293 drawn and driven (none shorter than a step)
    int both = 0, onTo = 0;
    for (size_t k = 0; k < net().ramps().size(); ++k) {
        const NetRamp& rp = net().ramps()[k];
        if (!both_drivable(rp)) continue;
        ++both;
        INFO("ramp " << k << " osm " << network().ramps[k].osmId << " " << int(rp.from) << " -> " << int(rp.to));
        const Drive d = drive_ramp(rp, on_road(rp.from, rp.pts.front().x, rp.pts.front().z), 0.0,
                                   static_cast<double>(rp.length()) + m(10.0));
        CHECK(!d.held);                                       // never stopped on the way
        CHECK(d.maxHandoff <= m(road::network::kHandoffDy));  // every handoff within the height-continuity rule
        CHECK(d.pos.ramp < 0);  // ends on a road (N3: fragments are merged into whole ramps, none pinned)
        onTo += d.pos.ramp < 0 && d.pos.road == rp.to ? 1 : 0;
    }
    // N3: most ramps are now pieces of a chain (welded at forks), so few join two roads alone; movements below
    // drive whole chains. 2026-11-22 bake: 5 single ramps join two drivable roads, all landing on their target.
    CHECK(both >= 5);
    CHECK(onTo == both);
}

TEST_CASE("network runtime: the car is never held at a junction ramp end", "[corridor][network]") {
    // Every ramp end that meets a drivable road or the start of another ramp hands the car on.
    const double weld = m(3.0);
    for (size_t k = 0; k < net().ramps().size(); ++k) {
        const NetRamp& rp   = net().ramps()[k];
        bool           next = false;
        if (rp.to != 255) {
            double lo = 0.0, hi = 0.0;
            net().drivable(rp.to, lo, hi);
            next = rp.endS[1] >= lo && rp.endS[1] <= hi;
        }
        for (const NetRamp& o : net().ramps())
            next = next || (&o != &rp &&
                            std::hypot(o.pts.front().x - rp.pts.back().x, o.pts.front().z - rp.pts.back().z) < weld);
        if (!next || rp.closed) continue;  // N6: a closed local exit holds the car at its barricades (tested below)
        INFO("ramp " << k << " osm " << network().ramps[k].osmId << " to " << int(rp.to));
        const double u0 = std::max(0.0, static_cast<double>(rp.length()) - m(20.0));
        const Drive  d  = drive_ramp(rp, NetPos{rp.from, static_cast<int>(k), u0, 0.0f, +1, 0}, u0,
                                     static_cast<double>(rp.length()) + m(15.0));
        CHECK(!d.held);
        CHECK(d.pos.ramp != static_cast<int>(k));
        CHECK(d.maxHandoff <= m(road::network::kHandoffDy));
    }
}

TEST_CASE("network runtime: an overpass crossing never switches roads", "[corridor][network]") {
    for (const NetworkJunction& j : network().junctions) {
        if (std::string(j.name) == "bqe_gcp") continue;  // the Astoria merge, not a crossing
        const uint8_t r[2] = {j.roadA, j.roadB};
        const double  s[2] = {j.sA, j.sB};
        for (int k = 0; k < 2; ++k) {
            INFO(j.name << " on road " << int(r[k]));
            const CorridorAlignment& al = net().road(r[k]);
            for (const float lane : {0.0f, 1.0f}) {  // right lane, and hard against the outer edge
                NetPos p = net().place(r[k], s[k] - m(300.0));
                p.lat    = lane > 0.0f ? al.outer_edge(al.sample(p.s)) + m(1.0f) : p.lat;
                float y  = al.surface_y(p.s, p.lat);
                for (double ds = -m(300.0); ds <= m(300.0); ds += m(2.0)) {
                    const auto    sm = al.sample(s[k] + ds);
                    const double  x  = sm.x - static_cast<double>(p.lat * sm.tz),
                                  z  = sm.z + static_cast<double>(p.lat * sm.tx);
                    const NetStep st = net().step(p, x, z, y, kInset);
                    CHECK(st.pos.road == r[k]);
                    if (st.pos.ramp >= 0) {  // off the edge onto a ramp is fine only at the car's own height
                        CHECK(std::abs(st.y - y) < m(1.0f));
                        break;
                    }
                    p = st.pos;
                    y = st.y;
                }
            }
        }
    }
}

TEST_CASE("network runtime: a ramp above or below the car never takes it", "[corridor][network]") {
    for (size_t k = 0; k < net().ramps().size(); ++k) {
        const NetRamp& rp = net().ramps()[k];
        const int      rd = rp.from != 255 ? rp.from : rp.to;
        if (rd == 255) continue;
        const NetRampPoint& mid = rp.pts[rp.pts.size() / 2];
        for (const float dy : {-m(3.0f), m(3.0f)}) {
            NetPos out;
            CHECK(net().enter_ramp(rd, mid.x, mid.z, mid.y + dy, kInset, out) != static_cast<int>(k));
        }
    }
}

// ── N2b work zones: MUTCD layout, the U-turn gap, the closed tail ──

#include "scene/Corridor/CorridorWorkZone.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using swish::util::units::ft;

namespace {

const std::vector<WorkZone>& zones() {
    static const std::vector<WorkZone> z = net().work_zones({});
    return z;
}

// Distance before the wall along the approach.
double before(const WorkZone& z, double s) { return (z.wall - s) * z.dir; }

}  // namespace

TEST_CASE("work zones: one per wall, signs and tapers at the MUTCD freeway numbers", "[corridor][network][workzone]") {
    REQUIRE(zones().size() == network().walls.size());
    CHECK(wz_buffer_ft(50.0f) == 425.0f);  // MUTCD Table 6B-2
    int full = 0;
    for (const WorkZone& z : zones()) {
        INFO("road " << z.road << " end " << z.end << " lanes " << z.lanes << " k " << z.k);
        const double T = before(z, z.taper0);
        // L = W S: 12 ft x 50 mph = 600 ft per closed lane, unless the link is too short (then squeezed, never < 10 %)
        CHECK(z.L == Approx(static_cast<float>(ft(12.0 * 50.0) * z.k)).epsilon(1e-4));
        CHECK(z.k >= road::workzone::kTaperMinK);
        full += z.k == 1.0f ? 1 : 0;
        const auto find = [&](WzFace f) {
            for (const WzItem& it : z.items)
                if (it.kind == WzKind::Sign && it.face == f) return before(z, it.s);
            return -1.0;
        };
        // A / B / C = 1,000 / 1,500 / 2,640 ft: the signs stand 1,000, 2,500 and 5,140 ft before the taper
        CHECK(find(WzFace::LaneEnds) - T == Approx(ft(1000.0)).margin(m(0.5)));
        CHECK(find(WzFace::RoadWork) - T == Approx(ft(5140.0)).margin(m(0.5)));
        const double lc = std::max({find(WzFace::LaneClosed1), find(WzFace::LaneClosed2), find(WzFace::LaneClosed3)});
        CHECK(lc - T == Approx(ft(2500.0)).margin(m(0.5)));
        // Channelizers: at most S = 50 ft apart in the first taper, 40 ft (NYSDOT) in the buffer
        int          inTaper = 0, inBuffer = 0;
        const double u1 = m(road::workzone::kGapClear + road::workzone::kGapLen);
        for (const WzItem& it : z.items) {
            if (it.kind != WzKind::Drum) continue;
            const double u = before(z, it.s);
            inTaper += u > T - z.L + 1.0 && u <= T + 1.0 ? 1 : 0;
            inBuffer += u > u1 + 1.0 && u <= u1 + z.buffer + 1.0 ? 1 : 0;
        }
        CHECK(inTaper >= static_cast<int>(std::ceil(z.L / ft(50.0))));
        CHECK(inBuffer >= static_cast<int>(std::ceil(z.buffer / ft(40.0))));
        // one Type C arrow board per closed lane; one R11-2 on the barricades; the wall spans both carriageways
        int   arrows = 0, r112 = 0;
        float wallW = 0.0f;
        for (const WzItem& it : z.items) {
            arrows += it.kind == WzKind::Arrow ? 1 : 0;
            r112 += it.kind == WzKind::Barricade && it.face == WzFace::RoadClosed ? 1 : 0;
            if (it.kind == WzKind::Wall && std::abs(before(z, it.s) + m(road::workzone::kFaceBack)) < m(1.0))
                wallW += it.len;
        }
        CHECK(arrows == z.lanes - 1);
        CHECK(r112 == 1);
        const auto& al = net().road(z.road);
        CHECK(wallW >= 2.0f * al.outer_edge(al.sample(z.wall)));
    }
    CHECK(full >= 5);  // 2026-11-21 bake: the short links (Van Wyck north, Clearview north) squeeze
}

TEST_CASE("work zones: the median U-turn gap is drivable, the barrier holds elsewhere",
          "[corridor][network][workzone]") {
    for (const WorkZone& z : zones()) {
        INFO("road " << z.road << " end " << z.end);
        const auto&  al    = net().road(z.road);
        const double sg    = 0.5 * (z.gap0 + z.gap1);
        const auto   cross = [&](double s, bool& clamped) {
            NetPos p = net().place(z.road, s);
            p.side   = z.dir;
            p.lat    = static_cast<float>(z.dir) * (al.paint_inner(al.sample(s)) + 0.5f * al.header().laneWidth);
            float y  = al.surface_y(s, p.lat);
            clamped  = false;
            const float lat0 = p.lat;
            for (int i = 0; i <= 40; ++i) {  // steer straight across to the same lane on the other side
                const float   lat = lat0 * (1.0f - 2.0f * static_cast<float>(i) / 40.0f);
                const auto    sm  = al.sample(s);
                const NetStep st  = net().step(p, sm.x - static_cast<double>(lat * sm.tz),
                                               sm.z + static_cast<double>(lat * sm.tx), y, kInset);
                clamped           = clamped || st.clamped || st.held;
                p = st.pos, y = st.y;
            }
            return p;
        };
        bool         clamped = true;
        const NetPos p       = cross(sg, clamped);
        CHECK(!clamped);
        CHECK(p.ramp < 0);
        CHECK(p.side == -z.dir);  // re-committed to the way back
        const NetPos q = cross(z.wall - z.dir * m(12.0), clamped);
        INFO("after: ramp " << q.ramp << " side " << q.side << " lat " << q.lat << " s " << q.s);
        CHECK(clamped);  // between the gap and the wall the median barrier stops the car
    }
}

TEST_CASE("work zones: the horizon tail past the wall is not drivable", "[corridor][network][workzone]") {
    for (const WorkZone& z : zones()) {
        INFO("road " << z.road << " end " << z.end);
        const auto& al = net().road(z.road);
        NetPos      p  = net().place(z.road, z.wall - z.dir * m(20.0));
        p.side         = z.dir;  // lane 1, clear of the junction ramps that run on into the closed zone (N3)
        p.lat          = static_cast<float>(z.dir) * (al.paint_inner(al.sample(p.s)) + 0.5f * al.header().laneWidth);
        float   y      = al.surface_y(p.s, p.lat);
        bool    held   = false;
        NetStep st;
        for (double d = -m(20.0); d <= m(200.0); d += m(2.0)) {
            const double s  = std::clamp(z.wall + z.dir * d, 0.0, al.total_length());
            const auto   sm = al.sample(s);
            st = net().step(p, sm.x - static_cast<double>(p.lat * sm.tz), sm.z + static_cast<double>(p.lat * sm.tx), y,
                            kInset);
            held = held || st.held;
            p = st.pos, y = st.y;
        }
        CHECK(held);
        CHECK(p.s == Approx(z.wall).margin(1.0));
        // no lane paint past the barrier face
        CHECK((z.end ? al.paint_to() : al.paint_from()) == Approx(z.face));
    }
}

TEST_CASE("work zones: the closed-exit kit (N6) blocks a ramp past its gore", "[corridor][network][workzone]") {
    int k = 0;
    while (k < static_cast<int>(net().ramps().size()) && net().ramps()[static_cast<size_t>(k)].from == 255) ++k;
    REQUIRE(k < static_cast<int>(net().ramps().size()));
    const NetRamp& rp    = net().ramps()[static_cast<size_t>(k)];
    const WorkZone z     = exit_closure(net(), k, {});
    int            board = 0;
    for (const WzItem& it : z.items) {
        board += it.face == WzFace::ExitClosed ? 1 : 0;
        CHECK(std::abs(it.lat) <= rp.halfWidth);
        CHECK(it.s <= rp.length());
    }
    CHECK(board == 1);
    CHECK(z.wall >= rp.pin[0]);
}

// ── N3 movements: chains of ramps (welded end to start) from one road to another ──

namespace {

// Ramps whose start is welded to the end of ramp k (the next pieces of a movement).
std::vector<int> next_ramps(int k) {
    std::vector<int> out;
    const NetRamp&   a = net().ramps()[static_cast<size_t>(k)];
    for (size_t j = 0; j < net().ramps().size(); ++j) {
        const NetRamp& b = net().ramps()[j];
        if (static_cast<int>(j) != k &&
            std::hypot(b.pts.front().x - a.pts.back().x, b.pts.front().z - a.pts.back().z) < m(1.0))
            out.push_back(static_cast<int>(j));
    }
    return out;
}

// Every ramp path (no repeats, at most 8 pieces) from a ramp leaving a road to one ending on a road, at junction j.
std::vector<std::vector<int>> movements(int j) {
    std::vector<std::vector<int>> out, stack;
    for (size_t k = 0; k < net().ramps().size(); ++k)
        if (net().ramps()[k].junction == j && net().ramps()[k].from != 255) stack.push_back({static_cast<int>(k)});
    while (!stack.empty()) {
        std::vector<int> p = stack.back();
        stack.pop_back();
        const NetRamp& last = net().ramps()[static_cast<size_t>(p.back())];
        if (last.to != 255) out.push_back(p);
        if (p.size() >= 8) continue;
        for (const int n : next_ramps(p.back()))
            if (std::find(p.begin(), p.end(), n) == p.end()) {
                p.push_back(n);
                stack.push_back(p);
                p.pop_back();
            }
    }
    return out;
}

// Steer along each ramp of the path in turn from its first road, as a car would; ends 15 m past the last ramp.
Drive drive_path(const std::vector<int>& path) {
    const NetRamp& r0 = net().ramps()[static_cast<size_t>(path.front())];
    Drive          d;
    d.pos = on_road(r0.from, r0.pts.front().x, r0.pts.front().z);
    d.y   = net().road(d.pos.road).surface_y(d.pos.s, d.pos.lat);
    for (size_t q = 0; q < path.size(); ++q) {
        const NetRamp& rp  = net().ramps()[static_cast<size_t>(path[q])];
        const double   end = static_cast<double>(rp.length()) + (q + 1 == path.size() ? m(15.0) : 0.0);
        for (double u = 0.0; u <= end; u += m(1.0)) {
            double x = 0.0, z = 0.0;
            ramp_xz(rp, u, x, z);
            const NetStep st  = net().step(d.pos, x, z, d.y, kInset);
            d.held            = d.held || st.held;
            d.maxStep         = std::max(d.maxStep, std::abs(st.y - d.y));
            const bool change = st.pos.ramp != d.pos.ramp || (st.pos.ramp < 0 && st.pos.road != d.pos.road);
            d.pieces += change ? 1 : 0;
            if (change) d.maxHandoff = std::max(d.maxHandoff, std::abs(st.y - d.y));
            d.pos = st.pos;
            d.y   = st.y;
        }
    }
    return d;
}

}  // namespace

// Side of road r a ramp end sits on: +1 = the +s carriageway (EB on the LIE, NB on the BQE).
int side_at(int r, double x, double z) {
    NetPos p = on_road(r, x, z);
    return p.side;
}

// Drivable movements (path, first road side, last road side), ends inside their roads' drivable ranges.
struct Move {
    std::vector<int> path;
    int              from = 0, to = 0, fromSide = 0, toSide = 0;
};
std::vector<Move> drivable_moves(int j) {
    std::vector<Move> out;
    for (const auto& p : movements(j)) {
        const NetRamp& a  = net().ramps()[static_cast<size_t>(p.front())];
        const NetRamp& b  = net().ramps()[static_cast<size_t>(p.back())];
        double         lo = 0.0, hi = 0.0;
        net().drivable(a.from, lo, hi);
        if (a.endS[0] < lo || a.endS[0] > hi) continue;
        net().drivable(b.to, lo, hi);
        if (b.endS[1] < lo || b.endS[1] > hi) continue;
        out.push_back({p, a.from, b.to, side_at(a.from, a.pts.front().x, a.pts.front().z),
                       side_at(b.to, b.pts.back().x, b.pts.back().z)});
    }
    return out;
}

TEST_CASE("N3: every movement through every junction is drivable end to end", "[corridor][network][n3]") {
    int total = 0;
    for (const NetworkJunction& j : network().junctions)
        for (const Move& mv : drivable_moves(j.id)) {
            std::string ids;
            for (const int k : mv.path) ids += std::to_string(network().ramps[static_cast<size_t>(k)].osmId) + " ";
            INFO(j.name << " " << mv.from << " -> " << mv.to << " via " << ids);
            const Drive d = drive_path(mv.path);
            CHECK(!d.held);                                       // continuous surface, never stopped
            CHECK(d.maxHandoff <= m(road::network::kHandoffDy));  // every piece change at the car's height
            CHECK(d.pos.ramp < 0);
            CHECK(d.pos.road == mv.to);  // N5: every junction (gcp_cip's C/D rides the GCP's lane past half way)
            ++total;
        }
    // 2026-11-22 bake (N3 chains): 39 movements; pinned so a re-bake that loses movements fails here.
    CHECK(total >= 39);
}

TEST_CASE("N3: the LIE - BQE - GCP loop works both ways", "[corridor][network][n3]") {
    // (junction, from road, from side, to road, to side); side 0 = either carriageway
    struct Need {
        int j, from, fs, to, ts;
    };
    const Need needs[] = {
        {0, 0, +1, 1, +1},  // LIE EB -> BQE NB
        {1, 1, +1, 2, 0},   // BQE NB -> GCP (the Y legs)
        {1, 2, 0, 1, -1},   // GCP -> BQE SB
        {0, 1, -1, 0, -1},  // BQE SB -> LIE WB: back
        {0, 0, -1, 1, 0},   // LIE WB -> BQE
        {0, 1, 0, 0, +1},   // BQE -> LIE EB
        {0, 1, 0, 0, -1},   // BQE -> LIE WB
    };
    for (const Need& n : needs) {
        INFO("junction " << n.j << ": " << n.from << (n.fs > 0 ? "+" : "-") << " -> " << n.to
                         << (n.ts > 0 ? "+" : "-"));
        int ok = 0;
        for (const Move& mv : drivable_moves(n.j))
            if (mv.from == n.from && mv.to == n.to && (!n.fs || mv.fromSide == n.fs) && (!n.ts || mv.toSide == n.ts)) {
                const Drive d = drive_path(mv.path);
                ok += !d.held && d.pos.ramp < 0 && d.pos.road == n.to ? 1 : 0;
            }
        CHECK(ok >= 1);
    }
    // The Y: both legs reach the GCP from the BQE, one to each carriageway (3 lanes, then 2 + 2).
    int east = 0, west = 0;
    for (const Move& mv : drivable_moves(1))
        if (mv.from == 1 && mv.to == 2) (mv.toSide > 0 ? east : west)++;
    CHECK(east >= 1);
    CHECK(west >= 1);
}

#include "scene/Corridor/CorridorRamps.h"
#include "scene/Corridor/CorridorSigns.h"

TEST_CASE("N3: the BQE profile is viaduct, LIRR trench, viaduct (roads.md section 1)", "[corridor][network][n3]") {
    const CorridorAlignment& bqe = net().road(1);
    REQUIRE(bqe.has_terrain());
    const auto& st = net().structures(1);
    const auto  in = [&](double s, bool trench) {
        return std::any_of(st.begin(), st.end(),
                           [&](const RoadStructure& r) { return r.trench == trench && s >= r.s0 && s <= r.s1; });
    };
    double lirr = -1.0, north = -1.0, y = -1.0;
    for (const CorridorCrossing& x : bqe.crossings())
        if (std::string(x.name) == "Main Line") lirr = x.station;  // LIRR Main Line over the Woodside cut
    for (const CorridorExit& e : bqe.exits()) {
        if (std::string(e.ref) == "41" && !e.westbound) north = e.station;  // Northern Blvd
        if (std::string(e.ref) == "42" && !e.westbound) y = e.station;      // the east leg (GCP east)
    }
    REQUIRE(lirr > 0.0);
    REQUIRE(north > 0.0);
    REQUIRE(y > north);
    const double lie = network().junctions[0].sB;
    CHECK(in(lie, false));  // over the LIE on the viaduct
    CHECK(in(lirr, true));  // under the LIRR in the trench
    CHECK(in(north, false));
    CHECK(bqe.surface_at(lie + m(500.0)) == Pavement::Concrete);  // concrete deck on the viaduct
    CHECK(bqe.surface_at(lirr) == Pavement::Concrete);  // N7: the trench's concrete slabs (roads.md SV-3, LOOK row)
    CHECK(bqe.surface_at(y + m(300.0)) != Pavement::Concrete);  // asphalt past the Y
    // 3 through lanes from the LIE to the Y, then the west leg's 2 (the east leg is a 2-lane piece)
    for (double s = lie + m(300.0); s < y - m(400.0); s += m(50.0)) CHECK(bqe.sample(s).lanes > 2.9f);
    CHECK(bqe.sample(y + m(600.0)).lanes < 2.1f);
    CHECK(bqe.header().outerShoulder <= util::units::ft(2.5f));  // "most of the length has no shoulder"
    // the trench is deep enough to pass under a rail deck: 16 ft clear + girders under the ground line
    CHECK(bqe.terrain_y(lirr) - bqe.surface_y(lirr, 0.0f) > util::units::ft(16.0f));
}

TEST_CASE("N3: BQE guide signs and the LIE exit 17 diamond replaced", "[corridor][network][n3]") {
    CorridorSigns s;
    add_guide_signs(s, net().road(1), 1, [](double v, int side) { return v + side * m(100.0); },
                    {{"lie_bqe", network().junctions[0].sB}, {"bqe_gcp", network().junctions[1].sA}});
    int rows = 0;
    for (const auto& r : road::network::kGuideSigns) rows += r.road == 1 ? 1 : 0;
    CHECK(static_cast<int>(s.gantries.size()) == rows);  // every BQE row finds its anchor
    for (const SignGantry& g : s.gantries) {
        CHECK(g.board >= 0);
        CHECK(g.board < static_cast<int>(std::size(sign_atlas::kNetBoards)));
        CHECK(g.s > 0.0);
        CHECK(g.s < net().road(1).total_length());
    }
    CHECK(s.gores.size() >= 8);
    for (const SignGore& g : s.gores) CHECK(g.cell < static_cast<int>(std::size(sign_atlas::kNetGores)));
    // the LIE keeps no diamond at 16 / 17 (the real junction owns that stretch)
    for (const CorridorCrossroad& c : corridor_crossroads(net().road(0), m(6.5f))) {
        CHECK(c.exitNo != 16);
        CHECK(c.exitNo != 17);
    }
}

#include "scene/Corridor/CorridorBridges.h"
#include "scene/Corridor/CorridorEnds.h"

namespace {

// The network on the LIE as the debug build grades it (AppInit): ends, diamonds and their dips, D4 bridges. The
// GCP then follows the D4 trench under the LIE at exit 22.
const CorridorAlignment& graded_lie() {
    static const CorridorAlignment a = [] {
        CorridorAlignment l;
        l.load(ASSET_DIR "corridor/lie_corridor_full.bin");
        l.set_ends(corridor_ends(l));
        l.set_grading(corridor_crossroads(l, m(6.5f)), m(350.0f), m(10.0f));
        const std::vector<CorridorPath> paths = corridor_diamond_paths(l);
        std::vector<CorridorCrossroad>  cr    = l.crossroads();
        l.set_bridges(corridor_bridges(l, paths, cr, 16.5f));
        l.set_grading(std::move(cr), m(350.0f), m(10.0f));
        return l;
    }();
    return a;
}

const CorridorNetwork& net4() {
    static const CorridorNetwork n = [] {
        CorridorNetwork c;
        c.load(ASSET_DIR "corridor/queens_network_full.bin", ASSET_DIR, &graded_lie());
        return c;
    }();
    return n;
}

}  // namespace

TEST_CASE("N3: ramps at the LIE x BQE junction clear the roads they cross", "[corridor][network][n3]") {
    // Over a junction road's pavement, a ramp stands at least 14 ft 6 in (NYSDOT local minimum) + its deck above, or
    // as far below; measured from the centreline, as CorridorNetwork::clear_ramps does. Exempt: where it rides its
    // road and the kClearEase before that (descending onto the shoulder to merge), and (N4) a ramp running beside a
    // road over its pavement edge (a C/D road), which is not a crossing. N4 adds the Astoria Y and exit 22, on the
    // graded LIE (D4 bridges; the GCP in its trench under the LIE).
    int bad = 0, over = 0;
    for (const NetRamp& rp : net4().ramps()) {
        // N4: the Y and exit 22 too; N5: every junction, except a piece welded at both ends and too short to ease
        // to a crossing (gcp_clearview 5706870, 80 m: open, N7)
        if (rp.from == 255 && rp.to == 255 && rp.length() < 2.0f * m(road::network::kClearSpread)) continue;
        const NetworkJunction& j = network().junctions[rp.junction];
        for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
            const CorridorAlignment& al = net4().road(r);
            size_t                   h  = 0;
            double                   s  = 0.0;
            float                    t  = 0.0f;
            al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
            for (const NetRampPoint& q : rp.pts) {
                al.project(q.x, q.z, h, s, t);
                const float ease = m(road::network::kClearEase);
                if ((rp.from == r && q.u <= rp.pin[0] + ease) || (rp.to == r && rp.length() - q.u <= rp.pin[1] + ease))
                    continue;
                const auto sm = al.sample(s);
                if (std::abs(t) > al.outer_edge(sm) || s <= 0.0 || s >= al.total_length()) continue;
                if (std::abs(q.tx * sm.tx + q.tz * sm.tz) > 0.94f) continue;  // N4: beside the road, not across it
                ++over;
                // under the LIE its D4 girders count (exit 22's C/D roads in its trench)
                const float dg = r == 0 && q.y < al.surface_y(s, t)
                                     ? m(road::bridges::kSlab + road::bridges::kHaunch + road::bridges::kGirderD +
                                         road::bridges::kLieCover)
                                     : m(road::network::kDeck);
                const float g  = std::abs(q.y - al.surface_y(s, t));
                // N7: cars only on a parkway below, or on a parkway's ramp under a road: kParkwayClearFt
                const auto pk   = [](int x) { return x == 2 || x == 5; };
                const bool cars = q.y > al.surface_y(s, t) ? pk(r) : pk(rp.from) || pk(rp.to);
                bad += g + m(0.02f) < util::units::ft(cars ? road::network::kParkwayClearFt : 14.5f) + dg ? 1 : 0;
            }
        }
    }
    INFO(bad << " of " << over << " ramp points over a road lack clearance");
    CHECK(over > 0);
    CHECK(bad == 0);
}

// ── N4: the GCP, exit 22 and the loop ──

namespace {

// One car driven through the network: every step is a CorridorNetwork::step from the last answer.
struct Run {
    NetPos           pos;
    float            y = 0.0f, maxDy = 0.0f;
    double           maxDs = 0.0;
    bool             held  = false;
    std::vector<int> roads;  // road ids in the order the car lands on them
};

void step_to(Run& c, double x, double z) {
    const NetStep st     = net4().step(c.pos, x, z, c.y, kInset);
    const bool    change = st.pos.ramp != c.pos.ramp || (st.pos.ramp < 0 && st.pos.road != c.pos.road);
    c.held               = c.held || st.held;
    c.maxDy              = std::max(c.maxDy, std::abs(st.y - c.y));
    if (!change && st.pos.ramp < 0) c.maxDs = std::max(c.maxDs, std::abs(st.pos.s - c.pos.s));
    if (change && st.pos.ramp < 0 && (c.roads.empty() || c.roads.back() != st.pos.road)) c.roads.push_back(st.pos.road);
    c.pos = st.pos;
    c.y   = st.y;
}

// Along the car's road to station s1, easing to lateral lat1 over the last 150 m.
void road_to(Run& c, double s1, float lat1) {
    const CorridorAlignment& al = net4().road(c.pos.road);
    const double             s0 = c.pos.s, dir = s1 > s0 ? 1.0 : -1.0;
    const float              l0 = c.pos.lat;
    for (double s = s0; dir * (s1 - s) > 0.0; s += dir * m(2.0)) {
        const float w  = static_cast<float>(std::clamp(std::abs(s1 - s) / m(150.0), 0.0, 1.0));
        const float l  = lat1 + (l0 - lat1) * w;
        const auto  sm = al.sample(s);
        step_to(c, sm.x - static_cast<double>(l * sm.tz), sm.z + static_cast<double>(l * sm.tx));
    }
}

// The first clean movement at junction j from road `from` (side fs) to road `to` (side ts), driven from where the
// car is: along its road to the ramp's start, then along each ramp, 15 m onto the new road.
bool take(Run& c, int j, int from, int fs, int to, int ts) {
    for (const Move& mv : drivable_moves(j)) {
        if (mv.from != from || mv.to != to || mv.fromSide != fs || mv.toSide != ts || drive_path(mv.path).held)
            continue;
        const NetRamp& r0 = net4().ramps()[static_cast<size_t>(mv.path.front())];
        size_t         h  = 0;
        double         s  = 0.0;
        float          t  = 0.0f;
        net4().road(from).project_global(r0.pts.front().x, r0.pts.front().z, h, s, t);
        road_to(c, s, t);
        for (size_t q = 0; q < mv.path.size(); ++q) {
            const NetRamp& rp  = net4().ramps()[static_cast<size_t>(mv.path[q])];
            const double   end = static_cast<double>(rp.length()) + (q + 1 == mv.path.size() ? m(15.0) : 0.0);
            for (double u = 0.0; u <= end; u += m(1.0)) {
                double x = 0.0, z = 0.0;
                ramp_xz(rp, u, x, z);
                step_to(c, x, z);
            }
        }
        return c.pos.ramp < 0 && c.pos.road == to;
    }
    return false;
}

Run start_on_lie(double s) {
    Run c;
    c.pos = net4().place(0, s);
    c.y   = net4().road(0).surface_y(c.pos.s, c.pos.lat);
    return c;
}

}  // namespace

TEST_CASE("N4: the LIE - BQE - GCP loop closes both ways", "[corridor][network][n4]") {
    // Both loops start and end on the LIE 1 km west of the BQE junction; every step continues the last one (road id
    // and s continuous on a road, no height jump past the handoff limit, never held).
    const double s0 = network().junctions[0].sA - m(1000.0);
    SECTION("LIE EB, BQE NB, the Y, GCP EB, exit 22, LIE WB") {
        Run c = start_on_lie(s0);
        REQUIRE(take(c, 0, 0, +1, 1, +1));  // LIE EB -> BQE NB
        REQUIRE(take(c, 1, 1, +1, 2, +1));  // BQE NB -> GCP EB (the Y)
        REQUIRE(take(c, 2, 2, +1, 0, -1));  // GCP EB -> LIE WB (exit 22)
        road_to(c, s0, c.pos.lat);
        CHECK(c.pos.road == 0);
        CHECK(c.pos.s == Approx(s0).margin(m(5.0)));
        CHECK(c.roads == std::vector<int>{1, 2, 0});
        CHECK(!c.held);
        CHECK(c.maxDy <= m(road::network::kHandoffDy));
        CHECK(c.maxDs <= m(3.0));
    }
    SECTION("LIE EB, exit 22, GCP WB, the Y, BQE SB, LIE WB") {
        Run c = start_on_lie(s0);
        REQUIRE(take(c, 2, 0, +1, 2, -1));  // LIE EB -> GCP WB (exit 22)
        REQUIRE(take(c, 1, 2, -1, 1, -1));  // GCP WB -> BQE SB
        REQUIRE(take(c, 0, 1, -1, 0, -1));  // BQE SB -> LIE WB
        road_to(c, s0, c.pos.lat);
        CHECK(c.pos.road == 0);
        CHECK(c.pos.s == Approx(s0).margin(m(5.0)));
        CHECK(c.roads == std::vector<int>{2, 1, 0});
        CHECK(!c.held);
        CHECK(c.maxDy <= m(road::network::kHandoffDy));
        CHECK(c.maxDs <= m(3.0));
    }
}

TEST_CASE("N4: the GCP passes under the LIE's exit 22 bridge", "[corridor][network][n4]") {
    const NetworkJunction& j   = network().junctions[2];  // lie_gcp
    const CorridorBridge*  hit = nullptr;
    for (const CorridorBridge& b : graded_lie().bridges())
        if (b.net && std::abs(b.s - j.sA) < b.half) hit = &b;
    REQUIRE(hit != nullptr);  // the D4 stone arch stays, its under-road is the GCP's
    const CorridorAlignment& gcp = net4().road(2);
    CHECK(gcp.surface_y(j.sB, 0.0f) == Approx(hit->yLow).margin(m(0.3f)));
    // 14 ft 6 in under the LIE's lowest point over the GCP's carriageways (the arch rise on top)
    for (const float t : {-gcp.outer_edge(gcp.sample(j.sB)), 0.0f, gcp.outer_edge(gcp.sample(j.sB))})
        CHECK(graded_lie().surface_y(j.sA, 0.0f) - gcp.surface_y(j.sB, t) > util::units::ft(14.5f));
    // and back on its own profile past the climb and the ease
    CHECK(gcp.surface_y(j.sB + m(600.0), 0.0f) == Approx(net().road(2).surface_y(j.sB + m(600.0), 0.0f)));
    // no LIE diamond at 22 (the real interchange owns it)
    for (const CorridorCrossroad& c : graded_lie().crossroads()) CHECK(c.exitNo != 22);
}

TEST_CASE("N4: exit 22 serves all eight cloverleaf movements", "[corridor][network][n4]") {
    // OSM routes the LIE side through the Horace Harding service roads (C/D), folded into the LIE; N5: LIE EB -> GCP EB
    // through the signed connector 25419995 (tagged secondary) off the C/D.
    std::set<std::array<int, 4>> got;
    for (const Move& mv : drivable_moves(2))
        if (!drive_path(mv.path).held) got.insert({mv.from, mv.fromSide, mv.to, mv.toSide});
    for (const std::array<int, 4> need : std::initializer_list<std::array<int, 4>>{{0, +1, 2, -1},
                                                                                   {0, +1, 2, +1},
                                                                                   {0, -1, 2, +1},
                                                                                   {0, -1, 2, -1},
                                                                                   {2, +1, 0, +1},
                                                                                   {2, +1, 0, -1},
                                                                                   {2, -1, 0, -1},
                                                                                   {2, -1, 0, +1}})
        CHECK(got.count(need) == 1);
}

TEST_CASE("N4: GCP parkway guide signs and the LIE exit 22 panels", "[corridor][network][n4]") {
    CorridorSigns s;
    add_guide_signs(s, net().road(2), 2, [](double v, int side) { return v + side * m(100.0); },
                    {{"bqe_gcp", network().junctions[1].sB}, {"lie_gcp", network().junctions[2].sB}});
    int rows = 0, lie = 0;
    for (const auto& r : road::network::kGuideSigns) rows += r.road == 2 ? 1 : 0, lie += r.road == 0 ? 1 : 0;
    CHECK(static_cast<int>(s.gantries.size()) == rows);  // every GCP row finds its exit or junction
    for (const SignGantry& g : s.gantries) {
        CHECK(g.board >= 11);  // the N4 parkway boards
        CHECK(g.board < static_cast<int>(std::size(sign_atlas::kNetBoards)));
        CHECK(g.s > 0.0);
        CHECK(g.s < net().road(2).total_length());
    }
    CHECK(s.gores.size() == 8u);  // EXIT 10 both ways, EXIT 4 westbound; N5: 13 eastbound, 21 and 23 both ways
    CHECK(lie == 8);              // LIE exits 17 and 22 (N5: 27 and 31), both directions
    CHECK(sign_panel(22, false) >= 0);
    CHECK(sign_panel(22, true) >= 0);
}

// ── N5: the rungs (Van Wyck, Clearview, Cross Island) ──

namespace {

// The first clean movement at junction j from road `from` on side fs onto road `to` (either side, ts = 0), as take().
bool take_any(Run& c, int j, int from, int fs, int to, int ts = 0) {
    for (const int side : {+1, -1})
        if ((!ts || side == ts) && take(c, j, from, fs, to, side)) return true;
    return false;
}

struct Rung {
    int  road, lie, gcp;  // road id, its LIE junction, its GCP junction
    bool turn;            // OSM has no rung SB -> GCP / GCP -> rung NB at the GCP junction: U-turn at the south wall
};
// Kew Gardens joins the Van Wyck and the GCP only GCP EB -> VW SB and VW NB -> GCP WB (the JFK movements, as built)
constexpr Rung kRungs[] = {{3, 3, 4, true}, {4, 5, 6, false}, {5, 7, 8, false}};

// Through the median U-turn gap of road r's high-s work zone, onto its -s carriageway (N2b).
void uturn(Run& c, int r) {
    for (const WorkZone& z : net4().work_zones({})) {
        if (z.road != r || z.dir < 0) continue;
        const auto&  al = net4().road(r);
        const double sg = 0.5 * (z.gap0 + z.gap1);
        road_to(c, sg, c.pos.lat);
        const float lat0 = c.pos.lat;
        for (int i = 0; i <= 40; ++i) {
            const float lat = lat0 * (1.0f - 2.0f * static_cast<float>(i) / 40.0f);
            const auto  sm  = al.sample(sg);
            step_to(c, sm.x - static_cast<double>(lat * sm.tz), sm.z + static_cast<double>(lat * sm.tx));
        }
    }
}

}  // namespace

TEST_CASE("N5: each rung hands the car LIE - rung - GCP and back", "[corridor][network][n5]") {
    // Down: from the LIE (either carriageway) onto the rung's southbound (+s: every rung runs from the LIE south to the
    // GCP), along it to its GCP junction, onto the GCP. Up: from the GCP (either carriageway) onto its northbound, back
    // to the LIE. Every step continues the last (never held, no height jump past the handoff limit). The Van Wyck has
    // neither movement at Kew Gardens, so its walks U-turn in the south work zone and use the ones it has.
    for (const Rung& g : kRungs) {
        INFO("rung road " << g.road);
        const NetworkJunction& jl   = network().junctions[static_cast<size_t>(g.lie)];
        const NetworkJunction& jg   = network().junctions[static_cast<size_t>(g.gcp)];
        bool                   down = false, up = false;
        for (const int ls : {+1, -1}) {
            if (down) break;
            Run c = start_on_lie(jl.sA - static_cast<double>(ls) * m(1500.0));
            if (ls < 0) c.pos.side = -1, c.pos.lat = -c.pos.lat;
            if (!take(c, g.lie, 0, ls, g.road, +1)) continue;
            if (g.turn) uturn(c, g.road);
            down = take_any(c, g.gcp, g.road, g.turn ? -1 : +1, 2) && !c.held &&
                   c.maxDy <= m(road::network::kHandoffDy) && c.roads == std::vector<int>{g.road, 2};
        }
        for (const int gs : {+1, -1}) {
            if (up) break;
            Run c;
            c.pos = net4().place(2, jg.sA - static_cast<double>(gs) * m(1500.0));
            if (gs < 0) c.pos.side = -1, c.pos.lat = -c.pos.lat;
            c.y = net4().road(2).surface_y(c.pos.s, c.pos.lat);
            if (!take(c, g.gcp, 2, gs, g.road, g.turn ? +1 : -1)) continue;
            if (g.turn) uturn(c, g.road);
            up = take_any(c, g.lie, g.road, -1, 0) && !c.held && c.maxDy <= m(road::network::kHandoffDy) &&
                 c.roads == std::vector<int>{g.road, 0};
        }
        CHECK(down);
        CHECK(up);
    }
}

TEST_CASE("N5: the rung junctions replace LIE diamonds 26-28, 31; the rungs pass the LIE", "[corridor][network][n5]") {
    for (const CorridorCrossroad& c : graded_lie().crossroads()) {
        CHECK(c.exitNo != 26);
        CHECK(c.exitNo != 27);
        CHECK(c.exitNo != 28);
        CHECK(c.exitNo != 31);
    }
    // the Van Wyck crosses over the LIE on its own viaduct (no D4 bridge); the Clearview and the CIP under a LIE
    // girder bridge, at 14 ft 6 in under its lowest point
    for (const int k : {3, 5, 7}) {
        const NetworkJunction& j   = network().junctions[static_cast<size_t>(k)];
        const CorridorBridge*  hit = nullptr;
        for (const CorridorBridge& b : graded_lie().bridges())  // the rung's own (College Point Blvd stays by the VW)
            if (std::abs(b.s - j.sA) < b.half && (k == 3 ? b.kind == CrossingKind::RoadOver : b.net)) hit = &b;
        const CorridorAlignment& r = net4().road(j.roadB);
        if (k == 3) {
            CHECK(hit == nullptr);
            CHECK(r.surface_y(j.sB, 0.0f) - graded_lie().surface_y(j.sA, 0.0f) > util::units::ft(16.0f));
            CHECK(std::any_of(net4().structures(3).begin(), net4().structures(3).end(),
                              [&](const RoadStructure& st) { return !st.trench && j.sB > st.s0 && j.sB < st.s1; }));
            continue;
        }
        REQUIRE(hit != nullptr);
        CHECK(hit->net);
        CHECK(r.surface_y(j.sB, 0.0f) == Approx(hit->yLow).margin(m(0.3f)));
        for (const float t : {-r.outer_edge(r.sample(j.sB)), 0.0f, r.outer_edge(r.sample(j.sB))})
            CHECK(graded_lie().surface_y(j.sA, 0.0f) - r.surface_y(j.sB, t) > util::units::ft(14.5f));
    }
}

TEST_CASE("N5: rung guide signs find their exits", "[corridor][network][n5]") {
    for (int r = 2; r <= 5; ++r) {
        CorridorSigns                               s;
        std::vector<std::pair<std::string, double>> jn;
        for (const NetworkJunction& j : network().junctions)
            if (j.roadA == r || j.roadB == r) jn.emplace_back(j.name, j.roadA == r ? j.sA : j.sB);
        add_guide_signs(s, net().road(r), r, [](double v, int side) { return v + side * m(100.0); }, jn);
        int rows = 0;
        for (const auto& g : road::network::kGuideSigns) rows += g.road == r ? 1 : 0;
        INFO("road " << r);
        CHECK(static_cast<int>(s.gantries.size()) == rows);
        for (const SignGantry& g : s.gantries) CHECK(g.board < static_cast<int>(std::size(sign_atlas::kNetBoards)));
        for (const SignGore& g : s.gores) CHECK(g.cell < static_cast<int>(std::size(sign_atlas::kNetGores)));
    }
    for (const int n : {27, 31})
        for (const bool wb : {false, true}) CHECK(sign_panel(n, wb) >= 0);
}

TEST_CASE("N5: no ramp sinks into a junction road's shoulder", "[corridor][network][n5]") {
    // A ramp running beside a road with its band over the shoulder (centreline off the pavement) rides at or over the
    // shoulder there. Exempt: where it rides its road and kHugEase past that, 30 m from a weld (the ends it shares with
    // another ramp keep their height), and within kClearSpread of an underpass (2 m or more under the road: its
    // approach dips past the edge, as behind a real retaining wall).
    int bad = 0, n = 0;
    for (const NetRamp& rp : net4().ramps()) {
        const NetworkJunction& j = network().junctions[rp.junction];
        for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
            const CorridorAlignment& al = net4().road(r);
            size_t                   h  = 0;
            double                   s  = 0.0;
            float                    t  = 0.0f;
            al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
            std::vector<float> sink(rp.pts.size(), 0.0f);
            std::vector<char>  hug(rp.pts.size(), 0);
            std::vector<float> deep;  // arc lengths 2 m or more under the road
            for (size_t k = 0; k < rp.pts.size(); ++k) {
                const NetRampPoint& q = rp.pts[k];
                al.project(q.x, q.z, h, s, t);
                if (s <= 0.0 || s >= al.total_length()) continue;
                const float e = al.outer_edge(al.sample(s)), in = std::abs(t) - rp.halfWidth;
                if (in >= e) continue;  // clear of the pavement
                sink[k] = al.surface_y(s, (t >= 0.0f ? 1.0f : -1.0f) * std::clamp(in, 0.0f, std::abs(t))) - q.y;
                if (sink[k] >= m(2.0f)) deep.push_back(q.u);
                const float ease = m(road::network::kHugEase);  // the lift eases in over this past a pin
                const float a    = rp.from == 255 ? m(30.0f) : rp.from == r ? rp.pin[0] + ease : 0.0f;
                const float b    = rp.to == 255 ? m(30.0f) : rp.to == r ? rp.pin[1] + ease : 0.0f;
                // a ramp climbing past the edge steeper than 12 % is the open grade defect (tuning notes N5), not a hug
                const float gr = k ? std::abs(q.y - rp.pts[k - 1].y) / std::max(1.0f, q.u - rp.pts[k - 1].u) : 0.0f;
                hug[k]         = std::abs(t) >= e && q.u > a && rp.length() - q.u > b && gr < 0.12f;
            }
            for (size_t k = 0; k < rp.pts.size(); ++k) {
                if (!hug[k] || std::any_of(deep.begin(), deep.end(), [&](float u) {
                        return std::abs(u - rp.pts[k].u) < m(road::network::kClearSpread);
                    }))
                    continue;
                ++n;
                bad += sink[k] > m(0.25f) ? 1 : 0;
            }
        }
    }
    INFO(bad << " of " << n << " ramp points over a shoulder sit under it");
    CHECK(n > 0);
    CHECK(bad == 0);
}

// ── N6: ramp grades (the N5 leftover) ──

TEST_CASE("N6: every network ramp is at most kMaxGrade steep", "[corridor][network][n6]") {
    // Steepest grade between neighbouring ramp points, on the graded LIE as the game builds it. N5 left 54-93 % next to
    // pins and welds; the N6 solve (CorridorNetwork::grade_ramps) keeps pins, welds and 16 ft clearance. Where those
    // leave no room in the compact layout a ramp may reach kMaxGradeTight (N7: 17 ramps, 8.5-10.3 %). The LIE x CIP
    // pair that passes under the LIE and over the CIP within 30 m (19.9 % in N6) is 8.8 % with the parkway clearance.
    namespace nw = road::network;
    int tight    = 0;
    for (size_t k = 0; k < net4().ramps().size(); ++k) {
        const NetRamp& rp    = net4().ramps()[k];
        float          worst = 0.0f;
        for (size_t i = 1; i < rp.pts.size(); ++i)
            worst = std::max(worst,
                             std::abs(rp.pts[i].y - rp.pts[i - 1].y) / std::max(1.0f, rp.pts[i].u - rp.pts[i - 1].u));
        const int64_t osm = network().ramps[k].osmId;
        INFO("ramp " << k << " osm " << osm << " grade " << worst * 100.0f << " %");
        if (worst <= nw::kMaxGrade + 0.001f) continue;
        ++tight;
        CHECK(worst <= nw::kMaxGradeTight);  // N7: the LIE x CIP pair 19.9 % -> 8.8 % (parkway clearance)
    }
    CHECK(tight <= 17);
    // and the handoffs still hold: N3-N5 movements, loops and clearance are the tests above
}
TEST_CASE("N6: closed local exits per road match the bake; none hands the car on", "[corridor][network][n6]") {
    // Decision 8: every local exit on a network road's drivable span is signed but closed. The bake adds a stub for
    // each exit node whose link is no network ramp (2026-11-22: BQE 8, GCP 21, Van Wyck 1, Clearview 1, CIP 3); the
    // runtime also closes junction ramps that reach no road (the Kew Gardens Union Tpke links, C/D forks to streets).
    const int kStubs[6] = {0, 8, 21, 1, 1, 3};
    int       stubs[6] = {}, closed[6] = {}, branches = 0;
    for (size_t k = 0; k < net4().ramps().size(); ++k) {
        const NetRamp& rp = net4().ramps()[k];
        const bool     st = network().ramps[k].stub != 0;
        INFO("ramp " << k << " osm " << network().ramps[k].osmId);
        if (st) {
            CHECK(rp.closed);  // every baked stub is closed at runtime
            CHECK(rp.from >= 1);
            CHECK(rp.from <= 5);
            ++stubs[rp.from < 6 ? rp.from : 0];
        }
        if (!rp.closed) continue;
        CHECK(rp.from != 0);  // the LIE keeps its exits
        rp.from == 255 ? ++branches : ++closed[rp.from];
        CHECK(rp.end() < rp.length());
        // Drive it from where it starts (its road, or the live ramp it forks off) to past its end: held at the
        // barricades, never handed to another piece, never on a road again
        Run c;
        c.pos   = rp.from != 255 ? on_road(rp.from, rp.pts.front().x, rp.pts.front().z)
                                 : NetPos{0, static_cast<int>(k), 0.0, 0.0f, +1, 0};
        c.y     = rp.from != 255 ? net4().road(rp.from).surface_y(c.pos.s, c.pos.lat) : rp.pts.front().y;
        bool on = false, left = false;
        for (double u = 0.0; u <= static_cast<double>(rp.length()) + m(10.0); u += m(1.0)) {
            double x = 0.0, z = 0.0;
            ramp_xz(rp, u, x, z);
            step_to(c, x, z);
            on   = on || c.pos.ramp == static_cast<int>(k);
            left = left || (on && c.pos.ramp != static_cast<int>(k));
        }
        CHECK(on);
        CHECK(!left);
        CHECK(c.held);
        CHECK(c.pos.ramp == static_cast<int>(k));
        CHECK(c.pos.s <= static_cast<double>(rp.end()) + 1.0);
    }
    for (int r = 1; r < 6; ++r) {
        INFO("road " << r);
        CHECK(stubs[r] == kStubs[r]);
        CHECK(closed[r] >= stubs[r]);
    }
    CHECK(branches > 0);
}

TEST_CASE("N6: every network exit is signed, closed ones with EXIT CLOSED, parkways on posts",
          "[corridor][network][n6]") {
    // Per road: advance boards (1 mile / 1/2 mile by the collapse rule) or its N3-N5 row, an exit-direction sign before
    // each gore, a gore sign; every closed exit leaving the road shows the EXIT CLOSED panel on a board before it.
    int dirs = 0, panels = 0;
    for (int r = 1; r <= 5; ++r) {
        INFO("road " << r);
        const CorridorSigns      s  = network_signs(net4(), r);
        const CorridorAlignment& al = net4().road(r);
        for (const SignGantry& g : s.gantries) {
            CHECK(g.board < static_cast<int>(std::size(sign_atlas::kNetBoards)));
            CHECK(g.post == (is_parkway(r) && g.board2 < 0 && !g.left));
            dirs += g.arrow ? 1 : 0;
            panels += g.closed ? 1 : 0;
        }
        for (const SignGore& g : s.gores) CHECK(g.cell < static_cast<int>(std::size(sign_atlas::kNetGores)));
        for (const NetRamp& rp : net4().ramps()) {
            if (!rp.closed || rp.from != r) continue;
            size_t h  = al.index_at(rp.endS[0]);
            double ps = 0.0;
            float  pt = 0.0f;
            al.project(rp.pts.front().x, rp.pts.front().z, h, ps, pt);
            const int    side = pt >= 0.0f ? +1 : -1;
            const double gore = ps + side * static_cast<double>(rp.pin[0]);
            bool         ok   = false;
            for (const SignGantry& g : s.gantries)
                ok = ok ||
                     (g.closed && g.side == side && (gore - g.s) * side > -m(5.0) && (gore - g.s) * side < m(2500.0));
            INFO("closed ramp at s " << ps / m(1.0) << " side " << side);
            // an exit with no exit record near it (a junction's dead end) has no board to close
            bool rec = false;
            for (const CorridorExit& e : al.exits())
                rec = rec || ((e.westbound != 0) == (side < 0) && (ps - e.station) * side > -m(200.0) &&
                              (ps - e.station) * side < m(900.0));
            if (rec) CHECK(ok);
        }
    }
    CHECK(dirs > 20);
    CHECK(panels > 20);
}

// ── N7: look per road, network ground ──

namespace {
const CorridorNetwork& net7() {
    static const CorridorNetwork n = [] {
        CorridorNetwork c;
        c.load(ASSET_DIR "corridor/queens_network_full.bin", ASSET_DIR, &graded_lie());
        c.dress(1.0f, m(250.0f), m(4.5f));
        return c;
    }();
    return n;
}
}  // namespace

TEST_CASE("N7: every network road has LOOK rows naming a D5 kit; parkway W-beam, GCP sound walls",
          "[corridor][network][n7]") {
    for (int r = 1; r < static_cast<int>(net7().road_count()); ++r) {
        const CorridorAlignment& al = net7().road(r);
        INFO("road " << r);
        REQUIRE_FALSE(al.look_rows().empty());
        for (const CorridorLookRow& row : al.look_rows()) {
            CHECK(std::any_of(std::begin(road::dressing::kKits), std::end(road::dressing::kKits),
                              [&](const road::dressing::Kit& k) { return k.fromExit == row.kit; }));
            CHECK(row.lamp <= static_cast<uint8_t>(road::dressing::Lamp::Cobra));
        }
        CHECK(al.edges().ground == &net7().ground());
        CHECK(al.edges().netRoad == r);
    }
    const auto len = [](const CorridorEdges::Spans& v) {
        double t = 0.0;
        for (const auto& [a, b] : v) t += b - a;
        return t;
    };
    CHECK(len(net7().road(5).edges().rail[1]) > m(1000.0));  // CIP: W-beam through Alley Pond (SV-11)
    CHECK(len(net7().road(2).edges().wall[1]) > m(300.0));   // GCP: sound walls at the Nassau line
    // rails and walls stop off structures (the BQE has no rail rows at all)
    CHECK(net7().road(1).edges().rail[0].empty());
}

TEST_CASE("N7: the network ground never covers at-grade pavement and leaves no void", "[corridor][network][n7]") {
    const CorridorGround& g = net7().ground();
    REQUIRE(g.valid());
    int over = 0, n = 0;
    for (const NetRamp& rp : net7().ramps())
        for (size_t i = 0; i < rp.pts.size(); i += 5) {
            const NetRampPoint& p = rp.pts[i];
            if (p.y - p.ground > m(road::network::kPierMinH)) continue;  // on piers: the ground is the DEM under it
            ++n;
            const float h = g.height(p.x, p.z);
            REQUIRE(std::isfinite(h));
            over += h > p.y - std::abs(p.bank) * rp.halfWidth ? 1 : 0;
            CHECK(g.clearance(p.x, p.z, 0) < 0.0f);  // a ramp is pavement to every road's dressing
        }
    INFO(over << " of " << n << " at-grade ramp points under the ground");
    CHECK(n > 1000);
    CHECK(over == 0);
    for (int r = 1; r < static_cast<int>(net7().road_count()); ++r) {  // at-grade road centrelines too
        const CorridorAlignment& al = net7().road(r);
        for (double s = 0.0; s < al.total_length(); s += m(50.0)) {
            const auto sm = al.sample(s);
            const bool st = std::any_of(net7().structures(r).begin(), net7().structures(r).end(),
                                        [&](const RoadStructure& x) { return s >= x.s0 && s <= x.s1; });
            if (st) continue;
            CHECK(g.height(sm.x, sm.z) <= al.surface_y(s, 0.0f));
        }
    }
    // far from every road the coarse grid still answers (the horizon is ground, not the void)
    const auto c = net7().road(2).sample(m(9000.0));
    CHECK(std::isfinite(g.height(c.x + m(2000.0), c.z + m(2000.0))));
}

// ── N8: landmarks (decision 9) ──

TEST_CASE("N8: every landmark row builds; Throgs Neck is the one far segment; Jamaica Yard replaces its crossings",
          "[corridor][network][n8]") {
    namespace nm                = road::netmarks;
    const CorridorNetwork& net  = net7();
    const auto&            rows = net.asset().landmarks;
    const CorridorLook look{Vec4(0.3f), Vec4(0.3f), Vec4(0.6f), Vec4(0.4f), Vec4(1.0f), Vec4(1.0f, 0.8f, 0.0f, 1.0f),
                            4000.0f,    4000.0f,    4000.0f};
    REQUIRE(rows.size() == 12);
    const auto segs = network_landmark_segments(net, look, StructureParams{});
    CHECK(segs.size() == rows.size());  // none came out empty (the arch found the GCP over the CIP, Astoria its street)
    int far = 0;
    for (const CorridorSegment& s : segs) {
        CHECK_FALSE(s.draws.empty());
        if (!s.far) continue;
        ++far;
        CHECK(s.far == s.draws.size());
        for (const DrawCall& d : s.draws) CHECK(d.noShadow);
    }
    CHECK(far == 1);
    for (const NetworkLandmark& l : rows) {
        if (static_cast<nm::Kind>(l.kind) == nm::Kind::Jamaica) {
            const auto& x = net.road(l.road).crossings();
            CHECK(std::count_if(x.begin(), x.end(), [&](const CorridorCrossing& c) {
                      return landmark_replaces(net, l.road, c.station);
                  }) >= 1);
        }
        if (static_cast<nm::Kind>(l.kind) == nm::Kind::Astoria) {
            const auto& x = net.road(l.road).crossings();
            CHECK(std::any_of(x.begin(), x.end(), [&](const CorridorCrossing& c) {
                return std::abs(c.station - l.s[0]) < m(nm::kSnap) &&
                       std::string(c.name).find("31st") != std::string::npos;
            }));
        }
    }
}

TEST_CASE("N8: building landmarks stand clear of every network road and ramp", "[corridor][network][n8]") {
    namespace nm               = road::netmarks;
    const CorridorNetwork& net = net7();
    int                    n   = 0;
    for (const NetworkLandmark& l : net.asset().landmarks) {
        const auto  k = static_cast<nm::Kind>(l.kind);
        float       r = 0.0f;
        double      x = 0.0, z = 0.0;
        const auto& al  = net.road(l.road);
        const auto  sm  = al.sample(l.s[0]);
        const float lat = landmark_lat(net, l);
        if (k == nm::Kind::Gas) r = m(15.0f);
        if (k == nm::Kind::Citi) r = m(nm::kCitiA);
        if (k == nm::Kind::Ashe) r = m(nm::kAsheR);
        if (k == nm::Kind::Terminal) r = m(0.5f * nm::kTermD);
        if (r == 0.0f) continue;
        ++n;
        x = sm.x - sm.tz * lat, z = sm.z + sm.tx * lat;
        INFO("landmark kind " << static_cast<int>(l.kind) << " slid " << (lat - l.lat[0]) / 1e3 << " m");
        for (int rd = 0; rd < static_cast<int>(net.road_count()); ++rd) {
            const CorridorAlignment& o = net.road(rd);
            size_t                   h = 0;
            double                   s = 0.0;
            float                    t = 0.0f;
            o.project_global(x, z, h, s, t);
            if (s <= 0.0 || s >= o.total_length()) continue;
            INFO("road " << rd << " s " << s / 1e3 << " m lat " << t / 1e3 << " m");
            CHECK(std::abs(t) - o.outer_edge(o.sample(s)) > r);
        }
        float  near  = 1e30f;
        size_t which = 0;
        for (size_t i = 0; i < net.ramps().size(); ++i)
            for (const NetRampPoint& p : net.ramps()[i].pts) {
                const float d = static_cast<float>(std::hypot(p.x - x, p.z - z)) - net.ramps()[i].halfWidth;
                if (d < near) near = d, which = i;
            }
        INFO("ramp " << which << " osm " << net.asset().ramps[which].osmId << " closed " << net.ramps()[which].closed
                     << " junction " << int(net.ramps()[which].junction) << " from " << int(net.ramps()[which].from)
                     << " len " << net.ramps()[which].length() / 1e3);
        CHECK(near > r);
    }
    CHECK(n == 4);
}

TEST_CASE("N8: the Throgs Neck towers stand ahead of the Clearview north wall, 3-8 km out", "[corridor][network][n8]") {
    namespace nm                = road::netmarks;
    const CorridorNetwork& net  = net7();
    const auto&            rows = net.asset().landmarks;
    const auto             it   = std::find_if(rows.begin(), rows.end(), [](const NetworkLandmark& l) {
        return static_cast<nm::Kind>(l.kind) == nm::Kind::Throgs;
    });
    REQUIRE(it != rows.end());
    const CorridorAlignment& cv = net.road(it->road);
    double                   lo = 0.0, hi = 0.0;
    net.drivable(it->road, lo, hi);
    const auto w    = cv.sample(lo);  // the north wall; the car looks along -s
    const auto segs = network_landmark_segments(net, CorridorLook{}, StructureParams{});
    const auto far  = std::find_if(segs.begin(), segs.end(), [](const CorridorSegment& s) { return s.far > 0; });
    REQUIRE(far != segs.end());
    const double dx = far->originX - w.x, dz = far->originZ - w.z, d = std::hypot(dx, dz);
    const double ang = std::acos(std::clamp((-dx * w.tx - dz * w.tz) / d, -1.0, 1.0)) * 180.0 / M_PI;
    INFO("Throgs Neck " << d / 1e6 << " km from the Clearview north wall, " << ang << " deg off its axis");
    CHECK(d > m(3000.0f));
    CHECK(d < m(8000.0f));
    CHECK(ang < 20.0);
}
