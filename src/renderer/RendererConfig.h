#pragma once

// ── RendererConfig.h ─────────────────────────────────────────────────
// Renderer-wide compile-time settings. (Was in utils/Constants.h, which was removed: this is a
// renderer decision, not a generic utility.)

#include "utils/types/StdTypes.h"

namespace swish {

// Frames the CPU may record ahead of the GPU: sizes every per-frame ring (UBOs, descriptor sets,
// framebuffers, sync objects, command buffers). Changing it needs a renderer-wide review.
static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

template <u32 N = MAX_FRAMES_IN_FLIGHT>
[[nodiscard]] constexpr u32 prev_frame(u32 frame) noexcept {
    static_assert(N >= 2, "ring need at least 2 slots");
    return (frame + (N - 1)) % N;
}

}  // namespace swish
