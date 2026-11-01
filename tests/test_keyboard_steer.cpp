// Keyboard steering (scene/Entity/KeyboardSteer.h + CarEntity::keyboard_input_frame): the arrow keys ramp a
// virtual stick, and full stick is capped near the grip-limited steer angle for the current speed and mu, so a
// short tap at highway speed no longer saturates the tyres. Also the config/car.toml [keyboard_steer] table.

#include "scene/CarConfig/CarConfig.h"
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/KeyboardSteer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

using namespace swish;
using Catch::Approx;

namespace {

constexpr float kDt = 1.f / 60.f;

// Steps the stick for `seconds` with a fixed direction; returns the final stick.
float hold(float stick, int dir, float seconds, const KeyboardSteerParams& p) {
    const int n = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < n; ++i) stick = keyboard_stick_step(stick, dir, kDt, p);
    return stick;
}

// Throttle straight ahead until the car reaches `v_mps` (or 20 s pass).
void drive_up_to(CarEntity& car, float v_mps) {
    InputFrame f;
    f.steer_mode = SteerMode::Target;
    f.throttle   = 1.f;
    for (int i = 0; i < static_cast<int>(20.f / kDt) && car.get_speed_mps() < v_mps; ++i) {
        car.apply_input_frame(f, kDt);
        car.update(kDt);
    }
}

struct TapResult {
    float max_abs_steer_deg = 0.f;
    float max_abs_yaw_rate  = 0.f;
};

// A 0.1 s right tap at full throttle, then 3 s of hands-off, through `frame_for(right_held)`.
template <class FrameFor>
TapResult tap_right(CarEntity& car, FrameFor&& frame_for) {
    TapResult r;
    const int tap_steps = static_cast<int>(std::lround(0.1f / kDt));
    const int total     = tap_steps + static_cast<int>(3.f / kDt);
    for (int i = 0; i < total; ++i) {
        const InputFrame f = frame_for(i < tap_steps);
        car.apply_input_frame(f, kDt);
        car.update(kDt);
        r.max_abs_steer_deg = std::max(r.max_abs_steer_deg, std::abs(car.get_steering_angle()));
        r.max_abs_yaw_rate  = std::max(r.max_abs_yaw_rate, std::abs(car.get_yaw_rate()));
    }
    return r;
}

}  // namespace

TEST_CASE("keyboard steer limit: full lock at rest, the grip angle at speed, smaller in the wet", "[input][keyboard]") {
    const KeyboardSteerParams p = kKeyboardSteerDefaults;
    const float               L = 2.259f, g = 9.81f, lock = 29.5f;

    CHECK(keyboard_steer_limit_deg(0.f, 1.05f, L, g, lock, p) == lock);
    CHECK(keyboard_steer_limit_deg(5.f, 1.05f, L, g, lock, p) == lock);  // gain x 42.9 deg, capped

    // 100 km/h dry: atan(2.259 * 1.05 * 9.81 / 27.78^2) = 1.72 deg, x 1.5 = 2.59 deg.
    const float hwy = keyboard_steer_limit_deg(100.f / 3.6f, 1.05f, L, g, lock, p);
    CHECK(hwy == Approx(2.59f).margin(0.02f));

    // Monotone in speed, smaller with less grip, and floored at minDeg.
    float prev = lock;
    for (float v = 1.f; v <= 80.f; v += 1.f) {
        const float d = keyboard_steer_limit_deg(v, 1.05f, L, g, lock, p);
        CHECK(d <= prev);
        prev = d;
    }
    CHECK(keyboard_steer_limit_deg(100.f / 3.6f, 0.5f, L, g, lock, p) < hwy);
    CHECK(keyboard_steer_limit_deg(80.f, 1.05f, L, g, lock, p) == Approx(p.minDeg));
    CHECK(keyboard_steer_limit_deg(30.f, 0.f, L, g, lock, p) == Approx(p.minDeg));
}

