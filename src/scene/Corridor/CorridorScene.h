#pragma once

#include "CorridorAlignment.h"
#include "CorridorRamps.h"
#include "scene/SceneTypes.h"

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorScene — mesh generation for the real LIE corridor (Phase 3a).
//
// Replaces the retired treadmill's canonical-chunk generator. The old system
// could upload ONE 300 m tile and instance it, because every tile was
// identical; a real corridor is different everywhere, so the mesh has to
// exist in full — 18 mi of it.
//
// Two levels of subdivision, for two different reasons:
//
//   SEGMENT (1.2 km) — one vertex/index buffer and one render offset.
//     Vertices are stored RELATIVE to the segment's own origin, and the origin
//     (a double) is turned into a small render-frame offset each frame.
//     Absolute coordinates at the corridor's far end are 2.9e7 WU, where
//     float32's ULP is 2 WU (2 mm) — survivable, but this brings it to 0.06 WU
//     for free, and it is what keeps the scheme sound if the corridor ever
//     grows. (The design note's "~3e10 WU / 2 m ULP" applied WORLD_SCALE
//     twice; the measured figure is 1000x smaller. See CorridorAsset.h.)
//
//   TILE (300 m)     — one draw call per cross-section band, and one restart
//     of the arc-length UV. 300 m for the same reason the retired canonical
//     chunk was 300 m: it is a multiple of every surface texture tile size
//     (3000 / 4000 / 1500 / 1000 WU), so the UV restart is seamless. The
//     restart itself is required — arc length accumulated over the whole
//     corridor would divide down to ~1e4 UV units, where float32 quantises to
//     a whole texture repeat. Tiling also sets how tightly per-draw bounding
//     spheres fit, and therefore how well frustum/distance culling works.
//
// Everything is swept off CorridorAlignment, the same object the driving code
// queries, so the surface you see and the surface you drive on cannot drift.
// ══════════════════════════════════════════════════════════════════════

struct CorridorSegment {
    MeshData              mesh;
    std::vector<DrawCall> draws;
    // World position of this segment's local origin, in double so the
    // origin + shift arithmetic is exact before it is narrowed to a float
    // render offset.
    double originX = 0.0, originZ = 0.0;
    // Station span, for the visible-window test.
    double startStation = 0.0, endStation = 0.0;
    // Segment-local bounding sphere, for the coarse "record this segment at
    // all?" test that runs before per-draw culling.
    Vec3  boundsCenter = Vec3(0.0f);
    float boundsRadius = 0.0f;
    // Street lamps (lighting L6.1), segment-local like the vertices: one downward spot per pole.
    std::vector<SpotLightDesc> lamps;
    // C3a: signal lens draws (index into `draws`); App drives their colour + glow each frame (debug).
    std::vector<SignalLens> signals;
};

// What CorridorDriver needs to keep about a segment once its mesh has been
// handed to the renderer: where it is (double) and how big it is, for the
// coarse visible-window test.
struct CorridorPlacement {
    double                     originX = 0.0, originZ = 0.0;
    double                     startStation = 0.0, endStation = 0.0;
    Vec3                       boundsCenter = Vec3(0.0f);
    float                      boundsRadius = 0.0f;
    std::vector<SpotLightDesc> lamps;  // segment-local; the driver adds the render offset
};

inline CorridorPlacement placement_of(const CorridorSegment& s) {
    return CorridorPlacement{s.originX,      s.originZ,      s.startStation, s.endStation,
                             s.boundsCenter, s.boundsRadius, s.lamps};
}

// Builds every segment of the corridor. Called once at scene setup; the
// result is uploaded to the renderer and never regenerated (the corridor
// is finite and static — that is the whole point of Phase 3).
// (A free function: it was a class with a single static method and no state.)
// `withLamps` (lighting L6.1, debug first) adds street lamps per district (D5, CorridorDressing.h): Queens median
// twin arms every kCorridorLampSpacingM, shoulder singles elsewhere, one spot light per pole in CorridorSegment::lamps.
inline constexpr float kCorridorLampSpacingM = 60.0f;
inline constexpr float kCorridorLampHeightM  = 10.0f;
inline constexpr float kCorridorLampLumens   = 32000.0f;  // two 16,000 lm sodium luminaires per pole (typical)
// `paths` (C2b diamonds, debug only) are ramps / service roads swept into the segments they cross; crossroads
// come from alignment.crossroads(). A graded (v2) asset also gets the cut-slope verge (CorridorDiamonds.h).
// `signs` (D3, debug only): gantries, gore signs and mile markers, batched per segment.
struct CorridorSigns;
std::vector<CorridorSegment> build_corridor_segments(const CorridorAlignment& alignment, bool withLamps = false,
                                                     const std::vector<CorridorPath>& paths = {},
                                                     const CorridorSigns*             signs = nullptr);

}  // namespace swish
