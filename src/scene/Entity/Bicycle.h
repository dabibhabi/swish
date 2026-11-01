#pragma once

// Dynamic bicycle model: yaw/sideslip limits, axle lateral forces, top speed.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "Longitudinal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace swish {

struct BicycleDeriv {
    float vlDot;  // d(lateral velocity)/dt  (m/s²)
    float rDot;   // d(yaw rate)/dt          (rad/s²)
};

// QUASI-STEADY friction-circle limit on yaw rate: in a steady corner
// v̇l = 0, so lateral accel a_y = vx·r ≤ μ·g and therefore |r| ≤ μg/|vx|.
// Applied after integration, this caps cornering at the grip limit (understeer
// plow at the edge) and — since v̇l = a_y − vx·r → 0 when saturated — also stops
// sideslip from growing unbounded under a sustained over-drive input.
//
// |vx| (not vx): the bound is symmetric, so it is already correct at negative
// vx. Phase 2 re-derived this (archive/docs/phase-2-hydroplane-spinout.md §5 claimed the
// floor was `max(vx, 1)`; it was already `max(|vx|, 1)` before Phase 2 and needs
// no sign-convention fix — a symmetric clamp on |r| carries no sign at all).
//
// LIMIT OF VALIDITY, and why Phase 2 relaxes it: v̇l = 0 is exactly what stops
// being true in a spin. The general statement is a_y = v̇l + vx·r ≤ μg, which the
// tire model already enforces structurally (both axle forces are clamped at
// μ·Fz), so under a diverging sideslip this clamp is not physics — it is a
// numerical rail that happens to suppress the divergence. See
// yaw_rate_limit()/sideslip_limit() below.
inline float max_yaw_rate(float vx, const CarParams& p) { return p.tireMu * p.gravity / std::max(std::fabs(vx), 1.0f); }

// ── Phase 2: hydroplane-gated release of the quasi-steady clamps ──────────
// archive/docs/phase-2-hydroplane-spinout.md, as-built §5.
//
// Kinematic pivot rail: a rigid body translating at speed V cannot exceed
// r = V/ℓ while pivoting about a contact patch a distance ℓ from its CG. With
// ℓ = L/2 (half the wheelbase) this is ~28 rad/s at highway speed — three
// orders of magnitude above any observed spin, i.e. a pure NaN/blow-up guard
// rather than a behavioural limit, which is the whole point: it replaces the
// quasi-steady bound where that bound is invalid without substituting a new
// invented number.
inline float yaw_pivot_rail(float speed, const TireParams& t) {
    return std::max(std::fabs(speed), 1.0f) / (0.5f * (t.a + t.b));
}

// The yaw-rate clamp actually applied: the quasi-steady bound, blended toward
// the pivot rail by `planeFrac` (0 = fully enforced, exactly max_yaw_rate();
// 1 = released). planeFrac comes from hydroplane_fraction() below, which is
// zero on dry roads, on merely-wet roads, and on standing water below the
// hydroplane speed — so this is bit-identical to max_yaw_rate() everywhere
// except when the tire is genuinely planing.
inline float yaw_rate_limit(float vx, float speed, const CarParams& p, const TireParams& t, float planeFrac) {
    const float qs = max_yaw_rate(vx, p);
    if (planeFrac <= 0.0f) return qs;
    return qs + std::clamp(planeFrac, 0.0f, 1.0f) * (yaw_pivot_rail(speed, t) - qs);
}

// The companion sideslip bound. |vl| ≤ |vx| is a 45° sideslip cap — a real spin
// passes straight through it — so it is released the same way. The rail is the
// body-speed magnitude, which |vl| satisfies identically (|vl| ≤ √(vx²+vl²)),
// i.e. at planeFrac = 1 the bound is inert by construction. That is safe
// because the body-frame transport term (body_transport_accel) makes the model
// conserve |V| under pure rotation, which is what the 45° cap was standing in
// for; see the as-built note in archive/docs/phase-2-hydroplane-spinout.md.
inline float sideslip_limit(float vx, float speed, float planeFrac) {
    const float qs = std::fabs(vx);
    if (planeFrac <= 0.0f) return qs;
    return qs + std::clamp(planeFrac, 0.0f, 1.0f) * (std::max(std::fabs(speed), 1.0f) - qs);
}