TEST_CASE("keyboard stick: a tap is a fraction, a hold reaches full, release and reversal return fast",
          "[input][keyboard]") {
    const KeyboardSteerParams p = kKeyboardSteerDefaults;

    CHECK(hold(0.f, +1, 0.1f, p) == Approx(0.25f).margin(1e-4f));  // 2.5 /s x 0.1 s
    CHECK(hold(0.f, -1, 0.1f, p) == Approx(-0.25f).margin(1e-4f));
    CHECK(hold(0.f, +1, 0.5f, p) == 1.f);            // full after 0.4 s, then clamped
    CHECK(hold(1.f, 0, 1.f / 6.f + kDt, p) == 0.f);  // 6 /s back to centre, no overshoot

    // Reversal: from full right, LEFT first returns at returnRate, then builds at riseRate.
    const float s1 = keyboard_stick_step(1.f, -1, kDt, p);
    CHECK(s1 == Approx(1.f - p.returnRate * kDt));
    const float s2 = keyboard_stick_step(-0.1f, -1, kDt, p);
    CHECK(s2 == Approx(-0.1f - p.riseRate * kDt));

    CHECK(keyboard_steer_dir(KeyboardKeys{false, false, true, true}) == -1);  // LEFT wins over RIGHT
    CHECK(keyboard_steer_dir(KeyboardKeys{false, false, false, true}) == 1);
    CHECK(keyboard_steer_dir(KeyboardKeys{}) == 0);
}

TEST_CASE("keyboard frame: full lock at standstill, throttle/brake precedence unchanged", "[input][keyboard]") {
    CarEntity    car;
    KeyboardKeys keys;
    keys.right = true;
    for (int i = 0; i < 60; ++i) car.apply_input_frame(car.keyboard_input_frame(keys, kDt), kDt);
    CHECK(car.get_steering_angle() == Approx(car.steer_max()));  // parked: full lock is still available

    keys               = KeyboardKeys{true, true, false, false};
    const InputFrame f = car.keyboard_input_frame(keys, kDt);
    CHECK(f.throttle == 1.f);
    CHECK(f.brake == 0.f);  // UP wins over DOWN
    CHECK(f.steer_mode == SteerMode::Target);
}

TEST_CASE("keyboard tap at 100 km/h steers and yaws far less than the old fixed-rate keys", "[input][keyboard]") {
    const float v_hwy = 100.f / 3.6f;

    CarEntity legacy;
    drive_up_to(legacy, v_hwy);
    REQUIRE(legacy.get_speed_mps() >= v_hwy);
    const TapResult old_tap = tap_right(legacy, [](bool right) {
        InputFrame f;
        f.steer_mode = SteerMode::Rate;  // the pre-change keyboard: 90 deg/s while held
        f.throttle   = 1.f;
        f.steer      = right ? 1.f : 0.f;
        return f;
    });

    CarEntity car;
    drive_up_to(car, v_hwy);
    REQUIRE(car.get_speed_mps() >= v_hwy);
    const CarProfile& prof  = car.get_profile();
    const float       limit = keyboard_steer_limit_deg(car.get_speed_mps(), car.get_effective_mu(), prof.car.wheelbase,
                                                       prof.car.gravity, prof.steerLockDeg, kKeyboardSteerDefaults);
    const TapResult   new_tap = tap_right(car, [&car](bool right) {
        KeyboardKeys k;
        k.up    = true;
        k.right = right;
        return car.keyboard_input_frame(k, kDt);
    });

    CHECK(old_tap.max_abs_steer_deg == Approx(9.f).margin(0.5f));  // 90 deg/s x 0.1 s
    CHECK(new_tap.max_abs_steer_deg <= 0.25f * limit + 0.05f);     // a quarter-stick of the grip limit
    CHECK(new_tap.max_abs_yaw_rate < 0.5f * old_tap.max_abs_yaw_rate);
    CHECK(std::isfinite(new_tap.max_abs_yaw_rate));
}

TEST_CASE("config/car.toml [keyboard_steer] matches the defaults; the reader falls back and clamps",
          "[input][keyboard][config]") {
    const KeyboardSteerParams f = CarConfig::load(std::string(CONFIG_DIR) + "car.toml").keyboardSteer;
    const KeyboardSteerParams d = kKeyboardSteerDefaults;
    CHECK(f.riseRate == d.riseRate);
    CHECK(f.returnRate == d.returnRate);
    CHECK(f.gripGain == d.gripGain);
    CHECK(f.minDeg == d.minDeg);

    CHECK(CarConfig::load("/nonexistent/car.toml").keyboardSteer.gripGain == d.gripGain);

    const KeyboardSteerParams c =
        CarConfig::parse("[keyboard_steer]\nrise_rate = 100.0\nreturn_rate = 0.0\ngrip_gain = 9.0\nmin_deg = -3.0\n")
            .keyboardSteer;
    CHECK(c.riseRate == 20.f);
    CHECK(c.returnRate == Approx(0.1f));
    CHECK(c.gripGain == 5.f);
    CHECK(c.minDeg == 0.f);
}
