#pragma once

// Pure road-wheel kinematics (SI units), independent of the renderer, GLFW,
// and GLM so it can be unit-tested without a GPU — same contract as
// CarPhysics.h. CarEntity converts world units to/from SI at the boundary.

#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

inline constexpr float kTwoPi = util::units::kTwoPi;

// Wrap an angle into [0, 2π). std::fmod keeps the sign of the dividend, so a
// negative remainder needs one fixup. Wrapping every step bounds θ below 2π,
// where a float ULP is ~4e-7 rad; left unwrapped, θ after a few km reaches
// ~1e4 rad where the ULP is ~1e-3 rad (≈0.06° of visible rim quantization)
// and keeps degrading as the drive continues.
inline float wrap_angle_2pi(float theta) {
    theta = std::fmod(theta, kTwoPi);
    return theta < 0.0f ? theta + kTwoPi : theta;
}

// One kinematic rolling step: θ' = wrap(θ + (v / r)·dt). Rolling without
// slip ties spin rate to travel speed: ω = v / r. v is signed (negative in
// reverse, so θ runs backward); r is the rolling radius in meters and must
// be > 0 (callers pass the loader-measured per-corner radius).
inline float wheel_spin_step(float theta, float v, float r, float dt) {
    return wrap_angle_2pi(theta + (v / r) * dt);
}

// One spin step from a wheel's angular speed ω (rad/s): θ' = wrap(θ + ω·dt). This is
// what the visuals integrate from CarEntity's per-corner wheel_omega hook, so a later
// slip/lockup model (Wheel Phase C) only has to write ω, never θ.
inline float wheel_spin_advance(float theta, float omega, float dt) {
    return wrap_angle_2pi(theta + omega * dt);
}

// ── Per-wheel kinematic rolling speed (S1a, tasks/todo.md "Rigged Porsche") ──
// Car space: nose +X, up +Y, right +Z (metres). Yaw rate ψ̇ is right-positive, so the
// body's angular velocity is (0, −ψ̇, 0) and a point p moves at v_ref + ω × p =
// (v_x − ψ̇·p_z, 0, v_l + ψ̇·p_x). The reference is the entity origin, the point the
// integrator actually moves (CarEntity::update), so hub_x/hub_z are hub positions in
// car space. The tyre rolls with the component along its own heading (δ_w, right-
// positive; 0 for the rears):
//   v_i = (v_x − ψ̇·z_i)·cos δ_w + (v_l + ψ̇·x_i)·sin δ_w
// In a right turn (ψ̇ > 0) the left (outer, z < 0) wheels roll faster. No slip: this
// is the kinematic speed; ω_i = v_i / R_i.
inline float wheel_rolling_speed(float v_x, float v_l, float yaw_rate, float hub_x_m, float hub_z_m,
                                 float wheel_steer_rad) {
    const float along = v_x - yaw_rate * hub_z_m;
    const float side  = v_l + yaw_rate * hub_x_m;
    return along * std::cos(wheel_steer_rad) + side * std::sin(wheel_steer_rad);
}

// ── Ackermann front-wheel angles (S1a, draw-only) ─────────────────────
// Convention: the bicycle angle δ is the VIRTUAL CENTRE WHEEL, and pure Ackermann puts
// the turn centre on the rear-axle line at R = L / tan δ from the centreline. A front
// wheel at lateral offset z_w (right +Z, ±t/2) then needs
//   cot δ_w = cot δ − z_w / L   ⇔   δ_w = atan( L·tan δ / (L − z_w·tan δ) )
// so cot δ_outer − cot δ_inner = t / L (the textbook relation) and the mean of the two
// COTANGENTS is cot δ; the arithmetic mean sits slightly above δ (second order). The
// atan form has no singularity at δ = 0 and stays valid while z_w·tan δ < L (δ < ~72°
// on the 993, far past any lock). `ackermann` blends 0 = parallel steer (δ_w = δ) to
// 1 = pure Ackermann. Mirror-symmetric: f(−δ, −z) = −f(δ, z).
inline float ackermann_wheel_angle(float delta_rad, float hub_z_m, float wheelbase_m, float ackermann) {
    const float t     = std::tan(delta_rad);
    const float pure  = std::atan(wheelbase_m * t / (wheelbase_m - hub_z_m * t));
    const float blend = std::clamp(ackermann, 0.f, 1.f);
    return delta_rad + blend * (pure - delta_rad);
}