// Largest yaw DECELERATION (rad/s²) the tires can produce, i.e. the fully
// asymmetric saturated yaw moment over the yaw inertia:
//   |M|_max = a·μF_zf + b·μF_zr = 2·μ·m·g·a·b/L   ⇒   |ṙ|_max = |M|_max / I_z
// Used as a rate limit on how fast the yaw CLAMP may tighten — see the note in
// CarEntity::update. Not a new model: it is the same bound the negative-vx
// test asserts dynamic_bicycle_deriv already respects.
inline float max_yaw_decel(const CarParams& p, const TireParams& t) {
    const float L = t.a + t.b;
    return 2.0f * p.tireMu * p.mass * p.gravity * t.a * t.b / (L * t.iz);
}

// Rate-limited form of a state bound. A clamp is a CONSTRAINT, not a brake: it
// may stop a state from growing, but it must never remove more of it than the
// physical mechanism (tire friction) could have removed in one step. Without
// this, the moment a hydroplane ends the sideslip bound snaps from "released"
// back to |v_l| <= |v_x| and teleports the car's lateral momentum away — a live
// run measured |V| dropping 31.8 -> 12.5 m/s in a single 0.5 s sample (~4 g of
// deceleration that no tire produced).
//
// `bound` is the constraint we want; `prevMag` the magnitude the state had
// before this step; `maxRate` the largest physically achievable rate of change.
// Returns the bound actually safe to apply. A no-op whenever the constraint was
// not already violated, which is every non-planing case.
inline float rate_limited_bound(float bound, float prevMag, float maxRate, float dt) {
    return std::max(bound, std::fabs(prevMag) - std::fabs(maxRate) * dt);
}

// Body-frame transport (centrifugal) term of the longitudinal equation:
//   m(v̇x − vl·r) = F_x   ⇒   v̇x = F_x/m + vl·r
// This term was MISSING. Its absence is invisible while sideslip is small, but
// it is what makes the (vx, vl) velocity vector rotate rigidly with the body
// instead of the body's nose dragging the velocity around with it: without it
//   d(|V|²/2)/dt = (vx·F_x + vl·F_y)/m − vx·vl·r
// i.e. rotation spuriously creates or destroys kinetic energy in proportion to
// yaw rate. With it, the r-terms cancel and rotation alone does no work — so a
// spinning car keeps its momentum, its forward speed genuinely passes through
// zero and goes negative as it comes round, and the slide bleeds speed through
// tire scrub rather than through a bookkeeping error.
//
// Gated by planeFrac for the same reason as the clamps: Phase 2's scope is the
// hydroplane case (spec decision 2), and gating keeps dry/merely-wet handling
// bit-identical to the shipped, tuned car. It is universally correct physics and
// is the first thing to un-gate when the deferred dry-road spin lands.
inline float body_transport_accel(float vl, float r, float planeFrac) {
    return std::clamp(planeFrac, 0.0f, 1.0f) * vl * r;
}

// Signed (right-positive) lateral tire force at each axle, N. Split out of
// dynamic_bicycle_deriv (identical math, same order of operations) so the
// LONGITUDINAL side can see how much of each axle's friction circle the
// cornering is already spending — see longitudinal_accel's `lat` argument.
struct AxleLateralForces {
    float front = 0.f;
    float rear  = 0.f;
};

