#pragma once

// C3b: what the in-game HUD strip and route minimap show. App fills it once per frame (debug build only,
// AppHud.cpp); renderer/Passes/HudOverlayPass.cpp draws it over the finished frame. Header-only, no ImGui or
// Vulkan, so the pure helpers below are unit-tested from swish_tests.

#include "utils/types/Types.h"
#include "utils/units/Units.h"

#include <string>
#include <vector>

namespace swish {

struct HudFeed {
    bool        corridor = false;  // false (lab, no asset): the strip shows speed only and no map
    float       speedMph = 0.0f;
    std::string district, piece;                // "Nassau", "Mainline" / "Ramp" / "Service road" / "Crossroad"
    std::string exitRef, exitShield, exitDest;  // next exit ahead; exitRef empty = none
    float       exitMiles = 0.0f;
    Vec2        car{0.0f}, heading{0.0f, -1.0f};  // true world x, z (WU); unit forward in x, z

    // Built once per corridor (true world x, z in WU): the whole route, service-road stretches, exit ticks.
    std::vector<Vec2>              route;
    std::vector<std::vector<Vec2>> service;
    std::vector<Vec2>              exitAt, exitDir;  // tick foot and unit direction (towards the exit's side)
    Vec2                           routeMin{0.0f}, routeMax{0.0f};
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

}  // namespace hud
}  // namespace swish
