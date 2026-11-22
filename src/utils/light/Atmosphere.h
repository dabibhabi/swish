#pragma once

// ── Atmosphere.h ─────────────────────────────────────────────────────
// Purpose   : the CPU copy of shaders/common/atmosphere.glsl: a physically based clear sky (Rayleigh, Mie and
//             ozone single scattering, marched; no lookup tables). The renderer uses it for three numbers the
//             shader cannot hand back cheaply: the sun's tint and strength at the ground, the zenith luminance
//             (to normalise the baked sky cube so it stays in half-float range at noon and at midnight), and
//             the overcast level. The tests pin the model here.
// Units     : directions are unit vectors with +Y up; distances inside are km; radiance in cd/m² (nits);
//             illuminance in lux. `toa` means "at the top of the atmosphere".
// Complexity: O(view steps x light steps) = 20 x 6 density evaluations per direction, no allocation.
// Example   : namespace ul = swish::util::light;
//             glm::vec3 T = ul::atm_transmittance(sunDir, 2.5f);            // the sun's colour at the ground
//             glm::vec3 L = ul::atm_scatter({0, 1, 0}, sunDir, 2.5f) * ul::kSunToaLux;  // zenith radiance
// See also  : shaders/common/atmosphere.glsl (keep in step), docs/lighting-l2-l10.md
// Test      : tests/utils/test_light_Atmosphere.cpp

#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace swish::util::light {

inline constexpr float kSunToaLux       = 128000.0f;  // solar illuminance above the atmosphere
inline constexpr float kAirglowNits     = 2.0e-4f;    // natural night-sky brightness with no moon
inline constexpr float kSunSolidAngleSr = 6.8e-5f;    // the sun's disc; the moon is nearly the same size
inline constexpr float kAtmGroundKm     = 6360.0f;
inline constexpr float kAtmTopKm        = 6460.0f;
inline constexpr float kAtmEyeKm        = 0.05f;
inline constexpr float kAtmMieG         = 0.8f;
inline constexpr float kAtmMultiScatter = 1.2f;
inline constexpr int   kAtmViewSteps    = 20;
inline constexpr int   kAtmLightSteps   = 6;
inline const glm::vec3 kAtmRayleigh{5.802e-3f, 13.558e-3f, 33.1e-3f};
inline constexpr float kAtmMieScatter    = 3.996e-3f;
inline constexpr float kAtmMieExtinction = 4.40e-3f;
inline const glm::vec3 kAtmOzone{0.650e-3f, 1.881e-3f, 0.085e-3f};

