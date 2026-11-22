#pragma once

// C3b: what the in-game HUD strip and route minimap show. App fills it once per frame (debug build only,
// AppHud.cpp); renderer/Passes/HudOverlayPass.cpp draws it over the finished frame. Header-only, no ImGui or
// Vulkan, so the pure helpers below are unit-tested from swish_tests.

#include "utils/types/Types.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <string>
#include <vector>

namespace swish {

struct HudFeed {
    bool        corridor = false;  // false (lab, no asset): the strip shows speed only and no map
    float       speedMph = 0.0f;
    std::string district, piece;            // "Nassau", "Mainline" / "Ramp" / "Service road" / "Crossroad"
    std::string shield;                     // N9: Interstate shield before the piece ("" = none)
    std::string tab, exitShield, exitDest;  // "EXIT 13" / N9 "EXIT 39 CLOSED", "ROAD ENDS", "TO"; empty = none
    float       exitMiles = 0.0f;
    bool        warn      = false;                // N9: orange tab (closed exit, road ends)
    Vec2        car{0.0f}, heading{0.0f, -1.0f};  // true world x, z (WU); unit forward in x, z

    // Built once per corridor (true world x, z in WU): the whole route, service-road stretches, exit ticks.
    std::vector<Vec2>              route;
    std::vector<std::vector<Vec2>> service;
    std::vector<Vec2>              exitAt, exitDir;  // tick foot and unit direction (towards the exit's side)
    Vec2                           routeMin{0.0f}, routeMax{0.0f};
    // N9 (debug network): roads 1..5 then the ramps (built once), their extent, and the lit one (-1 = the LIE).
    std::vector<std::vector<Vec2>> net;
    size_t                         netRoads = 0;
    Vec2                           netMin{0.0f}, netMax{0.0f};
    int                            lit = -1;
};

namespace hud {

inline constexpr float kWuPerMile = util::units::ft(5280.0f);

constexpr float mph(float mps) { return mps * 3600.0f / util::units::to_m(kWuPerMile); }

// Station distance -> miles; `real / baked` undoes the corridor compaction (1 when the asset has none).
constexpr float miles(double dsWu, double realLen, double bakedLen) {
    const double k = (realLen > 0.0 && bakedLen > 0.0) ? realLen / bakedLen : 1.0;
    return static_cast<float>(dsWu * k / static_cast<double>(kWuPerMile));
}

// World (x, z) -> minimap pixel: `centre` sits mid-square, `halfSpan` WU reaches the edge, -z is up.
inline Vec2 map_px(Vec2 w, Vec2 centre, float halfSpan, Vec2 origin, float sizePx) {
    const float k = 0.5f * sizePx / halfSpan;
    return origin + Vec2(0.5f * sizePx) + (w - centre) * k;
}

// Minimap framing: local = 2 km on the car; else the whole LIE, or the network (N9) while the car is on it.
inline void map_frame(const HudFeed& h, bool local, Vec2& centre, float& half) {
    const bool net = h.lit >= 0 && !h.net.empty();
    const Vec2 lo = net ? h.netMin : h.routeMin, hi = net ? h.netMax : h.routeMax;
    centre = local ? h.car : 0.5f * (lo + hi);
    half   = local ? util::units::m(1000.0f) : 0.5f * std::max(hi.x - lo.x, hi.y - lo.y) * 1.08f;
}

}  // namespace hud
}  // namespace swish
