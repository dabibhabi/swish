#pragma once

// ── Polyline.h ───────────────────────────────────────────────────────
// Purpose   : arc-length queries on an open polyline (a road ribbon centre-line): total length and
//             the position + unit tangent at a given distance along it.
// Units     : the caller's length unit (WU for road ribbons); tangents are unit length.
// Complexity: O(n) per call, no allocation.
// Precision : float; moved verbatim from RoadGeometry::ribbon_length / ribbon_sample.
// Example   : namespace ug = swish::util::geometry;
//             float len = ug::polyline_length(ribbon.pts);
//             glm::vec3 pos, tan;
//             ug::polyline_sample(ribbon.pts, 0.5f * len, pos, tan);
// See also  : scene/RoadGeometry/RoadGeometry.h (ribbon_length / ribbon_sample forward here),
//             scene/Treadmill/Treadmill.cpp (ribbon follower)
// Test      : tests/utils/test_geometry_Polyline.cpp

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <span>

namespace swish::util::geometry {

inline float polyline_length(std::span<const glm::vec3> pts) {
    float len = 0.0f;
    for (size_t i = 1; i < pts.size(); ++i) len += glm::length(pts[i] - pts[i - 1]);
    return len;
}

// Position and unit tangent at arc length `s` (clamped to >= 0; past the end it clamps to the last
// point and returns the last segment's tangent).
// Fewer than two points: pos = first point (or origin), tan = (0, 0, -1).
inline void polyline_sample(std::span<const glm::vec3> pts, float s, glm::vec3& pos, glm::vec3& tan) {
    if (pts.size() < 2) {
        pos = pts.empty() ? glm::vec3(0.0f) : pts[0];
        tan = glm::vec3(0.0f, 0.0f, -1.0f);
        return;
    }
    s = std::max(0.0f, s);
    for (size_t i = 1; i < pts.size(); ++i) {
        float seg = glm::length(pts[i] - pts[i - 1]);
        if (s <= seg || i == pts.size() - 1) {
            float u = (seg > 1e-4f) ? std::min(s / seg, 1.0f) : 0.0f;
            pos     = pts[i - 1] + (pts[i] - pts[i - 1]) * u;
            tan     = glm::normalize(pts[i] - pts[i - 1]);
            return;
        }
        s -= seg;
    }
}

}  // namespace swish::util::geometry
