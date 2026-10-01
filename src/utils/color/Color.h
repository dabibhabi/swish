#pragma once

// ── Color.h ──────────────────────────────────────────────────────────
// Purpose   : small colour helpers shared by scene builders: turn a raw 4-float array (as stored in
//             the baked RoadConfig tints) into a glm::vec4.
// Units     : linear RGBA, each component nominally in [0, 1]; no conversion is performed.
// Complexity: O(1), no allocation.
// Example   : glm::vec4 asphalt = swish::util::color::rgba(cfg.asphalt_tint);
// See also  : scene/RoadScene/RoadConfig.h (float tint[4] fields), scene/RoadScene/RoadScene.cpp
// Test      : tests/utils/test_color_Color.cpp

#include <glm/glm.hpp>

namespace swish::util::color {

// `c` must point at four floats (r, g, b, a).
inline glm::vec4 rgba(const float* c) {
    return {c[0], c[1], c[2], c[3]};
}

}  // namespace swish::util::color
