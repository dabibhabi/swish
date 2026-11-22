// App HUD feed (C3b, debug build only): car + corridor state -> DebugParams::hud, drawn by HudOverlayPass.
#ifdef SWISH_DEBUG_UI

#include "App.h"
#include "debug/DebugParams.h"
#include "renderer/Renderer/Renderer.h"
#include "scene/Entity/CarEntity.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace swish {

namespace {

// Asset text fields are fixed char arrays that may fill to the last byte (no terminator).
template <size_t N>
std::string text(const char (&a)[N]) {
    return std::string(a, strnlen(a, N));
}

Vec2 xz(const CorridorAlignment::Sample& sm) { return Vec2(static_cast<float>(sm.x), static_cast<float>(sm.z)); }

// The minimap's static geometry: once per corridor, true world x, z.
void build_route(const CorridorAlignment& al, HudFeed& h) {
    const double len  = al.total_length();
    const double step = std::max(static_cast<double>(al.header().stationSpacing), len / 2000.0);  // <= 2000 points
    for (double s = 0.0; s < len; s += step) h.route.push_back(xz(al.sample(s)));
    h.route.push_back(xz(al.sample(len)));
    h.routeMin = h.routeMax = h.route.front();
    for (Vec2 p : h.route) {
        h.routeMin = glm::min(h.routeMin, p);
        h.routeMax = glm::max(h.routeMax, p);
    }
    // Eastbound pieces sit right of travel (lateral > 0), westbound left.
    for (const CorridorServiceRoad& sr : al.service_roads()) {
        const float side = sr.westbound ? -1.0f : 1.0f;
        auto&       line = h.service.emplace_back();
        for (double s = sr.s0;; s = std::min(s + step, sr.s1)) {
            const CorridorAlignment::Sample sm = al.sample(s);
            line.push_back(xz(sm) + Vec2(-sm.tz, sm.tx) * side * (al.outer_edge(sm) + al.sr_strip()));
            if (s >= sr.s1) break;
        }
    }
    for (const CorridorExit& e : al.exits()) {
        const CorridorAlignment::Sample sm  = al.sample(e.station);
        const Vec2                      dir = Vec2(-sm.tz, sm.tx) * (e.westbound ? -1.0f : 1.0f);
        h.exitAt.push_back(xz(sm) + dir * al.outer_edge(sm));
        h.exitDir.push_back(dir);
    }
}

}  // namespace

void App::sync_hud(DebugParams& dp) {
    HudFeed& h = dp.hud;
    h.speedMph = hud::mph(std::abs(m_car->get_speed_mps()));
    h.corridor = !m_labMode && m_corridor_alignment.valid();
    if (!h.corridor) return;
    const CorridorAlignment& al = m_corridor_alignment;
    if (h.route.empty()) build_route(al, h);

    const Vec3 p = m_car->get_position();
    h.car        = Vec2(p.x, static_cast<float>(static_cast<double>(p.z) - m_corridor.origin_shift()));
    const Vec3 f = m_car->get_forward();
    if (f.x * f.x + f.z * f.z > 1e-6f) h.heading = glm::normalize(Vec2(f.x, f.z));

    // N9: off the LIE the network says where the car is.
    // shortcut: map lines built once, a network rebuild is not redrawn; clear h.net on rebuild if ramps move visibly.
    const CorridorNetwork& net = m_corridor.network();
    const NetPos&          np  = m_corridor.net_pos();
    if (net.valid() && h.net.empty()) {
        h.net      = network_map_lines(net);
        h.netRoads = net.road_count() - 1;
        h.netMin = h.netMax = h.net.front().front();
        for (const auto& l : h.net)
            for (Vec2 q : l) h.netMin = glm::min(h.netMin, q), h.netMax = glm::max(h.netMax, q);
    }
    h.lit = !net.valid() || (np.road == 0 && np.ramp < 0) ? -1
            : np.ramp >= 0                                ? static_cast<int>(h.netRoads) + np.ramp
                                                          : np.road - 1;
    if (h.lit >= 0) {
        const NetHud n = network_hud(net, np, hud::kWuPerMile * dp.hudRoadEndsMi);
        h.district = n.district, h.shield = n.shield, h.piece = n.road, h.tab = n.tab, h.exitShield = n.top;
        h.exitDest = n.dest, h.warn = n.warn, h.exitMiles = hud::miles(n.dist, 0.0, 0.0);
        return;
    }

    const double s              = m_corridor.station();
    const auto*  d              = al.district_at(s);
    h.district                  = d != nullptr ? text(d->name) : std::string();
    static const char* kPiece[] = {"Mainline", "Mainline", "Ramp", "Service road", "Crossroad"};
    h.piece                     = kPiece[static_cast<int>(m_corridor.piece())];
    h.shield.clear();
    h.warn = false;

    const CorridorExit* e = al.next_exit(s, !m_corridor.eastbound());
    h.tab                 = e != nullptr ? "EXIT " + text(e->ref) : std::string();
    if (e != nullptr) {
        h.exitShield = text(e->destRef);
        h.exitDest   = text(e->dest);
        for (size_t at; (at = h.exitDest.find("; ")) != std::string::npos;) h.exitDest.replace(at, 2, " \xC2\xB7 ");
        h.exitMiles = hud::miles(std::abs(e->station - s), al.header().realLength, al.total_length());
    }
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
