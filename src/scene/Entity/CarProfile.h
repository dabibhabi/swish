#pragma once

// Purpose:    One car's complete physics description (CarProfile) and the two shipped profiles:
//             car_profiles::turbo_s_992() (the default; exactly the pre-profile numbers the goldens pin)
//             and car_profiles::rs_993() (the 1995 911 Carrera RS of the rigged mesh).
// Units:      SI (kg, m, N, N·m, rpm, rad, deg for the steering fields). CarEntity converts to WU.
// Complexity: O(1); every function here is constexpr.
// Example:    car.apply_profile(car_profiles::rs_993(&rigGeometry));
// See also:   CarParams.h, Powertrain.h (TorqueCurve, GearboxParams), Drivetrain.h (RWD = frontShareMax 0),
//             CarEntity::apply_profile.
// Test:       tests/test_car_profiles.cpp ([car][profile])

#include "CarParams.h"
#include "Drivetrain.h"
#include "Powertrain.h"

#include <string_view>

namespace swish {

// Wheel geometry measured from a named-rig GLB by the loader (metres).
// TODO(merge): S0c owns this in SceneTypes.h; this identical copy lets S0d build on its own.
struct CarRigGeometry {
    float wheelbase_m, track_front_m, track_rear_m, tyre_radius_front_m, tyre_radius_rear_m;
    bool  named_rig;
};

// Default member values ARE the 992 Turbo S: a default-constructed CarProfile is turbo_s_992(), so
// CarEntity's member default needs no function call and reproduces the pre-profile behaviour exactly.
struct CarProfile {
    std::string_view name = "992 Turbo S";
    CarParams        car{};
    TireParams       tire{};
    GearboxParams    gearbox{};
    TorqueCurve      torque = kTurboS992TorqueCurve;
    DrivetrainParams drivetrain{};  // AWD PTU; frontShareBase = frontShareMax = 0 is RWD
    // Driven (rear) rolling radius used until the loader's wheel frames arrive (then the measured
    // per-corner radius wins, CarEntity::rear_wheel_radius). 0.363 m = the 992 mesh's measured rear.
    float wheelRadiusM = 0.363f;
    float steerLockDeg = 35.f;          // road-wheel lock, each side
    float steerRatio   = 450.f / 35.f;  // steering-wheel deg per road-wheel deg (±450° at full lock)
};

// Cross-struct invariants a profile must keep (the per-struct ones are the Ch01 gearbox validators and
// torque_curve_valid). Margins, not equalities, where the runtime recomputes the value (Ch01 pitfall).
[[nodiscard]] constexpr bool car_profile_valid(const CarProfile& p) noexcept {
    const float L = p.tire.a + p.tire.b;
    return gearbox_params_valid(p.gearbox) && torque_curve_valid(p.torque) &&
           p.gearbox.upshiftRpm < p.torque.redline &&  // the box shifts before the fuel cut
           p.car.mass > 0.f && p.wheelRadiusM > 0.f && p.steerLockDeg > 0.f && p.steerRatio > 0.f && p.tire.a > 0.f &&
           p.tire.b > 0.f && L - p.car.wheelbase < 1e-3f && p.car.wheelbase - L < 1e-3f &&
           // Understeer at all speeds: Cα_r/Cα_f > W_r/W_f = a/b (TireParams comment).
           p.tire.cAlphaRear * p.tire.b > p.tire.cAlphaFront * p.tire.a &&
           p.drivetrain.frontShareBase <= p.drivetrain.frontShareMax;
}

namespace car_profiles {

// 992 Turbo S (AWD, 7-speed PDK): exactly the numbers CarParams / TireParams / GearboxParams /
// DrivetrainParams and the 992 torque table default to. The goldens pin this one.
[[nodiscard]] constexpr CarProfile turbo_s_992() {
    return CarProfile{};
}

// Plausibility window for a loader measurement; anything outside it (including an unset 0) is ignored.
inline constexpr float kMinWheelbaseM = 1.8f, kMaxWheelbaseM = 3.2f;
inline constexpr float kMinTyreRadiusM = 0.25f, kMaxTyreRadiusM = 0.45f;

// 1995 911 Carrera RS (993), RWD, G50/31 6-speed manual (driven here by the same auto-shift machine).
// Sources: [spec] = published factory figure (from memory; no web access when written, 2026-10-12);
// [est] = engineering estimate, reasoning given. `measured` (named-rig GLB) overrides the spec
// wheelbase and the rear tyre radius when its values are plausible; null = spec numbers only.
[[nodiscard]] constexpr CarProfile rs_993(const CarRigGeometry* measured = nullptr) {
    float wheelbase = 2.272f;  // m [spec]; the rigged mesh measures 2.259
    float radius    = 0.320f;  // m [spec-derived] 265/35ZR18 rear: 0.2286 + 0.35·0.265 = 0.321; mesh 0.320
    if (measured) {
        if (measured->wheelbase_m >= kMinWheelbaseM && measured->wheelbase_m <= kMaxWheelbaseM)
            wheelbase = measured->wheelbase_m;
        if (measured->tyre_radius_rear_m >= kMinTyreRadiusM && measured->tyre_radius_rear_m <= kMaxTyreRadiusM)
            radius = measured->tyre_radius_rear_m;
    }

    CarProfile p;
    p.name = "993 Carrera RS";

    // ── Chassis ──
    p.car.mass          = 1279.0f;    // kg kerb [spec; some sources give 1270]
    p.car.powerMax      = 221000.0f;  // W, 300 PS at 6500 rpm [spec] (reference only, as on the 992)
    p.car.drivetrainEff = 0.85f;      // [est] kept at the 992's generic value
    // Cd·A CALIBRATED to the 277 km/h [spec] top speed, as the 992 is to 330 km/h: in 6th at 76.94 m/s
    // (6484 rpm, at peak power) the wheels get ~2.44 kN, rolling takes 0.16 kN, so drag must be ~2.27 kN
    // ⇒ Cd·A ≈ 0.628 m² (bisection on longitudinal_accel). With A = 1.90 m² [est as briefed; the 0.8·w·h rule on
    // the 1.735 m × 1.27 m body gives 1.76, and only the product Cd·A matters] that is Cd ≈ 0.33, the published RS
    // figure [spec, fixed wing], so the calibration and the spec agree.
    p.car.dragCoeff   = 0.3305f;  // effective Cd at Vmax (calibrated; pinned by the top-speed test)
    p.car.frontalArea = 1.90f;    // m²
    // Period street rubber (225/40ZR18 F, 265/35ZR18 R) [spec sizes]; μ [est]. WetGripParams::muDry
    // tracks this via CarEntity::apply_profile.
    p.car.tireMu    = 1.0f;
    p.car.wheelbase = wheelbase;

    // ── Lateral / load transfer ──
    // ~40/60 F/R static distribution [spec-ish, est]: front load share b/L = 0.40 ⇒ a = 0.6 L.
    p.tire.a  = 0.6f * wheelbase;
    p.tire.b  = wheelbase - p.tire.a;
    p.tire.iz = 2336.0f;  // kg·m² [est] 992's 3050 scaled by mass, 3050·1279/1670
    // Same per-newton cornering stiffness as the 992 (14.65 / 17.70 rad⁻¹) on the 993's axle loads
    // Fz_f = 0.40·1279·9.81 = 5019 N, Fz_r = 7528 N [est]. Cα_r/Cα_f = 1.81 > a/b = 1.50 ⇒ understeer.
    p.tire.cAlphaFront = 73500.0f;   // N/rad
    p.tire.cAlphaRear  = 133200.0f;  // N/rad
    p.tire.hCg         = 0.46f;      // m [est]

    // ── Engine: M64/20 3746 cc flat-six, naturally aspirated ──
    // Anchors [spec]: 355 N·m at 5400 rpm; 221 kW at 6500 rpm ⇒ 221000 / (6500·2π/60) = 324.7 N·m.
    // Everything else [est]: a broad NA curve from ~900 rpm idle, peak power held at 6500 (P(6250) and
    // P(6600) both interpolate lower), and the fuel cut at 6800, where the painted tach's red zone starts.
    p.torque = TorqueCurve{
        .points  = {{{900.f, 220.f},
                     {1500.f, 255.f},
                     {2500.f, 295.f},
                     {3500.f, 320.f},
                     {4500.f, 342.f},
                     {5400.f, 355.f},
                     {6000.f, 344.f},
                     {6500.f, 324.7f},
                     {6700.f, 305.f},
                     {6800.f, 0.f}}},
        .count   = 10,
        .redline = 6800.f,
    };

    // ── Gearbox: G50/31 ──
    // Ratios and final drive as briefed (period figures, unverified offline) [spec?]. Speeds at the
    // 6800 rpm cut with r = 0.320 m: 75 / 119 / 157 / 192 / 233 / 290 km/h; 277 km/h in 6th is 6484 rpm,
    // at peak power, so top speed is drag-limited in top gear (consistent; no adjustment needed).
    p.gearbox.ratios     = {3.154f, 2.000f, 1.522f, 1.242f, 1.024f, 0.820f, 0.f};  // slot 6 unused
    p.gearbox.gearCount  = 6;
    p.gearbox.finalDrive = 3.444f;
    // Shift band [est]: upshift 200 rpm under the cut (the 992 keeps 300 under its 7200). Worst landings:
    // up 6600·2.000/3.154 = 4185 > 3200 and down 3200·3.154/2.000 = 5046 < 6600, so the box cannot hunt.
    p.gearbox.upshiftRpm   = 6600.f;
    p.gearbox.downshiftRpm = 3200.f;
    p.gearbox.shiftDwell   = 0.3f;

    // ── Driveline: RWD, no front axle drive ──
    p.drivetrain.frontShareBase = 0.f;
    p.drivetrain.frontShareMax  = 0.f;

    p.wheelRadiusM = radius;

    // ── Steering ── ~2.7 turns lock-to-lock at ~16.5:1 [spec, from memory] ⇒ road-wheel lock
    // 2.7·360 / (2·16.5) = 29.45° ⇒ 29.5° [est]; check: 2·29.5·16.5 = 973.5° = 2.70 turns.
    p.steerLockDeg = 29.5f;
    p.steerRatio   = 16.5f;
    return p;
}

}  // namespace car_profiles

static_assert(car_profile_valid(car_profiles::turbo_s_992()), "the 992 Turbo S profile must be valid");
static_assert(car_profile_valid(car_profiles::rs_993()), "the 993 Carrera RS spec profile must be valid");
static_assert(car_profiles::rs_993().gearbox.gearCount == 6, "the G50/31 is a 6-speed");

}  // namespace swish
