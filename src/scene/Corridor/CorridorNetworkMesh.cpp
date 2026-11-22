#include "CorridorDiamonds.h"
#include "CorridorNetwork.h"
#include "CorridorSigns.h"
#include "scene/RoadScene/RoadScene.h"  // MeshBuilder + build_sweep_frames
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace swish {

namespace {

namespace nw = road::network;
using util::container::filled;
using util::units::ft;
using util::units::m;

// Frames [i0, i1] of a ramp, local to (ox, oz), banked where it rides on its road.
std::vector<SweepFrame> ramp_frames(const NetRamp& rp, size_t i0, size_t i1, double ox, double oz) {
    std::vector<Vec3>  cl, tan;
    std::vector<float> bank;
    for (size_t i = i0; i <= i1 && i < rp.pts.size(); ++i) {
        const NetRampPoint& p = rp.pts[i];
        cl.emplace_back(static_cast<float>(p.x - ox), p.y, static_cast<float>(p.z - oz));
        tan.emplace_back(p.tx, 0.0f, p.tz);
        bank.push_back(p.bank);
    }
    return build_sweep_frames(cl, tan, bank);
}

// Point index range [i0, i1] of a ramp clear of its gores: kGoreClear past each road-attached pin.
void structure_range(const NetRamp& rp, size_t& i0, size_t& i1) {
    const float clear = m(20.0f);
    const float u0    = rp.from != 255 ? rp.pin[0] + clear : 0.0f,
                u1    = rp.length() - (rp.to != 255 ? rp.pin[1] + clear : 0.0f);
    i0 = rp.pts.size(), i1 = 0;
    for (size_t i = 0; i < rp.pts.size(); ++i)
        if (rp.pts[i].u >= u0 && rp.pts[i].u <= u1) i0 = std::min(i0, i), i1 = std::max(i1, i);
}

// Segment-local bounding sphere of everything in the mesh.
void fit_bounds(CorridorSegment& seg) {
    Vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
    for (const Vertex& v : seg.mesh.getVertices()) mn = glm::min(mn, v.position), mx = glm::max(mx, v.position);
    seg.boundsCenter = 0.5f * (mn + mx);
    seg.boundsRadius = 0.5f * glm::length(mx - mn);
}

// N3: a ramp's paintable band per point. Where it lies on its road, only the part past the road's outer edge is
// drawn (the road's own lanes and edge lines show through: no ramp paint across a carriageway); its left edge line
// is white through the gore (kGoreLen past the pin) where the road cut its left side.
struct RampCut {
    std::vector<float> lo, hi;
    std::vector<char>  gore;
};

RampCut ramp_cut(const CorridorNetwork& net, const NetRamp& rp) {
    const size_t n = rp.pts.size();
    RampCut      c{filled(n, -rp.halfWidth), filled(n, rp.halfWidth), std::vector<char>(n, 0)};
    for (int e = 0; e < 2; ++e) {
        const int rid = e ? rp.to : rp.from;
        if (rid >= static_cast<int>(net.road_count())) continue;
        const CorridorAlignment& al   = net.road(rid);
        size_t                   h    = al.index_at(rp.endS[e]);
        bool                     cut  = false;
        float                    uPin = -1.0f;
        for (size_t j = 0; j < n; ++j) {
            const size_t        i = e ? n - 1 - j : j;
            const NetRampPoint& q = rp.pts[i];
            const float         d = e ? rp.length() - q.u : q.u;  // arc length from this end
            if (d > rp.pin[e] + m(1.0f)) break;
            double s = 0.0;
            float  t = 0.0f;
            al.project(q.x, q.z, h, s, t);
            const auto  sm  = al.sample(s);
            const float sg  = t >= 0.0f ? 1.0f : -1.0f,
                        k   = sg * (q.tx * sm.tx + q.tz * sm.tz);  // ramp right . road right
            const float lim = (al.outer_edge(sm) - sg * t) / (std::abs(k) < 0.2f ? (k < 0.0f ? -0.2f : 0.2f) : k);
            if (k > 0.0f)
                c.lo[i] = std::max(c.lo[i], lim), cut = true;
            else
                c.hi[i] = std::min(c.hi[i], lim);
            c.lo[i] = std::min(c.lo[i], c.hi[i]);
            uPin    = d;
        }
        if (!cut) continue;
        for (size_t i = 0; i < n; ++i)
            if ((e ? rp.length() - rp.pts[i].u : rp.pts[i].u) <= uPin + m(nw::kGoreLen)) c.gore[i] = 1;
    }
    return c;
}

}  // namespace

std::vector<CorridorSegment> build_network_segments(const CorridorNetwork& net, const WorkZoneParams& wzp,
                                                    const StructureParams& sp) {
    std::vector<CorridorSegment> out;
    if (!net.valid()) return out;
    const CorridorLook look = corridor_look();
    // Roads 1..5: the LIE's own builders, mainline only; N3 adds each road's viaducts, trenches and overpasses.
    // LIE-junction ramp footprints (boxes over 40 m runs, padded): no structure ground band there, where the LIE's own
    // verge (on the LIE's level, not the DEM) and the ramps own the ground
    std::vector<XzBox> footprints;
    for (const NetRamp& rp : net.ramps())
        for (size_t i = 0; i < rp.pts.size(); i += 10) {
            const NetworkJunction& j = net.asset().junctions[rp.junction];
            if (j.roadA != 0 && j.roadB != 0) break;
            XzBox        k{1e300, 1e300, -1e300, -1e300};
            const double pad = static_cast<double>(rp.halfWidth + m(25.0f));
            for (size_t q = i; q < std::min(rp.pts.size(), i + 11); ++q)
                k = {std::min(k[0], rp.pts[q].x - pad), std::min(k[1], rp.pts[q].z - pad),
                     std::max(k[2], rp.pts[q].x + pad), std::max(k[3], rp.pts[q].z + pad)};
            footprints.push_back(k);
        }
    for (int r = 1; r < static_cast<int>(net.road_count()); ++r) {
        const CorridorAlignment&              al    = net.road(r);
        const CorridorSigns                   signs = sp.signs ? network_signs(net, r) : CorridorSigns{};
        std::vector<const CorridorAlignment*> unders;  // N5: every other road its viaducts may span
        for (int u = 0; u < static_cast<int>(net.road_count()); ++u)
            if (u != r) unders.push_back(&net.road(u));
        // N7: lamps and dressing from its LOOK rows once dress() ran (edges().ground set)
        for (CorridorSegment& s : build_corridor_segments(net.road(r), al.edges().ground != nullptr, {}, &signs)) {
            const bool any =
                std::any_of(al.crossings().begin(), al.crossings().end(),
                            [&](const CorridorCrossing& x) {
                                return x.station >= s.startStation && x.station < s.endStation;
                            }) ||
                std::any_of(net.structures(r).begin(), net.structures(r).end(),
                            [&](const RoadStructure& st) { return st.s1 > s.startStation && st.s0 < s.endStation; });
            if (any) {
                MeshBuilder b(s.mesh, s.draws);
                emit_road_structures(b, s.draws, al, net.structures(r), unders, footprints, s.startStation,
                                     s.endStation, s.originX, s.originZ, look, sp);
                for (const CorridorCrossing& x : al.crossings())
                    if (x.station >= s.startStation && x.station < s.endStation &&
                        !(sp.landmarks && landmark_replaces(net, r, x.station)))  // N8: Jamaica Yard draws its own
                        emit_overpass(b, s.draws, al, x, s.originX, s.originZ, look, sp);
                fit_bounds(s);
            }
            out.push_back(std::move(s));
        }
    }

    // Ramps: one segment per junction, one draw per material (they cull as a group, ~1 km across); N6: a closed local
    // exit is a segment of its own with its EXIT CLOSED kit (they lie along the roads, km from any junction).
    const float lw = ft(0.5f), my = road::kLie.marking_y, pw = m(nw::kParapetW), ph = m(nw::kParapetH);
    const float deck         = m(nw::kDeck);
    const auto  ramp_segment = [&](double ox, double oz, const std::vector<const NetRamp*>& rs) {
        CorridorSegment seg;
        seg.originX = ox;
        seg.originZ = oz;
        MeshBuilder                          b(seg.mesh, seg.draws);
        std::vector<RampCut>                 cuts;
        std::vector<std::vector<SweepFrame>> fs;
        for (const NetRamp* rp : rs) {
            cuts.push_back(ramp_cut(net, *rp));
            fs.push_back(ramp_frames(*rp, 0, rp->pts.size() - 1, seg.originX, seg.originZ));
        }
        // Fold every band of `emit` over all ramps into one draw.
        const auto batch = [&](const Vec4& col, MaterialId mat, auto emit) {
            const uint32_t mk = b.batch_mark();
            const size_t   d0 = seg.draws.size();
            for (size_t k = 0; k < rs.size(); ++k) emit(*rs[k], cuts[k], fs[k]);
            seg.draws.resize(d0);
            b.close_batch(mk, col, mat);
        };
        batch(look.asphalt, MAT_ASPHALT, [&](const NetRamp&, const RampCut& c, const std::vector<SweepFrame>& f) {
            b.emit_sweep_band(f, c.lo, c.hi, 0.0f, look.asphalt, MAT_ASPHALT, look.asphaltTile);
        });
        const size_t paint = seg.draws.size();
        // MUTCD 3B.06: yellow edge line on the left of a one-way roadway, white on the right (and through a gore)
        const auto edge = [&](bool left, bool gore) {
            return [&, left, gore](const NetRamp& rp, const RampCut& c, const std::vector<SweepFrame>& f) {
                const float e0 = left ? -rp.halfWidth + ft(nw::kShoulderLFt) : rp.halfWidth - ft(nw::kShoulderRFt) - lw;
                std::vector<float> lo(f.size()), hi(f.size());
                for (size_t i = 0; i < f.size(); ++i) {
                    const bool on = !left || (c.gore[i] != 0) == gore;
                    lo[i]         = std::max(e0, c.lo[i]);
                    hi[i]         = on ? std::max(lo[i], std::min(e0 + lw, c.hi[i])) : lo[i];
                }
                b.emit_sweep_band(f, lo, hi, my, look.white);
            };
        };
        batch(look.yellow, MAT_DEFAULT, edge(true, false));
        batch(look.white, MAT_DEFAULT, [&](const NetRamp& rp, const RampCut& c, const std::vector<SweepFrame>& f) {
            edge(true, true)(rp, c, f);
            edge(false, false)(rp, c, f);
            // lane lines of a multi-lane ramp (a Y leg), 10 ft dashes on 40 ft, clear of where it rides its road
            size_t i0 = f.size(), i1 = 0;
            for (size_t i = 0; i < f.size(); ++i)
                if (c.lo[i] <= -rp.halfWidth + 1.0f && c.hi[i] >= rp.halfWidth - 1.0f) i0 = std::min(i0, i), i1 = i;
            if (rp.lanes < 2 || i1 <= i0 + 1) return;
            const std::vector<SweepFrame> sub(f.begin() + static_cast<long>(i0), f.begin() + static_cast<long>(i1) + 1);
            for (int k = 1; k < rp.lanes; ++k) {
                const float l = -rp.halfWidth + ft(nw::kShoulderLFt) + ft(12.0f) * static_cast<float>(k);
                b.emit_sweep_dashes(sub, l - 0.5f * lw, l + 0.5f * lw, my, ft(10.0f), ft(30.0f), 0.0f, look.white);
            }
        });
        for (size_t i = paint; i < seg.draws.size(); ++i) seg.draws[i].retro = 1.0f;  // L8.1 glass beads
        // Concrete: parapets clear of the gores; on high stretches a deck edge, soffit and piers.
        batch(look.concrete, MAT_CONCRETE, [&](const NetRamp& rp, const RampCut&, const std::vector<SweepFrame>&) {
            const float hw = rp.halfWidth;
            size_t      i0 = 0, i1 = 0;
            structure_range(rp, i0, i1);
            if (i1 <= i0) return;
            const auto f = ramp_frames(rp, i0, i1, seg.originX, seg.originZ);
            for (const float sg : {-1.0f, 1.0f}) {
                const float in = sg * hw, outer = sg * (hw + pw);
                b.emit_sweep_wall(f, in, 0.0f, ph, sg < 0.0f, look.concrete, MAT_CONCRETE, look.concreteTile);
                b.emit_sweep_wall(f, outer, -deck, deck + ph, sg > 0.0f, look.concrete, MAT_CONCRETE,
                                  look.concreteTile);
                b.emit_sweep_band(f, filled(f.size(), std::min(in, outer)), filled(f.size(), std::max(in, outer)), ph,
                                  look.concrete, MAT_CONCRETE, look.concreteTile);
            }
            std::vector<Vec3> L, R;  // soffit, faces down (edges swapped)
            for (const SweepFrame& fr : f) {
                L.push_back(fr.center + fr.right * (hw + pw) - Vec3(0.0f, deck, 0.0f));
                R.push_back(fr.center - fr.right * (hw + pw) - Vec3(0.0f, deck, 0.0f));
            }
            cmesh::soffit(b, L, R, look.concrete, MAT_CONCRETE, look.concreteTile);
            // piers: one column every kPierEvery where the deck stands kPierMinH over the ground
            float next = rp.pts[i0].u;
            for (size_t i = i0; i <= i1; ++i) {
                const NetRampPoint& p = rp.pts[i];
                if (p.u < next || p.y - p.ground < m(nw::kPierMinH)) continue;
                next           = p.u + m(nw::kPierEvery);
                const float y0 = p.ground - m(1.0f), y1 = p.y - deck;
                const Vec3  c(static_cast<float>(p.x - seg.originX), 0.5f * (y0 + y1),
                              static_cast<float>(p.z - seg.originZ));
                const float half = 0.5f * m(nw::kPierW) * (rp.lanes > 1 ? 1.5f : 1.0f);
                cmesh::obox(b, c, Vec3(p.tx, 0.0f, p.tz) * half, Vec3(0.0f, 0.5f * (y1 - y0), 0.0f),
                            Vec3(-p.tz, 0.0f, p.tx) * half);
            }
        });
        return seg;
    };
    for (const NetworkJunction& j : net.asset().junctions) {
        std::vector<const NetRamp*> rs;
        for (const NetRamp& rp : net.ramps())
            if (rp.junction == j.id && !rp.closed) rs.push_back(&rp);
        CorridorSegment seg = ramp_segment(j.x, j.z, rs);
        if (seg.mesh.empty()) continue;
        fit_bounds(seg);
        out.push_back(std::move(seg));
    }
    for (size_t k = 0; k < net.ramps().size(); ++k) {
        const NetRamp& rp = net.ramps()[k];
        if (!rp.closed) continue;
        CorridorSegment seg = ramp_segment(rp.pts.front().x, rp.pts.front().z, {&rp});
        MeshBuilder     b(seg.mesh, seg.draws);
        emit_work_zone(b, seg.draws, seg.signals, exit_closure(net, static_cast<int>(k), wzp), seg.originX, seg.originZ,
                       look);
        fit_bounds(seg);
        out.push_back(std::move(seg));
    }
    // N8: landmarks, one segment each
    if (sp.landmarks)
        for (CorridorSegment& s : network_landmark_segments(net, look, sp)) {
            fit_bounds(s);
            out.push_back(std::move(s));
        }
    // N7: the network ground tiles
    if (net.ground().valid())
        for (CorridorSegment& s : net.ground().segments(look)) out.push_back(std::move(s));
    // N2b: one segment per work zone (signs, channelizers, closure, debris, extruded horizon tail), origin at the wall
    for (const WorkZone& wz : net.work_zones(wzp)) {
        CorridorSegment seg;
        const auto      sm = net.road(wz.road).sample(wz.wall);
        seg.originX = sm.x, seg.originZ = sm.z;
        seg.startStation = seg.endStation = wz.wall;
        MeshBuilder b(seg.mesh, seg.draws);
        emit_work_zone(b, seg.draws, seg.signals, wz, seg.originX, seg.originZ, look);
        emit_work_zone_tail(b, seg.draws, net.road(wz.road), wz, seg.originX, seg.originZ, look);
        if (seg.mesh.empty()) continue;
        Vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
        for (const Vertex& v : seg.mesh.getVertices()) mn = glm::min(mn, v.position), mx = glm::max(mx, v.position);
        seg.boundsCenter = 0.5f * (mn + mx);
        seg.boundsRadius = 0.5f * glm::length(mx - mn);
        out.push_back(std::move(seg));
    }
    return out;
}

}  // namespace swish
