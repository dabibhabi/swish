#include "utils/math/Smoothing.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

namespace um = swish::util::math;
using Catch::Matchers::WithinAbs;

// ══════════════════════════════════════════════════════════════════════
// exp_smooth — framerate independence (tasks/todo.md F3)
//
// The property that actually matters isn't any single-step formula detail;
// it's that the SAME rate `k`, run for the SAME wall-clock duration, settles
// to (nearly) the same value whether it's stepped in a few big dt's or many
// small ones. A raw per-frame lerp fails this (it settles slower at lower
// framerates for a fixed factor); exp_smooth must not.
// ══════════════════════════════════════════════════════════════════════

namespace {
float settle_over(float x0, float target, float k, float dt, float total_time) {
    float x     = x0;
    int   steps = static_cast<int>(std::lround(total_time / dt));
    for (int i = 0; i < steps; ++i) x = um::exp_smooth(x, target, k, dt);
    return x;
}
}  // namespace

TEST_CASE("exp_smooth settles to the same value at very different dt for the same wall-clock time", "[smoothing]") {
    constexpr float k       = 4.0f;  // 1/s
    constexpr float target  = 10.0f;
    constexpr float x0      = 0.0f;
    constexpr float total_t = 1.0f;  // one simulated wall-clock second

    float x_30fps  = settle_over(x0, target, k, 1.0f / 30.0f, total_t);   // 30 big steps
    float x_240fps = settle_over(x0, target, k, 1.0f / 240.0f, total_t);  // 240 small steps

    // Closed-form continuous solution: x(t) = target - (target-x0)*e^{-k t}
    float expected = target - (target - x0) * std::exp(-k * total_t);

    REQUIRE_THAT(x_30fps, WithinAbs(expected, 1e-3f));
    REQUIRE_THAT(x_240fps, WithinAbs(expected, 1e-3f));
    // The property under test: 30 Hz and 240 Hz must agree with each other,
    // not just both with the closed form.
    REQUIRE_THAT(x_30fps, WithinAbs(x_240fps, 1e-3f));
}

TEST_CASE("exp_smooth never overshoots the target", "[smoothing]") {
    float x = 0.0f;
    for (int i = 0; i < 500; ++i) x = um::exp_smooth(x, 5.0f, 6.0f, 1.0f / 90.0f);
    REQUIRE(x <= 5.0f);
    REQUIRE_THAT(x, WithinAbs(5.0f, 1e-3f));
}

TEST_CASE("exp_smooth is a no-op at k<=0 or dt<=0", "[smoothing]") {
    REQUIRE_THAT(um::exp_smooth(1.0f, 5.0f, 0.0f, 0.016f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(um::exp_smooth(1.0f, 5.0f, 4.0f, 0.0f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(um::exp_smooth(1.0f, 5.0f, -2.0f, 0.016f), WithinAbs(1.0f, 1e-6f));
}

// ── exp_smooth_angle_deg — shortest-arc wrap ───────────────────────────

TEST_CASE("exp_smooth_angle_deg takes the short way around a heading wrap", "[smoothing]") {
    // x=179, target=-179: the raw numeric gap is -358 (the long way through
    // 0°), but the short way is only +2 (179 -> 180/-180 -> -179).
    // A huge k*dt drives it almost exactly to the (wrapped) target.
    float x       = um::exp_smooth_angle_deg(179.0f, -179.0f, 100.0f, 1.0f);
    float wrapped = std::fmod(x + 180.0f, 360.0f) - 180.0f;
    REQUIRE_THAT(wrapped, WithinAbs(-179.0f, 0.5f));
}

TEST_CASE("exp_smooth_angle_deg holds still at k=0", "[smoothing]") {
    float x = um::exp_smooth_angle_deg(10.0f, 170.0f, 0.0f, 0.016f);
    REQUIRE_THAT(x, WithinAbs(10.0f, 1e-6f));
}
