#pragma once

// Keyboard steering: turns the binary arrow keys into a smoothed, speed-limited steer target.
// A held key ramps a virtual stick toward +-1 (release or reversal returns it toward 0 faster), and
// full stick commands at most `gripGain` times the steady-state grip-limited steer angle
//   delta_grip = atan(L * mu * g / v^2)            (lateral accel v^2 tan(delta) / L reaches mu g)
// capped by the steer lock. At parking speed that is the full lock; at highway speed it is a few
// degrees, so a short tap no longer saturates the tyres. Pure and GLFW-free: CarEntity reads the keys
// and feeds the result to apply_input_frame as a SteerMode::Target frame.
// Config: config/car.toml [keyboard_steer] (CarConfig). Test: tests/test_keyboard_steer.cpp.

#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

struct KeyboardSteerParams {
    float riseRate   = 2.5f;  // stick units/s while a key is held (0 -> full in 0.4 s)
    float returnRate = 6.0f;  // stick units/s back toward 0 on release or reversal
    float gripGain   = 1.5f;  // full stick = this multiple of the grip-limited steer angle
    float minDeg     = 1.0f;  // floor on the full-stick angle at very high speed or very low grip
};

inline constexpr KeyboardSteerParams kKeyboardSteerDefaults{};

// Arrow-key state for one frame. LEFT wins over RIGHT and UP over DOWN (the historical precedence).
struct KeyboardKeys {
    bool up    = false;
    bool down  = false;
    bool left  = false;
    bool right = false;
};

// Steer angle (deg) that a full key commands at forward speed v (m/s) with grip mu, wheelbase L (m) and
// gravity g (m/s^2): gripGain * delta_grip, clamped to [min(minDeg, lock), lock].
inline float keyboard_steer_limit_deg(float vMps, float mu, float wheelbaseM, float gravity, float lockDeg,
                                      const KeyboardSteerParams& p) {
    const float v2 = vMps * vMps;
    if (v2 < 1e-6f) return lockDeg;
    const float gripDeg = std::atan(wheelbaseM * std::max(mu, 0.f) * gravity / v2) * (180.f / util::units::kPi);
    return std::clamp(p.gripGain * gripDeg, std::min(p.minDeg, lockDeg), lockDeg);
}

// One step of the virtual stick. dir: -1 left, 0 none, +1 right. Returns the new stick in [-1, 1].
inline float keyboard_stick_step(float stick, int dir, float dt, const KeyboardSteerParams& p) {
    const float target  = static_cast<float>(std::clamp(dir, -1, 1));
    const bool  growing = dir != 0 && (stick == 0.f || (stick > 0.f) == (dir > 0));
    const float step    = (growing ? p.riseRate : p.returnRate) * std::max(dt, 0.f);
    const float diff    = target - stick;
    return std::abs(diff) <= step ? target : stick + std::copysign(step, diff);
}

inline int keyboard_steer_dir(const KeyboardKeys& k) { return k.left ? -1 : (k.right ? 1 : 0); }

}  // namespace swish
