#pragma once

// Pure road-wheel kinematics (SI units), independent of the renderer, GLFW,
// and GLM so it can be unit-tested without a GPU — same contract as
// CarPhysics.h. CarEntity converts world units to/from SI at the boundary.

#include <cmath>

namespace swish {

inline constexpr float kTwoPi = 6.28318530717958647692f;

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

}  // namespace swish
