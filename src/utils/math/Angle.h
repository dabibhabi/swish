#pragma once

// ── Angle.h ──────────────────────────────────────────────────────────
// Purpose   : the one home of the angle-wrap idioms that were scattered over WheelKinematics.h, Smoothing.h and
//             CarEntity.cpp, each copied VERBATIM so every caller keeps its exact bits.
// Units     : wrap_2pi in RADIANS; every *_deg function in DEGREES.
// Complexity: O(1), one fmod per call, no allocation.
// Precision : float. wrap_2pi, shortest_delta_deg and wrap_heading_deg_legacy are bit-identical to the expressions
//             they replace. wrap_heading_deg_legacy keeps a known quirk: fmod keeps the dividend's sign, so its range
//             is (-540, 180), NOT ±180, and the car golden pins it. wrap_deg_180 is the correct (-180, 180].
// Example   : namespace um = swish::util::math;
//             float spin  = um::wrap_2pi(spin + omega * dt);          // [0, 2π)
//             float delta = um::shortest_delta_deg(yaw, target_yaw);  // shortest signed arc
// See also  : Smoothing.h, scene/Entity/WheelKinematics.h, docs/tutorials/typed-cpp/12-pure-step-functions.md
// Test      : tests/utils/test_math_Angle.cpp

#include "utils/units/Units.h"

#include <cmath>

namespace swish::util::math {

inline constexpr float kHalfTurnDeg = 180.0f;
inline constexpr float kFullTurnDeg = 360.0f;

// [0, 2π): WheelKinematics.h wrap_angle_2pi, verbatim.
inline float wrap_2pi(float rad) noexcept {
    rad = std::fmod(rad, units::kTwoPi);
    return rad < 0.0f ? rad + units::kTwoPi : rad;
}

// Shortest signed arc from `from` to `to`, in (-180, 180]: Smoothing.h exp_smooth_angle_deg, verbatim
// (shift into a positive fmod range, wrap, re-centre; exact while to - from >= -540).
inline float shortest_delta_deg(float from, float to) noexcept {
    return std::fmod(to - from + 540.0f, kFullTurnDeg) - kHalfTurnDeg;
}

// CarEntity.cpp heading wrap, verbatim. Range (-540, 180): kept because the golden records it.
inline float wrap_heading_deg_legacy(float deg) noexcept {
    return std::fmod(deg + kHalfTurnDeg, kFullTurnDeg) - kHalfTurnDeg;
}

// The correct wrap into (-180, 180].
inline float wrap_deg_180(float deg) noexcept {
    deg = std::fmod(deg, kFullTurnDeg);  // (-360, 360), sign of the dividend
    if (deg <= -kHalfTurnDeg) return deg + kFullTurnDeg;
    if (deg > kHalfTurnDeg) return deg - kFullTurnDeg;
    return deg;
}

}  // namespace swish::util::math
