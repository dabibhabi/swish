#pragma once

// GPU particle rain, CPU reference (docs/realism-learning-plan.md, modules M5 and M6). The particle
// layout the compute shader will use, plus pure versions of each per-particle rule so
// tests/test_rain_particle.cpp can pin them before they are ported to GLSL. Vulkan-free.
//
// LEARNING SCAFFOLD: the functions are stubs returning placeholders. The `[.learning]` tests describe the
// intended behaviour; fill the bodies until `swish_tests "[learning][rain_particle]"` passes.
// Units: positions in WU, velocities in WU/s, diameters in mm (RainPhysics.h), times in s.

#include "RainPhysics.h"
#include "utils/types/Types.h"
#include "utils/units/Units.h"

#include <cmath>

namespace swish {

// One drop as stored in the SSBO (std430). Mirror this exactly in the compute and vertex shaders.
// Positions are CAMERA-RELATIVE: the corridor's origin rebase shifts world space, and a drop that
// lives for seconds would jump with it (SpraySystem's 1.5 s particles hide that; rain cannot).
struct RainParticle {
    Vec4 posLife;  // xyz = position relative to the camera (WU), w = remaining life (s)
    Vec4 velSize;  // xyz = velocity (WU/s), w = streak length (WU)
    Vec4 seedDia;  // x = per-drop seed [0,1), y = diameter (mm), z = oscillation phase, w = unused
};
static_assert(sizeof(RainParticle) == 48, "mirror the std430 RainParticle in the rain compute shader");

// Velocity a drop settles to: the horizontal wind plus its Gunn-Kinzer terminal fall speed (down = -Y).
[[nodiscard]] inline Vec3 rain_target_velocity([[maybe_unused]] Vec3 wind, [[maybe_unused]] float diameterMm) {
    // TODO(learning M5): use gunn_kinzer_speed (m/s) and util::units::kWuPerMetre; ignore wind.y.
    return Vec3(0.f);
}

// Exponential relaxation of v toward target with time constant tau (exact for any dt, never overshoots):
//   v' = v + (target - v) * (1 - exp(-dt / tau))
[[nodiscard]] inline Vec3 rain_relax_velocity(Vec3 v, [[maybe_unused]] Vec3 target, [[maybe_unused]] float dt,
                                              [[maybe_unused]] float tau) {
    // TODO(learning M5): implement the formula above (docs M2 exercise 5 shows why not explicit Euler).
    return v;
}

// Wrap a camera-relative position into the box [-halfExtent, halfExtent) on every axis, so drops that
// leave one face re-enter through the opposite one (rain.vert does the same with mod()).
[[nodiscard]] inline Vec3 rain_wrap_camera_relative(Vec3 rel, [[maybe_unused]] float halfExtent) {
    // TODO(learning M5): per axis, x - 2h * floor((x + h) / (2h)).
    return rel;
}

// A drop respawns when its life runs out or it has fallen through the road surface.
[[nodiscard]] inline bool rain_should_respawn([[maybe_unused]] float life, [[maybe_unused]] float worldY,
                                              [[maybe_unused]] float roadY) {
    // TODO(learning M5)
    return false;
}

// M6: does the step p0 -> p1 cross the plane (point q, normal n)? On a crossing, t in [0,1] is where
// along the step it happened (hit = p0 + t * (p1 - p0)). A step parallel to the plane never crosses.
//   t = dot(n, q - p0) / dot(n, p1 - p0)
[[nodiscard]] inline bool rain_crosses_plane([[maybe_unused]] Vec3 p0, [[maybe_unused]] Vec3 p1,
                                             [[maybe_unused]] Vec3 q, [[maybe_unused]] Vec3 n, float& t) {
    // TODO(learning M6): guard the parallel case, then accept only 0 <= t <= 1.
    t = 0.f;
    return false;
}

}  // namespace swish
