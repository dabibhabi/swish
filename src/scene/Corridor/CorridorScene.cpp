#include "CorridorScene.h"

#include "CorridorBridges.h"
#include "CorridorDiamonds.h"
#include "CorridorDressing.h"
#include "CorridorEdges.h"
#include "CorridorEnds.h"
#include "CorridorLandmarks.h"
#include "CorridorSigns.h"
#include "scene/RoadScene/RoadConfig.h"
#include "scene/RoadScene/RoadScene.h"  // MeshBuilder + emit_sweep_* + build_sweep_frames
#include "utils/container/VectorOps.h"
#include "utils/io/AppPaths.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace swish {

namespace {

// The look (tints, texture tile sizes) comes from the SAME baked road.bin the
// retired road used — there is one source of truth for "what LIE asphalt looks
// like", and it is config/road.toml. config/lie_corridor.toml owns the
// *geometry* (alignment, cross-section, compaction); this owns the *material*.
using Look = CorridorLook;

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

// D1: aged highway PCC is mid grey; Concrete012 alone reads beige (albedo ~0.5, warm).
const Vec4 kPccTint(0.80f, 0.82f, 0.88f, 1.0f);

using util::container::affine;
using util::container::filled;

}  // namespace

std::vector<CorridorSegment> build_corridor_segments(const CorridorAlignment& al, bool withLamps,
                                                     const std::vector<CorridorPath>& paths,
                                                     const CorridorSigns*             signs) {
    std::vector<CorridorSegment> out;
    if (!al.valid()) return out;

    const CorridorHeader& h    = al.header();
    const Look            look = look_from(load_road_config(util::paths::config_dir() + "road.bin"));

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

    const CorridorHov hov = corridor_hov(al);  // C3a: empty without an exit table (release)

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
            const double te = std::min({ts + tileLen, s1, al.main_end()});  // C4: the roundabout takes over
            if (al.has_ends() && te - ts < sp) continue;

            // ── Build the tile's shared frames ────────────────────────
            // Station samples are taken at exact multiples of the station
            // spacing and INCLUDE the tile's far end, so the next tile's first
            // frame is the same alignment sample — the shared edge matches to
            // within a float rounding of the segment origin subtraction
            // (~0.06 WU), which is why tiles join without a crack.
            TileSlice          sl;
            std::vector<Vec3>  cl, tan;
            std::vector<float> banks;  // stays empty for the flat (v1) asset: frames are then exactly as before
            std::vector<float> wide;   // D2: extra median half-width per station (debug, east of exit 64)
            bool               anyWide = false;
            const bool         graded  = h.version != CorridorHeader::VERSION_FLAT;
            const int          nSta    = std::max(2, static_cast<int>(std::llround((te - ts) / sp)) + 1);
            cl.reserve(nSta);
            tan.reserve(nSta);
            sl.lanes.reserve(nSta);
            sl.minLanes = std::numeric_limits<float>::max();
            for (int i = 0; i < nSta; ++i) {
                const double                    s  = std::min(ts + static_cast<double>(i) * sp, te);
                const CorridorAlignment::Sample sm = al.sample(s);
                // C2b: the mainline dips under each crossroad (dip is 0 without grading, so release is unchanged)
                cl.emplace_back(static_cast<float>(sm.x - seg.originX), sm.y - al.dip(s),
                                static_cast<float>(sm.z - seg.originZ));
                if (graded) banks.push_back(sm.bank);
                tan.emplace_back(sm.tx, 0.0f, sm.tz);
                sl.lanes.push_back(sm.lanes);
                sl.minLanes = std::min(sl.minLanes, sm.lanes);
                wide.push_back(sm.med - m);
                anyWide = anyWide || sm.med > m;
            }
            sl.frames      = build_sweep_frames(cl, tan, banks);
            const size_t n = sl.frames.size();
            if (n < 2) continue;
            // D2: each carriageway sweeps off its own frames, shifted out by the median widening (bank carried)
            std::vector<SweepFrame> wideFrames[2];
            if (anyWide)
                for (const int w : {0, 1}) {
                    std::vector<Vec3> c  = cl;
                    const float       sg = w ? 1.0f : -1.0f;
                    for (size_t i = 0; i < c.size(); ++i)
                        c[i] += Vec3(-tan[i].z * sg * wide[i], -banks[i] * sg * wide[i], tan[i].x * sg * wide[i]);
                    wideFrames[w] = build_sweep_frames(c, tan, banks);
                }
            const std::vector<SweepFrame>& fE = anyWide ? wideFrames[1] : sl.frames;
            const std::vector<SweepFrame>& fW = anyWide ? wideFrames[0] : sl.frames;

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
            // Graded asset (C2b): level grass at ground height with cut slopes down to a dipped mainline or a ramp.
            if (graded) {
                for (const int side : {+1, -1})  // D4: cut where an LIE-over trench crosses (empty without bridges)
                    for (const auto& [a, z] : cmesh::minus(ts, te, bridge_verge_holes(al, side)))
                        if (z - a >= 1.0)
                            emit_corridor_verge(builder, seg.draws, al, paths, side, a, z, seg.originX, seg.originZ,
                                                look);
            } else {
                builder.emit_sweep_band(sl.frames, ebEdge, ebGrass, gy, look.grass, MAT_GRASS, look.grassTile);
                builder.emit_sweep_band(sl.frames, wbGrass, wbEdge, gy, look.grass, MAT_GRASS, look.grassTile);
            }
            emit_dressing(builder, seg.draws, al, paths, ts, te, seg.originX, seg.originZ);  // D5 (debug asset only)

            // ── D1 pavement (debug asset; Legacy = old look): material per stretch, tag for gbuffer.frag ──
            // shortcut: kind snaps to 300 m tiles, fine for km-long stretches; split the tile if a seam must be exact.
            const Pavement pv = al.surface_at(0.5 * (ts + te));
            const uint8_t  pTag =
                pv == Pavement::Legacy
                    ? 0
                    : static_cast<uint8_t>(static_cast<int>(pv) |
                                           std::clamp(static_cast<int>(std::lround(sl.minLanes)), 0, 15) << 4);
            const MaterialId laneMat = pv == Pavement::Concrete ? MAT_CONCRETE
                                       : pv == Pavement::Fresh  ? MAT_ASPHALT_FRESH
                                                                : MAT_ASPHALT;
            const Vec4       laneCol = pv == Pavement::Concrete ? kPccTint : look.asphalt;
            const auto       tag     = [&](size_t from, bool shoulder) {
                for (size_t i = from; pTag != 0 && i < seg.draws.size(); ++i) {
                    seg.draws[i].surface = static_cast<uint8_t>(pTag | (shoulder ? 8 : 0));
                    seg.draws[i].arc0    = static_cast<float>(ts);
                }
            };
            size_t d = seg.draws.size();

            // ── Carriageways: inner shoulder | lanes | outer shoulder ──
            builder.emit_sweep_band(fE, filled(n, m), filled(n, m + si), ry, look.shoulder, MAT_ASPHALT,
                                    look.asphaltTile);
            tag(d, true);
            // C4: on a banked (graded) asset a lane-wide quad folds along its diagonal up to ~13 WU off the plane, over
            // the 5 WU paint; one band per lane puts every lane line on a band edge. One draw either way.
            const int  laneLines  = std::max(0, static_cast<int>(std::lround(sl.minLanes)) - 1);
            const auto lanes_band = [&](float sg, const std::vector<float>& paintOut) {
                const uint32_t                 mk = builder.batch_mark();
                const size_t                   d0 = seg.draws.size();
                float                          a  = m + si;
                const std::vector<SweepFrame>& fr = sg > 0.0f ? fE : fW;
                for (int j = 1; j <= laneLines; ++j, a += lw)
                    builder.emit_sweep_band(fr, filled(n, std::min(sg * a, sg * (a + lw))),
                                            filled(n, std::max(sg * a, sg * (a + lw))), ry, laneCol, laneMat,
                                            look.asphaltTile);
                const std::vector<float> lo = sg > 0.0f ? filled(n, a) : paintOut;
                const std::vector<float> hi = sg > 0.0f ? paintOut : filled(n, -a);
                builder.emit_sweep_band(fr, lo, hi, ry, laneCol, laneMat, look.asphaltTile);
                seg.draws.resize(d0);  // fold the per-lane draws back into one
                builder.close_batch(mk, laneCol, laneMat);
            };
            d = seg.draws.size();
            if (graded)
                lanes_band(1.0f, ebPaintOut);
            else
                builder.emit_sweep_band(fE, filled(n, m + si), ebPaintOut, ry, laneCol, laneMat, look.asphaltTile);
            tag(d, false);
            d = seg.draws.size();
            builder.emit_sweep_band(fE, ebPaintOut, ebEdge, ry, look.shoulder, MAT_ASPHALT, look.asphaltTile);

            builder.emit_sweep_band(fW, filled(n, -(m + si)), filled(n, -m), ry, look.shoulder, MAT_ASPHALT,
                                    look.asphaltTile);
            tag(d, true);
            d = seg.draws.size();
            if (graded)
                lanes_band(-1.0f, wbPaintOut);
            else
                builder.emit_sweep_band(fW, wbPaintOut, filled(n, -(m + si)), ry, laneCol, laneMat, look.asphaltTile);
            tag(d, false);
            d = seg.draws.size();
            builder.emit_sweep_band(fW, wbEdge, wbPaintOut, ry, look.shoulder, MAT_ASPHALT, look.asphaltTile);
            tag(d, true);

            // ── Median: jersey barrier, or a U-turn pad at the ends ────
            // The pads (spec decision #7) are the only lateral opening in the
            // whole corridor; everywhere else the barrier commits you to your
            // carriageway, exactly as the retired system's drivable_bounds did.
            if (al.has_ends()) {
                emit_median(builder, seg.draws, al, ts, te, seg.originX, seg.originZ, look);  // D2 (C4 opening kept)
            } else if (al.in_turnaround(0.5 * (ts + te))) {
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

            const size_t firstMarking = seg.draws.size();
            const float  pt = pTag != 0 ? look.asphaltTile : 0.0f;  // D1: paint UVs for wear breakup (debug asset only)
            // ── Edge lines ────────────────────────────────────────────
            // On a divided highway the LEFT (median-side) edge line is yellow
            // and the RIGHT is white — per carriageway, both directions.
            builder.emit_sweep_band(fE, filled(n, m + si), filled(n, m + si + ew), my, look.yellow, MAT_DEFAULT, pt);
            builder.emit_sweep_band(fE, affine(ebPaintOut, 1.0f, -ew), ebPaintOut, my, look.white, MAT_DEFAULT, pt);
            builder.emit_sweep_band(fW, filled(n, -(m + si + ew)), filled(n, -(m + si)), my, look.yellow, MAT_DEFAULT,
                                    pt);
            builder.emit_sweep_band(fW, wbPaintOut, affine(wbPaintOut, 1.0f, ew), my, look.white, MAT_DEFAULT, pt);

            // ── Dashed lane lines ─────────────────────────────────────
            // Line count from the tile's MINIMUM lane count, so a line never
            // gets painted across a lane that is still opening up in a taper —
            // which is how a real lane-add is striped.
            const int lines = std::max(0, static_cast<int>(std::lround(sl.minLanes)) - 1);
            for (int j = 1; j <= lines; ++j) {
                const float t = m + si + static_cast<float>(j) * lw;
                // C4: solid lane lines in the tunnel (no lane changes), dashes again past the portal
                if (al.in_tunnel(ts)) {
                    const double pz = std::min(te, al.ends().portal);
                    const auto   f  = corridor_mainline_frames(al, ts, pz, seg.originX, seg.originZ);
                    for (const float c : {t, -t})
                        builder.emit_sweep_band(f, filled(f.size(), c - 0.5f * ew), filled(f.size(), c + 0.5f * ew), my,
                                                look.white, MAT_DEFAULT, pt);
                    if (te - pz > 1.0) {
                        const auto  g  = corridor_mainline_frames(al, pz, te, seg.originX, seg.originZ);
                        const float ph = -static_cast<float>(std::fmod(pz, static_cast<double>(dashCycle)));
                        builder.emit_sweep_dashes(g, t - 0.5f * ew, t + 0.5f * ew, my, h.dashLength, h.dashGap, ph,
                                                  look.white, MAT_DEFAULT, pt);
                        builder.emit_sweep_dashes(g, -t - 0.5f * ew, -t + 0.5f * ew, my, h.dashLength, h.dashGap, ph,
                                                  look.white, MAT_DEFAULT, pt);
                    }
                    continue;
                }
                // C3a: in the HOV range the lane 1 | 2 line is the HOV buffer (emit_hov); dashes only outside it
                if (j == 1 && hov.s1 > ts && hov.s0 < te) {
                    for (const auto& k : {std::pair{ts, std::min(te, hov.s0)}, std::pair{std::max(ts, hov.s1), te}}) {
                        if (k.second - k.first < 1.0) continue;
                        const auto  f  = corridor_mainline_frames(al, k.first, k.second, seg.originX, seg.originZ);
                        const float ph = -static_cast<float>(std::fmod(k.first, static_cast<double>(dashCycle)));
                        builder.emit_sweep_dashes(f, t - 0.5f * ew, t + 0.5f * ew, my, h.dashLength, h.dashGap, ph,
                                                  look.white, MAT_DEFAULT, pt);
                        builder.emit_sweep_dashes(f, -t - 0.5f * ew, -t + 0.5f * ew, my, h.dashLength, h.dashGap, ph,
                                                  look.white, MAT_DEFAULT, pt);
                    }
                    continue;
                }
                builder.emit_sweep_dashes(fE, t - 0.5f * ew, t + 0.5f * ew, my, h.dashLength, h.dashGap, phase,
                                          look.white, MAT_DEFAULT, pt);
                builder.emit_sweep_dashes(fW, -t - 0.5f * ew, -t + 0.5f * ew, my, h.dashLength, h.dashGap, phase,
                                          look.white, MAT_DEFAULT, pt);
            }
            // Lighting L8.1: road paint is retroreflective (glass beads). A tag only; the debug shader's spot
            // loop turns it into the glow you see in your own headlights.
            for (size_t i = firstMarking; i < seg.draws.size(); ++i) seg.draws[i].retro = 1.0f;
            tag(firstMarking, false);  // D1: faded paint per stretch
        }

        // ── Ramps / service roads (C1, C2b): clipped to this segment, so one crossing the edge is split ──
        for (const CorridorPath& p : paths) {
            if (p.kind == CorridorPath::Kind::Plain)
                emit_corridor_path(builder, corridor_path_frames(al, p, s0, s1, seg.originX, seg.originZ), p,
                                   look.asphalt, look.white, look.yellow, look.asphaltTile);
            else
                emit_diamond_path(builder, seg.draws, al, p, paths, s0, s1, seg.originX, seg.originZ, look);
        }
        // ── HOV buffer and diamonds (C3a) ──
        emit_hov(builder, seg.draws, al, hov, s0, s1, seg.originX, seg.originZ, look);
        // ── Crossroads (C2b): whole, in the segment holding their station ──
        for (const CorridorCrossroad& c : al.crossroads())
            if (c.s >= s0 && c.s < s1)
                emit_crossroad(builder, seg.draws, al, c, paths, seg.originX, seg.originZ, look, seg.signals);
        // ── Bridges (D4): whole, in the segment holding their station ──
        for (const CorridorBridge& br : al.bridges())
            if (br.s >= s0 && br.s < s1) emit_bridge(builder, seg.draws, al, br, paths, seg.originX, seg.originZ, look);

        // ── Route ends (C4, debug): tunnel, portal; signs and the roundabout. D2 walls + W-beam. D6 landmarks ──
        if (al.has_ends()) {
            emit_tunnel(builder, seg.draws, al, s0, s1, seg.originX, seg.originZ, look);
            emit_road_end(builder, seg.draws, al, s0, s1, seg.originX, seg.originZ, look);
            emit_edges(builder, al, paths, s0, s1, seg.originX, seg.originZ, look);
            emit_landmarks(builder, al, s0, s1, seg.originX, seg.originZ, look);
        }
        if (signs) emit_signs(builder, seg.draws, al, paths, *signs, s0, s1, seg.originX, seg.originZ, look);  // D3

        // ── Street lamps per district kit (lighting L6.1, D5) ──
        if (withLamps)
            emit_lamps(builder, seg.draws, seg.lamps, al, paths, signs, s0, s1, seg.originX, seg.originZ, look);

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
