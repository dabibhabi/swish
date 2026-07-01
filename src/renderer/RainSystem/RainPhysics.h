#pragma once

// Pure rain-microphysics functions, independent of Vulkan/GLM so they can be
// unit-tested on the CPU. The rain vertex shader (rain.vert) mirrors the same
// formulas in GLSL; this header is the reference and the basis for tests.

#include <algorithm>
#include <cmath>

namespace swish {

// Marshall–Palmer slope Λ (1/mm) for a rain rate R (mm/hr):
//   N(D) = N0 · exp(−Λ·D),   Λ = 4.1 · R^(−0.21)
// Heavier rain → smaller Λ → larger typical drops.
inline float marshall_palmer_lambda(float R) {
    return 4.1f * std::pow(std::max(R, 0.1f), -0.21f);
}

// Inverse-CDF sample of drop diameter D (mm) from a uniform u ∈ (0,1):
//   CDF(D) = 1 − exp(−Λ·D)  ⇒  D = −ln(u)/Λ.
// Clamped to a physical range so degenerate u values can't blow up.
inline float sample_drop_diameter(float u, float R) {
    u = std::clamp(u, 1.0e-3f, 0.999f);
    const float D = -std::log(u) / marshall_palmer_lambda(R);
    return std::clamp(D, 0.1f, 6.0f);
}

// Gunn–Kinzer terminal velocity (m/s) for a drop of diameter D (mm):
//   v_t(D) = 9.65 − 10.3 · exp(−0.6·D),  floored so sub-mm drops still fall.
inline float gunn_kinzer_speed(float D) {
    return std::max(9.65f - 10.3f * std::exp(-0.6f * D), 0.5f);
}

// Map the legacy 0..1 "intensity" knob to a physical rain rate (mm/hr). This is
// the single shared parameter that drives drop size, fall speed, and wetness.
inline float intensity_to_rain_rate(float intensity, float maxRate = 25.0f) {
    return std::clamp(intensity, 0.0f, 1.0f) * maxRate;
}

}  // namespace swish
