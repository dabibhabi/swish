#pragma once

// Engine torque curve, gearbox parameters/state, PDK-style automatic shift machine.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "CarParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace swish {

// ── Engine torque curve + PDK-style automatic (Wave 2 step 3b) ────────────
// Replaces the flat powerMax*eff/v drive term with engine_torque(rpm) fed
// through real gear ratios. This is Chrono's "SimpleMap" engine tier plus its
// §3 automatic-shift machine — see
// docs/lessons-from-chrono/06-driveline-powertrain-drivers.md §5, which calls
// this out by name as the one upgrade that would actually be felt (torque
// hole between shifts, RPM-dependent push) vs the old idealized curve.
//
// USER DECISION: automatic, kept SIMPLE. No clutch/torque-converter slip, no
// launch control, no driver shift input — engine rpm is a direct (no-slip)
// kinematic function of wheel speed and the engaged gear. This is the "PDK
// as a rigid multi-ratio gearbox" simplification (see CHANGELOG.md 2026-08-15
// "torque curve + 7-speed PDK-style automatic"); revisit only if the user
// asks for launch/converter fidelity later.

// Twin-turbo 3.7 flat-six WOT torque (N·m) vs rpm, table + linear
// interpolation (Chrono's ChFunctionInterp tier). Phase 1 replaces the GT3's
// naturally-aspirated 4.0 curve with the Turbo S's forced-induction shape.
// Anchors are exact table nodes (0% error by construction): the published
// **800 N·m (590 lb-ft) plateau from 2500 to 4000 rpm** and **peak power
// 478 kW (641 hp) @ 6750 rpm** (P=Tω ⇒ 676.2 N·m there — 478 kW computed back
// out to 0.01%). Intermediate nodes are an ENGINEERING ESTIMATE (no factory
// dyno sheet to hand) shaped like a modern twin-turbo flat-six:
//   * a genuine LOW-RPM HOLE (250 N·m at 900 rpm rising to 700 at 2000) — this
//     is turbo lag, and it is load-bearing, not decoration: it is why the
//     standing launch is not simply mu*g from t=0 (see longitudinal_accel);
//   * the flat 800 N·m boost plateau (the defining trait of the engine);
//   * a gentle taper past the plateau as boost is bled back to protect the
//     power peak, then a fast fall to EXACTLY 0 at the 7200 rpm redline —
//     continuous (no value jump) and ≤0 for every rpm at or past redline, so
//     the rev limiter genuinely limits revs rather than the curve just
//     petering out.
// Compared with the outgoing GT3 curve this is +70% peak torque arriving
// 3600 rpm earlier, against a 1800 rpm lower redline: a torque-rich, short-
// revving engine instead of a peaky, high-revving one.
inline float engine_torque(float rpm) {
    struct Pt {
        float rpm, nm;
    };
    static constexpr Pt kCurve[] = {
        {900.f, 250.f},                                      // idle — turbos not spooled
        {1500.f, 480.f}, {2000.f, 700.f},                    // boost building
        {2500.f, 800.f}, {4000.f, 800.f},                    // 590 lb-ft plateau (both ends are anchors)
        {5000.f, 760.f}, {6000.f, 715.f}, {6750.f, 676.2f},  // peak power anchor (478 kW)
        {7000.f, 620.f},                                     // steep taper begins (soft rev limiter)
        {7200.f, 0.f},                                       // redline — torque reaches exactly zero here
    };
    constexpr float  kRedline = 7200.f;
    constexpr size_t kCurveN  = sizeof(kCurve) / sizeof(kCurve[0]);
    if (rpm >= kRedline)
        return 0.f;  // past redline: rev limiter cuts it, full stop
    if (rpm <= kCurve[0].rpm)
        return kCurve[0].nm;  // below idle: hold the idle value (no extrapolation)
    for (size_t i = 0; i + 1 < kCurveN; ++i) {
        if (rpm >= kCurve[i].rpm && rpm <= kCurve[i + 1].rpm) {
            const float f = (rpm - kCurve[i].rpm) / (kCurve[i + 1].rpm - kCurve[i].rpm);
            return kCurve[i].nm + f * (kCurve[i + 1].nm - kCurve[i].nm);
        }
    }
    return 0.f;  // unreachable given the redline/idle guards above
}

// PDK-style automatic, modeled with the **first seven** ratios of the 992's
// 8-speed PDK family (widely published as 5.20 / 3.13 / 2.10 / 1.54 / 1.19 /
// 0.97 / 0.82 / 0.67 on a 3.44 final drive). Treat that as an engineering
// reference, not a verified factory sheet for this exact model.
//
// WHY ONLY SEVEN. The 992's 8th is a long cruise OVERDRIVE: at 0.67 × 3.44 it
// needs 93.0 m/s to reach the upshift rpm, which is ABOVE the drag-limited
// Vmax of 91.7 m/s, so wide-open throttle can never engage it — the real car
// likewise takes Vmax in 7th. Since the shift machine below is
// throttle-independent (it cannot model the real "lift and it grabs 8th"
// cruise upshift), an 8th ratio would be unreachable dead state; it is
// deliberately omitted rather than shipped inert.
//
// Both hard physical requirements were re-verified after the Turbo S retarget:
//   1) top gear (7th, 0.82 × 3.44 = 2.821) is tall enough that the
//      drag-limited ~205 mph top speed is reached essentially AT the
//      power-peak rpm (6804 of 6750) with ~400 rpm of headroom below the
//      7200 redline — Vmax stays drag-limited, not rev-limited (see
//      steady_state_top_speed below).
//   2) 1st and 2nd are short enough that engine force clears the AWD
//      four-wheel traction ceiling for the whole of their range, so the
//      0-60 sprint is TIRE-limited (not gearing-limited) up to ~23.4 m/s.
//      1st at the launch rpm makes 18.7 kN against a 17.2 kN ceiling — the
//      margin is thin precisely because of the turbo hole in engine_torque(),
//      which is the honest reason the launch is not a flat mu*g.
struct GearboxParams {
    // 1st..7th, dimensionless. finalDrive multiplies every gear.
    std::array<float, 7> ratios     = {5.20f, 3.13f, 2.10f, 1.54f, 1.19f, 0.97f, 0.82f};
    float                finalDrive = 3.44f;