// Loads default to STATIC (no longitudinal transfer), which is Phase 1's
// behaviour exactly. Phase 2 takes the transfer + the longitudinal half of the
// friction circle, but only in proportion to `planeFrac`, so every non-planing
// call — including every existing caller and test, which pass neither optional
// argument — is bit-identical to Phase 1.
//
// WHY the circle closure is here at all (this is the Phase-2 finding, and it
// contradicts archive/docs/phase-2-hydroplane-spinout.md §2/§3.1): relaxing the yaw
// clamp does NOT make the saturated bicycle model diverge. When both axles
// saturate at the same sign the yaw moment is
//     a·μF_zf − b·μF_zr = μ(a·mgb/L − b·mga/L) = 0
// IDENTICALLY, because static load ∝ the opposite lever. The saturated model is
// therefore yaw-NEUTRAL, not unstable: it holds whatever yaw rate it had and
// plows. Measured: releasing the clamp alone moved peak yaw rate from 4.0 to
// 2.4 deg/s. With longitudinal load transfer folded in, the same moment becomes
//     a·μF_zf − b·μF_zr = −μ·m·a_x·h_cg
// which is non-zero whenever the car is accelerating or braking, and THAT is
// the oversteer moment a spin grows from. It is also the exact extension Phase
// 1 left a note for (see the pre-Phase-2 comment this replaces).
//
// SIGN AT NEGATIVE vx (spec §5.2). The slip angle is atan(v_lat/|v_long|) in the
// WHEEL frame; expanding for small δ with v_lat = −vx·sinδ + (vl + a·r)·cosδ
// gives α_f ≈ (vl + a·r)/|vx| − sgn(vx)·δ. Dividing by |vx| is already correct
// (friction opposes the slip velocity, whichever way the tire rolls), but the
// steer term needs sgn(vx) — without it, steering while moving backwards yaws
// the car the WRONG WAY. That bug is not hypothetical and predates Phase 2:
// reverse reaches kMaxReverseSpeed = 12 m/s, well above CarEntity's 5 m/s
// kinematic/dynamic threshold, so the dynamic model was already running (and
// yawing backwards, 42.8 deg/s wrong at 10 m/s with 0.2 rad of steer) in
// reverse. The sgn(vx) factor is therefore applied UNCONDITIONALLY, not gated.
inline AxleLateralForces axle_lateral_forces(float vx, float vl, float r, float delta, const CarParams& p,
                                             const TireParams& t, AxleLongitudinal lon = AxleLongitudinal{},
                                             float planeFrac = 0.0f) {
    const float vxs = std::max(std::fabs(vx), 1.0f);
    const float sgn = (vx >= 0.0f) ? 1.0f : -1.0f;
    const float L   = t.a + t.b;

    // Slip angles (right-positive): velocity heading at each axle minus steer.
    const float alphaF = (vl + t.a * r) / vxs - sgn * delta;
    const float alphaR = (vl - t.b * r) / vxs;

    // Static axle loads → per-axle friction limits (Phase-1 behaviour).
    const float FzF  = p.mass * p.gravity * t.b / L;
    const float FzR  = p.mass * p.gravity * t.a / L;
    float       capF = p.tireMu * FzF;
    float       capR = p.tireMu * FzR;

    if (planeFrac > 0.0f) {
        // Load transfer at the acceleration the longitudinal side actually
        // produced, then the lateral half of each axle's friction circle.
        const float    ax    = (lon.front + lon.rear) / p.mass;
        const AxleLoad load  = axle_loads_long(p, t, ax);
        const float    capF1 = friction_circle_fy_max(p.tireMu, load.front, lon.front);
        const float    capR1 = friction_circle_fy_max(p.tireMu, load.rear, lon.rear);
        const float    s     = std::clamp(planeFrac, 0.0f, 1.0f);
        capF                 = capF + s * (capF1 - capF);
        capR                 = capR + s * (capR1 - capR);
    }

    // Saturating linear tire forces (rightward-positive, opposing slip).
    AxleLateralForces fy;
    fy.front = std::clamp(-t.cAlphaFront * alphaF, -capF, capF);
    fy.rear  = std::clamp(-t.cAlphaRear * alphaR, -capR, capR);
    return fy;
}

