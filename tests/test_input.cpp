// Input seam tests: gamepad mapping, InputFrame -> CarEntity equivalence, FFB self-aligning torque.
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/ForceFeedback.h"
#include "utils/input/GamepadMapping.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace swish;
using Catch::Approx;

TEST_CASE("deadzone endpoints and monotonicity", "[input]") {
    CHECK(apply_deadzone(0.f, 0.1f) == 0.f);
    CHECK(apply_deadzone(0.1f, 0.1f) == 0.f);
    CHECK(apply_deadzone(-0.05f, 0.1f) == 0.f);
    CHECK(apply_deadzone(1.f, 0.1f) == Approx(1.f));
    CHECK(apply_deadzone(-1.f, 0.1f) == Approx(-1.f));
    CHECK(apply_deadzone(2.f, 0.1f) == Approx(1.f));  // clamped
    float prev = -2.f;
    for (int i = -100; i <= 100; ++i) {
        const float y = apply_deadzone(static_cast<float>(i) / 100.f, 0.15f);
        CHECK(y >= prev);
        prev = y;
    }
}

TEST_CASE("expo curve endpoints, oddness, monotonicity", "[input]") {
    for (float e : {0.f, 0.35f, 1.f}) {
        CHECK(apply_expo(0.f, e) == 0.f);
        CHECK(apply_expo(1.f, e) == Approx(1.f));
        CHECK(apply_expo(-1.f, e) == Approx(-1.f));
        CHECK(apply_expo(0.3f, e) == Approx(-apply_expo(-0.3f, e)));
        float prev = -2.f;
        for (int i = -100; i <= 100; ++i) {
            const float y = apply_expo(static_cast<float>(i) / 100.f, e);
            CHECK(y >= prev);
            prev = y;
        }
    }
    CHECK(apply_expo(0.5f, 0.f) == Approx(0.5f));
    CHECK(apply_expo(0.5f, 1.f) < 0.5f);  // softer near centre
}

TEST_CASE("map_gamepad_axes", "[input]") {
    const GamepadConfig cfg;
    SECTION("rest") {
        const InputFrame f = map_gamepad_axes(GamepadAxes{}, cfg);
        CHECK(f.steer == 0.f);
        CHECK(f.throttle == 0.f);
        CHECK(f.brake == 0.f);
        CHECK(f.steer_mode == SteerMode::Target);
    }
    SECTION("full deflection") {
        GamepadAxes ax;
        ax.left_x          = 1.f;
        ax.right_trigger   = 1.f;
        ax.left_trigger    = 1.f;
        const InputFrame f = map_gamepad_axes(ax, cfg);
        CHECK(f.steer == Approx(1.f));
        CHECK(f.throttle == Approx(1.f));
        CHECK(f.brake == Approx(1.f));
    }
    SECTION("steer sign and half trigger") {
        GamepadAxes ax;
        ax.left_x          = -0.6f;
        ax.right_trigger   = 0.f;  // signed mid-travel -> 0.5
        const InputFrame f = map_gamepad_axes(ax, cfg);
        CHECK(f.steer < 0.f);
        CHECK(f.throttle == Approx((0.5f - 0.05f) / 0.95f));
    }
    SECTION("unsigned triggers") {
        GamepadConfig c2;
        c2.triggers_signed = false;
        GamepadAxes ax;
        ax.right_trigger = 0.f;
        CHECK(map_gamepad_axes(ax, c2).throttle == 0.f);
        ax.right_trigger = 1.f;
        CHECK(map_gamepad_axes(ax, c2).throttle == Approx(1.f));
    }
}

namespace {
// Verbatim copy of the pre-InputFrame handle_input() logic, keys given as booleans.
struct Ref {
    float throttle = 0.f, brake = 0.f, angle = 0.f;
    bool  reverse = false;
    void  step(bool up, bool down, bool left, bool right, float speed, float dt) {
        constexpr float kSteerRate = 90.f, kSteerReturn = 120.f, kMaxSteer = 35.f, kDead = 0.5f;
        throttle = brake = 0.f;
        reverse          = false;
        if (up) {
            throttle = 1.f;
        } else if (down) {
            if (speed > kDead)
                brake = 1.f;
            else
                reverse = true;
        }
        if (left) {
            angle -= kSteerRate * dt;
        } else if (right) {
            angle += kSteerRate * dt;
        } else {
            float ret = kSteerReturn * dt;
            if (std::abs(angle) <= ret)
                angle = 0.f;
            else
                angle -= std::copysign(ret, angle);
        }
        angle = std::clamp(angle, -kMaxSteer, kMaxSteer);
    }
};
}  // namespace