    // Auto-shift thresholds. TWO-LAYER anti-chatter, exactly Chrono's
    // ChAutomaticTransmissionShafts pattern (lessons-06 §3): a wide
    // hysteresis band (upshift near redline, downshift well below it) means
    // the rpm right after a shift lands INSIDE the band — not at its edge —
    // so it cannot immediately re-trigger the opposite shift; shiftDwell is
    // the secondary latency guard against any single-frame transient. With a
    // real (progressive, non-uniform) PDK ratio steps above, the LARGEST step
    // is 1st→2nd (1.661) and the SMALLEST is 6th→7th (1.183), so an upshift at
    // upshiftRpm lands somewhere in 4154-5832 rpm — the worst case is still
    // ~950 rpm (=30%) above downshiftRpm, so hysteresis alone already cannot
    // oscillate; the dwell timer is belt-and-suspenders.
    float upshiftRpm   = 6900.f;  // just past the peak-power rpm, before the steep taper
    float downshiftRpm = 3200.f;  // comfortably below any post-upshift landing rpm
    float shiftDwell   = 0.3f;    // s, minimum time between shifts
};

// Engine rpm implied by (signed) wheel speed v (m/s) in the given gear,
// assuming the simple no-slip driveline above. Floored at 3 m/s so a standing
// start uses an idle-adjacent rpm rather than the literal (and singular)
// rpm = 0 a true no-slip mapping would give. Physically this floor stands in
// for the PDK's clutch SLIPPING at launch: a real dual-clutch never lets the
// engine fall to zero. It is an unconditional kinematic floor, NOT a launch-
// control state machine (explicitly out of scope, Phase-1 spec §3.3) — no
// brake/throttle trigger, no rpm target, no hold. At the Turbo S's 1st-gear
// ratio it evaluates to 1412 rpm, where the turbo hole in engine_torque()
// gives 446 N·m; that, not a scripted number, is what the standing launch
// starts from.
inline float engine_rpm(float v, int gear, const GearboxParams& g, float rWheel) {
    const float vEff  = std::max(std::fabs(v), 3.0f);
    const float ratio = g.ratios[static_cast<size_t>(gear)] * g.finalDrive;
    return vEff * ratio * 60.0f / (2.0f * kEnginePi * rWheel);
}

// Drive force at the wheel (N) for the given gear — REPLACES the flat
// powerMax*eff/v term. rWheel is the REAR rolling radius: on the AWD Turbo S
// both axles are driven, but the front and rear run different tire sizes
// (255/35-20 vs 315/30-21) and the gearbox output speed is defined by the
// rear, so the rear radius remains the driveline's reference. This is the
// TOTAL demand at the road, before the AWD split (awd_deliverable_force).
inline float gear_drive_force(float v, int gear, const GearboxParams& g, const CarParams& p, float rWheel) {
    const float rpm   = engine_rpm(v, gear, g, rWheel);
    const float ratio = g.ratios[static_cast<size_t>(gear)] * g.finalDrive;
    return engine_torque(rpm) * ratio * p.drivetrainEff / rWheel;
}

// Idealized "which gear would WOT full-throttle be in at this speed" — the
// FIRST (shortest) gear whose rpm is still under the upshift threshold,
// mirroring the sequential upshift behavior of gearbox_shift_step below but
// WITHOUT its hysteresis/dwell state. Reference/test convenience only — the
// real per-frame gear lives in GearState, advanced by gearbox_shift_step.
inline int gear_for_speed(float v, const GearboxParams& g, float rWheel) {
    for (int i = 0; i < 6; ++i) {
        if (engine_rpm(v, i, g, rWheel) < g.upshiftRpm)
            return i;
    }
    return 6;  // top gear
}

// Per-car gear-shift state: current gear (0-indexed, 0 = 1st) and time since
// the last shift (the shiftDwell lockout timer). Lives on CarEntity;
// CarPhysics.h only advances it (pure function in, new state out — same
// pattern as CornerSpring/BicycleDeriv).
struct GearState {
    int   gear       = 0;     // 0 = 1st ... 6 = 7th
    float sinceShift = 1e9f;  // s; large ⇒ not inside a dwell lockout
};

// One step of the auto-shift state machine: upshift near redline, downshift
// at low rpm, both gated by the shiftDwell latency (see GearboxParams
// comment for why this pair cannot hunt at a steady speed).
inline GearState gearbox_shift_step(GearState state, float v, const GearboxParams& g, float rWheel, float dt) {
    state.sinceShift += dt;
    const float rpm = engine_rpm(v, state.gear, g, rWheel);
    if (state.sinceShift >= g.shiftDwell) {
        if (rpm >= g.upshiftRpm && state.gear < 6) {
            state.gear += 1;
            state.sinceShift = 0.0f;
        } else if (rpm <= g.downshiftRpm && state.gear > 0) {
            state.gear -= 1;
            state.sinceShift = 0.0f;
        }
    }
    return state;
}

}  // namespace swish