// The |F_y| an axle is spending, in the form the friction circle wants.
inline AxleLateral axle_lateral_use(const AxleLateralForces& fy) {
    return AxleLateral{std::fabs(fy.front), std::fabs(fy.rear)};
}

// One step of the dynamic bicycle model, in a RIGHT-positive body frame that
// matches the kinematic yaw used elsewhere (heading -= deg(r)·dt):
//   vx    forward speed (m/s),  vl lateral (rightward) velocity (m/s)
//   r     yaw rate (right-positive, rad/s),  delta road-wheel steer (right-pos, rad)
// Front/rear lateral tire forces are linear in slip angle but SATURATE at μ·Fz,
// so the car can understeer / slide and lateral accel is capped at ~μg — the
// grip limit the pure kinematic model lacked. Caller blends to kinematic below
// ~5 m/s to avoid the 1/vx singularity.
inline BicycleDeriv dynamic_bicycle_deriv(float vx, float vl, float r, float delta, const CarParams& p,
                                          const TireParams& t, AxleLongitudinal lon = AxleLongitudinal{},
                                          float planeFrac = 0.0f) {
    const AxleLateralForces fy = axle_lateral_forces(vx, vl, r, delta, p, t, lon, planeFrac);

    // m(v̇l + vx·r) = FyF + FyR ;   Iz·ṙ = a·FyF − b·FyR
    const float vlDot = (fy.front + fy.rear) / p.mass - vx * r;
    const float rDot  = (t.a * fy.front - t.b * fy.rear) / t.iz;
    return {vlDot, rDot};
}

// Emergent steady-state top speed (m/s) IN ONE GEAR: the v where full-throttle
// net acceleration crosses zero. Bisection (monotone in the region of interest:
// thrust falls past the torque plateau, drag rises quadratically). Takes
// TireParams too because the per-axle load-transfer geometry feeds the traction
// model; in practice traction never binds up here (power-limited long before
// top speed), but the signature must match.
// `d` / `curve` default to the 992 Turbo S driveline and engine (CarProfile supplies its own).
inline float top_speed_in_gear(int gear, const CarParams& p, const TireParams& t, const GearboxParams& g,
                               float rWheelRear, const DrivetrainParams& d = DrivetrainParams{},
                               const TorqueCurve& curve = kTurboS992TorqueCurve) {
    float lo = 0.0f, hi = 150.0f;
    for (int i = 0; i < 64; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (longitudinal_accel(mid, 1.0f, 0.0f, gear, p, t, g, rWheelRear, d, AxleLateral{}, nullptr, curve) > 0.0f)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

// Emergent top speed (m/s) — the best any gear can hold. Scanning instead of
// assuming the tallest gear is the fast one is not pedantry: a real overdrive
// top gear (the 992's 8th, see GearboxParams) is TOO tall to reach Vmax, so
// "top speed = top gear" is only true when the gearing happens to make it so.
// Scanning makes the answer correct for any ratio set and, in this one, proves
// the last ratio is genuinely the Vmax gear. GearboxParams' upshiftRpm was
// calibrated so the rpm at the resulting Vmax sits below redline with real
// headroom (see the top-speed test) — top speed stays drag-limited, not
// rev-limited.
// Scans the gearCount fitted gears only (the unused ratio slots past it are not gears).
inline float steady_state_top_speed(const CarParams& p, const TireParams& t, const GearboxParams& g, float rWheelRear,
                                    const DrivetrainParams& d     = DrivetrainParams{},
                                    const TorqueCurve&      curve = kTurboS992TorqueCurve) {
    float best = 0.0f;
    for (int i = 0; i < g.gearCount; ++i) best = std::max(best, top_speed_in_gear(i, p, t, g, rWheelRear, d, curve));
    return best;
}

}  // namespace swish
