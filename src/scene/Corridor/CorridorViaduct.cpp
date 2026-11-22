#include "CorridorViaduct.h"

#include "CorridorBridges.h"
#include "CorridorEdges.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace swish {

namespace {

namespace nw = road::network;
using util::container::filled;
using util::units::m;

const Vec3 kUp(0.0f, 1.0f, 0.0f);
const Vec4 kSteelQueens(0.42f, 0.56f, 0.46f, 1.0f);  // as CorridorBridges.cpp: weathered green paint (photo exit-17)

// The stations corridor_mainline_frames uses over [a, b] (one per frame).
std::vector<double> frame_stations(double a, double b, double sp) {
    std::vector<double> ss{a};
    for (double s = (std::floor(a / sp) + 1.0) * sp; s < b - 1.0; s += sp)
        if (s - a >= 1.0) ss.push_back(s);
    ss.push_back(b);
    return ss;
}

// Profile swept along frames on side sg: pt(i, k) = (d outward of the frame's side, h over the surface) of point k at
// frame i. Outward normal of a profile edge (dd, dh) is (-dh, dd), so walk each profile with the solid on its far side.
template <class Pt>
void sweep(MeshBuilder& b, const std::vector<SweepFrame>& f, float sg, float ry, size_t np, Pt pt, float tile) {
    const auto P = [&](size_t i, size_t k) {
        const Vec2 q = pt(i, k);
        return f[i].center + f[i].right * (sg * q.x) + kUp * (ry + q.y);
    };
    for (size_t i = 0; i + 1 < f.size(); ++i)
        for (size_t k = 0; k + 1 < np; ++k) {
            const Vec2 d = pt(i, k + 1) - pt(i, k);
            if (glm::length(d) < 1e-3f) continue;
            const Vec3 n = glm::normalize(f[i].right * (sg * -d.y) + kUp * d.x);
            cmesh::face(b, P(i, k), P(i, k + 1), P(i + 1, k), P(i + 1, k + 1), n, tile);
        }
}

}  // namespace

std::vector<RoadStructure> road_structures(const CorridorAlignment& al) {
    std::vector<RoadStructure> out;
    if (!al.has_terrain()) return out;
    const double sp    = static_cast<double>(al.header().stationSpacing);
    int          cur   = 0;
    double       a     = 0.0;
    const auto   close = [&](double z) {
        if (cur != 0 && z - a >= static_cast<double>(m(nw::kMinSpan))) out.push_back({a, z, cur < 0});
    };
    for (double s = 0.0; s <= al.total_length(); s += sp) {
        const float h = al.surface_y(s, 0.0f) - al.terrain_y(s);
        const int   k = h > m(nw::kViaductMinH) ? 1 : h < -m(nw::kTrenchMinD) ? -1 : 0;
        if (k == cur) continue;
        close(s);
        cur = k, a = s;
    }
    close(al.total_length());
    return out;
}

