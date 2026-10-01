#pragma once

// Wet-road grip, hydroplaning and the puddle mask mirrored from GLSL.
// Part of the CarPhysics split (umbrella: CarPhysics.h). Verbatim move; SI units,
// no renderer/GLFW/GLM dependency.

#include "scene/RoadScene/RoadParams.h"

#include <algorithm>
#include <cmath>

namespace swish {

// ── Wet-road grip + standing-water hydroplaning (Wave 2 step 4) ──────────
// docs/lessons-from-chrono/07-wet-roads-and-rain.md §4 — lifted essentially
// as written. `wetness` is RainSystem's INTEGRATED wetness (asymmetric
// wet/dry ramp — 0.08/s wet, 0.012/s dry, RainSystem.cpp), NOT raw rain
// intensity, so grip loss outlasts a rain shower exactly like the visual
// sheen does (CarEntity::set_wetness()).
struct WetGripParams {
    float muDry     = 1.05f;    // MUST match CarParams::tireMu exactly (pinned by a test)
    float muWet     = 0.60f;    // low-speed wet asphalt (Phase C decision 4)
    float speedFade = 0.0045f;  // per m/s, scaled by wetness
    float muPlane   = 0.08f;    // grip while hydroplaning
    float vPlane    = 28.0f;    // m/s, NASA/Horne v_p ~= 1.76*sqrt(kPa) at ~250 kPa
    float planeBand = 4.0f;     // m/s, smooth onset width (no hard switch)
};

// How completely the tire is planing, in [0,1]: 0 = tread still cutting
// through to the road, 1 = riding on the water film. This is the C¹ smoothstep
// effective_mu() has always used internally to blend grip down to muPlane,
// hoisted into its own function so Phase 2 can gate on the SAME quantity
// (there must not be two definitions of "is the car hydroplaning").
//
// Zero unless BOTH gates are open — there is standing water deeper than the
// tread can clear (filmOverTread >= 1) AND the speed is past the NASA/Horne
// hydroplane speed. That is exactly spec decision 2's "hydroplane-only
// trigger", expressed as a structural property rather than a tuning choice:
// dry roads, merely-wet roads, and standing water taken slowly all return 0,
// so everything Phase 2 gates on this is provably inert there.
//
// `speed` should be the tire's speed over the water — the body-speed MAGNITUDE
// √(vx²+vl²), not the forward component. They agree until the car is sideways,
// and during a spin they differ enormously: keying off vx alone would have the
// car "stop hydroplaning" merely because its nose rotated.
inline float hydroplane_fraction(float speed, float filmOverTread, const WetGripParams& g) {
    if (filmOverTread < 1.0f)
        return 0.0f;
    const float t = std::clamp((std::fabs(speed) - g.vPlane) / g.planeBand, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Effective tire-road friction. wetness in [0,1]; filmOverTread >= 1 means
// standing water at this position/time exceeds tread capacity (see
// film_over_tread below). HARD INVARIANT: wetness <= 0 returns muDry EXACTLY
// — dry handling stays bit-identical to before this feature. (Every other
// test in this file constructs CarParams/TireParams with their own dry
// defaults and never calls this function, so they are that invariant's
// regression guard; see also the dedicated test for this function.)
inline float effective_mu(float wetness, float v, float filmOverTread, const WetGripParams& g) {
    if (wetness <= 0.0f)
        return g.muDry;
    const float muLow = g.muDry + wetness * (g.muWet - g.muDry);
    const float vAbs  = std::fabs(v);
    const float faded = muLow * (1.0f - wetness * g.speedFade * std::min(vAbs, g.vPlane));
    // Hydroplaning: C¹ smoothstep from faded grip down to muPlane across
    // [vPlane, vPlane+planeBand]. No kink at the vPlane end: for vAbs <=
    // vPlane the fraction is 0 and this collapses to exactly `faded`, so the
    // planing and non-planing cases agree on their shared boundary and only
    // differ where filmOverTread actually gates in.
    const float s = hydroplane_fraction(vAbs, filmOverTread, g);
    if (s <= 0.0f)
        return faded;
    return faded + s * (g.muPlane - faded);
}

// ── Standing-water depth proxy (renderer puddle mask x wetness) ──────────
// shaders/lighting.frag's puddleHash()/puddleNoise()/puddleAmount(), ported
// to C++ so physics can query "is there standing water here" without a
// G-buffer. THIS IS A THIRD MANUAL COPY: shaders/ssr.frag already carries its
// own hand-synced duplicate of lighting.frag's puddle functions (its own
// comment says as much) — that duplication is an already-acknowledged wart,
// not a new pattern introduced here. If puddle shape/scale/coverage ever
// changes, ALL THREE copies (lighting.frag, ssr.frag, and the three
// functions below) must move together, or physics and the rendered puddles
// will silently disagree about where the standing water is. Consequently
// this C++ copy can only ever be an APPROXIMATION of what the GPU renders.
inline float puddle_hash(float x, float y) {
    const float s = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return s - std::floor(s);  // GLSL fract(): in [0,1) for negative x/y too
}

inline float puddle_noise(float x, float y) {
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    float       fx = x - ix;
    float       fy = y - iy;
    fx             = fx * fx * (3.0f - 2.0f * fx);  // Hermite smoothing, matches GLSL
    fy             = fy * fy * (3.0f - 2.0f * fy);
    const float a  = puddle_hash(ix, iy);
    const float b  = puddle_hash(ix + 1.0f, iy);
    const float c  = puddle_hash(ix, iy + 1.0f);
    const float d  = puddle_hash(ix + 1.0f, iy + 1.0f);
    const float ab = a + (b - a) * fx;
    const float cd = c + (d - c) * fx;
    return ab + (cd - ab) * fy;
}

// worldX/worldZ in WORLD UNITS (1 m = 1000 WU — matches lighting.frag's
// fragWorldPos.xz). coverage in [0,1] mirrors SP_PUDDLE_COVERAGE. The GLSL
// version also gates on the G-buffer's road-material tag (grass/barriers
// never pool); physics has no G-buffer, so this implicitly assumes the car
// is on puddle-eligible pavement — true for the LIE corridor it drives on.
inline float puddle_amount(float worldX, float worldZ, float coverage) {
    if (coverage <= 0.0f)
        return 0.0f;
    const float px = worldX * 0.00013f;  // ~7.7 m puddle cells
    const float pz = worldZ * 0.00013f;
    const float n  = puddle_noise(px, pz) * 0.65f + puddle_noise(px * 2.7f + 11.0f, pz * 2.7f + 11.0f) * 0.35f;
    const float lo = 1.0f - coverage;
    const float hi = lo + 0.12f;  // soft shoreline, matches GLSL
    const float t  = std::clamp((n - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);  // GLSL smoothstep(lo, hi, n)
}

// Shipped puddle coverage: alias of the single source swish::road::kPuddleCoverage (RoadParams.h), which the
// registry default and lighting.frag's SP_PUDDLE_COVERAGE are pinned to by tests/test_debug_params_io.cpp.
// Physics has no wire into the live debug-UI slider, so hydroplaning always tests the SHIPPED coverage.
inline constexpr float kPuddleCoveragePhysics = road::kPuddleCoverage;

// kFilmDepthScale: puddle_amount() is a visual mask, not a real water depth
// (the Gallaway rain-rate/drainage-path model from the wet-roads doc §3 is
// not built), so this substitutes for it, weighted by integrated wetness so
// an early, barely-wet road never claims standing water even directly over a
// puddle cell. 1.4 is chosen so a FULLY-FORMED puddle (puddle_amount -> 1,
// which — given the narrow 0.12 shoreline transition above — is most of a
// puddle cell's interior) crosses the filmOverTread >= 1 hydroplane gate once
// wetness exceeds 1/1.4 ~= 0.71 (the road has been rained on hard for ~9 s at
// RainSystem's 0.08/s wet ramp): "above a SPEED *and* water-depth threshold",
// per the reported issue. A merely damp, non-flooded patch (puddle_amount up
// to ~0.5 — the shoreline at a pool's edge) stays under the gate even at full
// wetness (1.4 * 0.5 * 1.0 = 0.7 < 1) — it fades grip via speedFade above,
// same as any other wet tarmac, but never planes.
inline constexpr float kFilmDepthScale = 1.4f;

inline float film_over_tread(float worldX, float worldZ, float wetness) {
    return kFilmDepthScale * puddle_amount(worldX, worldZ, kPuddleCoveragePhysics) * std::max(wetness, 0.0f);
}

}  // namespace swish
