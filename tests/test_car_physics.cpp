#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scene/Entity/CarPhysics.h"

using namespace swish;
using Catch::Matchers::WithinAbs;

// Exercises the longitudinal force model (P0 #5). All pure math — no GPU.

TEST_CASE("Launch acceleration is traction-limited (~mu*g)", "[car][longitudinal]") {
    CarParams p;
    float a0 = longitudinal_accel(0.0f, /*throttle=*/1.0f, /*brake=*/0.0f, p);
    // At standstill the power-limited term is huge, so the tire traction cap
    // (mu*m*g) dominates: a ≈ mu*g ≈ 10.8 m/s^2.
    REQUIRE(a0 > 9.5f);
    REQUIRE(a0 < 11.5f);
    REQUIRE_THAT(a0, WithinAbs(p.tireMu * p.gravity, 0.5f));
}

TEST_CASE("Acceleration falls with speed (power-limited)", "[car][longitudinal]") {
    CarParams p;
    float a0  = longitudinal_accel(0.0f,  1.0f, 0.0f, p);
    float a40 = longitudinal_accel(40.0f, 1.0f, 0.0f, p);
    float a85 = longitudinal_accel(85.0f, 1.0f, 0.0f, p);
    REQUIRE(a0 > a40);
    REQUIRE(a40 > a85);
    REQUIRE(a85 > 0.0f);  // still accelerating below top speed
}

TEST_CASE("Top speed emerges from the force balance (~205 mph), not a clamp", "[car][longitudinal]") {
    CarParams p;
    float vTop = steady_state_top_speed(p);
    // ~205 mph = 91.6 m/s; accept a physically reasonable band.
    REQUIRE(vTop > 88.0f);
    REQUIRE(vTop < 96.0f);
    // Emergent: accelerating just below vTop, decelerating just above.
    REQUIRE(longitudinal_accel(vTop - 5.0f, 1.0f, 0.0f, p) > 0.0f);
    REQUIRE(longitudinal_accel(vTop + 5.0f, 1.0f, 0.0f, p) < 0.0f);
}

TEST_CASE("Braking is tire-limited (peak decel ~ mu*g, <= 1.3 g)", "[car][longitudinal]") {
    CarParams p;
    // At low speed (negligible aero drag) braking decel ≈ mu*g.
    float aBrake = longitudinal_accel(5.0f, /*throttle=*/0.0f, /*brake=*/1.0f, p);
    REQUIRE(aBrake < 0.0f);                             // decelerating
    float g = p.gravity;
    REQUIRE(std::fabs(aBrake) <= 1.3f * g);             // not physically impossible
    REQUIRE(std::fabs(aBrake) >= 0.9f * g);             // strong performance brakes
}

TEST_CASE("Coasting decelerates (drag + rolling resistance)", "[car][longitudinal]") {
    CarParams p;
    REQUIRE(longitudinal_accel(30.0f, 0.0f, 0.0f, p) < 0.0f);
}
