#pragma once

// ── RoadParams.h ─────────────────────────────────────────────────────
// Purpose   : single source of truth for the LIE cross-section constants that
//             RoadGeometry (physics/drivable bounds) and RoadScene (mesh
//             generators, incl. generate_exit_ramp) both need, so the two can
//             no longer drift. Values are moved VERBATIM from the former
//             RoadGeometry.h mirror constants; tests/test_road_params.cpp pins
//             them against config/road.toml -> road.bin.
// Units     : *_ft members are FEET (convert with util::units::ft() / kWuPerFt);
//             everything else is world units (WU) or a dimensionless fraction.
// Complexity: O(1), constexpr, no allocation.
// Precision : callers keep the legacy expression form (`kXxxFt * kFt`), so
//             float results are bit-identical to the pre-refactor constants.
// Example   : const float ramp_w = swish::road::kExit.width_ft * util::units::kWuPerFt;
// See also  : ../../utils/units/Units.h, RoadGeometry/RoadGeometry.h
// Test      : tests/test_road_params.cpp

#include <algorithm>

namespace swish::road {

// Mainline cross-section values that config/road.toml bakes into road.bin.
struct CrossSectionParams {
    float barrier_ft   = 2.67f;  // [barrier] width_ft
    float clearance_ft = 3.0f;   // gap between barrier and first EB lane (layout offset)
    float lane_ft      = 13.0f;  // [road] lane_width_ft
    float lanes        = 4.0f;   // [road] lane_count (kept float: used in float arithmetic)
    float shoulder_eb_ft = 10.0f;  // [road] shoulder_width_eb_ft
    float marking_y    = 5.0f;   // [markings] y_offset (WU)
};
inline constexpr CrossSectionParams kLie{};

// Crown (cross-slope) shared by RoadConfig::kCrownSlope, road_surface_y and the marking generators.
inline constexpr float kCrownSlope = 0.02f;

// Shipped road puddle coverage [0,1]. ONE C++ source: WetGrip.h (physics puddle film) reads it directly, and
// tests/test_debug_params_io.cpp pins the src/debug/Tunables.def `puddleCoverage` default AND
// shaders/lighting.frag's SP_PUDDLE_COVERAGE literal against it (the registry stays a literal: the .def is
// X-macro-included by renderer code and sits in the debug layer, so it cannot cheaply include scene headers).
inline constexpr float kPuddleCoverage = 0.501f;
static_assert(kPuddleCoverage == 0.501f, "shipped puddle coverage must stay bit-identical to the GLSL literal");

// EB frontage ("marginal") road: ~55 ft berm past the mainline edge, two 12 ft lanes,
// lifted above the grass. Offsets are from the EB road edge, in feet.
struct ServiceRoadParams {
    float inner_ft = 50.0f;
    float outer_ft = 92.0f;
    float y        = 6.0f;  // WU lift above grass (avoids reverse-Z coplanar fighting)
};
inline constexpr ServiceRoadParams kService{};

// Intro EB exit -> N Marginal. Z: z_far = -introLen; the ramp is fully separated at
// end_frac * z_far; diverge/decel lengths are real-world feet.
struct ExitRampParams {
    float end_frac         = 0.38f;   // earlier in the intro so the gantry is easy to reach
    float diverge_ft       = 671.5f;
    float decel_ft         = 328.5f;
    float gantry_lead_ft   = 80.0f;   // EXIT ONLY gantry upstream of diverge
    float offset_ft        = 40.0f;
    float width_ft         = 14.0f;
    float marginal_gap_ft  = 10.0f;
    float marginal_lane_ft = 12.0f;
    float connect_ft       = 200.0f;
    float asphalt_y        = 6.0f;    // WU lift (same idea as endless service y)
};
inline constexpr ExitRampParams kExit{};

// Dashed-lane cycle (WU). Chunks snap the cycle to an exact divisor of the chunk length so
// markings tile with no phase jump at seams, keeping the dash:gap ratio. Pure: nothing mutated.
struct DashPattern {
    float length;
    float gap;
};
constexpr DashPattern snap_dash_to_chunk(DashPattern in, float chunk_len) noexcept {
    const float desired_cycle = in.length + in.gap;  // ~12192 WU
    const int   n             = std::max(1, static_cast<int>(chunk_len / desired_cycle + 0.5f));
    const float cycle         = chunk_len / static_cast<float>(n);  // exact divisor of chunk_len
    const float ratio         = in.length / desired_cycle;          // preserve dash:gap
    const float length        = cycle * ratio;
    return {length, cycle - length};
}

static_assert(kExit.diverge_ft + kExit.decel_ft == 1000.0f, "exit ramp spans 1000 ft");
static_assert(kExit.end_frac > 0.0f && kExit.end_frac < 1.0f);

}  // namespace swish::road
