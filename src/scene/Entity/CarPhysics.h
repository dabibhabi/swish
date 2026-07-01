#pragma once

// Pure vehicle-physics functions (SI units), independent of the renderer,
// GLFW, and GLM so they can be unit-tested without a GPU. CarEntity converts
// its world-unit state to/from SI at the call boundary.

#include <algorithm>
#include <cmath>

namespace swish {

// Longitudinal + tire parameters, anchored on a 911 Turbo S (AWD). SI units.
struct CarParams {
    float mass          = 1670.0f;    // kg
    float powerMax      = 478000.0f;  // W  (~641 hp)
    float drivetrainEff = 0.85f;      // fraction of engine power at the wheels
    float dragCoeff     = 0.39f;      // effective Cd at speed (rear wing deployed)
    float frontalArea   = 2.10f;      // m^2
    float airDensity    = 1.225f;     // kg/m^3
    float rollResist    = 0.013f;     // Crr
    float gravity       = 9.81f;      // m/s^2
    float tireMu        = 1.10f;      // dry-asphalt friction
    float wheelbase     = 2.45f;      // m
};

// Net longitudinal acceleration (m/s^2) at forward speed v (m/s), with throttle
// and brake in [0,1]. Drive force is power-limited (P*eta / v) and traction-
// capped (mu*m*g, AWD); the only resistances are quadratic aero drag and
// rolling resistance. Top speed therefore EMERGES from the force balance rather
// than a hard clamp.
inline float longitudinal_accel(float v, float throttle, float brake, const CarParams& p) {
    const float vAbs = std::fabs(v);
    const float dir  = (v >= 0.0f) ? 1.0f : -1.0f;

    // Power-limited thrust capped by tire traction. Floor v to avoid the 1/v
    // singularity near standstill (there the traction cap dominates anyway).
    const float vEff        = std::max(vAbs, 3.0f);
    const float tractionCap = p.tireMu * p.mass * p.gravity;  // AWD: all four wheels
    const float fDrive      = throttle * std::min(p.powerMax * p.drivetrainEff / vEff, tractionCap);

    // Braking is tire-limited and opposes current motion.
    const float fBrake = brake * p.tireMu * p.mass * p.gravity;

    // Resistances oppose motion: quadratic aero + (near-)constant rolling.
    const float fDrag = 0.5f * p.airDensity * p.dragCoeff * p.frontalArea * vAbs * vAbs;
    const float fRoll = (vAbs > 0.05f) ? p.rollResist * p.mass * p.gravity : 0.0f;

    const float fNet = fDrive - dir * (fBrake + fDrag + fRoll);
    return fNet / p.mass;
}

// ── Lateral / yaw dynamics (dynamic bicycle model, P0 #4) ─────────────────
// Parameters for a rear-biased-CG 911 (rear engine ⇒ CG ~61% toward the rear
// axle, so the front lever a > rear lever b). Equal cornering stiffness with
// a>b yields a positive understeer gradient → stable (won't spin blindly).
struct TireParams {
    float a           = 1.49f;      // CG → front axle (m); a + b ≈ wheelbase
    float b           = 0.96f;      // CG → rear axle (m)
    float iz          = 2600.0f;    // yaw moment of inertia (kg·m²)
    // Understeer (stable at ALL speeds, no critical speed) needs
    // Cα_rear/Cα_front > W_rear/W_front = a/b ≈ 1.55. A stiffer rear delivers that.
    float cAlphaFront = 80000.0f;   // N/rad
    float cAlphaRear  = 150000.0f;  // N/rad
};

struct BicycleDeriv {
    float vlDot;  // d(lateral velocity)/dt  (m/s²)
    float rDot;   // d(yaw rate)/dt          (rad/s²)
};

// Friction-circle limit on yaw rate: lateral accel a_y = vx·r ≤ μ·g, so
// |r| ≤ μg/vx. Applied after integration, this caps cornering at the grip limit
// (understeer plow at the edge) and — since v̇l = a_y − vx·r → 0 when saturated —
// also stops sideslip from growing unbounded under a sustained over-drive input.
inline float max_yaw_rate(float vx, const CarParams& p) {
    return p.tireMu * p.gravity / std::max(std::fabs(vx), 1.0f);
}

// One step of the dynamic bicycle model, in a RIGHT-positive body frame that
// matches the kinematic yaw used elsewhere (heading -= deg(r)·dt):
//   vx    forward speed (m/s),  vl lateral (rightward) velocity (m/s)
//   r     yaw rate (right-positive, rad/s),  delta road-wheel steer (right-pos, rad)
// Front/rear lateral tire forces are linear in slip angle but SATURATE at μ·Fz,
// so the car can understeer / slide and lateral accel is capped at ~μg — the
// grip limit the pure kinematic model lacked. Caller blends to kinematic below
// ~5 m/s to avoid the 1/vx singularity.
inline BicycleDeriv dynamic_bicycle_deriv(float vx, float vl, float r, float delta,
                                          const CarParams& p, const TireParams& t) {
    const float vxs = std::max(std::fabs(vx), 1.0f);
    const float L   = t.a + t.b;

    // Slip angles (right-positive): velocity heading at each axle minus steer.
    const float alphaF = (vl + t.a * r) / vxs - delta;
    const float alphaR = (vl - t.b * r) / vxs;

    // Static axle loads → per-axle friction limits.
    const float FzF = p.mass * p.gravity * t.b / L;
    const float FzR = p.mass * p.gravity * t.a / L;

    // Saturating linear tire forces (rightward-positive, opposing slip).
    const float FyF = std::clamp(-t.cAlphaFront * alphaF, -p.tireMu * FzF, p.tireMu * FzF);
    const float FyR = std::clamp(-t.cAlphaRear * alphaR, -p.tireMu * FzR, p.tireMu * FzR);

    // m(v̇l + vx·r) = FyF + FyR ;   Iz·ṙ = a·FyF − b·FyR
    const float vlDot = (FyF + FyR) / p.mass - vx * r;
    const float rDot  = (t.a * FyF - t.b * FyR) / t.iz;
    return {vlDot, rDot};
}

// Emergent steady-state top speed (m/s): the v where full-throttle net
// acceleration crosses zero. Bisection (monotone: thrust falls, drag rises).
inline float steady_state_top_speed(const CarParams& p) {
    float lo = 0.0f, hi = 150.0f;
    for (int i = 0; i < 64; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (longitudinal_accel(mid, 1.0f, 0.0f, p) > 0.0f)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

}  // namespace swish