void emit_road_structures(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                          const std::vector<RoadStructure>& sts, const std::vector<const CorridorAlignment*>& unders,
                          const std::vector<XzBox>& keepOut, double from, double to, double ox, double oz,
                          const CorridorLook& look, const StructureParams& p) {
    const CorridorHeader& h  = al.header();
    const float           ry = h.roadY, deck = m(nw::kRoadDeck), ct = look.concreteTile;
    // NJ traffic face from the base outward (d, h), then the flat top of a 2 ft wide parapet
    const auto  nj = nj_profile();
    const float t = nj[0].x, W = 2.0f * t;
    const Vec2  face[4] = {{0.0f, 0.0f}, {0.0f, nj[1].y}, {t - nj[2].x, nj[2].y}, {t - nj[3].x, nj[3].y}};
    for (const RoadStructure& st : sts) {
        const double a = std::max(st.s0, from), z = std::min(st.s1, to);
        if (z - a < 1.0) continue;
        const auto   ss = frame_stations(a, z, static_cast<double>(h.stationSpacing));
        const auto   f  = corridor_mainline_frames(al, a, z, ox, oz);
        const size_t n  = std::min(ss.size(), f.size());
        if (n < 2) continue;
        std::vector<float>  E(n), tg(n), g(n);  // outer edge, DEM ground and eased ground band (over the surface)
        std::vector<char>   over(n, 0);         // deck spans an under-road's pavement here
        std::vector<char>   clash[2] = {std::vector<char>(n, 0), std::vector<char>(n, 0)};  // ground band would cut it
        std::vector<size_t> hints(unders.size(), 0);
        for (size_t u = 0; u < unders.size(); ++u) {
            double us = 0.0;
            float  ut = 0.0f;
            unders[u]->project_global(f[0].center.x + static_cast<float>(ox), f[0].center.z + static_cast<float>(oz),
                                      hints[u], us, ut);
        }
        for (size_t i = 0; i < n; ++i) {
            const auto sm = al.sample(ss[i]);
            E[i]          = al.outer_edge(sm);
            for (int sd = 0; sd < 2; ++sd) {  // the middle of each half of the ground band
                const double l  = static_cast<double>((sd ? 1.0f : -1.0f) * (E[i] + 0.5f * m(nw::kGroundW)));
                const double px = sm.x - l * static_cast<double>(sm.tz), pz = sm.z + l * static_cast<double>(sm.tx);
                for (const XzBox& k : keepOut)
                    if (px > k[0] && px < k[2] && pz > k[1] && pz < k[3]) clash[sd][i] = 1;
            }
            tg[i]         = al.terrain_y(ss[i]) - (ry + f[i].center.y);
            const float e = static_cast<float>(std::min(ss[i] - st.s0, st.s1 - ss[i])) / m(nw::kGroundEase);
            const float w = std::clamp(e, 0.0f, 1.0f);
            g[i] = (st.trench ? 0.0f : -m(0.3f)) + (tg[i] - (st.trench ? 0.0f : -m(0.3f))) * w * w * (3.0f - 2.0f * w);
            for (size_t u = 0; u < unders.size(); ++u) {
                const CorridorAlignment& ul = *unders[u];
                double                   us = 0.0;
                float                    ut = 0.0f;
                ul.project(sm.x, sm.z, hints[u], us, ut);
                if (us <= 0.0 || us >= ul.total_length()) continue;
                // N5: only a road below this deck (the GCP stacks over the CIP and the Clearview)
                if (al.surface_y(ss[i], 0.0f) - ul.surface_y(us, ut) < m(nw::kViaductMinH)) continue;
                const float uo = ul.outer_edge(ul.sample(us));
                if (std::abs(ut) < uo + m(nw::kUnderClear)) over[i] = 1;
                for (int sd = 0; sd < 2; ++sd)  // either half of the ground band reaching the road or its verge
                    for (const float f2 : {0.25f, 0.5f, 0.75f, 1.0f}) {
                        const float l  = (sd ? 1.0f : -1.0f) * f2 * (E[i] + m(nw::kGroundW));
                        size_t      h2 = hints[u];
                        double      s2 = 0.0;
                        float       t2 = 0.0f;
                        ul.project(sm.x - static_cast<double>(l * sm.tz), sm.z + static_cast<double>(l * sm.tx), h2, s2,
                                   t2);
                        if (std::abs(t2) < uo + ul.header().grassExtent) clash[sd][i] = 1;
                    }
            }
        }
        const auto run = [&](const Vec4& col, MaterialId mat, auto emit) {
            const uint32_t mk = b.batch_mark();
            emit();
            if (b.close_batch(mk, col, mat) && mat == MAT_METAL) draws.back().metalness = 0.0f;  // paint is dielectric
        };
        if (!st.trench) {
            run(look.concrete, MAT_CONCRETE, [&] {
                for (const float sg : {-1.0f, 1.0f}) {  // parapet: NJ face, top, outer face down to the fascia
                    sweep(
                        b, f, sg, ry, 6,
                        [&](size_t i, size_t k) {
                            const Vec2 q = k < 4 ? face[k] : Vec2(W, k == 4 ? face[3].y : 0.0f);
                            return Vec2(E[i] + q.x, q.y);
                        },
                        ct);
                    sweep(
                        b, f, sg, ry, 2,  // fascia, concrete where no steel girder line shows
                        [&](size_t i, size_t k) { return Vec2(E[i] + W, over[i] ? 0.0f : (k ? -deck : 0.0f)); }, ct);
                }
                std::vector<Vec3> L, R;  // soffit, faces down (edges swapped)
                for (size_t i = 0; i < n; ++i) {
                    L.push_back(f[i].center + f[i].right * (E[i] + W) + kUp * (ry - deck));
                    R.push_back(f[i].center - f[i].right * (E[i] + W) + kUp * (ry - deck));
                }
                cmesh::soffit(b, L, R, look.concrete, MAT_CONCRETE, ct);
                // pier bents: three columns under a cap beam, clear of the span ends and of `under`'s pavement
                const double step = static_cast<double>(m(p.pierSpacing)), end = static_cast<double>(m(15.0f));
                for (double s = st.s0 + end; s <= st.s1 - end; s += step) {
                    if (s < a || s >= z) continue;
                    const size_t i =
                        std::min(n - 1, static_cast<size_t>(std::lower_bound(ss.begin(), ss.end(), s) - ss.begin()));
                    if (over[i]) continue;
                    const auto  sm = al.sample(s);
                    const Vec3  T(sm.tx, 0.0f, sm.tz), Rt(-sm.tz, 0.0f, sm.tx);
                    const float half = E[i] + W - m(0.5f), top = al.surface_y(s, 0.0f) - deck;
                    cmesh::obox(b, cmesh::at(al, s, 0.0f, top - 0.5f * m(nw::kCapH), ox, oz), Rt * half,
                                kUp * (0.5f * m(nw::kCapH)), T * (0.5f * m(nw::kCapD)));
                    for (const float l : {-0.62f * E[i], 0.0f, 0.62f * E[i]}) {
                        const float y0 = al.terrain_y(s) - m(1.0f), y1 = top - m(nw::kCapH);
                        if (y1 - y0 < m(1.0f)) continue;
                        cmesh::obox(b, cmesh::at(al, s, l, 0.5f * (y0 + y1), ox, oz), Rt * (0.5f * m(nw::kColumn)),
                                    kUp * (0.5f * (y1 - y0)), T * (0.5f * m(nw::kColumn)));
                    }
                }
            });
            run(kSteelQueens, MAT_METAL, [&] {  // plate girders and fascia where the deck spans the road below
                const auto in = [&](size_t i) { return over[i] || (i + 1 < n && over[i + 1]); };
                for (size_t i = 0; i + 1 < n; ++i) {
                    if (!in(i)) continue;
                    const std::vector<SweepFrame> fi{f[i], f[i + 1]};
                    for (const float sg : {-1.0f, 1.0f})
                        sweep(
                            b, fi, sg, ry, 2,
                            [&](size_t j, size_t k) { return Vec2(E[i + j] + W, k ? -deck - m(nw::kGirderD) : 0.0f); },
                            ct);
                    const float span = E[i] + W - m(0.6f), gw = 0.5f * m(nw::kGirderW), gd = m(nw::kGirderD);
                    for (float l = -span; l <= span + 1.0f; l += m(nw::kGirderGap))
                        sweep(
                            b, fi, 1.0f, ry, 4,  // web + bottom flange, open at the soffit
                            [&](size_t, size_t k) {
                                const Vec2 q[4] = {
                                    {l - gw, -deck}, {l - gw, -deck - gd}, {l + gw, -deck - gd}, {l + gw, -deck}};
                                return q[k];
                            },
                            ct);
                }
            });
        } else {
            run(look.concrete, MAT_CONCRETE, [&] {
                for (const float sg : {-1.0f, 1.0f})  // NJ toe, wall face to above the ground, coping, back to it
                    sweep(
                        b, f, sg, ry, 7,
                        [&](size_t i, size_t k) {
                            const float top = std::max(face[3].y, g[i] + m(p.wallAbove)), c = face[3].x;
                            const Vec2  q = k < 4    ? face[k]
                                            : k == 4 ? Vec2(c, top)
                                            : k == 5 ? Vec2(c + m(nw::kCoping), top)
                                                     : Vec2(c + m(nw::kCoping), g[i]);
                            return Vec2(E[i] + q.x, q.y);
                        },
                        ct);
            });
        }
        // ground: under the deck (not where it spans `under`), or at the top of the trench walls
        std::vector<float> lo(n), hi(n), lo2(n), hi2(n);
        for (size_t i = 0; i < n; ++i) {
            const float in = st.trench ? E[i] + face[3].x + m(nw::kCoping) : 0.0f, out = E[i] + m(nw::kGroundW);
            const bool  onL = !over[i] && !clash[0][i], onR = !over[i] && !clash[1][i];
            lo[i] = -out, hi[i] = onL ? -in : -out;
            lo2[i] = onR ? in : out, hi2[i] = out;
        }
        const uint32_t mk = b.batch_mark();
        const size_t   d0 = draws.size();
        for (size_t i = 0; i + 1 < n; ++i) {  // per slice: the band height follows the eased ground
            const std::vector<SweepFrame> fi{f[i], f[i + 1]};
            for (int sd = 0; sd < 2; ++sd) {
                std::vector<SweepFrame> fg = fi;
                for (size_t j = 0; j < 2; ++j) fg[j].center += kUp * g[i + j];
                b.emit_sweep_band(fg,
                                  sd ? std::vector<float>{lo2[i], lo2[i + 1]} : std::vector<float>{lo[i], lo[i + 1]},
                                  sd ? std::vector<float>{hi2[i], hi2[i + 1]} : std::vector<float>{hi[i], hi[i + 1]},
                                  ry, look.grass, MAT_GRASS, look.grassTile);
            }
        }
        draws.resize(d0);
        b.close_batch(mk, look.grass, MAT_GRASS);
    }
}

