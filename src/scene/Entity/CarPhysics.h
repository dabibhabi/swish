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
