// Wheel articulation (S1a, tasks/todo.md "Rigged Porsche"): per-wheel kinematic spin, the
// wheel_omega hook, draw-only Ackermann, and the config/car.toml loader. Pure math from
// WheelKinematics.h plus a headless CarEntity on synthetic 993-sized hub frames (no GPU).
#include "scene/CarConfig/CarConfig.h"
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/CarProfile.h"
#include "scene/Entity/WheelKinematics.h"
#include "utils/input/InputFrame.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using namespace swish;
using Catch::Matchers::WithinAbs;

namespace {
// The rigged 993's measured geometry (CarRig.h::measure_car_rig_geometry on porsche_rigged.glb).
constexpr float kL = 2.259f;  // wheelbase, m
constexpr float kT = 1.404f;  // front track, m
constexpr float kR = 0.320f;  // tyre radius, m

float deg(float d) {
    return glm::radians(d);
}
float cot(float a) {
    return 1.f / std::tan(a);
}
}  // namespace

// ── Ackermann (draw-only) ─────────────────────────────────────────────

TEST_CASE("Ackermann: zero steer gives zero wheel angle at any blend", "[wheel][ackermann]") {
    for (float k : {0.f, 0.5f, 1.f}) {
        REQUIRE(ackermann_wheel_angle(0.f, 0.5f * kT, kL, k) == 0.f);
        REQUIRE(ackermann_wheel_angle(0.f, -0.5f * kT, kL, k) == 0.f);
    }
}

TEST_CASE("Ackermann: left/right mirror symmetry", "[wheel][ackermann]") {
    for (float d : {5.f, 15.f, 29.5f}) {
        const float fr_right = ackermann_wheel_angle(deg(d), 0.5f * kT, kL, 1.f);    // FR, right turn (inner)
        const float fl_left  = ackermann_wheel_angle(deg(-d), -0.5f * kT, kL, 1.f);  // FL, left turn (inner)
        REQUIRE_THAT(fl_left, WithinAbs(-fr_right, 1e-6f));
        const float fl_right = ackermann_wheel_angle(deg(d), -0.5f * kT, kL, 1.f);  // FL, right turn (outer)
        const float fr_left  = ackermann_wheel_angle(deg(-d), 0.5f * kT, kL, 1.f);  // FR, left turn (outer)
        REQUIRE_THAT(fr_left, WithinAbs(-fl_right, 1e-6f));
    }
}

TEST_CASE("Ackermann 100%: cot(outer) - cot(inner) = track / wheelbase", "[wheel][ackermann]") {
    for (float d : {2.f, 10.f, 20.f, 29.5f}) {
        const float inner = ackermann_wheel_angle(deg(d), 0.5f * kT, kL, 1.f);   // right turn: FR is inner
        const float outer = ackermann_wheel_angle(deg(d), -0.5f * kT, kL, 1.f);  // FL is outer
        REQUIRE(inner > deg(d));
        REQUIRE(outer < deg(d));
        REQUIRE_THAT(cot(outer) - cot(inner), WithinAbs(kT / kL, 1e-4f));
        // Centreline convention: the bicycle angle is the cotangent mean of the two.
        REQUIRE_THAT(0.5f * (cot(outer) + cot(inner)), WithinAbs(cot(deg(d)), 1e-4f));
    }
    // 993 at full lock: 34.47 deg inner, 25.69 deg outer (spec arithmetic).
    REQUIRE_THAT(glm::degrees(ackermann_wheel_angle(deg(29.5f), 0.5f * kT, kL, 1.f)), WithinAbs(34.47f, 0.02f));
    REQUIRE_THAT(glm::degrees(ackermann_wheel_angle(deg(29.5f), -0.5f * kT, kL, 1.f)), WithinAbs(25.69f, 0.02f));
}

TEST_CASE("Ackermann 0% is parallel steer; the blend is linear and clamped", "[wheel][ackermann]") {
    const float d = deg(20.f);
    REQUIRE(ackermann_wheel_angle(d, 0.5f * kT, kL, 0.f) == d);
    REQUIRE(ackermann_wheel_angle(d, -0.5f * kT, kL, 0.f) == d);
    const float full = ackermann_wheel_angle(d, 0.5f * kT, kL, 1.f);
    REQUIRE_THAT(ackermann_wheel_angle(d, 0.5f * kT, kL, 0.5f), WithinAbs(0.5f * (d + full), 1e-6f));
    REQUIRE(ackermann_wheel_angle(d, 0.5f * kT, kL, 2.f) == full);
    REQUIRE(ackermann_wheel_angle(d, 0.5f * kT, kL, -1.f) == d);
}

// ── Per-wheel kinematic rolling speed ────────────────────────────────

TEST_CASE("Rolling speed: straight line is v at every hub", "[wheel][kinematic]") {
    for (float x : {1.3f, -0.9f})
        for (float z : {0.7f, -0.7f})
            REQUIRE_THAT(wheel_rolling_speed(20.f, 0.f, 0.f, x, z, 0.f), WithinAbs(20.f, 1e-6f));
}

TEST_CASE("Rolling speed: outer wheels roll faster in a turn", "[wheel][kinematic]") {
    const float v = 10.f, r = 0.5f;                                      // right turn (yaw rate right-positive)
    const float rl = wheel_rolling_speed(v, 0.f, r, -0.9f, -0.7f, 0.f);  // left = outer
    const float rr = wheel_rolling_speed(v, 0.f, r, -0.9f, 0.7f, 0.f);   // right = inner
    REQUIRE_THAT(rl, WithinAbs(v + r * 0.7f, 1e-5f));
    REQUIRE_THAT(rr, WithinAbs(v - r * 0.7f, 1e-5f));
    REQUIRE_THAT(rl - rr, WithinAbs(r * 1.4f, 1e-5f));  // Δv = ψ̇·t
}

