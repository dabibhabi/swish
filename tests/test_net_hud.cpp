// N9 HUD and minimap off the LIE (CorridorNetHud.cpp): road name, shield and direction, next exit (closed or open),
// ROAD ENDS before a wall, the ramp's landing road, and the minimap network frame.
#include "debug/HudFeed.h"
#include "scene/Corridor/CorridorNetwork.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

using namespace swish;
using swish::util::units::m;

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

NetPos at(int r, double s, int side) {
    NetPos p = net().place(r, s);
    p.side   = side;
    return p;
}

constexpr float kEnds = hud::kWuPerMile;  // the hud_road_ends_mi default (1 mile)

}  // namespace

TEST_CASE("N9: signed directions per road (MUTCD 2D.13)", "[corridor][network][n9]") {
    // +s: LIE east, BQE to Astoria = I-278 East, GCP east, the rungs south
    const char up[6] = {'E', 'E', 'E', 'S', 'S', 'S'};
    const char dn[6] = {'W', 'W', 'W', 'N', 'N', 'N'};
    for (int r = 0; r < 6; ++r) {
        INFO("road " << r);
        CHECK(net_direction(r, +1) == up[r]);
        CHECK(net_direction(r, -1) == dn[r]);
    }
}

TEST_CASE("N9: HUD on each network road names it and finds the next exit ahead", "[corridor][network][n9]") {
    for (int r = 1; r < 6; ++r) {
        double lo = 0.0, hi = 0.0;
        net().drivable(r, lo, hi);
        const CorridorAlignment& al = net().road(r);
        const double             k  = al.header().realLength / al.total_length();
        for (const int side : {+1, -1}) {
            for (double f : {0.25, 0.5, 0.75}) {
                const double s = lo + (hi - lo) * f;
                INFO("road " << r << " side " << side << " s " << s / m(1.0));
                const NetHud h = network_hud(net(), at(r, s, side), kEnds);
                CHECK(h.district == "Queens");
                CHECK(h.shield == road::network::kHudRoads[r].shield);
                CHECK(h.road == std::string(road::network::kHudRoads[r].name) + " " + net_direction(r, side));
                // brute force: the nearest exit record ahead on this carriageway inside the walls
                const CorridorExit* want = nullptr;
                for (const CorridorExit& e : al.exits())
                    if ((e.westbound != 0) == (side < 0) && (e.station - s) * side > 0.0 && e.station >= lo &&
                        e.station <= hi && (!want || std::abs(e.station - s) < std::abs(want->station - s)))
                        want = &e;
                if (h.tab == "ROAD ENDS") continue;
                if (!want) {
                    CHECK(h.tab.empty());
                    continue;
                }
                CHECK(h.tab.rfind("EXIT " + std::string(want->ref), 0) == 0);
                CHECK(h.dist == Catch::Approx(std::abs(want->station - s) * k));
            }
        }
    }
}

TEST_CASE("N9: a closed exit reads EXIT n CLOSED in orange, an open one does not", "[corridor][network][n9]") {
    int closed = 0, open = 0;
    for (int r = 1; r < 6; ++r) {
        double lo = 0.0, hi = 0.0;
        net().drivable(r, lo, hi);
        for (const CorridorExit& e : net().road(r).exits()) {
            const int    side = e.westbound ? -1 : +1;
            const double s    = e.station - side * m(150.0);
            if (e.station < lo || e.station > hi || s < lo || s > hi) continue;
            const ExitSite x = net_exit_site(net(), r, e.station, side);
            const NetHud   h = network_hud(net(), at(r, s, side), 0.0f);      // no ROAD ENDS
            if (h.tab.rfind("EXIT " + std::string(e.ref), 0) != 0) continue;  // another record nearer (12A / 12B)
            INFO("road " << r << " exit " << e.ref);
            const bool shut = x.gore < 0.0 || x.closed;
            CHECK(h.warn == shut);
            CHECK((h.tab.find("CLOSED") != std::string::npos) == shut);
            (shut ? closed : open)++;
        }
    }
    CHECK(closed >= 20);  // N6: 34 baked stubs, most of them alone at their spot
    CHECK(open >= 3);     // the junction exits
}

TEST_CASE("N9: ROAD ENDS shows before every work-zone wall, at the real distance", "[corridor][network][n9]") {
    int ends = 0;
    for (const NetworkWall& w : net().asset().walls) {
        const int                side = w.end ? +1 : -1;  // driving towards the wall
        const CorridorAlignment& al   = net().road(w.road);
        const double             k    = al.header().realLength / al.total_length();
        const double             s    = w.s - side * m(200.0);
        INFO("road " << int(w.road) << " end " << int(w.end));
        const NetHud h = network_hud(net(), at(w.road, s, side), kEnds);
        if (h.tab.rfind("EXIT", 0) == 0) continue;  // an exit between the car and the wall comes first
        CHECK(h.tab == "ROAD ENDS");
        CHECK(h.warn);
        CHECK(h.dist == Catch::Approx(m(200.0) * k));
        CHECK(network_hud(net(), at(w.road, s, side), 0.0f).tab != "ROAD ENDS");  // threshold respected
        ++ends;
    }
    CHECK(ends >= 7);  // of 9 walls; an exit record can sit in the last 200 m
}

