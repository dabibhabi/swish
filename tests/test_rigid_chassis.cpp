#include "scene/Entity/RigidChassis.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace swish;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// GPU-free tests of the stub 6-DOF chassis (scene/Entity/RigidChassis.h): static sag, equilibrium at the
// design ride height, damped energy, friction-circle clamp. The bicycle golden is covered by test_golden_car.cpp.
namespace {
constexpr float kH = RigidChassis::kStepS;

// Symmetric test car: CoG at the centre of a 2.4 x 1.6 m rectangle of contacts, equal springs.
RigidChassisParams symmetric_params(float k, float c) {
    RigidChassisParams p;
    p.body.mass_kg = 1670.f;
    const float x = 1.2f, z = 0.8f, hubY = 0.34f - p.body.cog_height_m;  // hub below the CoG
    const vec3  hubs[4] = {vec3(x, hubY, -z), vec3(x, hubY, z), vec3(-x, hubY, -z), vec3(-x, hubY, z)};
    for (int i = 0; i < 4; ++i) {
        p.corner[i].hub_body_m = hubs[i];
        p.corner[i].radius_m   = 0.34f;
        p.corner[i].k_n_per_m  = k;
        p.corner[i].c_ns_per_m = c;
        p.corner[i].front      = i < 2;
    }
    p.c_alpha_front_n_per_rad = 48000.f;
    p.c_alpha_rear_n_per_rad  = 87000.f;
    return p;
}

void run(RigidChassis& ch, float seconds, const ChassisInput& in, float ground = 0.f) {
    const int n = static_cast<int>(std::lround(seconds / kH));
    for (int i = 0; i < n; ++i) ch.step(kH, in, ground);
}

float pitch_of(const RigidChassis& ch) { return util::physics::euler_yxz_from_rotation(ch.body().rotation()).x; }
float roll_of(const RigidChassis& ch) { return util::physics::euler_yxz_from_rotation(ch.body().rotation()).z; }
}  // namespace

TEST_CASE("chassis settles to static equilibrium: sag = m g / 4k per wheel", "[rigidchassis]") {
    constexpr float k = 40000.f, c = 3000.f;
    RigidChassis    ch(symmetric_params(k, c));
    ch.body().set_position(vec3(0.f, 0.45f, 0.f));  // wheel bottoms exactly touch the ground, preload 0

    run(ch, 10.f, ChassisInput{});

    const float sag = 1670.f * 9.81f / (4.f * k);  // 0.1024 m
    for (const auto& s : ch.corner_state()) {
        REQUIRE_THAT(s.pen_m, WithinRel(sag, 0.005f));
        REQUIRE_THAT(s.fn_n, WithinRel(1670.f * 9.81f / 4.f, 0.005f));
    }
    REQUIRE_THAT(ch.body().position().y, WithinAbs(0.45f - sag, 5e-4f));
    REQUIRE_THAT(pitch_of(ch), WithinAbs(0.f, 1e-4f));
    REQUIRE_THAT(roll_of(ch), WithinAbs(0.f, 1e-4f));
    REQUIRE_THAT(glm::length(ch.body().linear_velocity()), WithinAbs(0.f, 1e-3f));
}

TEST_CASE("design ride height from make_chassis_params is the equilibrium (no sink on switch-on)", "[rigidchassis]") {
    const std::array<vec3, 4>  hub{vec3(1.25f, 0.34f, -0.82f), vec3(1.25f, 0.34f, 0.82f), vec3(-1.20f, 0.36f, -0.84f),
                                   vec3(-1.20f, 0.36f, 0.84f)};
    const std::array<float, 4> radius{0.34f, 0.34f, 0.36f, 0.36f};
    const RigidChassisParams   p = make_chassis_params(hub, radius, CarParams{}, TireParams{}, SuspensionParams{});
    RigidChassis               ch(p);
    // Mesh origin on the ground => CoG at cog_mesh_m.
    ch.body().set_position(p.cog_mesh_m);

    run(ch, 5.f, ChassisInput{});

    REQUIRE_THAT(ch.body().position().y, WithinAbs(p.cog_mesh_m.y, 1e-3f));
    REQUIRE_THAT(glm::degrees(pitch_of(ch)), WithinAbs(0.f, 0.02f));
    REQUIRE_THAT(glm::degrees(roll_of(ch)), WithinAbs(0.f, 0.02f));
    float sumFn = 0.f;
    for (const auto& s : ch.corner_state()) sumFn += s.fn_n;
    REQUIRE_THAT(sumFn, WithinRel(1670.f * 9.81f, 0.005f));
}

