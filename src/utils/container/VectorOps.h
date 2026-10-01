#pragma once

// ── VectorOps.h ──────────────────────────────────────────────────────
// Purpose   : element-wise builders for std::vector<float> (the per-station lateral-offset arrays
//             the corridor sweep bands consume): an affine map and a constant fill.
// Units     : none; values keep whatever unit the caller's array has (WU for lateral offsets).
// Complexity: O(n), one allocation of n floats.
// Precision : float; affine computes src[i] * k + bias in that order (no fused rewrite).
// Example   : namespace uc = swish::util::container;
//             auto edge   = uc::affine(lanes, lane_w, margin);   // lanes[i] * lane_w + margin
//             auto centre = uc::filled(lanes.size(), 0.0f);
// See also  : scene/Corridor/CorridorScene.cpp (only user)
// Test      : tests/utils/test_container_VectorOps.cpp

#include <cstddef>
#include <vector>

namespace swish::util::container {

// out[i] = src[i] * k + bias
inline std::vector<float> affine(const std::vector<float>& src, float k, float bias) {
    std::vector<float> out(src.size());
    for (size_t i = 0; i < src.size(); ++i)
        out[i] = src[i] * k + bias;
    return out;
}

// n copies of v.
inline std::vector<float> filled(size_t n, float v) {
    return std::vector<float>(n, v);
}

}  // namespace swish::util::container
