#pragma once

// Brake / rear-traction caps and the longitudinal acceleration solve.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "Drivetrain.h"

#include <algorithm>
#include <cmath>

namespace swish {

// Four-wheel brake ceiling (N). Brakes act on every corner regardless of which
// axle is driven, so this is the SAME per-axle friction circle summed over
// both axles — with no lateral demand it is exactly mu*m*g (the axle-load sum
// is invariant under load transfer), i.e. unchanged in kind from the pre-AWD
// model, but it now correctly loses stopping power while cornering.
// `split`, when non-null, reports the per-axle caps whose sum is the return
// value, so a caller scaling by the brake pedal gets the per-axle brake force.
// Braking is inherently load-proportional (all four corners, each capped by its
// own circle), which is why no drivetrain split appears here.
inline float brake_cap_force(const CarParams& p, const TireParams& t, AxleLateral lat = AxleLateral{},
                             AxleLongitudinal* split = nullptr) {
    float cap  = 0.0f;
    float ax   = 0.0f;
    float capF = 0.0f, capR = 0.0f;
    for (int i = 0; i < kLoadIters; ++i) {
        const AxleLoad load = axle_loads_long(p, t, ax);
        capF                = friction_circle_fx_max(p.tireMu, load.front, lat.front);
        capR                = friction_circle_fx_max(p.tireMu, load.rear, lat.rear);
        cap                 = capF + capR;
        ax                  = -cap / p.mass;  // braking transfers load FORWARD
    }
    if (split) {
        split->front = capF;
        split->rear  = capR;
    }
    return cap;
}

// LEGACY / REFERENCE: the pre-Phase-1 RWD rear-axle-only traction cap, kept
// because it is the closed-form solution the AWD model must reduce to, and a
// test pins that equivalence (awd_deliverable_force with frontShareBase = 0,
// λ = 0 and unlimited demand reproduces it). No longer on the per-frame path.
//
// At the traction limit a_x = mu*Fz_rear(a_x)/m — implicit, since the load
// depends on the very accel it caps. Both sides are linear in a_x:
//   a_x*(1 - mu*hCg/L) = mu*g*(a/L)  ⇒  a_x = mu*g*(a/L) / (1 - mu*hCg/L)
// Denominator guarded away from zero/sign flip for any physically sane
// mu/hCg/L (~0.81 at these Turbo S values).
inline float rear_traction_cap_force(const CarParams& p, const TireParams& t) {
    const float L          = t.a + t.b;
    const float denom      = std::max(1.0f - p.tireMu * t.hCg / L, 0.05f);
    const float axTraction = p.tireMu * p.gravity * (t.a / L) / denom;
    return p.mass * axTraction;
}

// Net longitudinal acceleration (m/s^2) at forward speed v (m/s), with throttle
// and brake in [0,1] and the gearbox in the given gear (0-indexed; advance it
// per-frame with gearbox_shift_step — see CarEntity::update). Drive force is
// engine_torque(rpm) fed through that gear (gear_drive_force), split across
// the axles by the AWD PTU and then limited by each axle's own friction
// circle under load transfer (see awd_deliverable_force). Braking is not
// drive-limited and uses the four-wheel circle (brake_cap_force). The only
// resistances are quadratic aero drag and rolling resistance. Both the launch
// accel and the top speed therefore EMERGE from the force balance — neither
// is clamped or scripted.
//
// `lat` lets the caller feed the lateral force each axle is already using
// (CarEntity passes the bicycle model's own F_yf/F_yr), so power-down while
// cornering is limited by the combined-slip circle rather than the
// straight-line limit. Defaulted so the straight-line call sites and the
// existing tests are unaffected.
//
// `split`, when non-null, reports the SIGNED per-axle longitudinal tire force
// that produced the returned acceleration (drive positive, brake/resistance
// negative). Phase 2 (hydroplane spin-out) feeds this straight back into
// axle_lateral_forces so cornering capacity is reduced by the longitudinal
// force in use — the other half of the friction circle, and the mechanism the
// spin actually comes from (archive/docs/phase-2-hydroplane-spinout.md §5, as-built).
// Drive is split by the AWD PTU; brake and the resistances are split by axle
// load, because they act on all four corners / through the CG rather than
// through the driveline.
//
// `curve` is the engine's torque table (CarProfile::torque); the default is the
// 992 Turbo S table every pre-profile caller has always used.
inline float longitudinal_accel(float v, float throttle, float brake, int gear, const CarParams& p, const TireParams& t,
                                const GearboxParams& g, float rWheelRear,
                                const DrivetrainParams& d = DrivetrainParams{}, AxleLateral lat = AxleLateral{},
                                AxleLongitudinal* split = nullptr, const TorqueCurve& curve = kTurboS992TorqueCurve) {
    const float vAbs = std::fabs(v);
    const float dir  = (v >= 0.0f) ? 1.0f : -1.0f;

    // Engine demand at the wheels in the current gear, then AWD-split and
    // friction-circle-limited. gear_drive_force/engine_rpm floor v internally
    // (clutch-slip stand-in) to avoid the standstill singularity.
    // Note the throttle scales the DEMAND before the limit, not the limited
    // result: a part-throttle request cannot exceed grip either, and it is
    // what makes the PTU throttle-responsive (a gentle cruise leaves the
    // clutch open and the car rear-driven; a stab closes it).
    const float      fDemand = std::clamp(throttle, 0.0f, 1.0f) * gear_drive_force(v, gear, g, p, rWheelRear, curve);
    const float      lambda  = ptu_lock_fraction(fDemand, p, t, d, lat);
    AxleLongitudinal driveSplit{};
    const float      fDrive = awd_deliverable_force(fDemand, p, t, d, lambda, lat, split ? &driveSplit : nullptr);

    // Braking is tire-limited on ALL FOUR wheels (not drive-limited) and
    // opposes current motion.
    AxleLongitudinal brakeCaps{};
    const float      brakePedal = std::clamp(brake, 0.0f, 1.0f);
    const float      fBrake     = brakePedal * brake_cap_force(p, t, lat, split ? &brakeCaps : nullptr);

    // Resistances oppose motion: quadratic aero + (near-)constant rolling.
    const float fDrag = 0.5f * p.airDensity * p.dragCoeff * p.frontalArea * vAbs * vAbs;
    const float fRoll = (vAbs > 0.05f) ? p.rollResist * p.mass * p.gravity : 0.0f;

    const float fNet = fDrive - dir * (fBrake + fDrag + fRoll);

    if (split) {
        // CONTACT-PATCH forces only. Aerodynamic drag is deliberately EXCLUDED:
        // it acts on the body shell, not through the tires, so it does not
        // consume any of the friction circle. Charging it to the tires is not a
        // rounding error — at 45 m/s drag is ~1.0 kN while a hydroplaning axle's
        // whole circle is ~0.5 kN, so including it zeroed the lateral capacity
        // outright and the car went rigidly straight. Rolling resistance IS a
        // contact-patch force and is kept (~0.2 kN, load-proportional).
        //
        // Consequence for callers: split.front + split.rear is the tire force,
        // which is m*a_x PLUS the aero drag, not m*a_x. A test pins that.
        const AxleLoad load   = axle_loads_long(p, t, fNet / p.mass);
        const float    total  = load.front + load.rear;
        const float    fShare = (total > 1.0f) ? load.front / total : 0.5f;
        const float    roll   = dir * fRoll;
        split->front          = driveSplit.front - dir * brakePedal * brakeCaps.front - roll * fShare;
        split->rear           = driveSplit.rear - dir * brakePedal * brakeCaps.rear - roll * (1.0f - fShare);
    }
    return fNet / p.mass;
}

}  // namespace swish
