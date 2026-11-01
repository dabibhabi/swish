// Realistic-wipe helpers (scene/Entity/WiperWipe.h), the learning scaffold for
// docs/realism-learning-plan.md modules M1 and M4. Hidden ([.learning]): `make test` skips them;
// run `build/tests/swish_tests "[learning][wiper_wipe]"`. They fail until the stubs are filled in.
// When they pass, drop `.learning` from the tags so ctest guards them.

#include "scene/Entity/Wiper.h"
#include "scene/Entity/WiperWipe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/packing.hpp>

using namespace swish;
using Catch::Approx;

TEST_CASE("M1: sweep direction follows the angle", "[.learning][wiper_wipe]") {
    REQUIRE(wiper_sweep_direction(0.10f, 0.20f) == +1);  // moving away from park
    REQUIRE(wiper_sweep_direction(0.20f, 0.10f) == -1);  // returning
    REQUIRE(wiper_sweep_direction(0.30f, 0.30f) == 0);   // dwelling
    REQUIRE(wiper_sweep_direction(0.30f, 0.30f + 1e-8f) == 0);
}

TEST_CASE("M1: sweep direction over one real wipe is out, then back", "[.learning][wiper_wipe]") {
    const WiperParams p;
    WiperState        s    = wiper_set_mode({}, WiperMode::Low);
    float             prev = wiper_angle(s, p);
    int               outs = 0, backs = 0, flips = 0, last = 0;
    for (int i = 0; i < 160; i++) {  // 160 x 1/120 s = 1.33 s = exactly one Low wipe
        s                 = wiper_step(s, p, 1.f / 120.f);
        const float angle = wiper_angle(s, p);
        const int   dir   = wiper_sweep_direction(prev, angle);
        outs += dir > 0;
        backs += dir < 0;
        if (dir != 0 && last != 0 && dir != last) flips++;
        if (dir != 0) last = dir;
        prev = angle;
    }
    REQUIRE(outs > 60);
    REQUIRE(backs > 60);
    REQUIRE(flips == 1);  // one reversal at the top
}

TEST_CASE("M4: the pile is released on the frame the blade parks", "[.learning][wiper_wipe]") {
    REQUIRE(wiper_reached_park(0.05f, 0.0f));
    REQUIRE_FALSE(wiper_reached_park(0.0f, 0.0f));  // already parked
    REQUIRE_FALSE(wiper_reached_park(0.10f, 0.05f));
    REQUIRE_FALSE(wiper_reached_park(0.0f, 0.05f));  // leaving park
}

TEST_CASE("M4: the blade leaves a residual film, never adds water", "[.learning][wiper_wipe]") {
    REQUIRE(wiper_film_after_clear(1.0f, 1.0f, 0.2f) == Approx(0.2f));  // full clear -> film
    REQUIRE(wiper_film_after_clear(1.0f, 0.0f, 0.2f) == Approx(1.0f));  // untouched
    REQUIRE(wiper_film_after_clear(1.0f, 0.5f, 0.2f) == Approx(0.6f));  // feathered band edge
    REQUIRE(wiper_film_after_clear(0.1f, 1.0f, 0.2f) == Approx(0.1f));  // drier than the film
}

TEST_CASE("M4: water collected is what the film did not keep", "[.learning][wiper_wipe]") {
    REQUIRE(wiper_water_collected(1.0f, 1.0f, 0.2f) == Approx(0.8f));
    REQUIRE(wiper_water_collected(1.0f, 0.0f, 0.2f) == Approx(0.0f).margin(1e-6));
    REQUIRE(wiper_water_collected(0.1f, 1.0f, 0.2f) == Approx(0.0f).margin(1e-6));
    for (float w = 0.f; w <= 1.f; w += 0.125f) REQUIRE(wiper_water_collected(w, 0.7f, 0.25f) >= 0.f);
}

TEST_CASE("M4: wipe generations wrap and survive a half-float channel", "[.learning][wiper_wipe]") {
    REQUIRE(wiper_next_generation(0) == 1);
    REQUIRE(wiper_next_generation(kWipeGenerations - 1) == 0);
    for (int g = 0; g < kWipeGenerations; g++) {
        const float v = wiper_generation_encode(g);
        REQUIRE(v >= 0.f);
        REQUIRE(v < 1.f);
        REQUIRE(wiper_generation_decode(v) == g);
        const float half = glm::unpackHalf1x16(glm::packHalf1x16(v));  // what an R16G16B16A16_SFLOAT texel keeps
        REQUIRE(wiper_generation_decode(half) == g);
    }
}
