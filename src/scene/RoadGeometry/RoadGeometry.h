#pragma once

#include "scene/RoadScene/RoadParams.h"
#include "scene/SceneTypes.h"   // Ribbon
#include "utils/types/Types.h"  // Vec3
#include "utils/units/Units.h"

namespace swish {

// ── RoadGeometry ──────────────────────────────────────────────────────
// Pure geometry helpers + tuning constants for the LIE road, extracted from
// App::run() so they're reusable and unit-testable. Stateless: every method is
// a pure function of its arguments (and the compile-time constants below), so a
// single shared instance is fine.
//
// Endless-road treadmill tuning: kChunkLen is 300 m and a multiple of every
// surface tile size (asphalt 3000, grass 4000, concrete 1500, metal 1000 → LCM
// 12000; 300000 = 25·12000), so the canonical chunk's surface UVs tile
// seamlessly. The active window spans a few chunks behind the car and enough
// ahead to reach the camera far plane; the origin is rebased (by a whole number
// of chunk lengths) whenever the car's render-frame Z exceeds the threshold,
// keeping float32 coordinates precise.
class RoadGeometry {
public:
    RoadGeometry();
    ~RoadGeometry();

    // ── Treadmill tuning (world units) ────────────────────────────────
    static constexpr float kChunkLen        = 300000.0f;  // 300 m
    static constexpr int   kChunksAhead     = 8;          // 8·300 m = 2.4 km ≈ camera far plane
    static constexpr int   kChunksBehind    = 2;
    static constexpr float kRebaseThreshold = 1200000.0f;  // rebase when |render Z| exceeds ~1.2 km
    static constexpr float kCameraFar       = 2400000.0f;  // ≈ kChunksAhead·kChunkLen (chunks fill to here)

    // ── Road cross-section constants (values live in RoadParams.h) ────
    static constexpr float kFt           = util::units::kWuPerFt;
    static constexpr float kBarrierRight = (road::kLie.barrier_ft + road::kLie.clearance_ft) * kFt;  // ~1728 WU
    static constexpr float kLaneWidth    = road::kLie.lane_ft * kFt;                                  // ~3962 WU
    static constexpr float kLaneCount    = road::kLie.lanes;
    static constexpr float kCrownSlope   = road::kCrownSlope;
    static constexpr float kMarkingY     = road::kLie.marking_y;
    static constexpr float kEbRoadRight =
        kBarrierRight + kLaneCount * kLaneWidth + road::kLie.shoulder_eb_ft * kFt;

    // EB frontage ("marginal") road X window + surface lift (RoadParams.h kService).
    static constexpr float kEbServiceInner = kEbRoadRight + road::kService.inner_ft * kFt;
    static constexpr float kEbServiceOuter = kEbRoadRight + road::kService.outer_ft * kFt;
    static constexpr float kServiceY       = road::kService.y;

    // ── Intro EB exit -> N Marginal (values live in RoadParams.h kExit) ──
    static constexpr float kExitRampEndFrac    = road::kExit.end_frac;
    static constexpr float kExitDivergeFt      = road::kExit.diverge_ft;
    static constexpr float kExitDecelFt        = road::kExit.decel_ft;
    static constexpr float kExitGantryLeadFt   = road::kExit.gantry_lead_ft;
    static constexpr float kExitRampOffsetFt   = road::kExit.offset_ft;
    static constexpr float kExitRampWidthFt    = road::kExit.width_ft;
    static constexpr float kExitMarginalGapFt  = road::kExit.marginal_gap_ft;
    static constexpr float kExitMarginalLaneFt = road::kExit.marginal_lane_ft;
    static constexpr float kExitConnectFt      = road::kExit.connect_ft;

    // N Marginal X band (flat asphalt at marking height for the car snap).
    static constexpr float kIntroMarginalInner =
        kEbRoadRight + (kExitRampOffsetFt + kExitRampWidthFt + kExitMarginalGapFt) * kFt;
    static constexpr float kIntroMarginalOuter = kIntroMarginalInner + 2.0f * kExitMarginalLaneFt * kFt;
    // Lift exit/marginal asphalt above grass (same idea as endless service y=6).
    static constexpr float kIntroExitAsphaltY = road::kExit.asphalt_y;

    // Intro exit Z stations for a given authored length (positive introLen, WU).
    static float intro_exit_ramp_end_z(float introLen) { return -kExitRampEndFrac * introLen; }
    static float intro_exit_ramp_start_z(float introLen) {
        return intro_exit_ramp_end_z(introLen) + kExitDivergeFt * kFt;
    }
    static float intro_exit_decel_start_z(float introLen) {
        return intro_exit_ramp_start_z(introLen) + kExitDecelFt * kFt;
    }
    static float intro_exit_gantry_z(float introLen) {
        return intro_exit_ramp_start_z(introLen) + kExitGantryLeadFt * kFt;
    }

    // Convenience drivable-lane bounds (mainline, inset 100 WU from the edges).
    static constexpr float kWbInnerBound = -kBarrierRight - 100.f;
    static constexpr float kWbOuterBound = -kEbRoadRight + 100.f;
    static constexpr float kEbInnerBound = kBarrierRight + 100.f;
    static constexpr float kEbOuterBound = kEbRoadRight - 100.f;

    // Road surface Y at world X using the crown formula. Symmetric about the
    // median (uses |x|) so it works on BOTH carriageways — EB (x>0) is
    // byte-identical to before; WB (x<0) mirrors it. On the EB frontage road
    // (reached via the interchange service ramp) it's flat at the service lift.
    //
    // The crown formula derives height from world X, which only means anything
    // on an axis-aligned road. The Phase 3a real corridor curves, so world X is
    // not a lateral offset there — and that stretch of the LIE is flat anyway
    // (docs/phase-3-osm-lie-corridor.md §2). set_flat_surface() switches this
    // query to a single height so the corridor's mesh and its physics surface
    // agree exactly. Not called on the legacy authored/treadmill path, which
    // therefore keeps its crown byte-for-byte.
    static float road_surface_y(float x);
    static void  set_flat_surface(float y);
    static void  clear_flat_surface();
    static bool  flat_surface() { return s_flatSurface; }

    // Guided drivable lateral bounds at the car's current (x, trueZ): normally
    // the EB (x>0) or WB (x<0) carriageway; inside a chunk crossover window the
    // bounds open across the median so the car can cross. In the authored intro
    // (trueZ ≥ −introLen) it's EB, and past the exit decel station maxX opens
    // onto the N Marginal band so the car can take the diverge.
    static void drivable_bounds(float x, double trueZ, float introLen, float& minX, float& maxX);

    // Total arc length of a ribbon centreline.
    static float ribbon_length(const Ribbon& r);

    // Position + unit tangent at arc length s along a ribbon (clamped to ends).
    static void ribbon_sample(const Ribbon& r, float s, Vec3& pos, Vec3& tan);

private:
    // Flat-surface override (Phase 3a corridor). Process-wide because
    // road_surface_y is a static query called from CarEntity's wheel/body
    // placement, which has no corridor handle to consult. Defaults to off, so
    // every existing caller is unaffected.
    static bool  s_flatSurface;
    static float s_flatSurfaceY;
};

}  // namespace swish
