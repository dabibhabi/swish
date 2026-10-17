#include "CorridorScene.h"

#include "scene/RoadScene/RoadConfig.h"
#include "scene/RoadScene/RoadScene.h"  // MeshBuilder + emit_sweep_* + build_sweep_frames
#include "utils/container/VectorOps.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swish {

namespace {

// The look (tints, texture tile sizes) comes from the SAME baked road.bin the
// retired road used — there is one source of truth for "what LIE asphalt looks
// like", and it is config/road.toml. config/lie_corridor.toml owns the
// *geometry* (alignment, cross-section, compaction); this owns the *material*.
struct Look {
    Vec4  asphalt, grass, concrete, shoulder, white, yellow;
    float asphaltTile, grassTile, concreteTile;
};

Look look_from(const RoadConfig& c) {
    auto v = [](const float f[4]) { return Vec4(f[0], f[1], f[2], f[3]); };
    return Look{v(c.asphalt_tint),   v(c.grass_tint), v(c.concrete_tint), v(c.shoulder_tint), v(c.white_marking),
                v(c.yellow_marking), c.asphalt_tile,  c.grass_tile,       c.concrete_tile};
}

// A tile's worth of alignment: shared sweep frames plus the per-station
// lateral offsets that define the cross-section.
struct TileSlice {
    std::vector<SweepFrame> frames;
    std::vector<float>      lanes;
    float                   minLanes = 0.0f;
};

using util::container::affine;
using util::container::filled;

}  // namespace

