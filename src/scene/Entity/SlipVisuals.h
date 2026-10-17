#pragma once

// Wheel Phase C visual hooks: per-wheel slip / skid / spray-boost summary.
// Pure (SI units, no renderer/GLFW/GLM); derived read-only from EXISTING car
// state, never fed back into the physics step.
//
// STUB QUALITY. The interface is the final one; the estimates behind it are
// deliberately minimal. What the full version must do
// (docs/wheels/README.md Phase C, docs/lessons-from-chrono/ tire models):
//   - SLIP ANGLE: per-WHEEL, not per-axle. Needs lateral load transfer and
//     per-wheel contact velocity (yaw-rate x track half-width), plus steer
//     (Ackermann) per front wheel. Here both wheels of an axle share the
//     bicycle-model axle slip angle (same formula as axle_lateral_forces).
//   - SLIP RATIO: needs real wheel angular speed (a driven wheel-spin state
//     with brake lockup and driveline inertia):
//         kappa = (omega * r_eff - v_x) / max(|v_x|, eps).
//     The car has no wheel-speed state, so here it is a PROXY: tyre force
//     utilisation |F_x| / (mu * F_z) scaled to kSlipRatioAtLimit. Signed
//     like F_x (positive = driving, negative = braking).
//   - SPRAY: the boost is a plain multiplier for SpraySystem to apply to its
//     emit rate; the full version should also scale droplet velocity and
//     depend on road film depth (WetGrip.h filmOverTread), not just slip.
//   - SKID: boolean now; the full version wants an intensity (mark opacity)
//     and per-wheel hysteresis so marks do not flicker at the threshold.
//
// Identity contract: at rest, in pure rolling, or with no slip every wheel is
// {slipRatio ~ 0, slipAngle ~ 0, sprayBoost == 1.0 exactly, skid == false}.
// Everything is symmetric in the sign of slip (only |.| drives boost/skid).

#include "CarParams.h"
#include "Tire.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace swish {

// Wheel order everywhere in Phase C: 0 FL, 1 FR, 2 BL, 3 BR (matches WheelFrame[4]).
struct WheelSlipVisual {
    float slipRatio  = 0.f;  // signed, dimensionless (+ = driving, - = braking). PROXY, see above.
    float slipAngle  = 0.f;  // signed rad, right-positive (same convention as Bicycle.h)
    float sprayBoost = 1.f;  // >= 1, multiplier for spray emit rate
    bool  skid       = false;
};

// Tunables. Defaults are the intended shipped behaviour.
struct SlipVisualParams {
    static constexpr float kDegToRad = util::units::kPi / 180.0f;

    float minSpeed = 2.0f;  // m/s; below this: identity (no skid, no boost)

    // Skid triggers (strictly greater than).
    float skidSlipRatio = 0.20f;
    float skidSlipAngle = 8.0f * kDegToRad;

    // Spray boost ramps with smoothstep from `start` (boost 1) to `full` (boost sprayBoostMax).
    float ratioStart    = 0.08f;
    float ratioFull     = 0.30f;
    float angleStart    = 4.0f * kDegToRad;
    float angleFull     = 12.0f * kDegToRad;
    float sprayBoostMax = 3.0f;
    // Slip ratio the force-utilisation proxy reports at full utilisation (|F_x| = mu F_z).
    float slipRatioAtLimit = 0.25f;
};

// Raw SI inputs, all already available in CarEntity (see get_wheel_slip_visuals).
struct SlipInputs {
    float            vx    = 0.f;  // forward speed, m/s (negative = reversing)
    float            vl    = 0.f;  // lateral (rightward) velocity, m/s
    float            r     = 0.f;  // yaw rate, rad/s (right-positive)
    float            delta = 0.f;  // road-wheel steer, rad (right-positive)
    AxleLongitudinal fx{};         // signed per-axle longitudinal tyre force, N
    float            mu = 1.05f;   // effective tyre-road friction this frame
};

