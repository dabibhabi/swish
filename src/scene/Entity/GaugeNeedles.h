#pragma once

// Purpose:    The rigged car's two dial needles (tasks/todo.md "Rigged Porsche" S1c): dial calibration
//             (tach linear fit, speedo lookup table), a critically damped needle follower, the startup
//             sweep, and the needle's draw matrix. Pure, GPU/GLFW-free; CarEntity owns the state.
// Units:      dial angles in DEGREES, theta = clockwise from 12 o'clock as seen by the driver; rpm; km/h;
//             seconds. The values below were measured off the painted faces in the GLB atlas.
// Complexity: O(1) per call (the LUT walk is O(kSpeedoLutMax)), one exp() per follower step.
// Example:    GaugeState g;  g = gauges_step(g, params, rpm, kmh, dt);
//             model = car_model * needle_pose(sm.rig_pivot_frame, sm.rig_part, g.rpm.pos, params.rpmRestDeg);
// See also:   CarRig.h (rig_part_local_axis), CarEntity::update_gauges, config/car.toml [gauges] (the defaults),
//             CarConfig (TOML reader), debug/Tunables.def group "gauges".
// Test:       tests/test_gauge_needles.cpp ([gauges])

#include "CarRig.h"  // rig_part_local_axis
#include "utils/types/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace swish {

// ── Calibration ───────────────────────────────────────────────────────
inline constexpr int kSpeedoLutMax = 16;

// Piecewise-linear km/h -> theta table; kmh strictly increasing, theta non-decreasing.
struct SpeedoLut {
    std::array<float, kSpeedoLutMax> kmh{};
    std::array<float, kSpeedoLutMax> deg{};
    int                              count = 0;
};

// The 993 face: 0-300 km/h, non-linear. 0 is extrapolated (the face prints no 0); the rest are marks.
inline constexpr SpeedoLut kSpeedoLut993{
    {0.f, 10.f, 20.f, 60.f, 100.f, 140.f, 180.f, 220.f, 260.f, 300.f},
    {-141.f, -133.f, -125.2f, -90.9f, -55.9f, -18.9f, 19.1f, 58.5f, 98.5f, 138.1f},
    10,
};

struct GaugeParams {
    // Tach ("1/min x1000"): theta = offset + slope * rpm/1000, clamped to [min, max] rpm.
    // Fit to the 1k (-109.0), 4k (-0.4) and 7k (+105.5) numerals; the red zone runs 6800-7600.
    float tachOffsetDeg  = -145.f;
    float tachDegPerKrpm = 36.f;
    float tachMinRpm     = 0.f;
    float tachMaxRpm     = 7600.f;  // last printed (red) tick
    // Speedo: the LUT's first/last km/h are the clamp range.
    SpeedoLut speedo = kSpeedoLut993;
    // Rest pose of the needle meshes as authored (the angle the mesh points at with no rotation).
    float rpmRestDeg   = -144.f;
    float speedRestDeg = -151.f;
    // Follower time constant (omega_n = 1 / tau) and the startup sweep (up and back) length.
    float needleTauS     = 0.080f;
    float sweepDurationS = 1.2f;
};

// The C++ fallback when config/car.toml [gauges] is missing; debug/Tunables.def defaults read it too.
inline constexpr GaugeParams kGaugeDefaults{};

inline float tach_angle_deg(const GaugeParams& p, float rpm) {
    const float r = std::clamp(rpm, p.tachMinRpm, p.tachMaxRpm);
    return p.tachOffsetDeg + p.tachDegPerKrpm * (r / 1000.f);
}

// Piecewise-linear lookup, clamped to the table's ends. A table with < 2 rows returns its only angle (or 0).
inline float speedo_angle_deg(const SpeedoLut& lut, float kmh) {
    if (lut.count <= 0) return 0.f;
    const auto n = static_cast<size_t>(lut.count);
    if (n == 1 || kmh <= lut.kmh[0]) return lut.deg[0];
    if (kmh >= lut.kmh[n - 1]) return lut.deg[n - 1];
    size_t i = 1;
    while (kmh > lut.kmh[i]) i++;
    if (kmh == lut.kmh[i]) return lut.deg[i];  // anchors map exactly (no a + 1*(b - a) rounding)
    const float t = (kmh - lut.kmh[i - 1]) / (lut.kmh[i] - lut.kmh[i - 1]);
    return lut.deg[i - 1] + t * (lut.deg[i] - lut.deg[i - 1]);
}

