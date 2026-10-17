// GPU particle rain, CPU reference (renderer/RainSystem/RainParticle.h), the learning scaffold for
// docs/realism-learning-plan.md modules M5 and M6. Hidden ([.learning]): `make test` skips them;
// run `build/tests/swish_tests "[learning][rain_particle]"`. They fail until the stubs are filled in.
// When they pass, drop `.learning` from the tags so ctest guards them.

#include "renderer/RainSystem/RainParticle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace swish;
using Catch::Approx;
namespace uu = swish::util::units;

TEST_CASE("M5: a drop settles to wind plus its terminal fall speed", "[.learning][rain_particle]") {
    const Vec3 wind(4500.f, 123.f, 1200.f);
    const Vec3 v = rain_target_velocity(wind, 2.0f);
    REQUIRE(v.x == Approx(4500.f));
    REQUIRE(v.z == Approx(1200.f));
    REQUIRE(v.y == Approx(-gunn_kinzer_speed(2.0f) * uu::kWuPerMetre));  // about -6548 WU/s, wind.y ignored
    REQUIRE(rain_target_velocity(Vec3(0.f), 4.0f).y <
            rain_target_velocity(Vec3(0.f), 1.0f).y);  // big drops fall faster
}

TEST_CASE("M5: velocity relaxes exponentially and never overshoots", "[.learning][rain_particle]") {
    const Vec3 v0(0.f, 0.f, 0.f);
    const Vec3 target(0.f, -6000.f, 0.f);
    REQUIRE(rain_relax_velocity(v0, target, 0.f, 0.1f).y == Approx(0.f).margin(1e-4));  // no time, no change
    REQUIRE(rain_relax_velocity(v0, target, 0.1f, 0.1f).y == Approx(-6000.f * (1.f - std::exp(-1.f))));  // one tau
    REQUIRE(rain_relax_velocity(v0, target, 10.f, 0.1f).y == Approx(-6000.f).epsilon(1e-4));  // long step lands on it

    // The stiff case from docs M2 exercise 5: dt (1/15 s) longer than tau (0.05 s). Never past the target.
    Vec3 v(0.f, 1000.f, 0.f);
    for (int i = 0; i < 8; i++) {
        v = rain_relax_velocity(v, Vec3(0.f), 1.f / 15.f, 0.05f);
        REQUIRE(v.y >= 0.f);
    }
    REQUIRE(v.y < 1.f);
}

TEST_CASE("M5: camera-relative positions wrap through the opposite face", "[.learning][rain_particle]") {
    const float h = 20000.f;
    const Vec3  inside(100.f, -200.f, 300.f);
    const Vec3  w0 = rain_wrap_camera_relative(inside, h);
    REQUIRE(w0.x == Approx(100.f));
    REQUIRE(w0.y == Approx(-200.f));
    REQUIRE(w0.z == Approx(300.f));

    const Vec3 w1 = rain_wrap_camera_relative(Vec3(1.5f * h, -1.25f * h, 0.f), h);
    REQUIRE(w1.x == Approx(-0.5f * h));
    REQUIRE(w1.y == Approx(0.75f * h));
    REQUIRE(w1.z == Approx(0.f).margin(1e-3));

    const Vec3 w2 = rain_wrap_camera_relative(Vec3(7.3f * h, -9.9f * h, 3.1f * h), h);  // many boxes away
    for (float c : {w2.x, w2.y, w2.z}) {
        REQUIRE(c >= -h);
        REQUIRE(c < h);
    }
}

TEST_CASE("M5: drops respawn when they expire or reach the road", "[.learning][rain_particle]") {
    const float roadY = 6.f;  // config/lie_corridor.toml road_y_wu
    REQUIRE(rain_should_respawn(0.f, 5000.f, roadY));
    REQUIRE(rain_should_respawn(-0.1f, 5000.f, roadY));
    REQUIRE(rain_should_respawn(2.f, 5.f, roadY));
    REQUIRE_FALSE(rain_should_respawn(2.f, 5000.f, roadY));
}

TEST_CASE("M6: a falling step crosses the windshield plane at the right point", "[.learning][rain_particle]") {
    const Vec3 q(0.f, 1000.f, 0.f);
    const Vec3 n(0.f, 1.f, 0.f);  // a horizontal plane at y = 1000 (a real windshield is tilted; same math)
    float      t = -1.f;

    REQUIRE(rain_crosses_plane(Vec3(0.f, 1100.f, 0.f), Vec3(50.f, 900.f, 0.f), q, n, t));
    REQUIRE(t == Approx(0.5f));

    REQUIRE_FALSE(rain_crosses_plane(Vec3(0.f, 1300.f, 0.f), Vec3(0.f, 1200.f, 0.f), q, n, t));   // stops short
    REQUIRE_FALSE(rain_crosses_plane(Vec3(0.f, 1100.f, 0.f), Vec3(80.f, 1100.f, 0.f), q, n, t));  // parallel

    const Vec3 tilted = glm::normalize(Vec3(0.6f, 0.8f, 0.f));  // works for any orientation
    REQUIRE(rain_crosses_plane(Vec3(0.f, 50.f, 0.f), Vec3(0.f, -50.f, 0.f), Vec3(0.f), tilted, t));
    REQUIRE(t == Approx(0.5f));
}