TEST_CASE("Rolling speed: a steered front rolls along its own heading", "[wheel][kinematic]") {
    // Hub moving at (vx, vl) = (8, 0) plus ψ̇×p; heading δ: v·ĥ = along·cos δ + side·sin δ.
    const float x = 1.3f, z = 0.7f, r = 0.4f, d = deg(20.f);
    const float along = 8.f - r * z, side = r * x;
    REQUIRE_THAT(wheel_rolling_speed(8.f, 0.f, r, x, z, d), WithinAbs(along * std::cos(d) + side * std::sin(d), 1e-5f));
    // Kinematic bicycle about the front hub: the hub velocity points along the wheel, so the
    // rolling speed equals the hub's full speed.
    const float rk  = 8.f * std::tan(d) / kL;  // rear axle at x = 0, front at x = L, z = 0
    const float vrk = wheel_rolling_speed(8.f, 0.f, rk, kL, 0.f, d);
    REQUIRE_THAT(vrk, WithinAbs(8.f / std::cos(d), 1e-4f));
}

TEST_CASE("wheel_spin_advance matches wheel_spin_step at omega = v / r", "[wheel][kinematic]") {
    const float dt = 1.f / 60.f;
    REQUIRE_THAT(wheel_spin_advance(0.3f, 12.f / kR, dt), WithinAbs(wheel_spin_step(0.3f, 12.f, kR, dt), 1e-6f));
    const float back = wheel_spin_advance(0.01f, -30.f, dt);
    REQUIRE(back >= 0.f);
    REQUIRE(back < kTwoPi);
}

// ── CarEntity: hook, draw angles, steering-wheel ratio ───────────────

namespace {
// A 993-sized car with axis-aligned synthetic hub frames (car space: nose +X, right +Z).
CarEntity make_993() {
    std::array<WheelFrame, 4> wf{};
    const float               xF = 0.5f * kL, xR = -0.5f * kL, z = 0.5f * kT;
    const Vec3                hub[4] = {{xF, kR, -z}, {xF, kR, z}, {xR, kR, -z}, {xR, kR, z}};
    for (int c = 0; c < 4; c++) {
        wf[c].frame  = glm::translate(Mat4(1.f), hub[c]);
        wf[c].radius = kR;
    }
    CarEntity car;
    car.set_wheel_frames(wf);
    const CarRigGeometry g = measure_car_rig_geometry(wf, true);
    car.set_rig_geometry(g);
    car.apply_profile(car_profiles::rs_993(&g));
    return car;
}
}  // namespace

TEST_CASE("CarEntity: front wheels drawn at Ackermann angles about the bicycle angle", "[wheel][ackermann][car]") {
    CarEntity car = make_993();
    car.set_steering_angle(29.5f);  // full right lock
    const float fl = car.get_wheel_steer_rad(0), fr = car.get_wheel_steer_rad(1);
    REQUIRE(fr > deg(29.5f));  // inner (right) turns more
    REQUIRE(fl < deg(29.5f));
    REQUIRE_THAT(cot(fl) - cot(fr), WithinAbs(kT / kL, 1e-4f));
    REQUIRE(car.get_wheel_steer_rad(2) == 0.f);
    REQUIRE(car.get_wheel_steer_rad(3) == 0.f);

    car.set_ackermann(0.f);
    REQUIRE_THAT(car.get_wheel_steer_rad(0), WithinAbs(deg(29.5f), 1e-6f));
    REQUIRE_THAT(car.get_wheel_steer_rad(1), WithinAbs(deg(29.5f), 1e-6f));

    // Steering wheel: 993 ratio 16.5 over ±29.5° = 2.70 turns lock to lock.
    REQUIRE_THAT(2.f * car.steer_max() * car.steer_ratio() / 360.f, WithinAbs(2.70f, 0.01f));
}

TEST_CASE("CarEntity: wheel_omega hook is per wheel, outer faster in a turn", "[wheel][kinematic][car]") {
    CarEntity  car = make_993();
    InputFrame f;
    f.steer_mode   = SteerMode::Target;
    f.throttle     = 1.f;
    const float dt = 1.f / 60.f;
    for (int i = 0; i < 180; i++) {  // 3 s full throttle, straight
        car.apply_input_frame(f, dt);
        car.update(dt);
    }
    const auto& w0 = car.get_wheel_omega();
    const float v  = car.get_speed_mps();
    REQUIRE(v > 5.f);
    for (int c = 0; c < 4; c++)
        REQUIRE_THAT(w0[static_cast<size_t>(c)], WithinAbs(v / kR, 1e-3f * v / kR));

    f.throttle = 0.3f;
    f.steer    = 0.5f;  // right
    for (int i = 0; i < 120; i++) {
        car.apply_input_frame(f, dt);
        car.update(dt);
    }
    REQUIRE(car.get_yaw_rate() > 0.f);
    const auto& w = car.get_wheel_omega();
    REQUIRE(w[2] > w[3]);  // rear left (outer) faster than rear right
    REQUIRE(w[0] > w[1]);  // front left (outer) faster than front right
}

// ── config/car.toml ──────────────────────────────────────────────────

TEST_CASE("config/car.toml loads and matches the struct defaults", "[car][config]") {
    const CarConfig cfg = CarConfig::load(CONFIG_DIR "car.toml");
    REQUIRE(cfg.wheels.ackermann == 1.0f);
    REQUIRE(cfg.wheels.ackermann == CarWheelConfig{}.ackermann);
    // A missing file falls back to the defaults instead of throwing.
    REQUIRE(CarConfig::load(CONFIG_DIR "does_not_exist.toml").wheels.ackermann == CarWheelConfig{}.ackermann);
}
