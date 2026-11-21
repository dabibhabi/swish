#pragma once

// ── SkyVisibility.h ──────────────────────────────────────────────────
// Purpose   : how much of the sky a point can see when it is under a roof (a tunnel, an overpass deck).
//             lighting.frag shadows only the SUN (CSM); the sky's ambient light and reflections are not
//             occluded by geometry. A roof volume is a box that says "inside here the sky is hidden",
//             fading in from the open ends. The lighting lab sends its boxes to the shader (debug builds).
// Units     : positions and distances in world units (1 m = 1000 WU); visibility is a factor in [floor, 1].
// Complexity: O(volumes), no allocation.
// Example   : namespace ul = swish::util::light;
//             ul::RoofVolume tunnel{{-6000, 0, -600000}, {6000, 6000, -200000}, 20000.0f, 2, 0.02f};
//             float v = ul::sky_visibility(std::span(&tunnel, 1), carPosition);  // 1 outside, 0.02 deep inside
// See also  : shaders/lighting.frag (the GLSL copy of roof_sky_visibility; keep the two in step),
//             src/scene/LightingLab/LightingLab.h (who builds the volumes), docs/lighting-lab.md
// Test      : tests/utils/test_light_SkyVisibility.cpp

#include <algorithm>
#include <glm/glm.hpp>
#include <span>

namespace swish::util::light {

// The most roof volumes the shader accepts (SceneParamsUBO rows in debug builds).
inline constexpr int kMaxRoofVolumes = 12;

// An axis-aligned box of covered space. The box is open (to the sky) at both ends along `openAxis`
// and closed everywhere else, like a tunnel or the underside of a bridge.
struct RoofVolume {
    glm::vec3 min{0.0f};      // WU
    glm::vec3 max{0.0f};      // WU
    float     fadeWu = 0.0f;  // distance from an open end over which the sky fades out
    // Which sides are open to the sky: 0 = both ends along X, 2 = both ends along Z (a tunnel, an underpass),
    // 3 = all four sides (a canopy on columns), 4 = the -Z end only (a garage entrance),
    // -1 = none (a closed room: the floor value everywhere).
    int   openAxis = 2;
    float floor    = 0.02f;  // visibility left deep inside (light that bounces in)
};

// GLSL smoothstep, so the CPU and the shader agree.
[[nodiscard]] inline float smoothstep01(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1.0e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Sky visibility of point `p` for one volume: 1 outside the box; inside, it falls from 1 at an open end to
// `floor` once the point is `fadeWu` in. A deck narrower than 2 * fadeWu therefore never gets fully dark.
[[nodiscard]] inline float roof_sky_visibility(const RoofVolume& v, const glm::vec3& p) {
    if (p.x < v.min.x || p.x > v.max.x || p.y < v.min.y || p.y > v.max.y || p.z < v.min.z || p.z > v.max.z) return 1.0f;
    if (v.openAxis < 0) return v.floor;  // a closed room
    const float dx = std::min(p.x - v.min.x, v.max.x - p.x);
    const float dz = std::min(p.z - v.min.z, v.max.z - p.z);
    // Distance to the nearest open edge.
    const float depth = v.openAxis == 0   ? dx
                        : v.openAxis == 3 ? std::min(dx, dz)
                        : v.openAxis == 4 ? p.z - v.min.z  // only the -Z end is open
                                          : dz;
    return glm::mix(1.0f, v.floor, smoothstep01(0.0f, v.fadeWu, depth));
}

// Several volumes: the darkest one wins (volumes do not overlap in practice).
[[nodiscard]] inline float sky_visibility(std::span<const RoofVolume> volumes, const glm::vec3& p) {
    float vis = 1.0f;
    for (const RoofVolume& v : volumes) vis = std::min(vis, roof_sky_visibility(v, p));
    return vis;
}

}  // namespace swish::util::light