TEST_CASE("damped mechanical energy never increases", "[rigidchassis]") {
    RigidChassis ch(symmetric_params(40000.f, 3000.f));
    // Dropped from 5 cm up, pitched and rolled so every mode (heave, pitch, roll) carries energy.
    ch.body().set_position(vec3(0.f, 0.50f, 0.f));
    ch.body().set_orientation(glm::angleAxis(glm::radians(3.f), vec3(0.f, 0.f, 1.f)) *
                              glm::angleAxis(glm::radians(2.f), vec3(1.f, 0.f, 0.f)));
    ChassisInput in;
    in.mu = 0.f;  // frictionless: the tire model must not inject or hide energy here

    // Sample every 0.25 s (symplectic Euler's shadow-energy ripple is O(h w) of the oscillation energy;
    // dissipation over a sample is far larger). Tolerance 0.5 J on ~10^3 J.
    float prev = ch.mechanical_energy(0.f);
    float e0   = prev;
    for (int s = 0; s < 40; ++s) {  // 10 s
        run(ch, 0.25f, in);
        const float e = ch.mechanical_energy(0.f);
        REQUIRE(e <= prev + 0.5f);
        prev = e;
    }
    REQUIRE(prev < e0);  // and it actually dissipated
}

TEST_CASE("tire force stays inside the friction circle", "[rigidchassis]") {
    RigidChassis ch(symmetric_params(40000.f, 3000.f));
    const float  sag = 1670.f * 9.81f / (4.f * 40000.f);
    ch.body().set_position(vec3(0.f, 0.45f - sag, 0.f));
    ch.body().set_linear_velocity(vec3(10.f, 0.f, 4.f));  // heavy sideslip

    ChassisInput in;
    in.fx_rear_n = 50000.f;  // absurd drive request
    ch.step(kH, in, 0.f);
    for (const auto& s : ch.corner_state()) {
        REQUIRE(s.fn_n > 0.f);
        REQUIRE(std::hypot(s.fx_n, s.fy_n) <= in.mu * s.fn_n * 1.0001f);
    }
    // Sliding right (+Z) is opposed by lateral force toward -Z on every wheel.
    for (const auto& s : ch.corner_state()) REQUIRE(s.fy_n < 0.f);
}

TEST_CASE("steering a moving chassis yaws it (lateral tire force acts at the patch)", "[rigidchassis]") {
    RigidChassis ch(symmetric_params(40000.f, 3000.f));
    const float  sag = 1670.f * 9.81f / (4.f * 40000.f);
    ch.body().set_position(vec3(0.f, 0.45f - sag, 0.f));
    ch.body().set_linear_velocity(vec3(15.f, 0.f, 0.f));
    ChassisInput in;
    in.steer_rad = glm::radians(5.f);  // right
    run(ch, 1.f, in);
    // Right-positive steer => right-positive yaw rate => negative world +Y angular velocity.
    REQUIRE(ch.body().angular_velocity().y < -0.05f);
}

// ── Wheel-spin model (plan B2-B5a) ────────────────────────────────────
namespace {
RigidChassis spin_car(float v0 = 0.f) {
    const std::array<vec3, 4>  hub{vec3(1.25f, 0.34f, -0.82f), vec3(1.25f, 0.34f, 0.82f), vec3(-1.20f, 0.36f, -0.84f),
                                   vec3(-1.20f, 0.36f, 0.84f)};
    const std::array<float, 4> radius{0.34f, 0.34f, 0.36f, 0.36f};
    RigidChassisParams         p = make_chassis_params(hub, radius, CarParams{}, TireParams{}, SuspensionParams{});
    p.spin_model                 = true;
    RigidChassis ch(p);
    ch.body().set_position(p.cog_mesh_m);
    ch.body().set_linear_velocity(vec3(v0, 0.f, 0.f));
    ch.set_rolling_speed(v0);
    return ch;
}
}  // namespace

TEST_CASE("tyre curve peaks at unit slip, then falls; force stays inside mu Fz", "[tire]") {
    const TireCurveParams p;
    CHECK_THAT(tire_curve(1.f, p.shape), WithinAbs(1.f, 1e-4f));
    CHECK(tire_curve(0.5f, p.shape) < 1.f);
    CHECK(tire_curve(3.f, p.shape) < tire_curve(1.f, p.shape));
    CHECK(tire_curve(3.f, p.shape) > 0.7f);  // sim-cade: a slide keeps most of the grip
    for (float k = -1.f; k <= 1.f; k += 0.1f)
        for (float a = -0.5f; a <= 0.5f; a += 0.05f) {
            const TireForce f = combined_tire_force(k, a, 4000.f, 4000.f, 1.05f, 0.12f, p);
            CHECK(std::hypot(f.fx, f.fy) <= 1.05f * 4000.f * 1.0001f);
        }
    // Pure slip at the peak uses the whole budget in that direction.
    CHECK_THAT(combined_tire_force(p.kappaPeak, 0.f, 4000.f, 4000.f, 1.05f, 0.12f, p).fx, WithinRel(4200.f, 1e-3f));
    // Load sensitivity: twice the load gives less than twice the force.
    CHECK(load_sensitive_mu(1.05f, 8000.f, 4000.f, p.loadSens) < 1.05f);
}