TEST_CASE("N9: on a ramp the HUD names the road it lands on, or EXIT CLOSED", "[corridor][network][n9]") {
    int to = 0, shut = 0;
    for (size_t k = 0; k < net().ramps().size(); ++k) {
        const NetRamp& rp = net().ramps()[k];
        const NetHud   h  = network_hud(net(), NetPos{0, static_cast<int>(k), m(5.0), 0.0f, +1, 0}, kEnds);
        INFO("ramp " << k << " " << int(rp.from) << " -> " << int(rp.to));
        CHECK(h.road == "Ramp");
        if (rp.closed) {
            CHECK(h.tab == "EXIT CLOSED");
            CHECK(h.warn);
            ++shut;
        } else if (rp.to != 255) {
            CHECK(h.tab == "TO");
            const auto& R = road::network::kHudRoads[rp.to];
            CHECK(h.dest.rfind(R.name, 0) == 0);
            if (*R.shield) CHECK(h.top.rfind(R.shield, 0) == 0);
            CHECK(h.dist == Catch::Approx(static_cast<double>(rp.end()) - m(5.0)));
            ++to;
        }
    }
    CHECK(to > 20);
    CHECK(shut > 40);
}

TEST_CASE("N9: minimap draws all roads and ramps; the network frame keeps the GCP readable",
          "[corridor][network][n9]") {
    HudFeed h;
    h.net      = network_map_lines(net());
    h.netRoads = 5;
    REQUIRE(h.net.size() == 5 + net().ramps().size());
    h.netMin = h.netMax = h.net.front().front();
    for (const auto& l : h.net) {
        CHECK(l.size() >= 2);
        for (Vec2 q : l) h.netMin = glm::min(h.netMin, q), h.netMax = glm::max(h.netMax, q);
    }
    // the LIE route as AppHud builds it (only its extent matters here)
    const CorridorAlignment& lie = net().road(0);
    for (double s = 0.0; s <= lie.total_length(); s += m(500.0))
        h.route.emplace_back(static_cast<float>(lie.sample(s).x), static_cast<float>(lie.sample(s).z));
    h.routeMin = h.routeMax = h.route.front();
    for (Vec2 q : h.route) h.routeMin = glm::min(h.routeMin, q), h.routeMax = glm::max(h.routeMax, q);

    // car on the GCP 1 km short of its east wall, far from the LIE
    double lo = 0.0, hi = 0.0;
    net().drivable(2, lo, hi);
    const NetPos p  = net().place(2, hi - m(1000.0));
    const auto   sm = net().road(2).sample(p.s);
    h.car           = Vec2(static_cast<float>(sm.x - static_cast<double>(p.lat * sm.tz)),
                           static_cast<float>(sm.z + static_cast<double>(p.lat * sm.tx)));
    h.lit           = 1;

    const float size = 220.0f;
    Vec2        c;
    float       half = 0.0f;
    hud::map_frame(h, false, c, half);
    const Vec2 car = hud::map_px(h.car, c, half, Vec2(0.0f), size);
    CHECK(car.x > 0.0f);
    CHECK(car.x < size);
    CHECK(car.y > 0.0f);
    CHECK(car.y < size);
    float near = 1e9f;  // the arrow sits on the lit (GCP) line
    for (Vec2 q : h.net[1]) near = std::min(near, glm::length(hud::map_px(q, c, half, Vec2(0.0f), size) - car));
    CHECK(near < 2.0f);
    // the GCP spans a good part of the square, several times more than in the whole-LIE frame
    Vec2 a(1e9f), b(-1e9f);
    for (Vec2 q : h.net[1]) {
        const Vec2 v = hud::map_px(q, c, half, Vec2(0.0f), size);
        a = glm::min(a, v), b = glm::max(b, v);
    }
    CHECK(std::max(b.x - a.x, b.y - a.y) > 0.5f * size);
    Vec2  c2;
    float half2 = 0.0f;
    h.lit       = -1;  // on the LIE: the whole-route frame (unchanged)
    hud::map_frame(h, false, c2, half2);
    CHECK(half2 > 3.0f * half);
    hud::map_frame(h, true, c2, half2);  // 2 KM view stays on the car
    CHECK(glm::length(hud::map_px(h.car, c2, half2, Vec2(0.0f), size) - Vec2(0.5f * size)) < 1e-3f);
}
