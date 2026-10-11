#pragma once

// Purpose:    Steering force-feedback target from the bicycle tire model: front-axle self-aligning
//             torque Mz = -Fy * pneumatic_trail. Pure math plus a sink interface; NO device output.
// Units:      SI (N, m, rad, m/s, N*m). Steer/slip right-positive, like CarPhysics.h.
// Complexity: O(1).
// Example:    FfbOutput o = self_aligning_torque(vx, vl, r, delta, CarParams{}, TireParams{});
//             sink.submit(o.normalized, dt);
// See also:   scene/Entity/Bicycle.h (axle_lateral_forces).
// Test:       tests/test_input.cpp
//
// TODO(stub): full model: per-wheel (not per-axle) Fy with load transfer, mechanical trail/caster,
//             scrub-radius and kingpin terms, tire relaxation lag, road-surface/curb texture,
//             damping/friction/inertia effects, wet-grip lightening (alpha_sat already follows mu).

#include "CarPhysics.h"

#include <algorithm>
#include <cmath>

namespace swish {

struct FfbParams {
    float trail0_m          = 0.035f;  // pneumatic trail at zero slip (m)  TODO(stub): tune/measure
    float max_torque_nm     = 6.f;     // steering-column torque mapped to normalized 1.0 (N*m)
    float full_effect_speed = 3.f;     // m/s: below this the effect fades in (no parking-lot slam)
};

struct FfbOutput {
    float torque_nm  = 0.f;  // self-aligning torque on the steering, right-positive steer axis
    float normalized = 0.f;  // torque_nm / max_torque_nm clamped to [-1,1]; opposes steer when
                             // sign(normalized) != sign(steer)
};

// Front-axle self-aligning torque. Trail shrinks linearly to 0 at the slip angle where the axle
// saturates (alpha_sat = mu*Fz / C_alpha), the standard pneumatic-trail collapse that makes the
// wheel go light at the limit.
inline FfbOutput self_aligning_torque(float vx, float vl, float r, float delta, const CarParams& p, const TireParams& t,
                                      const FfbParams& f = FfbParams{}) {
    const float vxs    = std::max(std::fabs(vx), 1.0f);
    const float sgn    = (vx >= 0.0f) ? 1.0f : -1.0f;
    const float alphaF = (vl + t.a * r) / vxs - sgn * delta;  // same as axle_lateral_forces

    const AxleLateralForces fy = axle_lateral_forces(vx, vl, r, delta, p, t);

    const float L        = t.a + t.b;
    const float capF     = p.tireMu * p.mass * p.gravity * t.b / L;
    const float alphaSat = capF / t.cAlphaFront;
    const float trail    = f.trail0_m * std::max(0.f, 1.f - std::fabs(alphaF) / alphaSat);

    const float fade = std::clamp(std::fabs(vx) / f.full_effect_speed, 0.f, 1.f);

    FfbOutput o;
    o.torque_nm  = -fy.front * trail * fade;
    o.normalized = std::clamp(o.torque_nm / f.max_torque_nm, -1.f, 1.f);
    return o;
}

// Where a force-feedback command goes. Real wheel drivers implement this later.
class IForceFeedbackSink {
public:
    virtual ~IForceFeedbackSink()                          = default;
    virtual void submit(float normalized_torque, float dt) = 0;
};

class NullForceFeedbackSink final : public IForceFeedbackSink {
public:
    void submit(float, float) override {}
};

}  // namespace swish