[[nodiscard]] inline float luminance(const glm::vec3& rgb) {
    return glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

namespace detail {
[[nodiscard]] inline float ray_sphere_far(const glm::vec3& o, const glm::vec3& d, float r) {
    const float b = glm::dot(o, d), c = glm::dot(o, o) - r * r, h = b * b - c;
    return h < 0.0f ? -1.0f : -b + std::sqrt(h);
}
[[nodiscard]] inline float ray_sphere_near(const glm::vec3& o, const glm::vec3& d, float r) {
    const float b = glm::dot(o, d), c = glm::dot(o, o) - r * r, h = b * b - c;
    if (h < 0.0f) return -1.0f;
    const float t = -b - std::sqrt(h);
    return t > 0.0f ? t : -1.0f;
}
[[nodiscard]] inline glm::vec3 density(float h) {
    return {std::exp(-h / 8.0f), std::exp(-h / 1.2f), std::max(0.0f, 1.0f - std::abs(h - 25.0f) / 15.0f)};
}
[[nodiscard]] inline glm::vec3 extinction(const glm::vec3& depth, float mie) {
    return kAtmRayleigh * depth.x + glm::vec3(kAtmMieExtinction * mie * depth.y) + kAtmOzone * depth.z;
}
// Optical depth per species toward the light; x < 0 when the planet is in the way.
[[nodiscard]] inline glm::vec3 light_depth(const glm::vec3& p, const glm::vec3& l) {
    if (ray_sphere_near(p, l, kAtmGroundKm) > 0.0f) return glm::vec3(-1.0f);
    const float len = ray_sphere_far(p, l, kAtmTopKm);
    glm::vec3   sum(0.0f);
    float       prev = 0.0f;
    for (int i = 1; i <= kAtmLightSteps; ++i) {
        const float u = static_cast<float>(i) / kAtmLightSteps, t = len * u * u, mid = 0.5f * (prev + t);
        sum += density(glm::length(p + l * mid) - kAtmGroundKm) * (t - prev);
        prev = t;
    }
    return sum;
}
}  // namespace detail

// Haze amount from turbidity: 2.5 (a clear day) is the reference.
[[nodiscard]] inline float atm_mie(float turbidity) { return std::max(turbidity - 1.0f, 0.2f) / 1.5f; }

// Clear-sky radiance toward `dir` for a light at `l`, in nits per lux at the top of the atmosphere.
[[nodiscard]] inline glm::vec3 atm_scatter(const glm::vec3& dir, const glm::vec3& l, float turbidity) {
    const float     mie = atm_mie(turbidity);
    const glm::vec3 o(0.0f, kAtmGroundKm + kAtmEyeKm, 0.0f);
    const float     len = detail::ray_sphere_far(o, dir, kAtmTopKm);
    const float     mu  = glm::dot(dir, l);
    const float     phR = 3.0f / (16.0f * units::kPi) * (1.0f + mu * mu);
    const float     g   = kAtmMieG;
    const float     phM =
        (1.0f - g * g) / (4.0f * units::kPi * std::pow(std::max(1.0f + g * g - 2.0f * g * mu, 1.0e-4f), 1.5f));
    glm::vec3 viewDepth(0.0f), sum(0.0f);
    float     prev = 0.0f;
    for (int i = 1; i <= kAtmViewSteps; ++i) {
        const float u = static_cast<float>(i) / kAtmViewSteps, t = len * u * u, ds = t - prev, mid = 0.5f * (prev + t);
        const glm::vec3 p    = o + dir * mid;
        const glm::vec3 dens = detail::density(glm::length(p) - kAtmGroundKm);
        viewDepth += dens * ds * 0.5f;
        const glm::vec3 lightDepth = detail::light_depth(p, l);
        if (lightDepth.x >= 0.0f) {
            const glm::vec3 T = glm::exp(-detail::extinction(viewDepth + lightDepth, mie));
            sum += T * (kAtmRayleigh * dens.x * phR + glm::vec3(kAtmMieScatter * mie * dens.y * phM)) * ds;
        }
        viewDepth += dens * ds * 0.5f;
        prev = t;
    }
    return sum * kAtmMultiScatter;
}

// Fraction of the light reaching the ground from direction `l`, per colour: the sun's tint. Zero below the horizon.
[[nodiscard]] inline glm::vec3 atm_transmittance(const glm::vec3& l, float turbidity) {
    const glm::vec3 depth = detail::light_depth(glm::vec3(0.0f, kAtmGroundKm + kAtmEyeKm, 0.0f), l);
    if (depth.x < 0.0f) return glm::vec3(0.0f);
    return glm::exp(-detail::extinction(depth, atm_mie(turbidity)));
}

// What the sky shader needs (the same four vectors go to ibl_sky.frag and lighting.frag).
struct SkyParams {
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};
    float     sunToaLux = kSunToaLux;
    glm::vec3 moonDir{0.0f, 1.0f, 0.0f};
    float     moonToaLux         = 0.0f;
    float     turbidity          = 2.5f;
    float     cloud              = 0.0f;  // 0 clear .. 1 overcast
    float     airglowNits        = kAirglowNits;
    float     overcastZenithNits = 0.0f;  // filled by finish_sky_params
};

// The whole sky in nits toward `dir` (the C++ copy of atmSky).
[[nodiscard]] inline glm::vec3 sky_radiance(const SkyParams& s, const glm::vec3& dir) {
    const glm::vec3 d     = glm::normalize(glm::vec3(dir.x, std::max(dir.y, 0.02f), dir.z));
    glm::vec3       clear = atm_scatter(d, s.sunDir, s.turbidity) * s.sunToaLux;
    if (s.moonToaLux > 0.0f) clear += atm_scatter(d, s.moonDir, s.turbidity) * s.moonToaLux;
    const glm::vec3 overcast(s.overcastZenithNits * (1.0f + 2.0f * d.y) / 3.0f);
    const glm::vec3 sky = glm::mix(clear, overcast, s.cloud) + glm::vec3(s.airglowNits);
    const float     e   = std::clamp(dir.y / -0.3f, 0.0f, 1.0f);
    return sky * glm::mix(1.0f, 0.25f, e * e * (3.0f - 2.0f * e));
}

// Fill the overcast level: a fully overcast sky is about as bright at the zenith as the clear zenith would be
// (thick cloud passes roughly what the clear sky scatters; a lab approximation, tune by eye).
inline void finish_sky_params(SkyParams& s) {
    SkyParams clear      = s;
    clear.cloud          = 0.0f;
    clear.airglowNits    = 0.0f;
    s.overcastZenithNits = luminance(sky_radiance(clear, glm::vec3(0.0f, 1.0f, 0.0f)));
}

// Scale that brings the sky into a comfortable half-float range: the baked cube stores nits / this, so its
// zenith is about 0.1 at noon and at midnight alike. Never zero.
[[nodiscard]] inline float sky_cube_scale(const SkyParams& s) {
    return std::max(10.0f * luminance(sky_radiance(s, glm::vec3(0.0f, 1.0f, 0.0f))), 1.0e-4f);
}

}  // namespace swish::util::light
