#pragma once

// ── Smoothing.h ──────────────────────────────────────────────────────
// Purpose   : framerate-independent exponential smoothing ("ease toward a target") so every
//             follower (camera, FOV kick, auto-exposure-style adaptation) shares one correct form.
// Units     : k is 1/seconds, dt is seconds; the *_angle_deg variant works in DEGREES.
// Complexity: O(1), one exp() per call, no allocation.
// Precision : float; exp_smooth_angle_deg wraps with fmod(delta + 540, 360) - 180.
// Example   : namespace um = swish::util::math;
//             fov = um::exp_smooth(fov, target_fov, 6.0f, dt);
//             yaw = um::exp_smooth_angle_deg(yaw, target_yaw, 8.0f, dt);
// See also  : ../units/Units.h, core/CameraController/CameraController.cpp (main user)
// Test      : tests/utils/test_math_Smoothing.cpp

#include <cmath>

namespace swish::util::math {

// ══════════════════════════════════════════════════════════════════════
// Framerate-independent exponential smoothing (tasks/todo.md F3).
//
// Eases a value toward a target at rate `k` (units: 1/second — bigger is
// snappier; the settle time-constant is 1/k seconds), independent of the
// caller's timestep `dt`. This is the shape auto-exposure adaptation
// (Renderer::updateAutoExposure) already uses inline; this header formalises
// it as one shared helper so every new "ease toward a target" (cockpit
// camera follow, FOV kick, future weather/wiper transitions) gets it for
// free instead of quietly reinventing a slightly-wrong version.
//
// Why NOT a raw per-frame lerp (`x += (target - x) * factor`)? A fixed
// `factor` implicitly bakes in whatever `dt` it was tuned at, so the same
// `factor` settles differently at different framerates: running the lerp
// every 1/240s takes 8x more (smaller) steps per wall-clock second than at
// 1/30s, and repeated small linear steps do NOT converge to the same total
// decay as fewer large ones — the sim would visibly ease faster or slower
// depending on frame time, i.e. exactly the "changes behaviour with
// framerate" bug this helper exists to prevent.
//
// The continuous-time system dx/dt = k*(target - x) has closed form
// x(t) = target - (target - x0)*e^{-k t}. Stepping it forward by exactly
// `dt` each call:
//
//     x += (x_target - x) * (1 - exp(-k * dt))
//
// reproduces that SAME continuous trajectory for any dt — fixed or
// variable, 30 Hz or 240 Hz — which is the property tests/utils/test_math_Smoothing.cpp
// checks directly (settle to the same value over the same wall-clock time
// at two very different dt's).
inline float exp_smooth(float x, float target, float k, float dt) {
    if (k <= 0.0f || dt <= 0.0f)
        return x;
    float alpha = 1.0f - std::exp(-k * dt);
    return x + (target - x) * alpha;
}

// Angle-aware variant (degrees): eases `x` toward `target` along the
// SHORTER arc, so a heading that wraps (e.g. +179° -> -179°, really just a
// 2° turn) doesn't make the follower spin the long way around through 0°.
// Used by the cockpit camera's heading-follow (App.cpp), which chases
// CarEntity's ±180°-wrapped rotation.y.
inline float exp_smooth_angle_deg(float x, float target, float k, float dt) {
    // Shortest signed delta in (-180, 180]: shift into a positive fmod range,
    // wrap, then re-center — the same wrap-to-range trick CarEntity.h uses
    // for heading itself (m_rotation.y = fmod(m_rotation.y + 180, 360) - 180),
    // just applied to the (target - x) delta instead of the angle itself.
    float delta = std::fmod(target - x + 540.0f, 360.0f) - 180.0f;
    return exp_smooth(x, x + delta, k, dt);
}

}  // namespace swish::util::math
