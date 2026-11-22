#pragma once

// Street-lamp placement along the driven corridor (tasks/todo.md "Lighting", phase L6.1). The retired road
// placed lamps on a straight Z axis (scene/RoadScene/RoadScene_Furniture.cpp, generate_street_lamps); the
// corridor curves, so this version walks a centre-line polyline instead. Header-only and GPU-free so
// tests/test_corridor_lamps.cpp can pin the rule before CorridorScene uses it. Nothing in the app
// includes this file yet.
//
// LEARNING SCAFFOLD: the function below is a stub that returns a placeholder. The `[.learning]` tests
// describe the intended behaviour; fill the body until
// `build/tests/swish_tests "[learning][corridor_lamps]"` passes. Guide: docs/tutorials/lighting/L06-local-lights.md.

#include "utils/geometry/Polyline.h"

#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace swish {

// One lamp post. All positions in WU.
struct LampPost {
    glm::vec3 base;  // foot of the pole, on the ground beside the road
    glm::vec3 head;  // the light source: top of the pole, moved `armLen` back toward the centre-line
    glm::vec3 aim;   // unit beam direction (straight down)
};

struct LampRowParams {
    float spacing       = 0.0f;  // distance between posts along the road
    float lateralOffset = 0.0f;  // centre-line to pole; positive = right of the travel direction
    float poleHeight    = 0.0f;
    float armLen        = 0.0f;  // how far the arm reaches back over the road
};

// Posts at arc lengths spacing, 2 * spacing, ... strictly below the polyline length (the same rule as the
// retired road: none at the very start, none at the very end). "Right" is cross(tangent, up) with Y up.
// Fewer than two points or a spacing <= 0 gives no posts.
[[nodiscard]] inline std::vector<LampPost> lamp_posts_along(std::span<const glm::vec3> centerline,
                                                            const LampRowParams&       p) {
    std::vector<LampPost> posts;
    if (centerline.size() < 2 || p.spacing <= 0.0f) return posts;
    const float     len = util::geometry::polyline_length(centerline);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    for (float s = p.spacing; s < len; s += p.spacing) {
        glm::vec3 pos, tan;
        util::geometry::polyline_sample(centerline, s, pos, tan);
        const glm::vec3 right = glm::normalize(glm::cross(tan, up));
        LampPost        post;
        post.base = pos + right * p.lateralOffset;
        // The arm reaches back toward the centre-line, whichever side the pole stands on.
        post.head = post.base + up * p.poleHeight - right * (p.lateralOffset >= 0.0f ? p.armLen : -p.armLen);
        post.aim  = glm::vec3(0.0f, -1.0f, 0.0f);
        posts.push_back(post);
    }
    return posts;
}

}  // namespace swish
