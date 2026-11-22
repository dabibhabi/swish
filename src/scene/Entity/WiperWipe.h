#pragma once

// Realistic-wipe helpers (docs/realism-learning-plan.md, modules M1 and M4): the pure CPU side of the
// water pile, the residual film and the per-wipe drop re-seeding. Header-only, std + GLM only, so
// tests/test_wiper_wipe.cpp can pin each rule before it is ported to windshield_wetness.frag.
//
// LEARNING SCAFFOLD: every function below is a stub that returns a placeholder. The `[.learning]` tests
// describe the intended behaviour; fill the bodies until `swish_tests "[.learning][wiper_wipe]"` passes.
// Angles are radians off park, as returned by wiper_angle() in Wiper.h.

#include <algorithm>
#include <cmath>

namespace swish {

// Distinct wipe generations stored in the wetness map before the counter wraps. Half floats are exact
// for integers up to 2048, so g / kWipeGenerations survives an R16G16B16A16_SFLOAT channel exactly.
inline constexpr int kWipeGenerations = 64;

// +1 while the blade sweeps away from park (angle rising), -1 while it returns, 0 when it moved less
// than `eps`. The leading edge of the water pile is on the +direction side of the blade.
[[nodiscard]] inline int wiper_sweep_direction([[maybe_unused]] float prevAngle, [[maybe_unused]] float angle,
                                               [[maybe_unused]] float eps = 1e-6f) {
    // TODO(learning M1): compare the two angles.
    return 0;
}

// True on the frame the blade arrives at park (angle 0) after being off it. This is when the pile is
// released into the run-off. Already parked on both frames is not an arrival.
[[nodiscard]] inline bool wiper_reached_park([[maybe_unused]] float prevAngle, [[maybe_unused]] float angle) {
    // TODO(learning M4): parked means angle <= 0 (Wiper.h wiper_parked uses phase <= 0 for the same idea).
    return false;
}

// Wetness after the blade passes. `clear` in [0,1] is wiperClear() coverage, `film` the thin residual
// layer the rubber leaves. The film never adds water: glass drier than the film stays as it was.
//   w' = mix(w, min(w, film), clear)
[[nodiscard]] inline float wiper_film_after_clear(float w, [[maybe_unused]] float clear, [[maybe_unused]] float film) {
    // TODO(learning M4): implement the formula above.
    return w;
}

// Water the blade removes this frame, which feeds the pile ahead of it: the difference between the
// wetness before and after wiper_film_after_clear. Never negative.
[[nodiscard]] inline float wiper_water_collected([[maybe_unused]] float w, [[maybe_unused]] float clear,
                                                 [[maybe_unused]] float film) {
    // TODO(learning M4): reuse wiper_film_after_clear.
    return 0.f;
}

// Next wipe generation, wrapping kWipeGenerations - 1 back to 0.
[[nodiscard]] inline int wiper_next_generation(int g) {
    // TODO(learning M4): increment and wrap.
    return g;
}

// Generation <-> wetness-map channel value in [0,1). decode(encode(g)) == g for every generation,
// including after a round trip through a 16-bit half float.
[[nodiscard]] inline float wiper_generation_encode([[maybe_unused]] int g) {
    // TODO(learning M4): scale into [0,1).
    return 0.f;
}

[[nodiscard]] inline int wiper_generation_decode([[maybe_unused]] float v) {
    // TODO(learning M4): invert encode; round, do not truncate.
    return 0;
}

}  // namespace swish
