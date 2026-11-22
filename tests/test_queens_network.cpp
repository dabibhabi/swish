#include "scene/Corridor/CorridorAlignment.h"
#include "scene/Corridor/CorridorNetworkAsset.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE("network: nine junctions, two walls per new road, ramps in range", "[corridor][network]") {
    CHECK(network().junctions.size() == 9);
    CHECK(network().walls.size() == 10);
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
        }
    }
}

TEST_CASE("network: drivable mainline radius stays above the bake's reported minimum", "[corridor][network]") {
    // Per road, the tightest compact radius between the walls (50 m baseline, as curve_radii). Real curves
    // tighter than 500 m are exempt from the 500 m floor (as on the LIE); these pins are the bake's measured
    // minima rounded down, so a re-bake that tightens a curve fails here. See docs/corridor/tuning-notes.md N1.
    const double kMinRadius[6] = {0.0, m(PIN_BQE), m(PIN_GCP), m(PIN_VW), m(PIN_CV), m(PIN_CIP)};
    std::vector<double> lo(6, 1e30), hi(6, -1e30);
    for (const NetworkWall& w : network().walls) (w.end ? hi : lo)[w.road] = w.s;
    for (size_t r = 1; r < roads().size(); ++r) {
        const CorridorAlignment& al   = roads()[r];
        const double             sp   = al.header().stationSpacing;
        const size_t             half = static_cast<size_t>(m(50.0) / sp);
        double                   rmin = 1e30;
        for (size_t i = static_cast<size_t>(lo[r] / sp); i <= static_cast<size_t>(hi[r] / sp); ++i)
            rmin = std::min(rmin, radius_at(al, i, half));
        INFO(network().roads[r].key << " min radius " << rmin / m(1.0) << " m");
        CHECK(rmin >= kMinRadius[r]);
    }
}