TEST_CASE("apply_input_frame matches the keyboard path bit-for-bit", "[input]") {
    CarEntity   car;
    Ref         ref;
    const float dt = 1.f / 60.f;
    for (int i = 0; i < 900; ++i) {
        const bool up    = (i / 90) % 3 != 2;
        const bool down  = (i / 90) % 3 == 2 || i % 7 == 0;
        const bool left  = (i / 40) % 4 == 0;
        const bool right = (i / 40) % 4 == 2;
        InputFrame f;
        f.steer_mode = SteerMode::Rate;
        if (up)
            f.throttle = 1.f;
        else if (down)
            f.brake = 1.f;
        f.steer = left ? -1.f : (right ? 1.f : 0.f);
        ref.step(up, down, left, right, car.get_speed(), dt);
        car.apply_input_frame(f, dt);
        REQUIRE(car.get_throttle() == ref.throttle);
        REQUIRE(car.get_brake() == ref.brake);
        REQUIRE(car.get_reverse() == ref.reverse);
        REQUIRE(car.get_steering_angle() == ref.angle);
        car.update(dt);
    }
}

TEST_CASE("analog steer approaches target at the steer rate", "[input]") {
    CarEntity  car;
    InputFrame f;
    f.steer = 0.5f;  // 17.5 deg
    car.apply_input_frame(f, 0.1f);
    CHECK(car.get_steering_angle() == Approx(9.f));  // 90 deg/s * 0.1 s
    for (int i = 0; i < 20; ++i) car.apply_input_frame(f, 0.1f);
    CHECK(car.get_steering_angle() == Approx(0.5f * car.steer_max()));
    f.steer = 5.f;  // out of range is clamped
    for (int i = 0; i < 20; ++i) car.apply_input_frame(f, 0.1f);
    CHECK(car.get_steering_angle() == Approx(car.steer_max()));
}

TEST_CASE("analog frame throttle/brake/reverse", "[input]") {
    CarEntity  car;
    InputFrame f;
    f.throttle = 0.4f;
    car.apply_input_frame(f, 0.01f);
    CHECK(car.get_throttle() == Approx(0.4f));
    f.throttle = 0.f;
    f.brake    = 0.7f;
    car.apply_input_frame(f, 0.01f);  // stopped -> reverse intent
    CHECK(car.get_reverse());
    CHECK(car.get_brake() == 0.f);
}

TEST_CASE("force feedback self-aligning torque", "[input][ffb]") {
    const CarParams  p;
    const TireParams t;
    SECTION("zero at zero slip") {
        const FfbOutput o = self_aligning_torque(20.f, 0.f, 0.f, 0.f, p, t);
        CHECK(o.torque_nm == 0.f);
        CHECK(o.normalized == 0.f);
    }
    SECTION("restoring: opposes steer, antisymmetric") {
        const FfbOutput r = self_aligning_torque(20.f, 0.f, 0.f, 0.03f, p, t);
        const FfbOutput l = self_aligning_torque(20.f, 0.f, 0.f, -0.03f, p, t);
        CHECK(r.torque_nm < 0.f);
        CHECK(l.torque_nm > 0.f);
        CHECK(r.torque_nm == Approx(-l.torque_nm));
        CHECK(std::fabs(r.normalized) <= 1.f);
    }
    SECTION("goes light past saturation") {
        const float mid = std::fabs(self_aligning_torque(20.f, 0.f, 0.f, 0.03f, p, t).torque_nm);
        const float big = std::fabs(self_aligning_torque(20.f, 0.f, 0.f, 0.5f, p, t).torque_nm);
        CHECK(big < mid);
    }
    SECTION("null sink accepts submit") {
        NullForceFeedbackSink s;
        s.submit(0.5f, 0.01f);
    }
}