// ── Quarter-car suspension (Phase B) ──────────────────────────────────
// archive/docs/wheels/phase-b-spec.md. Kinematic travel only — no load transfer yet
// (deferred to B2, spec §9). Pure SI, no GLM: CarEntity converts to/from
// world units (and composes the result into draw matrices only) at the
// boundary, exactly like the rolling-radius contract above.

// One corner's suspension state. + x_m = corner sits HIGHER than the
// already-Y-snapped car origin (spec §5 sign convention).
struct CornerSpring {
    float x_m   = 0.f;  // vertical offset from neutral (m)
    float v_mps = 0.f;  // dx/dt
};

// Phase-B scoped design inputs — does NOT touch CarParams/TireParams
// (CarPhysics.h). Ride frequency + damping ratio, not raw k/c, so a
// CarParams::mass retarget re-derives k/c automatically and the thing actually
// being specified — fn/zeta — stays correct (spec §0). That design choice paid
// off at the Phase-1 Turbo S retarget: only the four numbers below moved, and
// they moved because the CAR changed, not because the mass did.
//
// ── Turbo S ride rates (Phase 1, 2026-08-23) ────────────────────────────
// The GT3's 2.3/2.5 Hz and ζ 0.55/0.60 were explicitly flagged in
// archive/docs/phase-1-physics-speed-accuracy.md §7 as a risk: GT3 numbers must not
// sit under a Turbo S badge. No OEM spring sheet exists for either car, so
// these are ENGINEERING ESTIMATES, derived the same way phase-b-spec.md §1
// derived the GT3's — but for a very different car:
//
//   * The Turbo S is a 1670 kg all-weather GT with adaptive PASM dampers, not
//     a 1418 kg track car on Cup 2s. Real spring rates run roughly 1.3-1.4x
//     softer than a GT3's, and fn ∝ √k, so ≈2.4/1.35 ≈ 1.8 Hz front.
//   * Rear stiffer than front (flat-ride convention, and this is a rear-engine
//     car carrying 60% of its mass back there): 2.0 Hz.
//   * Damping: a PASM road car sits nearer ζ ≈ 0.45 than a GT3's 0.55. Still
//     firm enough that the visible response is one crisp overshoot, which is
//     what a visual-only suspension wants (phase-b-spec §1).
//   * Travel: ±45 mm, up from the GT3's ±35 mm. A road-biased car has more
//     wheel travel; it is also what makes the camber/toe curves below produce
//     a visible ±1° rather than a sub-half-degree twitch.
//
// Sanity check on the derived rates at these masses (300 kg front / 451 kg
// rear corner sprung mass): k_front ≈ 38.4 N/mm, k_rear ≈ 71.2 N/mm wheel
// rate — squarely in the range for a heavy rear-engine sports car, and 74-77%
// of the GT3 rates the same derivation produced, as intended.
//
// Stability (phase-b-spec §4's h < (2/ωₙ)(√(1+ζ²) − ζ)): 114 ms front /
// 98 ms rear against the 1/240 s substep — 27x / 24x margin, MORE than the
// GT3 numbers had. No substep change needed.
struct SuspensionParams {
    float fnFrontHz      = 1.8f;
    float zetaFront      = 0.45f;
    float fnRearHz       = 2.0f;
    float zetaRear       = 0.50f;
    float travelMaxM     = 0.045f;  // ±45 mm bump/rebound — estimate, tunable
    float unsprungMassKg = 42.f;    // per corner (AWD front driveshafts + bigger brakes)
};

// ── Anti-roll bars (Phase 1 §4.3) ─────────────────────────────────────
// NOT a rigid body and not a separate integrator: an ARB is a torsion spring
// between the two corners of one axle, so its force on corner i is
//   F_arb,i = -k_arb * (x_i - x_j)
// which folds exactly into the existing spring term,
//   -k(x_i - x_t) - k_arb(x_i - x_j) = -k(x_i - [x_t - (k_arb/k)(x_i - x_j)]),
// i.e. an equivalent TARGET SHIFT. So the ARB costs one subtraction per
// corner and adds zero state (see arb_target_shift_m).
//
// The ratio (not an absolute rate) is what's stored, for the same reason
// SuspensionParams stores fn: it is dimensionless, so it survives a mass or
// spring-rate retarget untouched. Road cars typically take 30-45% of front
// roll stiffness and 15-25% of rear from the bar; a front-biased bar split is
// also what a rear-engine car wants for balance.
//
// Effect on stability: in the anti-symmetric (roll) mode the effective rate
// becomes k(1 + 2ρ), so ωₙ rises by √1.7 = 1.30x front. The §4 bound at the
// resulting 2.35 Hz / ζ_eff 0.35 is still 97 ms — 23x the 1/240 s substep.
struct AntiRollParams {
    float frontRatio = 0.35f;  // k_arb / k_spring, front axle
    float rearRatio  = 0.22f;  // rear axle
};

