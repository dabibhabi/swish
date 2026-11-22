#pragma once

// Combined-slip tyre force: one magic-formula curve over the normalised slip magnitude
// (similarity method). Pure functions, SI units.

#include <algorithm>
#include <cmath>

namespace swish {

struct TireCurveParams {
    float shape     = 1.45f;  // C: force far past the peak falls to sin(C pi/2) of the peak (0.76 here)
    float kappaPeak = 0.10f;  // slip ratio at peak longitudinal force
    float loadSens  = 0.12f;  // mu lost per 100 % load above the static load
};

struct TireForce {
    float fx = 0.f;
    float fy = 0.f;
};

// B is chosen so the peak sits at rho = 1.
inline float tire_curve_b(float shape) { return std::tan(1.5707963f / shape); }
inline float tire_curve(float rho, float shape) { return std::sin(shape * std::atan(tire_curve_b(shape) * rho)); }

inline float load_sensitive_mu(float mu, float fz, float fz0, float loadSens) {
    return mu * std::clamp(1.f - loadSens * (fz / std::max(fz0, 1.f) - 1.f), 0.5f, 1.2f);
}

// kappa: slip ratio (+ = driving). tanAlpha: lateral slip, + pushes the tyre toward +fy.
inline TireForce combined_tire_force(float kappa, float tanAlpha, float fz, float fz0, float mu, float tanAlphaPeak,
                                     const TireCurveParams& p) {
    const float sx  = kappa / p.kappaPeak;
    const float sy  = tanAlpha / tanAlphaPeak;
    const float rho = std::hypot(sx, sy);
    if (rho < 1e-6f || fz <= 0.f) return {};
    const float f = load_sensitive_mu(mu, fz, fz0, p.loadSens) * fz * tire_curve(rho, p.shape) / rho;
    return {f * sx, f * sy};
}

}  // namespace swish
