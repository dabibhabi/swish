// C3b HUD helpers (debug/HudFeed.h): mph, miles to the next exit, world -> minimap pixel.
#include "debug/HudFeed.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace swish;
using Catch::Approx;

TEST_CASE("HUD: mph, exit miles and minimap transform", "[hud]") {
    CHECK(hud::mph(26.8224f) == Approx(60.0f).epsilon(1e-5));  // 60 mph = 26.8224 m/s exactly
    CHECK(hud::miles(hud::kWuPerMile, 0.0, 0.0) == Approx(1.0f));
    CHECK(hud::miles(hud::kWuPerMile, 70.0, 30.0) == Approx(70.0f / 30.0f));  // real/baked undoes compaction

    const Vec2 o(100.0f, 50.0f);
    const Vec2 mid = hud::map_px(Vec2(5000.0f, -7000.0f), Vec2(5000.0f, -7000.0f), 1000.0f, o, 200.0f);
    CHECK(mid.x == Approx(200.0f));
    CHECK(mid.y == Approx(150.0f));
    const Vec2 edge = hud::map_px(Vec2(6000.0f, -8000.0f), Vec2(5000.0f, -7000.0f), 1000.0f, o, 200.0f);
    CHECK(edge.x == Approx(300.0f));  // +x reaches the right edge
    CHECK(edge.y == Approx(50.0f));   // -z (north) is up
}
