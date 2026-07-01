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

// ── Lateral / tire model (P0 #4) ──────────────────────────────────────

// Integrate the dynamic bicycle to steady state for a fixed (vx, delta).
// Mirrors CarEntity::update's integration: dynamic bicycle + the friction-circle
// yaw cap + the sideslip bound that the game actually applies each step.
static void integrate_lateral(float vx, float delta, const CarParams& p, const TireParams& t,
                              float& vlOut, float& rOut, int steps = 4000, float dt = 1.0f / 240.0f) {
    float vl = 0.0f, r = 0.0f;
    for (int i = 0; i < steps; ++i) {
        BicycleDeriv d = dynamic_bicycle_deriv(vx, vl, r, delta, p, t);
        vl += d.vlDot * dt;
        r += d.rDot * dt;
        const float rMax = max_yaw_rate(vx, p);
        r  = std::clamp(r, -rMax, rMax);
        vl = std::clamp(vl, -std::fabs(vx), std::fabs(vx));
    }
    vlOut = vl;
    rOut = r;
}

TEST_CASE("Dynamic yaw matches kinematic sign and ~magnitude in the linear regime", "[car][tire]") {
    CarParams p;
    TireParams t;
    const float vx = 8.0f, delta = 0.03f;  // gentle steer, moderate speed
    float vl, r;
    integrate_lateral(vx, delta, p, t, vl, r);
    const float L     = t.a + t.b;
    const float r_kin = (vx / L) * std::tan(delta);  // right-positive
    REQUIRE(r > 0.0f);                    // right steer → right yaw (same sign as kinematic)
    REQUIRE(r < r_kin * 1.05f);           // understeer: at/below kinematic
    REQUIRE(r > r_kin * 0.55f);           // but same order of magnitude
}

TEST_CASE("Lateral acceleration saturates near mu*g (grip limit)", "[car][tire]") {
    CarParams p;
    TireParams t;
    // Aggressive steer at speed: kinematic would demand far more than the tires give.
    float vl, r;
    integrate_lateral(30.0f, 0.4f, p, t, vl, r);
    float ay = 30.0f * r;  // lateral acceleration (m/s^2)
    REQUIRE(std::fabs(ay) <= 1.15f * p.tireMu * p.gravity);  // capped at ~mu*g
    REQUIRE(std::fabs(ay) > 0.5f * p.tireMu * p.gravity);    // but genuinely cornering hard
}

TEST_CASE("Dynamic bicycle stays numerically stable at high speed", "[car][tire]") {
    CarParams p;
    TireParams t;
    float vl = 0.0f, r = 0.0f;
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 3000; ++i) {  // 25 s of a 0.1 rad step input at 40 m/s
        BicycleDeriv d = dynamic_bicycle_deriv(40.0f, vl, r, 0.1f, p, t);
        vl += d.vlDot * dt;
        r += d.rDot * dt;
        const float rMax = max_yaw_rate(40.0f, p);
        r  = std::clamp(r, -rMax, rMax);
        vl = std::clamp(vl, -40.0f, 40.0f);
    }
    REQUIRE(std::isfinite(vl));
    REQUIRE(std::isfinite(r));
    REQUIRE(std::fabs(r) < 2.0f);   // bounded, no divergence
    REQUIRE(std::fabs(vl) <= 40.0f);
}
