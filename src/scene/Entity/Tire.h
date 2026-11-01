#pragma once

// Per-axle load transfer + friction circle (AxleLoad, AxleLateral, AxleLongitudinal).
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "Powertrain.h"

#include <algorithm>
#include <cmath>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// AWD traction: per-axle friction circle + slip-responsive PTU torque split
// archive/docs/phase-1-physics-speed-accuracy.md §4.1 (split) and §4.2 (circle).
//
// PHASE-2 NOTE: everything below is written as (load transfer) → (per-axle
// friction circle) → (deliverable force), with the LATERAL component of the
// circle already a first-class input (AxleLateral). Phase 2 (hydroplane
// spin-out) needs exactly this decomposition at negative v_x and does NOT
// have to re-derive the load transfer — extend axle_loads_long() with the
// lateral ΔFz term (phase-b-spec.md §9) and pass a per-axle lateral demand.
// ══════════════════════════════════════════════════════════════════════

// Vertical tire load per axle (N). Static split is m*g*(b/L) front,
// m*g*(a/L) rear; accelerating forward at a_x squats the rear, transferring
//   ΔF_z = m * a_x * h_cg / L
// from the front axle to the rear (negative a_x, i.e. braking, moves it
// forward). Clamped at 0 — an axle cannot pull the road down. NOTE the SUM is
// invariant: F_zf + F_zr = m*g for any a_x, which is what makes the locked-
// coupling ceiling below exactly mu*m*g with no iteration.
struct AxleLoad {
    float front = 0.f;
    float rear  = 0.f;
};

inline AxleLoad axle_loads_long(const CarParams& p, const TireParams& t, float ax) {
    const float L   = t.a + t.b;
    const float dFz = p.mass * ax * t.hCg / L;
    AxleLoad    load;
    load.front = std::max(p.mass * p.gravity * (t.b / L) - dFz, 0.0f);
    load.rear  = std::max(p.mass * p.gravity * (t.a / L) + dFz, 0.0f);
    return load;
}

// Lateral force already being spent at each axle (N, magnitude). Feeds the
// friction circle so cornering eats into longitudinal capacity (and vice
// versa). Zero = straight-line, which reproduces the pure-longitudinal limits.
struct AxleLateral {
    float front = 0.f;
    float rear  = 0.f;
};

// The friction circle itself: F_x² + F_y² ≤ (mu·F_z)² ⇒ the longitudinal
// budget left over once |F_y| is committed. Returns 0 rather than NaN when
// the lateral demand alone already saturates the tire.
inline float friction_circle_fx_max(float mu, float fz, float fy) {
    const float cap = mu * fz;
    const float r2  = cap * cap - fy * fy;
    return r2 > 0.0f ? std::sqrt(r2) : 0.0f;
}

// The SAME circle, read along the other axis: the LATERAL budget left once
// |F_x| is committed. Identical algebra (the circle is symmetric); named
// separately because Phase 2 closes the coupling in this direction and a call
// reading `friction_circle_fx_max(mu, fz, someLongitudinalForce)` would look
// like a units/argument bug (see axle_lateral_forces).
inline float friction_circle_fy_max(float mu, float fz, float fx) { return friction_circle_fx_max(mu, fz, fx); }

// Signed longitudinal tire force in use at each axle (N, positive = driving
// the car forward). The mirror image of AxleLateral, and the input Phase 2
// needs to close the friction circle in the longitudinal→lateral direction.
struct AxleLongitudinal {
    float front = 0.f;
    float rear  = 0.f;
};

}  // namespace swish
