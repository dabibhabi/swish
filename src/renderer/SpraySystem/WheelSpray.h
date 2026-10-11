#pragma once

// ══════════════════════════════════════════════════════════════════════
// WheelSpray — pure, GPU-free per-wheel spray emitter model (STUB).
//
// Purpose : turn the four wheel contact states + road wetness + standing water into up to four `SprayEmitter`s that
//           SpraySystem can consume (SWISH_DEBUG_UI path only for now).
// Units   : WU (1 m = 1000 WU, utils/units/Units.h); velocities WU/s; angles radians; wetness / standing water / slip
//           multiplier inputs are unitless.
// Test    : tests/test_wheel_spray.cpp
//
// What this stub does: emitRate = speedGate * wetness * (kBase + (1-kBase)*standingWater) * slipMul, clamped [0,1].
//   Zero when dry (wetness 0) or stationary (speed below kMinSpeed); monotonic (non-decreasing) in wetness, standing
//   water, speed and slipMul. Cone widens with standing water. Only emitters with rate > 0 are returned (compacted).
//
// TODO(stub): what the full version must do:
//   - Use the real contact patch (tyre width, wheel radius, tyre rotation) instead of the wheel centre position, and
//     throw the plume tangentially off the tyre (v_wheel_surface), not along the car velocity.
//   - Per-wheel puddle depth from the road (sampled standing water under each wheel), not one global scalar.
//   - Front wheels throw forward/outward bow-wave sheets; rears throw the trailing rooster-tail; hydroplaning
//     (standing water * speed above a threshold) should switch to a high-volume, tall, wide cone.
//   - Feed the cone half angle and a size/velocity scale to the GPU sim (needs a spray_sim.comp change, which also
//     changes the release .spv hashes: do it deliberately with a golden update + changelog).
//   - slipSprayMul comes from scene/Entity/SlipVisuals.h (another agent): 1.0 = rolling, >1 = wheelspin/lockup.
// ══════════════════════════════════════════════════════════════════════

#include "utils/types/Types.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace swish {

// One spray source. Mirrors what SpraySystem needs per spawn origin.
struct SprayEmitter {
    Vec3  pos           = Vec3(0.0f);  // world position (WU)
    Vec3  vel           = Vec3(0.0f);  // emitter velocity (WU/s): the wheel's, the plume is thrown relative to it
    float emitRate      = 0.0f;        // [0,1]: 1 = full legacy single-emitter spawn probability share
    float coneHalfAngle = 0.0f;        // radians; plume half angle (stub: SpraySystem only maps it to lateral spread)
    float sizeScale     = 1.0f;        // billboard size multiplier (stub: unused by the sim)
};

inline constexpr std::uint32_t kMaxWheelEmitters = 4;

// One wheel's input state.
struct WheelSprayInput {
    Vec3  pos          = Vec3(0.0f);  // world position (WU)
    Vec3  vel          = Vec3(0.0f);  // world velocity (WU/s)
    float slipSprayMul = 1.0f;        // per-wheel slip multiplier (SlipVisuals); 1 = rolling
};

struct WheelSprayParams {
    float refSpeed     = util::units::m(20.0f);  // WU/s at which the speed gate saturates (~45 mph)
    float minSpeed     = util::units::m(0.5f);   // WU/s below which a wheel emits nothing
    float standingBase = 0.5f;                   // share of the rate present on a merely wet road (no standing water)
    float coneDry      = 0.35f;                  // rad half angle at standing water 0
    float coneWet      = 0.60f;                  // rad half angle at standing water 1
    float maxSlipMul   = 4.0f;                   // clamp on the slip multiplier input
};

struct WheelSprayResult {
    std::array<SprayEmitter, kMaxWheelEmitters> emitters{};
    std::uint32_t                               count = 0;  // active (rate > 0) emitters, compacted to the front
};

// Per-wheel emit rate in [0,1].
inline float wheel_spray_rate(const WheelSprayInput& w, float wetness, float standingWater,
                              const WheelSprayParams& p = {}) {
    const float wet      = std::clamp(wetness, 0.0f, 1.0f);
    const float standing = std::clamp(standingWater, 0.0f, 1.0f);
    const float speed    = glm::length(w.vel);
    if (wet <= 0.0f || speed <= p.minSpeed)
        return 0.0f;
    const float speedGate = std::clamp((speed - p.minSpeed) / std::max(p.refSpeed - p.minSpeed, 1.0f), 0.0f, 1.0f);
    const float slip      = std::clamp(w.slipSprayMul, 0.0f, p.maxSlipMul);
    return std::clamp(speedGate * wet * (p.standingBase + (1.0f - p.standingBase) * standing) * slip, 0.0f, 1.0f);
}

inline WheelSprayResult compute_wheel_spray(const std::array<WheelSprayInput, kMaxWheelEmitters>& wheels, float wetness,
                                            float standingWater, const WheelSprayParams& p = {}) {
    WheelSprayResult out;
    const float      standing = std::clamp(standingWater, 0.0f, 1.0f);
    for (const WheelSprayInput& w : wheels) {
        const float rate = wheel_spray_rate(w, wetness, standingWater, p);
        if (rate <= 0.0f)
            continue;
        SprayEmitter& e = out.emitters[out.count++];
        e.pos           = w.pos;
        e.vel           = w.vel;
        e.emitRate      = rate;
        e.coneHalfAngle = p.coneDry + (p.coneWet - p.coneDry) * standing;
    }
    return out;
}

}  // namespace swish