std::vector<CorridorSegment> build_corridor_segments(const CorridorAlignment& al) {
    std::vector<CorridorSegment> out;
    if (!al.valid()) return out;

    const CorridorHeader& h    = al.header();
    const Look            look = look_from(load_road_config(CONFIG_DIR "road.bin"));

    const double total   = h.totalLength;
    const double segLen  = static_cast<double>(h.segmentLength);
    const double tileLen = static_cast<double>(h.tileLength);
    const double sp      = static_cast<double>(h.stationSpacing);
    if (segLen <= 0.0 || tileLen <= 0.0 || sp <= 0.0) return out;

    // Cross-section offsets (lateral from the centreline, +ve = right = EB).
    const float m         = h.medianHalfWidth;
    const float si        = h.innerShoulder;
    const float so        = h.outerShoulder;
    const float lw        = h.laneWidth;
    const float bw        = h.barrierWidth;
    const float ew        = h.edgeLine;
    const float g         = h.grassExtent;
    const float ry        = h.roadY;
    const float gy        = h.grassY;
    const float my        = h.roadY + h.markingLift;
    const float dashCycle = h.dashLength + h.dashGap;

    const int nSeg = static_cast<int>(std::ceil(total / segLen));
    out.reserve(static_cast<size_t>(std::max(nSeg, 0)));

    for (int k = 0; k < nSeg; ++k) {
        const double s0 = static_cast<double>(k) * segLen;
        const double s1 = std::min(s0 + segLen, total);
        if (s1 - s0 < sp) continue;

        CorridorSegment seg;
        seg.startStation = s0;
        seg.endStation   = s1;
        // Segment origin at its MIDDLE station: halves the magnitude of the
        // local coordinates versus anchoring at the start.
        const CorridorAlignment::Sample mid = al.sample(0.5 * (s0 + s1));
        seg.originX                         = mid.x;
        seg.originZ                         = mid.z;

        MeshBuilder builder(seg.mesh, seg.draws);

        for (double ts = s0; ts < s1 - 1e-6; ts += tileLen) {
            const double te = std::min(ts + tileLen, s1);

            // ── Build the tile's shared frames ────────────────────────
            // Station samples are taken at exact multiples of the station
            // spacing and INCLUDE the tile's far end, so the next tile's first
            // frame is the same alignment sample — the shared edge matches to
            // within a float rounding of the segment origin subtraction
            // (~0.06 WU), which is why tiles join without a crack.
            TileSlice         sl;
            std::vector<Vec3> cl, tan;
            const int         nSta = std::max(2, static_cast<int>(std::llround((te - ts) / sp)) + 1);
            cl.reserve(nSta);
            tan.reserve(nSta);
            sl.lanes.reserve(nSta);
            sl.minLanes = std::numeric_limits<float>::max();
            for (int i = 0; i < nSta; ++i) {
                const double                    s  = std::min(ts + static_cast<double>(i) * sp, te);
                const CorridorAlignment::Sample sm = al.sample(s);
                cl.emplace_back(static_cast<float>(sm.x - seg.originX), 0.0f, static_cast<float>(sm.z - seg.originZ));
                tan.emplace_back(sm.tx, 0.0f, sm.tz);
                sl.lanes.push_back(sm.lanes);
                sl.minLanes = std::min(sl.minLanes, sm.lanes);
            }
            sl.frames      = build_sweep_frames(cl, tan);
            const size_t n = sl.frames.size();
            if (n < 2) continue;

            // Dash phase: arc is tile-local (restarting at 0 keeps UV floats
            // small), so the global dash cycle has to be re-derived per tile or
            // the pattern jumps at every seam.
            const float phase = -static_cast<float>(std::fmod(ts, static_cast<double>(dashCycle)));

            // ── Per-station lateral offsets ───────────────────────────
            // outer edge of the travelled way = m + si + lanes*lw
            const std::vector<float> ebPaintOut = affine(sl.lanes, lw, m + si);
            const std::vector<float> ebEdge     = affine(sl.lanes, lw, m + si + so);
            const std::vector<float> ebGrass    = affine(sl.lanes, lw, m + si + so + g);
            const std::vector<float> wbPaintOut = affine(sl.lanes, -lw, -(m + si));
            const std::vector<float> wbEdge     = affine(sl.lanes, -lw, -(m + si + so));
            const std::vector<float> wbGrass    = affine(sl.lanes, -lw, -(m + si + so + g));

            // ── Grass verges ──────────────────────────────────────────
            builder.emit_sweep_band(sl.frames, ebEdge, ebGrass, gy, look.grass, MAT_GRASS, look.grassTile);
            builder.emit_sweep_band(sl.frames, wbGrass, wbEdge, gy, look.grass, MAT_GRASS, look.grassTile);

            // ── Carriageways: inner shoulder | lanes | outer shoulder ──
            builder.emit_sweep_band(sl.frames, filled(n, m), filled(n, m + si), ry, look.shoulder, MAT_ASPHALT,
                                    look.asphaltTile);
            builder.emit_sweep_band(sl.frames, filled(n, m + si), ebPaintOut, ry, look.asphalt, MAT_ASPHALT,
                                    look.asphaltTile);
            builder.emit_sweep_band(sl.frames, ebPaintOut, ebEdge, ry, look.shoulder, MAT_ASPHALT, look.asphaltTile);

            builder.emit_sweep_band(sl.frames, filled(n, -(m + si)), filled(n, -m), ry, look.shoulder, MAT_ASPHALT,
                                    look.asphaltTile);
            builder.emit_sweep_band(sl.frames, wbPaintOut, filled(n, -(m + si)), ry, look.asphalt, MAT_ASPHALT,
                                    look.asphaltTile);
            builder.emit_sweep_band(sl.frames, wbEdge, wbPaintOut, ry, look.shoulder, MAT_ASPHALT, look.asphaltTile);

            // ── Median: jersey barrier, or a U-turn pad at the ends ────
            // The pads (spec decision #7) are the only lateral opening in the
            // whole corridor; everywhere else the barrier commits you to your
            // carriageway, exactly as the retired system's drivable_bounds did.
            if (al.in_turnaround(0.5 * (ts + te))) {
                builder.emit_sweep_band(sl.frames, filled(n, -m), filled(n, m), ry, look.asphalt, MAT_ASPHALT,
                                        look.asphaltTile);
            } else {
                builder.emit_sweep_band(sl.frames, filled(n, -0.5f * bw), filled(n, 0.5f * bw), ry + h.barrierHeight,
                                        look.concrete, MAT_CONCRETE, look.concreteTile);
                builder.emit_sweep_wall(sl.frames, 0.5f * bw, ry, h.barrierHeight, true, look.concrete, MAT_CONCRETE,
                                        look.concreteTile);
                builder.emit_sweep_wall(sl.frames, -0.5f * bw, ry, h.barrierHeight, false, look.concrete, MAT_CONCRETE,
                                        look.concreteTile);
            }

            // ── Edge lines ────────────────────────────────────────────
            // On a divided highway the LEFT (median-side) edge line is yellow
            // and the RIGHT is white — per carriageway, both directions.
            builder.emit_sweep_band(sl.frames, filled(n, m + si), filled(n, m + si + ew), my, look.yellow);
            builder.emit_sweep_band(sl.frames, affine(ebPaintOut, 1.0f, -ew), ebPaintOut, my, look.white);
            builder.emit_sweep_band(sl.frames, filled(n, -(m + si + ew)), filled(n, -(m + si)), my, look.yellow);
            builder.emit_sweep_band(sl.frames, wbPaintOut, affine(wbPaintOut, 1.0f, ew), my, look.white);

            // ── Dashed lane lines ─────────────────────────────────────
            // Line count from the tile's MINIMUM lane count, so a line never
            // gets painted across a lane that is still opening up in a taper —
            // which is how a real lane-add is striped.
            const int lines = std::max(0, static_cast<int>(std::lround(sl.minLanes)) - 1);
            for (int j = 1; j <= lines; ++j) {
                const float t = m + si + static_cast<float>(j) * lw;
                builder.emit_sweep_dashes(sl.frames, t - 0.5f * ew, t + 0.5f * ew, my, h.dashLength, h.dashGap, phase,
                                          look.white);
                builder.emit_sweep_dashes(sl.frames, -t - 0.5f * ew, -t + 0.5f * ew, my, h.dashLength, h.dashGap, phase,
                                          look.white);
            }
        }

        // Coarse segment bounds (local space) for the "record at all?" test.
        Vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
        for (const Vertex& v : seg.mesh.getVertices()) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        if (!seg.mesh.empty()) {
            seg.boundsCenter = 0.5f * (mn + mx);
            seg.boundsRadius = 0.5f * glm::length(mx - mn);
            out.push_back(std::move(seg));
        }
    }
    return out;
}

}  // namespace swish
