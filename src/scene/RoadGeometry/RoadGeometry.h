#pragma once

#include "../../utils/Types.h"  // Vec3
#include "../SceneTypes.h"      // Ribbon

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
    static constexpr float kChunkLen        = 300000.0f;   // 300 m
    static constexpr int   kChunksAhead     = 8;           // 8·300 m = 2.4 km ≈ camera far plane
    static constexpr int   kChunksBehind    = 2;
    static constexpr float kRebaseThreshold = 1200000.0f;  // rebase when |render Z| exceeds ~1.2 km
    static constexpr float kCameraFar       = 2400000.0f;  // ≈ kChunksAhead·kChunkLen (chunks fill to here)

    // ── Road cross-section constants (must match road.bin) ────────────
    static constexpr float kFt           = 0.3048f * 1000.0f;
    static constexpr float kBarrierRight = (2.67f + 3.0f) * kFt;  // ~1728 WU
    static constexpr float kLaneWidth    = 13.0f * kFt;           // ~3962 WU
    static constexpr float kLaneCount    = 4.0f;
    static constexpr float kCrownSlope   = 0.02f;  // RoadConfig::m_crown_slope
    static constexpr float kMarkingY     = 5.0f;   // marking_y_offset from road.bin
    static constexpr float kEbRoadRight  = kBarrierRight + kLaneCount * kLaneWidth + 10.0f * kFt;

    // EB frontage ("marginal") road X window + surface lift (matches
    // generate_service_roads: ~55 ft berm past the mainline edge, two 12 ft
    // lanes, lifted 6 WU above the grass).
    static constexpr float kEbServiceInner = kEbRoadRight + 50.0f * kFt;
    static constexpr float kEbServiceOuter = kEbRoadRight + 92.0f * kFt;
    static constexpr float kServiceY       = 6.0f;

    // Convenience drivable-lane bounds (mainline, inset 100 WU from the edges).
    static constexpr float kWbInnerBound = -kBarrierRight - 100.f;
    static constexpr float kWbOuterBound = -kEbRoadRight + 100.f;
    static constexpr float kEbInnerBound = kBarrierRight + 100.f;
    static constexpr float kEbOuterBound = kEbRoadRight - 100.f;

    // ── Getters (kept for callers that prefer accessors over the fields) ──
    float get_chunk_len() const { return kChunkLen; }
    int   get_chunks_ahead() const { return kChunksAhead; }
    int   get_chunks_behind() const { return kChunksBehind; }
    float get_rebase_threshold() const { return kRebaseThreshold; }
    float get_camera_far() const { return kCameraFar; }

    // Road surface Y at world X using the crown formula. Symmetric about the
    // median (uses |x|) so it works on BOTH carriageways — EB (x>0) is
    // byte-identical to before; WB (x<0) mirrors it. On the EB frontage road
    // (reached via the interchange service ramp) it's flat at the service lift.
    static float road_surface_y(float x);

    // Guided drivable lateral bounds at the car's current (x, trueZ): normally
    // the EB (x>0) or WB (x<0) carriageway; inside a chunk crossover window the
    // bounds open across the median so the car can cross. In the authored intro
    // (trueZ ≥ −introLen) it's always EB — the pre-Layer-3 behavior.
    static void drivable_bounds(float x, double trueZ, float introLen, float& minX, float& maxX);

    // Total arc length of a ribbon centreline.
    static float ribbon_length(const Ribbon& r);

    // Position + unit tangent at arc length s along a ribbon (clamped to ends).
    static void ribbon_sample(const Ribbon& r, float s, Vec3& pos, Vec3& tan);
};

}  // namespace swish
