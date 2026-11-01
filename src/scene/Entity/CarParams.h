#pragma once

// Chassis + tire parameter structs (CarParams, TireParams) and kEnginePi.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "utils/units/Units.h"

namespace swish {

inline constexpr float kEnginePi = util::units::kPi;

// Longitudinal + tire parameters, anchored on a 992 **911 Turbo S (AWD)**.
// SI units. Retargeted 2026-08-23 (Phase 1, archive/docs/phase-1-physics-speed-accuracy.md
// §3.1) from the 992 GT3 Touring the 2026-08-15 Wave-2 pass had tuned it for.
// Reference spec: 0-60 mph 2.8 s · Vmax ~205 mph (330 km/h) · 641 hp (478 kW) /
// 590 lb-ft (800 N·m) · ~1670 kg DIN.
// Sources / estimates, cited per-field below; drivetrainEff, airDensity,
// rollResist, gravity are generic and untouched by the retarget.
struct CarParams {
    float mass = 1670.0f;  // kg, DIN curb weight (992 Turbo S, PDK, AWD)
    // powerMax is now a REFERENCE spec only (peak-power anchor for
    // engine_torque() below) — Wave 2 step 3b replaces the flat P/v drive
    // term it used to feed with a real torque curve + gearbox (§ below).
    float powerMax      = 478000.0f;  // W  (641 hp / 478 kW, twin-turbo 3.7 flat-six)
    float drivetrainEff = 0.85f;      // fraction of engine power at the wheels
    // dragCoeff/frontalArea: the Turbo S's published Cd of ~0.33 is the LOW-DRAG
    // (wing retracted) configuration, which is NOT the configuration that exists
    // at Vmax — at 205 mph the rear wing is in its performance position and the
    // front spoiler is extended. No public Cd exists for that state, so it is
    // CALIBRATED against the one directly observable number, the published
    // 330 km/h top speed: with 7th's ratio (GearboxParams below) the engine
    // makes ~4.39 kN at 91.7 m/s, so drag+rolling must equal that there
    // ⇒ Cd·A ≈ 0.810 m². Frontal area 2.14 m² is the standard 0.8·w·h estimate
    // for the wide-body 992 Turbo (1.90 m × 1.303 m). Pinned by the top-speed test.
    float dragCoeff   = 0.379f;  // effective Cd at Vmax (wing in performance position)
    float frontalArea = 2.14f;   // m^2 (wide-body 992 Turbo)
    float airDensity  = 1.225f;  // kg/m^3
    float rollResist  = 0.013f;  // Crr
    float gravity     = 9.81f;   // m/s^2
    // The Turbo S wears Pilot Sport 4S, not the GT3's Cup 2 — a sticky street
    // tire rather than a semi-slick — so 1.05 rather than the 1.10 the GT3
    // retarget carried. WetGripParams::muDry MUST track this value exactly:
    // there is a test pinning muDry == tireMu, and that identity is what makes
    // the dry path bit-identical when wetness is 0.
    float tireMu    = 1.05f;  // dry-asphalt friction, PS4S
    float wheelbase = 2.45f;  // m (992 wheelbase, common to GT3 and Turbo S)
};

// ── Lateral / yaw dynamics + longitudinal load-transfer geometry ──────────
// Parameters for a rear-biased-CG 911 (rear engine ⇒ CG toward the rear axle,
// so the front lever a > rear lever b). Equal cornering stiffness with a>b
// yields a positive understeer gradient → stable (won't spin blindly).
// Also feeds the per-axle load transfer that the AWD traction model below uses
// (a, b, hCg) — moved ahead of it in this file for that reason.
//
// Phase 1: a/b retargeted from the GT3's 39.2/60.8 to the Turbo S's published
// **40/60** static distribution (the AWD front differential + driveshafts move
// a little mass forward). a + b = 2.45 m wheelbase is unchanged.
struct TireParams {
    float a  = 1.47f;    // CG → front axle (m); a + b = wheelbase, a/L = 60%
    float b  = 0.98f;    // CG → rear axle (m);  b/L = 40% (front axle load share)
    float iz = 3050.0f;  // yaw moment of inertia (kg·m²) — GT3's 2600 scaled by the mass ratio
                         // 1670/1418; lands inside the 2800-3200 range typical of a 911.
    // Understeer (stable at ALL speeds, no critical speed) needs
    // Cα_rear/Cα_front > W_rear/W_front = a/b = 1.50. A stiffer rear delivers that
    // (176/96 = 1.83). Values are the GT3's 80k/150k scaled by the new axle loads
    // at the SAME per-newton stiffness (14.7 and 17.7 rad⁻¹): Fz_front = 6553 N,
    // Fz_rear = 9829 N. PS4S is marginally softer than Cup 2 in cornering
    // stiffness too, which is inside this estimate's error bar.
    float cAlphaFront = 96000.0f;   // N/rad
    float cAlphaRear  = 174000.0f;  // N/rad
    // CG height (m) — low rear-engine car; also anticipated by the Phase-B2
    // lateral load-transfer spec (archive/docs/wheels/phase-b-spec.md:230, same value).
    // Unchanged by the Phase-1 retarget: the Turbo S and GT3 sit at essentially
    // the same ride height. Drives the longitudinal weight-jacking term in
    // axle_loads_long() below.
    float hCg = 0.45f;
};

}  // namespace swish
