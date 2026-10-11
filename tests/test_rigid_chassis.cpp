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
    for (int i = 0; i < n; ++i)
        ch.step(kH, in, ground);
}

float pitch_of(const RigidChassis& ch) {
    return util::physics::euler_yxz_from_rotation(ch.body().rotation()).x;
}
float roll_of(const RigidChassis& ch) {
    return util::physics::euler_yxz_from_rotation(ch.body().rotation()).z;
}
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
    for (const auto& s : ch.corner_state())
        sumFn += s.fn_n;
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
    for (const auto& s : ch.corner_state())
        REQUIRE(s.fy_n < 0.f);
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