TEST_CASE("static corner weights follow the 911's rear-heavy split", "[rigidchassis][spin]") {
    RigidChassis ch = spin_car();
    run(ch, 3.f, ChassisInput{});
    const auto&      s     = ch.corner_state();
    const float      front = s[0].fn_n + s[1].fn_n, rear = s[2].fn_n + s[3].fn_n;
    const TireParams t;
    CHECK_THAT(front / (front + rear), WithinAbs(t.b / (t.a + t.b), 0.01f));  // 40 % front
}

TEST_CASE("ABS keeps the wheels turning and stops shorter than locked wheels", "[rigidchassis][spin]") {
    auto stop = [](bool abs, float& minOmegaFront) {
        RigidChassis ch = spin_car(30.f);
        ChassisInput in;
        in.abs = abs;
        for (int c = 0; c < 4; ++c)
            in.brake_torque_nm[c] = 0.5f * (c < 2 ? 0.62f : 0.38f) * 1.4f * 1670.f * 9.81f * 0.35f;
        minOmegaFront = 1e9f;
        for (int i = 0; i < 240; ++i) {  // 1 s
            ch.step(kH, in, 0.f);
            if (i > 60) minOmegaFront = std::min(minOmegaFront, ch.wheel_omega()[0]);
        }
        return ch.body().linear_velocity().x;
    };
    float       wAbs = 0.f, wLock = 0.f;
    const float vAbs  = stop(true, wAbs);
    const float vLock = stop(false, wLock);
    CHECK(wLock == 0.f);                 // no ABS: fronts lock
    CHECK(wAbs > 10.f);                  // ABS: still rolling
    CHECK(vAbs < vLock);                 // and it has shed more speed
    CHECK(30.f - vAbs > 0.85f * 9.81f);  // better than 0.85 g
}

TEST_CASE("traction control limits wheelspin on launch", "[rigidchassis][spin]") {
    auto launch = [](bool tc) {
        RigidChassis ch = spin_car(0.f);
        ChassisInput in;
        in.tc                 = tc;
        in.drive_torque_nm[2] = in.drive_torque_nm[3] = 4000.f;  // far beyond rear grip
        run(ch, 1.f, in);
        return std::pair<float, float>(ch.body().linear_velocity().x, ch.wheel_omega()[2] * 0.36f);
    };
    const auto [vTc, wheelTc]   = launch(true);
    const auto [vRaw, wheelRaw] = launch(false);
    CHECK(vTc > 5.f);                   // it does launch
    CHECK(wheelRaw > 2.f * vRaw);       // no TC: tyres spinning far faster than the car
    CHECK(wheelTc < 1.5f * vTc + 1.f);  // TC: near the grip peak
    CHECK(vTc > vRaw);
}

TEST_CASE("on a slope the body pitches to the road", "[rigidchassis][spin]") {
    RigidChassis ch = spin_car();
    ChassisInput in;
    in.brake_torque_nm.fill(3000.f);
    in.abs = false;
    GroundPlane g;
    g.dydx = 0.05f;  // 5 % climb toward +X (the nose)
    for (int i = 0; i < 720; ++i) ch.step(kH, in, g);
    // Rz convention: nose up is positive rotation about +Z (body right), read as pitch here.
    CHECK_THAT(std::fabs(glm::degrees(pitch_of(ch))) + std::fabs(glm::degrees(roll_of(ch))), WithinAbs(2.86f, 0.4f));
    CHECK(std::fabs(ch.body().linear_velocity().x) < 0.3f);  // brakes hold it
}

TEST_CASE("anti-roll bars cut body roll in a steady corner", "[rigidchassis][spin]") {
    auto roll = [](float bar) {
        RigidChassis       ch = spin_car(25.f);
        RigidChassisParams p  = ch.params();
        p.arb_front_n_per_m = p.arb_rear_n_per_m = bar;
        ch.set_params(p);
        ChassisInput in;
        in.steer_rad          = 0.03f;
        in.drive_torque_nm[2] = in.drive_torque_nm[3] = 150.f;
        run(ch, 2.f, in);
        return std::fabs(glm::degrees(pitch_of(ch)));  // nose is +X, so the X Euler angle is body roll
    };
    CHECK(roll(30000.f) < 0.8f * roll(0.f));
    CHECK(roll(0.f) > 0.3f);
}