// A usable table: 2..kSpeedoLutMax rows, km/h strictly increasing, angles non-decreasing.
inline bool speedo_lut_valid(const SpeedoLut& lut) {
    if (lut.count < 2 || lut.count > kSpeedoLutMax) return false;
    for (size_t i = 1; i < static_cast<size_t>(lut.count); i++)
        if (!(lut.kmh[i] > lut.kmh[i - 1]) || lut.deg[i] < lut.deg[i - 1]) return false;
    return true;
}

inline bool gauge_params_valid(const GaugeParams& p) {
    return speedo_lut_valid(p.speedo) && p.tachMaxRpm > p.tachMinRpm && p.tachDegPerKrpm > 0.f && p.needleTauS > 0.f &&
           p.sweepDurationS >= 0.f;
}

// ── Damped follower ───────────────────────────────────────────────────
// x'' = -2 w x' - w^2 (x - target): critically damped, w = omega_n (rad/s). Stepped with the EXACT
// solution for a target held over dt, so it is frame-rate independent and stable at any dt:
//   e = x - target, k = v + w e,  e(dt) = (e + k dt) e^{-w dt},  v(dt) = (v - w k dt) e^{-w dt}.
// From rest, e(t) = e0 (1 + w t) e^{-w t} keeps its sign: no overshoot.
struct NeedleFollower {
    float pos = 0.f;  // deg
    float vel = 0.f;  // deg/s
};

inline NeedleFollower needle_follow(NeedleFollower s, float target, float dt, float omega_n) {
    if (dt <= 0.f || omega_n <= 0.f) return s;
    const float e  = s.pos - target;
    const float k  = s.vel + omega_n * e;
    const float ex = std::exp(-omega_n * dt);
    return {target + (e + k * dt) * ex, (s.vel - omega_n * k * dt) * ex};
}

// ── Startup sweep ─────────────────────────────────────────────────────
// Fraction of full scale at time t into a sweep of `duration`: smoothstep up over the first half,
// smoothstep back down over the second; 0 outside [0, duration].
inline float gauge_sweep_fraction(float t, float duration) {
    if (duration <= 0.f || t <= 0.f || t >= duration) return 0.f;
    const float half = 0.5f * duration;
    const float u    = t < half ? t / half : (duration - t) / half;
    return u * u * (3.f - 2.f * u);
}

// ── Both needles ──────────────────────────────────────────────────────
struct GaugeState {
    NeedleFollower rpm;
    NeedleFollower speed;
    float          sweepT  = 0.f;  // s since the car loaded
    bool           started = false;
};

// One frame: the startup sweep (needles slaved to it) until sweepDurationS has passed, then each needle
// follows its live dial angle. rpm and kmh are the live readings (kmh >= 0).
inline GaugeState gauges_step(GaugeState s, const GaugeParams& p, float rpm, float kmh, float dt) {
    const float rpm0 = tach_angle_deg(p, p.tachMinRpm), rpm1 = tach_angle_deg(p, p.tachMaxRpm);
    const float spd0 = speedo_angle_deg(p.speedo, -INFINITY), spd1 = speedo_angle_deg(p.speedo, INFINITY);
    if (!s.started) {
        s.rpm     = {rpm0, 0.f};
        s.speed   = {spd0, 0.f};
        s.started = true;
    }
    if (s.sweepT < p.sweepDurationS) {
        s.sweepT += std::max(dt, 0.f);
        const float f = gauge_sweep_fraction(s.sweepT, p.sweepDurationS);
        s.rpm         = {rpm0 + f * (rpm1 - rpm0), 0.f};
        s.speed       = {spd0 + f * (spd1 - spd0), 0.f};
        return s;
    }
    const float w = 1.f / p.needleTauS;
    s.rpm         = needle_follow(s.rpm, tach_angle_deg(p, rpm), dt, w);
    s.speed       = needle_follow(s.speed, speedo_angle_deg(p.speedo, kmh), dt, w);
    return s;
}

// ── Draw matrix ───────────────────────────────────────────────────────
// Clockwise-for-the-driver sign of a rotation about rig_part_local_axis (local -Z, which points at the
// driver). A right-handed turn about an axis that points at the viewer is counter-clockwise to them,
// so clockwise theta is a NEGATIVE angle about -Z (checked on screen: theta = +90 points at 3 o'clock).
inline constexpr float kNeedleClockwiseSign = -1.f;

// Car-space pose of a needle piece showing dial angle theta_deg: pivot * R_axis(sign*(theta - rest)) * pivot^-1.
inline Mat4 needle_pose(const Mat4& pivot, RigPart part, float theta_deg, float rest_deg) {
    const float a = kNeedleClockwiseSign * glm::radians(theta_deg - rest_deg);
    return pivot * glm::rotate(Mat4(1.f), a, rig_part_local_axis(part)) * glm::inverse(pivot);
}

}  // namespace swish
