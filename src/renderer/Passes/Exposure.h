#pragma once

// One source of truth for the multiplier the pre-exposed HDR image gets before tone mapping (L1.1, L1.2, L1.6).
// The composite multiplies by it, and the bloom extract multiplies by it before comparing to the threshold,
// so the glow follows the exposure the way a real lens flare does and the two passes can never disagree.
//
// Release: auto-exposure (HdrFinalizePass publishes it in FrameState) while DebugParams::autoExposure is on,
// otherwise the shipped EV100 below. Debug with auto-exposure off: the manual Exposure slider.

#include "debug/DebugParams.h"
#include "renderer/Passes/IRenderPass.h"
#include "utils/light/Photometry.h"

namespace swish {

// The shipped EV100 when auto-exposure is off. 15.7144 reproduces the old fixed composite exposure of 1.55:
//   EV100 = -log2(1.2 * 1.55 * kPreExposure) = -log2(1.86e-5)
inline constexpr float kShippedEv100 = 15.7144f;

[[nodiscard]] inline float frame_exposure(const FrameCtx& c) {
    if (c.tune.autoExposure) return c.state.exposure;
#ifdef SWISH_DEBUG_UI
    return c.tune.exposure;
#else
    return util::light::hdr_exposure_from_ev100(kShippedEv100);
#endif
}

}  // namespace swish