// Equivalent target shift (m) that reproduces this corner's ARB force.
// Anti-symmetric by construction: the two corners of an axle get equal and
// opposite shifts, so the bar can only resist ROLL — it applies nothing at all
// when both wheels move together (x_self == x_other ⇒ 0), which is exactly the
// defining property of an anti-roll bar.
inline float arb_target_shift_m(float x_self_m, float x_other_m, float ratio) {
    return -ratio * (x_self_m - x_other_m);
}

// ── Camber / toe gain curves (Phase 1 §4.3) ───────────────────────────
// Rather than solving the double-wishbone-front / multi-link-rear linkage as
// constrained rigid bodies every frame (Project Chrono was considered and
// deliberately rejected — spec §3.4), the alignment change with suspension
// travel is shipped as the polynomial the linkage geometry produces:
//
//   γ(δ) = γ₀ + k_γ·δ + k_γ2·δ²      (camber, deg)
//   τ(δ) = τ₀ + k_τ·δ                (toe, deg)
//
// δ is travel from ride height in METRES, POSITIVE IN BUMP (compression) —
// matching CornerSpring::x_m's sign convention (+x = corner sits higher than
// the already-Y-snapped body, which is what a raised road surface under that
// corner does). Angles in degrees, with the automotive sign conventions:
//   camber < 0 ⇒ wheel top leans IN toward the car centreline
//   toe    > 0 ⇒ toe-IN (leading edge of the wheel points at the centreline)
//
// SOURCES. Static alignment is the published 992 Turbo S factory range
// (≈ -0.7° front / -1.4° rear camber, a few hundredths of a degree of toe-in
// front, more at the rear). The GAIN constants are ENGINEERING ESTIMATES from
// the characteristic behaviour of these two linkage families rather than from
// digitised hardpoints — the GLB has no suspension linkage geometry in it to
// measure (the wheels are rigid submeshes on recovered pivot frames), so there
// is nothing in-asset to derive from and no public hardpoint table for the 992:
//   * Camber gain: a double-wishbone front gains ≈0.5°/25 mm of bump and a
//     multi-link rear slightly more (≈0.7°/25 mm) ⇒ -22 and -28 °/m.
//   * The δ² term makes the gain PROGRESSIVE in bump and flatter in droop
//     (dγ/dδ = k_γ + 2k_γ2·δ), which is the shape every real camber curve has;
//     it contributes -0.12° of the front's -1.11° at full 45 mm bump.
//   * Toe gain (bump steer) is deliberately small and toe-IN in bump, which is
//     the stabilising direction: +2 °/m front, +6 °/m rear (a multi-link rear
//     is designed with more, for stability under load).
struct SuspensionGeometry {
    float camberFrontDeg = -0.70f;  // static camber, front
    float camberRearDeg  = -1.40f;  // static camber, rear
    float toeFrontDeg    = 0.05f;   // static toe-in per wheel, front
    float toeRearDeg     = 0.15f;   // static toe-in per wheel, rear

    float camberGainFrontDegPerM   = -22.f;  // dγ/dδ, front
    float camberGainRearDegPerM    = -28.f;  // dγ/dδ, rear
    float camberCurveFrontDegPerM2 = -60.f;  // d²γ/dδ², front (progressive in bump)
    float camberCurveRearDegPerM2  = -80.f;  // d²γ/dδ², rear

    float toeGainFrontDegPerM = 2.f;  // dτ/dδ, front (toe-in in bump)
    float toeGainRearDegPerM  = 6.f;  // dτ/dδ, rear
};

struct CamberToe {
    float camberDeg = 0.f;
    float toeDeg    = 0.f;
};