struct SlipAggregate {
    float maxAbsSlipRatio = 0.f;
    float maxAbsSlipAngle = 0.f;  // rad
    float maxSprayBoost   = 1.f;
    int   skidCount       = 0;  // wheels currently skidding, 0..4
    bool  anySkid         = false;
};

struct CarSlipVisuals {
    std::array<WheelSlipVisual, 4> wheel{};  // 0 FL, 1 FR, 2 BL, 3 BR
    SlipAggregate                  agg{};
};

namespace slip_detail {
inline float smoothstep01(float x, float lo, float hi) {
    const float t = std::clamp((x - lo) / std::max(hi - lo, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}  // namespace slip_detail

// One wheel from its slip ratio / angle and the car speed. Symmetric in the sign
// of both slips; exact identity below minSpeed or under the ramp start.
inline WheelSlipVisual make_wheel_slip_visual(float slipRatio, float slipAngle, float speed,
                                              const SlipVisualParams& sp = SlipVisualParams{}) {
    WheelSlipVisual w;
    w.slipRatio = slipRatio;
    w.slipAngle = slipAngle;
    if (std::fabs(speed) <= sp.minSpeed) return w;
    const float aR = std::fabs(slipRatio);
    const float aA = std::fabs(slipAngle);
    const float s  = std::max(slip_detail::smoothstep01(aR, sp.ratioStart, sp.ratioFull),
                              slip_detail::smoothstep01(aA, sp.angleStart, sp.angleFull));
    w.sprayBoost   = 1.0f + (std::max(sp.sprayBoostMax, 1.0f) - 1.0f) * s;
    w.skid         = aR > sp.skidSlipRatio || aA > sp.skidSlipAngle;
    return w;
}

// All four wheels + aggregate from the car's SI state. Does not modify anything.
// TODO(stub): per-wheel (not per-axle) slip, real wheel-speed slip ratio; see header comment.
inline CarSlipVisuals compute_slip_visuals(const SlipInputs& in, const CarParams& p, const TireParams& t,
                                           const SlipVisualParams& sp = SlipVisualParams{}) {
    // Axle slip angles: identical to axle_lateral_forces (Bicycle.h).
    const float vxs = std::max(std::fabs(in.vx), 1.0f);
    const float sgn = (in.vx >= 0.0f) ? 1.0f : -1.0f;
    const float aF  = (in.vl + t.a * in.r) / vxs - sgn * in.delta;
    const float aR  = (in.vl - t.b * in.r) / vxs;

    // Force-utilisation proxy for slip ratio. TODO(stub): replace with wheel-speed slip.
    const float    ax    = (in.fx.front + in.fx.rear) / p.mass;
    const AxleLoad load  = axle_loads_long(p, t, ax);
    auto           ratio = [&](float fx, float fz) {
        const float cap = std::max(in.mu * fz, 1.0f);
        return std::clamp(fx / cap, -1.0f, 1.0f) * sp.slipRatioAtLimit;
    };
    const float kF = ratio(in.fx.front, load.front);
    const float kR = ratio(in.fx.rear, load.rear);

    const float speed = std::sqrt(in.vx * in.vx + in.vl * in.vl);

    CarSlipVisuals out;
    out.wheel[0] = out.wheel[1] = make_wheel_slip_visual(kF, aF, speed, sp);
    out.wheel[2] = out.wheel[3] = make_wheel_slip_visual(kR, aR, speed, sp);
    for (const WheelSlipVisual& w : out.wheel) {
        out.agg.maxAbsSlipRatio = std::max(out.agg.maxAbsSlipRatio, std::fabs(w.slipRatio));
        out.agg.maxAbsSlipAngle = std::max(out.agg.maxAbsSlipAngle, std::fabs(w.slipAngle));
        out.agg.maxSprayBoost   = std::max(out.agg.maxSprayBoost, w.sprayBoost);
        out.agg.skidCount += w.skid ? 1 : 0;
    }
    out.agg.anySkid = out.agg.skidCount > 0;
    return out;
}

}  // namespace swish