void emit_nj_median(MeshBuilder& b, const std::vector<SweepFrame>& f, float ry, const CorridorLook& look) {
    const auto     nj = nj_profile();
    const uint32_t mk = b.batch_mark();
    sweep(b, f, 1.0f, ry, nj.size(), [&](size_t, size_t k) { return nj[nj.size() - 1 - k]; }, look.concreteTile);
    b.close_batch(mk, look.concrete, MAT_CONCRETE);
}

void emit_overpass(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorCrossing& x,
                   double ox, double oz, const CorridorLook& look, const StructureParams& p) {
    const double s  = x.station;
    const auto   sm = al.sample(s);
    const Vec3   T(sm.tx, 0.0f, sm.tz), R(-sm.tz, 0.0f, sm.tx);
    const float  sk = std::clamp(x.skew, -0.7f, 0.7f);
    const Vec3   A = glm::normalize(R * std::cos(sk) + T * std::sin(sk)), Wd = glm::normalize(glm::cross(kUp, A));
    const bool   rail = x.kind == static_cast<uint8_t>(CrossingKind::RailOver),
                 el   = std::string(x.name).find("IRT") != std::string::npos;
    const float  E    = al.outer_edge(sm) + 2.0f * nj_profile()[0].x + m(nw::kCoping),
                 hw   = std::max(m(1.5f), 0.5f * x.width);
    const float  L = (E + m(2.0f)) / std::cos(sk), slab = m(0.4f), gd = m(rail ? 1.4f : 1.0f);
    const float  road = al.surface_y(s, 0.0f), gnd = al.terrain_y(s);
    const float  top = std::max(
        {gnd + m(p.wallAbove) + m(0.3f), road + util::units::ft(16.0f) + slab + gd, el ? gnd + m(9.0f) : -1e30f});
    const Vec3 O   = cmesh::at(al, s, 0.0f, 0.0f, ox, oz);
    const auto box = [&](float a, float w, float y, float ha, float hwid, float hy) {
        cmesh::obox(b, O + A * a + Wd * w + kUp * y, A * ha, kUp * hy, Wd * hwid);
    };
    const auto run = [&](const Vec4& col, MaterialId mat, auto emit) {
        const uint32_t mk = b.batch_mark();
        emit();
        if (b.close_batch(mk, col, mat) && mat == MAT_METAL) draws.back().metalness = 0.0f;
    };
    if (x.style == static_cast<uint8_t>(BridgeStyle::Stone) && !rail) {
        // N4: 1930s parkway overpass (roads.md GCP: triple-arch at Jewel Ave): stone arches springing 14 ft 6 in over
        // the road, piers at the median and past each edge, ashlar abutments
        const float po = (al.outer_edge(sm) + m(1.0f)) / std::cos(sk), spring = road + util::units::ft(14.5f);
        const float ar = spring + arch_rise(std::max(po, L - po)) + m(0.75f);
        emit_arch(b, draws, DeckFrame{O, Wd, A}, hw, {-L, -po, 0.0f, po, L}, spring, ar, -L, L, look);
        run(Vec4(1.0f, 0.95f, 0.88f, 1.0f), MAT_STONE, [&] {  // CorridorBridges kStone (granite ashlar tint)
            for (const float t : {-L, -po, 0.0f, po, L})
                box(t, 0.0f, 0.5f * (gnd - m(1.0f) + spring), m(t == 0.0f ? 0.5f : 0.8f), hw,
                    0.5f * (spring - gnd + m(1.0f)));
            box(0.0f, 0.0f, ar - 0.5f * slab, L, hw - m(0.45f), 0.5f * slab);  // deck fill under the street
        });
        return;
    }
    run(look.concrete, MAT_CONCRETE, [&] {
        box(0.0f, 0.0f, top - 0.5f * slab, L + m(0.5f), hw, 0.5f * slab);  // slab
        for (const float sg : {-1.0f, 1.0f}) {
            const float y0 = gnd - m(1.0f), y1 = top - slab - gd;  // wall pier past each edge, down to the ground
            box(sg * (L - m(0.6f)), 0.0f, 0.5f * (y0 + y1), m(0.6f), hw + m(0.3f), 0.5f * (y1 - y0));
            if (!rail) box(0.0f, sg * (hw - m(0.2f)), top + m(0.5f), L + m(0.5f), m(0.2f), m(0.5f));  // parapets
        }
    });
    const Vec4 steel = rail ? Vec4(0.48f, 0.52f, 0.48f, 1.0f) : kSteelQueens;  // CorridorBridges kSteelLirr / Queens
    run(steel, MAT_METAL, [&] {
        for (float w = -hw + m(0.6f); w <= hw - m(0.5f); w += m(2.5f))  // girders under the slab
            box(0.0f, w, top - slab - 0.5f * gd, L, m(0.2f), 0.5f * gd);
        if (rail)
            for (const float sg : {-1.0f, 1.0f})  // through girders either side of the tracks
                box(0.0f, sg * (hw - m(0.25f)), top + m(1.2f), L + m(0.5f), m(0.2f), m(1.2f));
    });
    if (!rail) return;
    run(Vec4(0.36f, 0.36f, 0.37f, 1.0f), MAT_CONCRETE, [&] {  // ballast, then the rails (rusted sides)
        box(0.0f, 0.0f, top + m(0.15f), L, hw - m(0.5f), m(0.15f));
    });
    run(Vec4(0.42f, 0.36f, 0.31f, 1.0f), MAT_METAL, [&] {
        const int tracks = std::max(1, static_cast<int>((2.0f * hw - m(1.0f)) / m(4.5f)));
        for (int k = 0; k < tracks; ++k) {
            const float c =
                -hw + m(0.5f) + (static_cast<float>(k) + 0.5f) * (2.0f * hw - m(1.0f)) / static_cast<float>(tracks);
            for (const float r : {-0.72f, 0.72f}) box(0.0f, c + m(r), top + m(0.38f), L, m(0.04f), m(0.08f));
        }
    });
}

}  // namespace swish
