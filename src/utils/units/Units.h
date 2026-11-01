#pragma once

// ── Units.h ──────────────────────────────────────────────────────────
// Purpose   : the ONE definition of the world-unit scale (1 m = 1000 WU) and
//             the feet/metre conversions; replaces the six private copies
//             (kFt, WORLD_SCALE, kWorldUnitsPerMeter, ...). Also hosts the
//             shared pi constants so no header re-types the literal.
// Units     : lengths in WU unless a name says _m / _ft; angles in RADIANS.
// Complexity: O(1), constexpr, no allocation.
// Precision : ft(x) multiplies by the precomputed float kWuPerFt, i.e. the
//             same single rounding as the legacy `x * kFt` with
//             kFt = 0.3048f * 1000.0f. Do NOT write x * 0.3048f * 1000.0f
//             (different rounding order, breaks byte-identical goldens).
// Example   : constexpr float lane = swish::util::units::ft(13.0f);  // 3962.4 WU
//             const float v_mps = wu_per_s / swish::util::units::kWuPerMetre;
// See also  : scene/RoadScene/RoadParams.h, docs/GLOSSARY.md (WU)
// Test      : tests/utils/test_units_Units.cpp (+ the static_asserts below)

#include "Quantity.h"

#include <numbers>

namespace swish::util::units {

// ── Length ────────────────────────────────────────────────────────────
inline constexpr float kWuPerMetre  = 1000.0f;  // the ONE definition: 1 scene-metre = 1000 WU
inline constexpr float kMetresPerFt = 0.3048f;
inline constexpr float kWuPerFt     = kMetresPerFt * kWuPerMetre;  // == legacy 0.3048f * 1000.0f (304.8 WU)

constexpr float m(float metres) noexcept { return metres * kWuPerMetre; }
constexpr float ft(float feet) noexcept { return feet * kWuPerFt; }
constexpr float to_m(float wu) noexcept { return wu / kWuPerMetre; }
constexpr float to_ft(float wu) noexcept { return wu / kWuPerFt; }

// ── Speed ─────────────────────────────────────────────────────────────
inline constexpr float kKmhPerMps = 3.6f;  // 3600 s/h ÷ 1000 m/km

// ── Angle ─────────────────────────────────────────────────────────────
inline constexpr float kPi    = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// ── Typed conversions (the ONLY place a Quantity changes tag) ─────────
// Each reuses the raw expression above so rounding is identical to the untyped call sites.
// Degree/radian factors are the exact double literals glm::radians / glm::degrees multiply by.
inline constexpr float kRadPerDeg = static_cast<float>(0.01745329251994329576923690768489);
inline constexpr float kDegPerRad = static_cast<float>(57.295779513082320876798154814105);

[[nodiscard]] constexpr Wu      to_wu(Metres x) noexcept { return Wu{m(x.value())}; }
[[nodiscard]] constexpr Metres  to_metres(Wu x) noexcept { return Metres{to_m(x.value())}; }
[[nodiscard]] constexpr Radians to_radians(Degrees x) noexcept { return Radians{x.value() * kRadPerDeg}; }
[[nodiscard]] constexpr Degrees to_degrees(Radians x) noexcept { return Degrees{x.value() * kDegPerRad}; }

// ── Compile-time guards ───────────────────────────────────────────────
static_assert(kWuPerMetre == 1000.0f);
static_assert(m(1.0f) == 1000.0f);
static_assert(kWuPerFt == 0.3048f * 1000.0f, "must stay bit-identical to the legacy kFt");
static_assert(ft(1.0f) > 304.79f && ft(1.0f) < 304.81f);
static_assert(ft(13.0f) == 13.0f * (0.3048f * 1000.0f), "same rounding as legacy 13.0f * kFt");
static_assert(to_m(m(2.45f)) == 2.45f);
// Bit-identical to the literals these replace (CarPhysics.h kEnginePi, WheelKinematics.h kTwoPi, Constants.h PI).
static_assert(kPi == 3.14159265358979323846f);
static_assert(kTwoPi == 6.28318530717958647692f);

}  // namespace swish::util::units
