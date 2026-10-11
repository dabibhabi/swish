#pragma once

// Purpose:    Pure gamepad-axis to InputFrame mapping: deadzone, expo response curve, trigger
//             handling. No GLFW, no GPU.
// Units:      All axes/outputs dimensionless; stick axes [-1,1], triggers per GamepadConfig.
// Complexity: O(1).
// Example:    InputFrame f = map_gamepad_axes({lx, 0.f, lt, rt}, GamepadConfig{});
// See also:   utils/input/InputFrame.h, core/Input/GlfwGamepadSource.h (the GLFW feeder).
// Test:       tests/test_input.cpp

#include "InputFrame.h"

#include <algorithm>
#include <cmath>

namespace swish {

// Raw axes as the device reports them (GLFW gamepad layout).
struct GamepadAxes {
    float left_x        = 0.f;   // steering stick, [-1,1], right positive
    float left_y        = 0.f;   // unused by the car today (kept for the later camera/menu use)
    float left_trigger  = -1.f;  // brake;    GLFW rest = -1, pressed = +1
    float right_trigger = -1.f;  // throttle; GLFW rest = -1, pressed = +1
};

struct GamepadConfig {
    float steer_deadzone   = 0.08f;  // [0,1) fraction of stick travel ignored around centre
    float steer_expo       = 0.35f;  // [0,1]: 0 = linear, 1 = fully cubic (fine control near centre)
    float trigger_deadzone = 0.05f;  // [0,1) fraction of trigger travel ignored at rest
    bool  triggers_signed  = true;   // true: raw [-1,1] (GLFW) is remapped to [0,1]; false: raw is [0,1]
};

// Radial-free (per-axis) scaled deadzone: 0 inside |x|<=dz, then rescaled so the output is
// continuous and still reaches +/-1 at |x|=1. Input is clamped to [-1,1].
inline float apply_deadzone(float x, float dz) {
    x             = std::clamp(x, -1.f, 1.f);
    dz            = std::clamp(dz, 0.f, 0.999f);
    const float a = std::fabs(x);
    if (a <= dz)
        return 0.f;
    return std::copysign((a - dz) / (1.f - dz), x);
}

// Odd expo blend (1-e)*x + e*x^3, e clamped to [0,1]. Monotone, f(0)=0, f(+/-1)=+/-1.
inline float apply_expo(float x, float expo) {
    x             = std::clamp(x, -1.f, 1.f);
    const float e = std::clamp(expo, 0.f, 1.f);
    return (1.f - e) * x + e * x * x * x;
}

// Raw trigger -> pedal fraction [0,1] (range remap, then one-sided deadzone).
inline float map_trigger(float raw, const GamepadConfig& cfg) {
    const float t = cfg.triggers_signed ? 0.5f * (raw + 1.f) : raw;
    return std::clamp(apply_deadzone(std::clamp(t, 0.f, 1.f), cfg.trigger_deadzone), 0.f, 1.f);
}

inline InputFrame map_gamepad_axes(const GamepadAxes& ax, const GamepadConfig& cfg = GamepadConfig{}) {
    InputFrame f;
    f.steer      = apply_expo(apply_deadzone(ax.left_x, cfg.steer_deadzone), cfg.steer_expo);
    f.throttle   = map_trigger(ax.right_trigger, cfg);
    f.brake      = map_trigger(ax.left_trigger, cfg);
    f.steer_mode = SteerMode::Target;
    return f;
}

}  // namespace swish