// ABSOLUTE alignment at travel x_m — the spec's γ(δ), τ(δ) as written.
inline CamberToe camber_toe_from_travel(float x_m, bool front, const SuspensionGeometry& g) {
    CamberToe out;
    if (front) {
        out.camberDeg = g.camberFrontDeg + g.camberGainFrontDegPerM * x_m + g.camberCurveFrontDegPerM2 * x_m * x_m;
        out.toeDeg    = g.toeFrontDeg + g.toeGainFrontDegPerM * x_m;
    } else {
        out.camberDeg = g.camberRearDeg + g.camberGainRearDegPerM * x_m + g.camberCurveRearDegPerM2 * x_m * x_m;
        out.toeDeg    = g.toeRearDeg + g.toeGainRearDegPerM * x_m;
    }
    return out;
}

// DELTA from the rest pose — this is what the draw path wants, because the
// STATIC camber and toe are already baked into the asset (WheelFrame::frame
// carries the as-modelled camber tilt, ≈1° front / 2° rear). Applying the
// absolute angle there would double-count it. HARD INVARIANT: at x_m == 0 both
// components are exactly 0, so the rest pose is bit-identical to pre-Phase-1
// and the added draw-matrix rotation collapses to the identity.
inline CamberToe camber_toe_delta_from_travel(float x_m, bool front, const SuspensionGeometry& g) {
    const CamberToe now  = camber_toe_from_travel(x_m, front, g);
    const CamberToe rest = camber_toe_from_travel(0.f, front, g);
    return CamberToe{now.camberDeg - rest.camberDeg, now.toeDeg - rest.toeDeg};
}

// k = m*omega_n^2, c = 2*zeta*sqrt(k*m) — derive spring/damper rate from the
// design inputs (ride frequency, damping ratio) rather than exposing k/c
// directly, so tuning stays in interpretable, checkable-against-§4 units.
inline void spring_from_ride_freq(float m_kg, float fn_hz, float zeta, float& k_n_per_m, float& c_ns_per_m) {
    const float omega_n = kTwoPi * fn_hz;
    k_n_per_m           = m_kg * omega_n * omega_n;
    c_ns_per_m          = 2.f * zeta * std::sqrt(k_n_per_m * m_kg);
}

// One semi-implicit Euler step of a quarter-car corner. Contract: stable iff
// h < (2/omega_n)*(sqrt(1+zeta^2) - zeta). The caller (suspension_advance)
// guarantees this; this function does not check it.
inline CornerSpring suspension_step(CornerSpring s, float x_target_m, float k_n_per_m, float c_ns_per_m, float m_kg,
                                    float h_s, float travel_max_m) {
    const float a = -(k_n_per_m / m_kg) * (s.x_m - x_target_m) - (c_ns_per_m / m_kg) * s.v_mps;
    s.v_mps += a * h_s;      // velocity FIRST
    s.x_m += s.v_mps * h_s;  // position with the NEW velocity
    if (s.x_m > travel_max_m) {
        s.x_m   = travel_max_m;
        s.v_mps = std::min(s.v_mps, 0.f);
    }
    if (s.x_m < -travel_max_m) {
        s.x_m   = -travel_max_m;
        s.v_mps = std::max(s.v_mps, 0.f);
    }
    return s;
}

// Convert a corner's suspension deflection into the Y translation the draw-matrix
// composition uses. This is the IDENTITY, in METERS — deliberately not a world-unit
// conversion: the translation is composed AFTER car_model, which already carries the
// entity scale (1000), i.e. the same mesh space kSeatEye is authored in (App.cpp).
// Named rather than inlined at the call site so the unit contract is stated once and
// pinned by a test — multiplying by util::units::kWuPerMetre (utils/units/Units.h) here scales by 1000 TWICE and
// lifts each wheel ~14 m off the car on ordinary road crown.
inline float suspension_draw_offset_m(float x_m) {
    return x_m;
}

// Adaptive fixed-h substepping — h_used <= h_target always satisfies the §4
// stability bound. No persistent accumulator: there is no replay/determinism
// requirement that would justify the extra hidden state, and h_used <=
// h_target already guarantees stability every call.
inline CornerSpring suspension_advance(CornerSpring s, float x_target_m, float k_n_per_m, float c_ns_per_m, float m_kg,
                                       float dt_s, float h_target_s, float travel_max_m, int max_substeps = 32) {
    dt_s          = std::min(dt_s, static_cast<float>(max_substeps) * h_target_s);
    const int   N = std::max(1, static_cast<int>(std::ceil(dt_s / h_target_s)));
    const float h = dt_s / static_cast<float>(N);
    for (int i = 0; i < N; ++i)
        s = suspension_step(s, x_target_m, k_n_per_m, c_ns_per_m, m_kg, h, travel_max_m);
    return s;
}

}  // namespace swish
