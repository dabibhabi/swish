#pragma once

// ── Noise.h ──────────────────────────────────────────────────────────
// Purpose   : deterministic, framerate-independent "quasi-noise" for camera shake: a weighted
//             sum of three incommensurate sines sampled at wall-clock time, so it does not pop or
//             alias differently at different framerates and is reproducible.
// Units     : t in SECONDS, f0/f1/f2 in Hz, phase in radians; result is unitless in [-1, 1]
//             (weights 0.55 + 0.30 + 0.15 = 1).
// Complexity: O(1), three sin() calls, no allocation.
// Precision : float; the expression order is the legacy CameraController::quasi_noise verbatim.
// Example   : namespace um = swish::util::math;
//             float shake = um::quasi_noise(t, 13.0f, 7.0f, 19.0f, 0.0f) * amplitude_wu;
// See also  : ../units/Units.h (kPi), core/CameraController/CameraController.cpp
// Test      : tests/utils/test_math_Noise.cpp

#include "utils/units/Units.h"

#include <cmath>

namespace swish::util::math {

// Not true Perlin/simplex noise, but three incommensurate frequencies stop it reading as one
// metronomic oscillation the way a single sine would; `phase` decorrelates one axis from another.
inline float quasi_noise(float t, float f0, float f1, float f2, float phase) {
    constexpr float kPi = units::kPi;
    return 0.55f * std::sin(2.0f * kPi * f0 * t + phase) + 0.30f * std::sin(2.0f * kPi * f1 * t + phase * 1.7f + 1.3f) +
           0.15f * std::sin(2.0f * kPi * f2 * t + phase * 2.3f + 2.6f);
}

}  // namespace swish::util::math
