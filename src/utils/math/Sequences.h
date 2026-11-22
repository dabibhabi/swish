#pragma once

// ── Sequences.h ──────────────────────────────────────────────────────
// Purpose   : low-discrepancy sequences (Halton) for sub-pixel jitter and sampling patterns.
// Units     : unitless; halton() returns a value in [0, 1).
// Complexity: O(log_base(index)), no allocation.
// Precision : float; identical arithmetic to the former Renderer_Frame.cpp anonymous helper
//             (the TAA jitter sequence).
// Example   : float jx = (swish::util::math::halton(n, 2) - 0.5f) * 2.0f / width_px;
//             float jy = (swish::util::math::halton(n, 3) - 0.5f) * 2.0f / height_px;
// See also  : renderer/Renderer/Renderer_Frame.cpp (TAA jitter), renderer/TaaPass
// Test      : tests/utils/test_math_Sequences.cpp

#include <cstdint>

namespace swish::util::math {

// Halton sample of `index` (1-based; index 0 gives 0) in `base` (a prime, or any integer >= 2).
inline float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

}  // namespace swish::util::math
