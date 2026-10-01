#pragma once

// ── Hash.h ───────────────────────────────────────────────────────────
// Purpose   : tiny integer hash mapped to [0, 1] for deterministic per-index variation
//             (e.g. per-tree jitter) with no RNG state.
// Units     : unitless; input is any integer slot index, output is in [0, 1].
// Complexity: O(1), no allocation.
// Precision : float; multiply by 2654435761u (Knuth), fold with x ^= x >> 16, divide by UINT32_MAX.
//             Same arithmetic as the former RoadScene_Furniture.cpp tree_hash, so the tree
//             layout is unchanged.
// Example   : float rnd = swish::util::math::hash01(slot);
//             float x_jitter = (swish::util::math::hash01(slot + 10000) - 0.5f) * 8.0f * ft;
// See also  : scene/RoadScene/RoadScene_Furniture.cpp
// Test      : tests/utils/test_math_Hash.cpp

#include <cstdint>
#include <limits>

namespace swish::util::math {

inline float hash01(int slot) {
    uint32_t x = static_cast<uint32_t>(slot) * 2654435761u;
    x ^= x >> 16;
    return static_cast<float>(x) / static_cast<float>(std::numeric_limits<uint32_t>::max());
}

}  // namespace swish::util::math
