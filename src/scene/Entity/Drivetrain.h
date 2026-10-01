#pragma once

// AWD PTU parameters, torque split, deliverable-force solve.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "Tire.h"

#include <algorithm>

namespace swish {

// AWD power-transfer-unit (PTU) parameters — Phase-1 spec §4.1. The real
// Turbo S drives the rear axle through the gearbox and feeds the front axle
// through a hydraulically-clamped multi-plate clutch, so the split is
// reactive, not a fixed ratio.
struct DrivetrainParams {
    // f_base: the always-present front share with the clutch essentially open
    // (spec §4.1 puts this at 0.05-0.10 — a light standing clamp that keeps
    // the front driveshafts loaded so engagement is instant).
    float frontShareBase = 0.08f;
    // Front share with the clutch fully closed. NOT 1.0 (which is what §4.1's
    // literal formula would give): a clutch pack physically cannot route the
    // whole engine torque forward, and the front axle could not use it anyway
    // — under launch squat the front carries well under a third of the weight.
    // 0.50 is the even-split ceiling quoted for the 911's PTU.
    float frontShareMax = 0.50f;
    // The spec's s_rear / s_max, expressed in the currency this model actually
    // has. There is no wheel-spin state here (no wheel inertia integration),
    // so §4.1's explicitly-allowed proxy is used: the rear axle's OVER-DEMAND
    // fraction, u - 1, where u = (rear torque demand)/(rear friction limit).
    // Slip ratio grows monotonically with excess drive torque past the
    // friction peak, so this is the same quantity up to a scale — and the
    // scale is exactly what this field is. 0.30 = "the clutch is fully closed
    // by the time the rear axle is asked for 30% more than it can hold".
    float lockSpan = 0.30f;
};

// Front torque share for a given clutch lock fraction λ ∈ [0,1] — spec §4.1's
//   f = f_base + (f_max - f_base) * clamp(s_rear/s_max, 0, 1)
inline float ptu_front_share(const DrivetrainParams& d, float lambda) {
    return d.frontShareBase + (d.frontShareMax - d.frontShareBase) * std::clamp(lambda, 0.0f, 1.0f);
}

// Longitudinal force (N) the tires can actually put down for a given engine
// DEMAND at the wheels, the PTU lock fraction λ, and the lateral load already
// committed. The split decides demand; the friction circle decides delivery.
//
// λ does double duty, because in the real hardware one clutch controls both:
//   * the demand split (ptu_front_share above), and
//   * how much CROSS-AXLE spill is allowed. An open clutch leaves the axles
//     independent, so an over-demanded rear axle simply spins and its excess
//     is lost (this is the RWD case, and it reproduces the old rear-only cap
//     exactly at λ=0, f_base=0). A CLOSED clutch mechanically ties the axles,
//     so torque flows to whichever axle still has grip — a locked centre
//     coupling — and the ceiling becomes mu*(F_zf + F_zr) = mu*m*g.
//
// The load transfer that sets each axle's ceiling depends on the very a_x
// being solved for, so this is a fixed point. Each pass contracts the residual
// by exactly mu*h_cg/L ≈ 0.19, so kLoadIters = 3 (from a cold a_x = 0 start)
// bounds the error at 0.19³ ≈ 0.7% in the WORST case — one axle carrying the
// whole limit, i.e. the degenerate RWD configuration, which a test pins
// against rear_traction_cap_force's exact closed form. In the fully-locked AWD
// case it is EXACT after one pass, because the axle-load sum is a_x-invariant.
// Deliberately a fixed iteration count rather than a convergence while-loop:
// bounded, branch-free per-frame cost.
inline constexpr int kLoadIters = 3;

// `split`, when non-null, reports the per-axle breakdown of the returned total
// (Phase 2: the lateral side needs to know which axle is spending its circle on
// drive torque). Purely additive — a null pointer, the default, leaves the
// function bit-identical to before.
inline float awd_deliverable_force(float fDemand, const CarParams& p, const TireParams& t, const DrivetrainParams& d,
                                   float lambda, AxleLateral lat = AxleLateral{}, AxleLongitudinal* split = nullptr) {
    lambda              = std::clamp(lambda, 0.0f, 1.0f);
    const float f       = ptu_front_share(d, lambda);
    const float demand  = std::max(fDemand, 0.0f);
    const float demandF = demand * f;
    const float demandR = demand * (1.0f - f);

    float total = 0.0f;
    float ax    = 0.0f;
    float lastF = 0.0f, lastR = 0.0f;
    for (int i = 0; i < kLoadIters; ++i) {
        const AxleLoad load = axle_loads_long(p, t, ax);
        const float    capF = friction_circle_fx_max(p.tireMu, load.front, lat.front);
        const float    capR = friction_circle_fx_max(p.tireMu, load.rear, lat.rear);

        float useF = std::min(demandF, capF);
        float useR = std::min(demandR, capR);
        // Cross-axle spill, scaled by how closed the clutch is.
        const float spillF = demandF - useF;
        const float spillR = demandR - useR;
        useR += lambda * std::min(spillF, capR - useR);
        useF += lambda * std::min(spillR, capF - useF);

        total = useF + useR;
        ax    = total / p.mass;
        lastF = useF;
        lastR = useR;
    }
    if (split) {
        split->front = lastF;
        split->rear  = lastR;
    }
    return total;
}

// The PTU's own control law: how closed is the clutch? Spec §4.1's
// clamp(s_rear/s_max, 0, 1), with over-demand as the slip proxy (see
// DrivetrainParams::lockSpan).
//
// Evaluated with the clutch treated as still OPEN, which is what breaks the
// otherwise-circular dependency (λ → split → a_x → load transfer → rear
// ceiling → λ) into one shot: "given that the front is only taking its base
// share right now, by how much is the rear axle over its limit?"
inline float ptu_lock_fraction(float fDemand, const CarParams& p, const TireParams& t, const DrivetrainParams& d,
                               AxleLateral lat = AxleLateral{}) {
    const float    demandR = std::max(fDemand, 0.0f) * (1.0f - d.frontShareBase);
    const float    axOpen  = awd_deliverable_force(fDemand, p, t, d, 0.0f, lat) / p.mass;
    const AxleLoad load    = axle_loads_long(p, t, axOpen);
    const float    capR    = friction_circle_fx_max(p.tireMu, load.rear, lat.rear);
    if (capR <= 1.0f)
        return 1.0f;  // no rear grip at all (hydroplaning / airborne) → clamp shut
    const float u = demandR / capR;
    return std::clamp((u - 1.0f) / std::max(d.lockSpan, 1e-3f), 0.0f, 1.0f);
}

}  // namespace swish
