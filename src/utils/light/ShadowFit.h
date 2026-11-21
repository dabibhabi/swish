#pragma once

// ── ShadowFit.h ──────────────────────────────────────────────────────
// Purpose   : the pure maths behind stable shadow maps: an up vector that never degenerates, texel
//             snapping (no shimmer while driving), per-cascade bias, and choosing which local lights
//             get a shadow map without popping.
// Units     : positions and texel sizes in the caller's length unit (WU in Swish); directions unit length.
// Complexity: O(1), except pick_shadow_casters: O(n log n) in the number of lights, one small allocation.
// Example   : namespace ul = swish::util::light;
//             glm::mat4 view = glm::lookAt(eye, eye - sunDir, ul::stable_light_up(sunDir));
// See also  : docs/tutorials/lighting/L03-sun-shadows.md, docs/tutorials/lighting/L07-local-shadows.md,
//             renderer GeometryProducer.cpp (cascade fit), shaders/lighting.frag (shadow lookup)
// Test      : tests/utils/test_light_ShadowFit.cpp
//
// LEARNING SCAFFOLD: every function below is a stub that returns a placeholder. The `[.learning]` tests
// describe the intended behaviour; fill the bodies until
// `build/tests/utils/swish_utils_tests "[learning][shadow_fit]"` passes.

#include <cmath>
#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace swish::util::light {

// Unit up vector for glm::lookAt along `lightDir` (unit length). World up (0,1,0) is fine until the light
// is nearly vertical, where lookAt divides by zero; switch to another axis there.
// Guarantee: |dot(result, lightDir)| < 0.99 for every direction.
[[nodiscard]] inline glm::vec3 stable_light_up(glm::vec3 lightDir) {
    return std::abs(lightDir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

// Snap a light-space position to the shadow-map texel grid: x and y rounded down to a multiple of
// `texelWorld`, z untouched. The cascade then moves in whole texels and edges stop crawling.
[[nodiscard]] inline glm::vec3 snap_to_texel(glm::vec3 lightSpacePos, float texelWorld) {
    lightSpacePos.x = std::floor(lightSpacePos.x / texelWorld) * texelWorld;
    lightSpacePos.y = std::floor(lightSpacePos.y / texelWorld) * texelWorld;
    return lightSpacePos;
}

// Depth bias for a cascade whose texels are `texelWorld` wide, given the bias tuned for a cascade with
// `refTexelWorld` texels. Far cascades have bigger texels and need proportionally more bias:
//   bias = baseBias * texelWorld / refTexelWorld
[[nodiscard]] inline float cascade_bias(float baseBias, float texelWorld, float refTexelWorld) {
    return baseBias * texelWorld / refTexelWorld;
}

// Indices of the lights that get a shadow map this frame: at most `maxCasters`, nearest to `camera`.
// `previous` is last frame's result. A light already in `previous` is only replaced when a challenger is
// closer than `hysteresis` (0..1) times its distance, so a lamp passing by does not flicker in and out.
// Indices in `previous` that are out of range are ignored. Order of the result is not specified.
[[nodiscard]] inline std::vector<int> pick_shadow_casters([[maybe_unused]] std::span<const glm::vec3> lightPositions,
                                                          [[maybe_unused]] glm::vec3                  camera,
                                                          [[maybe_unused]] std::span<const int>       previous,
                                                          [[maybe_unused]] int                        maxCasters,
                                                          [[maybe_unused]] float hysteresis = 0.8f) {
    // TODO(learning L7.2): keep valid previous casters, fill free slots with the nearest others, then let a
    // much closer light replace the farthest kept one.
    return {};
}

}  // namespace swish::util::light
