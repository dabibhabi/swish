#pragma once

// ── Photometry.h ─────────────────────────────────────────────────────
// Purpose   : the CPU side of physical light units: camera exposure from EV100, lumens to candela for
//             local lights, inverse-square falloff in metres, colour temperature, and the emissive
//             strength packing for the spare G-buffer alpha.
// Units     : illuminance in lux, luminance in cd/m² (nits), flux in lumens, intensity in candela,
//             distances passed in WU and converted with utils/units/Units.h; colour is linear RGB.
// Complexity: O(1), no allocation.
// Example   : namespace ul = swish::util::light;
//             float exposure = ul::exposure_from_ev100(15.0f);        // sunny noon
//             float cd       = ul::spot_candela_from_lumens(1000.0f); // one low-beam headlight
// See also  : docs/tutorials/lighting/L01-physical-units.md (formulas and where they plug in),
//             shaders/lighting.frag (the GLSL that must use the same formulas)
// Test      : tests/utils/test_light_Photometry.cpp
//
// LEARNING SCAFFOLD: every function below is a stub that returns a placeholder. The `[.learning]` tests
// describe the intended behaviour; fill the bodies until
// `build/tests/utils/swish_utils_tests "[learning][photometry]"` passes.

#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace swish::util::light {

// Brightest emissive the 8-bit alpha channel can store (cd/m²). A headlight lens is about 1e5 nits.
inline constexpr float kEmissiveMaxNits = 131071.0f;  // 2^17 - 1, so log2(1 + max) is exactly 17

// Multiplier applied to scene luminance before tone mapping (saturation-based sensor, ISO 100):
//   exposure = 1 / (1.2 * 2^ev100)
[[nodiscard]] inline float exposure_from_ev100(float ev100) { return 1.0f / (1.2f * std::exp2(ev100)); }

// EV100 a light meter reads for an average scene luminance (cd/m²), calibration constant K = 12.5:
//   ev100 = log2(luminance * 100 / 12.5)
[[nodiscard]] inline float ev100_from_luminance(float avgLuminance) {
    const float L = std::max(avgLuminance, 1e-4f);  // a black frame must not give -inf
    return std::log2(L * 100.0f / 12.5f);
}

// ── Pre-exposure (L1.3) ──────────────────────────────────────────────
// Lights are physical: a noon sun is 100,000 lux. The HDR target is half-float (largest finite value 65,504),
// so lighting.frag multiplies every light by kPreExposure before shading. The HDR image therefore holds
// luminance * kPreExposure, and the composite multiplies by exposure / kPreExposure to undo it.
// 1e-5 puts one "old scene unit" (sun = 1.0, the scale every pass was tuned in) at 100,000 lux, so the
// frame is unchanged. A fixed value is enough for daylight; night presets (L2) need it to follow the
// previous frame's exposure, because a dark road times 1e-5 is near the half-float floor.
inline constexpr float kLegacyUnitLux = 100000.0f;
inline constexpr float kPreExposure   = 1.0f / kLegacyUnitLux;

// What the composite multiplies the pre-exposed HDR image by, for a given EV100 and the pre-exposure the
// frame was shaded with.
[[nodiscard]] inline float hdr_exposure_from_ev100(float ev100, float preExposure = kPreExposure) {
    return exposure_from_ev100(ev100) / preExposure;
}

// ── Adaptive pre-exposure (lighting lab, night) ──────────────────────
// A fixed pre-exposure is exact for daylight but puts a dark scene near the half-float floor (a 0.2 nit road
// times 1e-5 is 2e-6), which bands. The adaptive form follows the exposure instead: it is chosen so the
// composite multiplier is always kShippedHdrExposure, which keeps the HDR image at the same comfortable scale
// at noon and at midnight. At the shipped EV100 (15.7144) it equals kPreExposure.
inline constexpr float     kShippedHdrExposure = 1.55f;
[[nodiscard]] inline float pre_exposure_for_ev100(float ev100) {
    return exposure_from_ev100(ev100) / kShippedHdrExposure;
}

// Luminous intensity (cd) of a point light that emits `lumens` equally in every direction:
//   I = lumens / (4 * pi)
[[nodiscard]] inline float point_candela_from_lumens(float lumens) { return lumens / (4.0f * units::kPi); }

// Luminous intensity (cd) of a spot light. Frostbite convention: the cone angle does not change the
// brightness, so narrowing a beam does not make it brighter:
//   I = lumens / pi
[[nodiscard]] inline float spot_candela_from_lumens(float lumens) { return lumens / units::kPi; }

// The physical alternative for a spot: the flux spread over the solid angle of its cone, so a tighter beam is
// brighter. Equal to spot_candela_from_lumens at a 60 degree half-angle (2 pi (1 - cos 60) = pi).
[[nodiscard]] inline float cone_candela_from_lumens(float lumens, float halfAngleRad) {
    const float solidAngle = 2.0f * units::kPi * std::max(1.0f - std::cos(halfAngleRad), 1.0e-6f);
    return lumens / solidAngle;
}

// Inverse-square falloff with the distance converted from WU to metres:
//   1 / max(d_m^2, 0.01^2)
// Illuminance (lux) at the surface = candela * this. The 1 cm floor stops the value blowing up.
[[nodiscard]] inline float inverse_square_m(float distanceWu) {
    const float d = units::to_m(distanceWu);  // WU to metres, never re-type 1000
    return 1.0f / std::max(d * d, 1.0e-4f);   // 1 cm floor: 0.01^2
}

// Linear-RGB tint of a black-body light at `kelvin` (1000 to 40000), scaled so the largest component is 1.
// 6500 K is close to white, 2100 K (high-pressure sodium street lamp) is deep orange.
// Tanner Helland's fit to the black-body curve (good to a few percent from 1000 K to 40000 K).
[[nodiscard]] inline glm::vec3 kelvin_to_rgb(float kelvin) {
    const float t = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    glm::vec3   c;
    c.r = t <= 66.0f ? 255.0f : 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);
    c.g = t <= 66.0f ? 99.4708025861f * std::log(t) - 161.1195681661f
                     : 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);
    c.b = t >= 66.0f ? 255.0f : (t <= 19.0f ? 0.0f : 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f);
    c   = glm::clamp(c / 255.0f, 0.0f, 1.0f);
    return c / std::max(std::max(c.r, c.g), std::max(c.b, 1.0e-6f));
}

// Emissive luminance (cd/m²) <-> a [0,1] value for an 8-bit UNORM channel. Logarithmic, so both a dim
// tail light and a headlight lens keep a few percent precision:
//   v = log2(1 + nits) / log2(1 + kEmissiveMaxNits)
[[nodiscard]] inline float emissive_encode([[maybe_unused]] float nits) {
    // TODO(learning L5.1): implement the formula above; clamp nits to [0, kEmissiveMaxNits].
    return 0.0f;
}

[[nodiscard]] inline float emissive_decode([[maybe_unused]] float v) {
    // TODO(learning L5.1): invert emissive_encode.
    return 0.0f;
}

}  // namespace swish::util::light
