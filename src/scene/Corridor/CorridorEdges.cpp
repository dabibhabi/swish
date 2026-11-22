#include "CorridorEdges.h"

#include "scene/RoadScene/MeshBuilder.h"
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace swish {

using util::container::filled;
using util::units::ft;
using util::units::m;
namespace ed = road::edges;
using Span   = std::pair<double, double>;

namespace {

const Vec3 kUp(0.0f, 1.0f, 0.0f);
const Vec4 kSteel(0.62f, 0.63f, 0.65f, 1.0f);   // galvanised, weathered
const Vec4 kBrick(1.0f, 0.92f, 0.88f, 1.0f);    // over Bricks054: red-brown stained precast (photo exit-33)
const Vec4 kTan(0.92f, 0.84f, 0.70f, 1.0f);     // over Concrete012: tan panels (photo exit-62)
const Vec4 kYellow(0.95f, 0.72f, 0.02f, 1.0f);  // attenuator

float inch(float v) { return ft(v / 12.0f); }

// [a, b] on the tiles' own station multiples, plus `extra` points inside.
std::vector<double> stations(double a, double b, double sp, std::initializer_list<double> extra = {}) {
    std::vector<double> ss{a, b};
    for (double s = (std::floor(a / sp) + 1.0) * sp; s < b - 1.0; s += sp)
        if (s - a >= 1.0) ss.push_back(s);
    for (const double e : extra)
        if (e > a + 1.0 && e < b - 1.0) ss.push_back(e);
    std::sort(ss.begin(), ss.end());
    return ss;
}

// Quad whose front faces v0.normal.
void quad(MeshBuilder& b, Vertex v0, Vertex v1, Vertex v2, Vertex v3) {
    if (glm::dot(glm::cross(v1.position - v0.position, v2.position - v0.position), v0.normal) < 0.0f) {
        std::swap(v0, v1);
        std::swap(v2, v3);
    }
    b.push_quad(v0, v1, v2, v3, QuadWinding::Fan);
}

// Sweeps profile `pr` (x outward from base(s).x, y up from base(s).y) along `ss` on side +-1. Each edge faces
// (dy, -dx) (CCW = outward). UVs run on along s from `u0` and round the profile, both per `tile`.
template <class F>
void sweep(MeshBuilder& b, const CorridorAlignment& al, const std::vector<double>& ss, int side,
           const std::vector<Vec2>& pr, const F& base, double ox, double oz, float tile, double u0) {
    const float        sg = static_cast<float>(side);
    std::vector<float> v(pr.size(), 0.0f);
    for (size_t k = 1; k < pr.size(); ++k) v[k] = v[k - 1] + glm::length(pr[k] - pr[k - 1]);
    for (size_t i = 0; i + 1 < ss.size(); ++i) {
        const auto  sa = al.sample(ss[i]), sb = al.sample(ss[i + 1]);
        const Vec2  ba = base(ss[i]), bb = base(ss[i + 1]);
        const Vec3  r = glm::normalize(Vec3(-sa.tz - sb.tz, 0.0f, sa.tx + sb.tx)) * sg;
        const Vec4  tan(glm::normalize(Vec3(sa.tx + sb.tx, 0.0f, sa.tz + sb.tz)), 1.0f);
        const float ua = static_cast<float>((ss[i] - u0) / tile), ub = static_cast<float>((ss[i + 1] - u0) / tile);
        const auto  P = [&](double s, const Vec2& bs, const Vec2& p) {
            return cmesh::at(al, s, sg * (bs.x + p.x), bs.y + p.y, ox, oz);
        };
        for (size_t k = 0; k + 1 < pr.size(); ++k) {
            const Vec2 d = pr[k + 1] - pr[k];
            if (glm::length(d) < 1.0f) continue;
            const Vec3  n  = glm::normalize(r * d.y - kUp * d.x);
            const float va = v[k] / tile, vb = v[k + 1] / tile;
            quad(b, {P(ss[i], ba, pr[k]), n, Vec2(ua, va), tan}, {P(ss[i + 1], bb, pr[k]), n, Vec2(ub, va), tan},
                 {P(ss[i], ba, pr[k + 1]), n, Vec2(ua, vb), tan}, {P(ss[i + 1], bb, pr[k + 1]), n, Vec2(ub, vb), tan});
        }
    }
}

// Mirror-symmetric closed profile (k and size-1-k pair up) as a flat cap at s, facing along `dir` * tangent.
void cap(MeshBuilder& b, const CorridorAlignment& al, double s, const std::vector<Vec2>& pr, float y0, float dir,
         double ox, double oz) {
    const auto sm = al.sample(s);
    const Vec3 n  = Vec3(sm.tx, 0.0f, sm.tz) * dir;
    const Vec4 tan(-sm.tz, 0.0f, sm.tx, 1.0f);
    const auto P = [&](const Vec2& p) { return Vertex{cmesh::at(al, s, p.x, y0 + p.y, ox, oz), n, p / m(1.0f), tan}; };
    for (size_t k = 0, j = pr.size() - 1; k + 1 < j - 1; ++k, --j)
        quad(b, P(pr[k]), P(pr[k + 1]), P(pr[j]), P(pr[j - 1]));
}

double district_start(const CorridorAlignment& al, const char* name) {
    for (const CorridorDistrict& d : al.districts())
        if (std::strncmp(d.name, name, sizeof(d.name)) == 0) return d.start;
    return -1.0;
}

}  // namespace

double exit_s(const CorridorAlignment& al, int n, bool last) {
    double out = -1.0;
    for (const CorridorExit& x : al.exits())
        if (std::atoi(x.ref) == n)
            out = out < 0.0 ? x.station : last ? std::max(out, x.station) : std::min(out, x.station);
    return out;
}

std::array<Vec2, 8> nj_profile() {
    const float t = inch(0.5f * ed::kNjBaseIn), b = t - inch(ed::kNjLowerRunIn), c = b - inch(ed::kNjUpperRunIn);
    const float r = inch(ed::kNjRevealIn), k = inch(ed::kNjBreakIn), h = inch(ed::kNjHeightIn);
    return {{{t, 0.0f}, {t, r}, {b, k}, {c, h}, {-c, h}, {-b, k}, {-t, r}, {-t, 0.0f}}};
}

CorridorEdges corridor_edges(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, float wideExtra) {
    CorridorEdges out;
    if (!al.has_ends()) return out;
    out.jersey = true;
    // Wide grass median from kWideLead past the last exit 64 record (the HOV paint ends there) to kWideTail before the
    // east lane drop ([WP] route description).
    if (const double s64 = exit_s(al, ed::kWideFromExit, true); s64 > 0.0 && wideExtra > 0.0f) {
        out.ease  = m(ed::kWideEase);
        out.wide0 = s64 + static_cast<double>(m(ed::kWideLead) + out.ease);
        out.wide1 = al.ends().taper0 - static_cast<double>(m(ed::kWideTail) + out.ease);
        if (out.wide1 > out.wide0) out.wideExtra = wideExtra;
    }

    // Busy stretches per side: ramps and crossroad decks, with a margin.
    const double hc = static_cast<double>(corridor_cross_half(al)), mg = static_cast<double>(m(ed::kWallMargin));
    CorridorEdges::Spans busy[2];
    for (const CorridorPath& p : paths)
        if (p.kind == CorridorPath::Kind::Ramp) busy[p.side > 0].push_back({p.s0 - mg, p.s1 + mg});
    // decks with piers at the shoulders: crossroads and D4 overpasses (s, half-length along s)
    std::vector<std::pair<double, double>> decks;
    for (const CorridorCrossroad& c : al.crossroads()) decks.push_back({c.s, hc});
    for (const CorridorBridge& b : al.bridges())
        if (b.kind != CrossingKind::LieOver) decks.push_back({b.s, static_cast<double>(b.half)});
    for (const CorridorCrossroad& c : al.crossroads())
        for (auto& v : busy) v.push_back({c.s - hc - mg, c.s + hc + mg});
    CorridorEdges::Spans bridges;  // D4: walls stop short of abutments and LIE-over trenches
    for (const CorridorBridge& b : al.bridges()) bridges.push_back({b.s - b.half - mg, b.s + b.half + mg});

    // Sound walls between diamonds: all of Nassau both ways (houses back onto the expressway, photo exit-33),
    // Queens westbound east of exit 31 (photo exit-31), Suffolk eastbound exits 62-63 in tan (photo exit-62).
    const double nassau = district_start(al, "Nassau"), suffolk = district_start(al, "Suffolk");
    struct Run {
        int    side;
        double a, b;
    };
    std::vector<Run> runs;
    if (nassau > 0.0 && suffolk > nassau) runs.insert(runs.end(), {{-1, nassau, suffolk}, {+1, nassau, suffolk}});
    if (const double x31 = exit_s(al, 31); x31 > 0.0 && nassau > x31) runs.push_back({-1, x31, nassau});
    if (const double a = exit_s(al, 62), z = exit_s(al, 63, true); a > 0.0 && z > a) runs.push_back({+1, a, z});
    for (const Run& r : runs)
        for (const Span& g : cmesh::minus(r.a, r.b, busy[r.side > 0]))
            if (g.second - g.first >= static_cast<double>(m(ed::kWallGapMin)))
                for (const Span& w : cmesh::minus(g.first, g.second, bridges))
                    if (w.second - w.first >= static_cast<double>(m(ed::kWallMargin)))
                        out.wall[r.side > 0].push_back(w);

    // W-beam shields in front of the crossroad piers, upstream kShieldUp, broken where a ramp leaves the shoulder.
    const float back = m(ed::kRailOffset + ed::kRailCrest + ed::kBlockout + ed::kPost + 0.5f);
    for (const auto& [cs, half] : decks)
        for (const int side : {-1, +1}) {
            const double up = static_cast<double>(m(ed::kShieldUp)), dn = static_cast<double>(m(ed::kShieldDown));
            const double lo = cs - half - (side > 0 ? up : dn), hi = cs + half + (side > 0 ? dn : up);
            double       a = -1.0;
            for (double s = lo; s < hi + static_cast<double>(m(5.0f)) + 1.0; s += static_cast<double>(m(5.0f))) {
                bool clear = s <= hi;
                for (const CorridorPath& p : paths)
                    if (clear && p.kind == CorridorPath::Kind::Ramp && p.side == side && s >= p.s0 && s <= p.s1)
                        clear =
                            std::abs(corridor_path_at(al, p, s).lat) - p.halfWidth > al.outer_edge(al.sample(s)) + back;
                if (clear && a < 0.0) a = s;
                if (!clear && a >= 0.0) {
                    if (s - a >= static_cast<double>(m(30.0f))) out.rail[side > 0].push_back({a, std::min(s, hi)});
                    a = -1.0;
                }
            }
        }
    return out;
}

void emit_median(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double ts, double te,
                 double ox, double oz, const CorridorLook& look) {
    const CorridorHeader& h  = al.header();
    const CorridorEnds&   e  = al.ends();
    const CorridorEdges&  E  = al.edges();
    const float           ry = h.roadY, mh = h.medianHalfWidth, dg = h.grassY - h.roadY;
    const double          sp = static_cast<double>(h.stationSpacing), end = std::min(te, e.barrierEnd);
    const double w0 = E.wideExtra > 0.0f ? E.wide0 - E.ease : -1.0, w1 = E.wideExtra > 0.0f ? E.wide1 + E.ease : -1.0;
    const auto   near = [](double x, double y) { return std::abs(x - y) < 1.0; };

    // ── NJ barrier, toes 50 WU deep so a banked shoulder never shows a slit under them ──
    const auto        nj = nj_profile();
    std::vector<Vec2> pr(nj.begin(), nj.end());
    pr.front().y = pr.back().y = -50.0f;
    for (const auto& [a, z] : cmesh::minus(std::max(ts, e.portal), end, {{e.padW0, e.padW1}, {w0, w1}})) {
        if (z - a < 1.0) continue;
        const auto f = corridor_mainline_frames(al, a, z, ox, oz);
        b.emit_sweep_band(f, filled(f.size(), -mh), filled(f.size(), mh), ry, look.concrete, MAT_CONCRETE,
                          look.concreteTile);  // footing strip either side of the toe
        const uint32_t mk = b.batch_mark();
        sweep(
            b, al, stations(a, z, sp), +1, pr, [&](double s) { return Vec2(0.0f, al.surface_y(s, 0.0f)); }, ox, oz,
            look.concreteTile, ts);
        // end caps where the run really ends (not at a tile seam, not hidden in the portal pier)
        if (near(a, e.padW1) || near(a, w1)) cap(b, al, a, pr, al.surface_y(a, 0.0f), -1.0f, ox, oz);
        if (near(z, e.padW0) || near(z, w0) || near(z, e.barrierEnd))
            cap(b, al, z, pr, al.surface_y(z, 0.0f), 1.0f, ox, oz);
        b.close_batch(mk, look.concrete, MAT_CONCRETE);
    }
    {  // U-turn opening (C4)
        const double a = std::max(ts, e.padW0), z = std::min(te, e.padW1);
        if (z - a > 1.0) {
            const auto f = corridor_mainline_frames(al, a, z, ox, oz);
            b.emit_sweep_band(f, filled(f.size(), -mh), filled(f.size(), mh), ry, look.asphalt, MAT_ASPHALT,
                              look.asphaltTile);
        }
    }
    if (w1 <= w0) return;

    // ── Wide grass median: shallow V swale, three-cable barrier down its bottom ──
    const double a = std::max(ts, w0), z = std::min(end, w1);
    if (z - a > 1.0) {
        const auto        ss = stations(a, z, sp);
        std::vector<Vec3> L, C, R;
        for (const double s : ss) {
            const float md = al.sample(s).med, dv = std::min(m(0.5f), 0.04f * md);
            L.push_back(cmesh::at(al, s, -md, al.surface_y(s, -md) + dg, ox, oz));
            C.push_back(cmesh::at(al, s, 0.0f, al.surface_y(s, 0.0f) + dg - dv, ox, oz));
            R.push_back(cmesh::at(al, s, md, al.surface_y(s, md) + dg, ox, oz));
        }
        uint32_t     mk = b.batch_mark();
        const size_t d0 = draws.size();
        b.emit_strip(L, C, look.grass, MAT_GRASS, look.grassTile);
        b.emit_strip(C, R, look.grass, MAT_GRASS, look.grassTile);
        draws.resize(d0);
        b.close_batch(mk, look.grass, MAT_GRASS);

        // cable runs where the median is wide enough; the cables drop to ground anchors over the last 20 m
        const double c0 = E.wide0 - 0.5 * E.ease, c1 = E.wide1 + 0.5 * E.ease, anc = static_cast<double>(m(20.0f));
        const double ca = std::max(a, c0), cz = std::min(z, c1);
        if (cz - ca > 1.0) {
            const auto bottom = [&](double s) {
                const float md = al.sample(s).med;
                return al.surface_y(s, 0.0f) + dg - std::min(m(0.5f), 0.04f * md);
            };
            const auto lift = [&](double s) {
                return static_cast<float>(std::clamp(std::min(s - c0, c1 - s) / anc, 0.0, 1.0));
            };
            mk            = b.batch_mark();
            const auto cs = stations(ca, cz, sp, {c0 + anc, c1 - anc});
            for (size_t i = 0; i + 1 < cs.size(); ++i)
                for (const float hk : ed::kCableH)
                    cmesh::rod(b, cmesh::at(al, cs[i], 0.0f, bottom(cs[i]) + m(hk) * lift(cs[i]), ox, oz),
                               cmesh::at(al, cs[i + 1], 0.0f, bottom(cs[i + 1]) + m(hk) * lift(cs[i + 1]), ox, oz),
                               m(0.0095f));  // 3/4 in wire rope
            const double ps = static_cast<double>(ft(ed::kCablePostFt));
            for (double s = std::ceil((ca + anc) / ps) * ps; s < std::min(cz, c1 - anc); s += ps) {
                const auto  sm = al.sample(s);
                const Vec3  r(-sm.tz, 0.0f, sm.tx), fw(sm.tx, 0.0f, sm.tz);
                const float y0 = bottom(s), top = m(ed::kCableH[2] + 0.08f);
                cmesh::obox(b, cmesh::at(al, s, 0.0f, y0 + 0.5f * (top - m(0.2f)), ox, oz), r * m(0.019f),
                            kUp * (0.5f * (top + m(0.2f))), fw * m(0.03f));  // S3x5.7 post
            }
            b.close_batch(mk, kSteel, MAT_METAL);
        }
    }
    // Crash cushions on the NJ ends that face the wide median's oncoming traffic.
    for (const auto& [s, dir] : {std::pair{w0, 1.0}, std::pair{w1, -1.0}}) {
        const double c = s + dir * static_cast<double>(m(1.75f));
        if (c < ts || c >= te) continue;
        const auto     sm = al.sample(c);
        const uint32_t mk = b.batch_mark();
        cmesh::obox(b, cmesh::at(al, c, 0.0f, al.surface_y(c, 0.0f) + m(0.43f), ox, oz),
                    Vec3(-sm.tz, 0.0f, sm.tx) * m(0.3f), kUp * m(0.43f), Vec3(sm.tx, 0.0f, sm.tz) * m(1.75f));
        if (b.close_batch(mk, kYellow, MAT_DEFAULT)) draws.back().retro = 1.0f;
    }
}

void emit_edges(MeshBuilder& b, const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double from,
                double to, double ox, double oz, const CorridorLook& look) {
    const CorridorHeader& h    = al.header();
    const CorridorEdges&  E    = al.edges();
    const double          tile = static_cast<double>(h.tileLength), sp = static_cast<double>(h.stationSpacing);
    const auto            each_tile = [&](const Span& k, const auto& fn) {
        for (double ta = std::floor(std::max(from, k.first) / tile) * tile; ta < std::min(to, k.second); ta += tile) {
            const double a = std::max({ta, from, k.first}), z = std::min({ta + tile, to, k.second});
            if (z - a >= 1.0) fn(a, z);
        }
    };
    for (const int side : {-1, +1}) {
        const int   w  = side > 0;
        const float sg = static_cast<float>(side);
        // ── Sound walls: brick panels on a plinth between posts, at the top of the cut ──
        for (const Span& k : E.wall[w])
            each_tile(k, [&](double a, double z) {
                const auto base = [&](double s) {
                    const float L = al.outer_edge(al.sample(s)) + al.sr_strip() - m(ed::kWallBack);
                    return Vec2(L, corridor_verge_y(al, paths, side, s, L));
                };
                const CorridorDistrict* d   = al.district_at(0.5 * (a + z));
                const bool              tan = d && std::strncmp(d->name, "Suffolk", 7) == 0;
                const auto              ss  = stations(a, z, sp);
                const float             t = 0.5f * m(ed::kWallT), bt = t + m(0.075f), B = m(ed::kWallBase), H = E.wallH;
                const float             bk = m(ed::kBrickTile);
                // whole brick repeats from u0, so the bond runs on across tiles
                const double u0 = a - std::fmod(a, static_cast<double>(bk));
                uint32_t     mk = b.batch_mark();
                sweep(b, al, ss, side, {{t, B}, {t, H}, {-t, H}, {-t, B}}, base, ox, oz, tan ? look.concreteTile : bk,
                      u0);
                b.close_batch(mk, tan ? kTan : kBrick, tan ? MAT_CONCRETE : MAT_BRICK);
                mk = b.batch_mark();
                sweep(b, al, ss, side, {{bt, -m(0.3f)}, {bt, B}, {-bt, B}, {-bt, -m(0.3f)}}, base, ox, oz,
                      look.concreteTile, u0);
                const double ps = static_cast<double>(m(ed::kWallPost));
                for (double s = std::ceil(a / ps) * ps; s < z; s += ps) {
                    const auto  sm = al.sample(s);
                    const Vec2  bs = base(s);
                    const Vec3  r(-sm.tz, 0.0f, sm.tx), fw(sm.tx, 0.0f, sm.tz);
                    const float pw = 0.5f * m(ed::kWallPostW), y0 = bs.y - m(0.3f), y1 = bs.y + H + m(0.15f);
                    cmesh::obox(b, cmesh::at(al, s, sg * bs.x, 0.5f * (y0 + y1), ox, oz), r * pw,
                                kUp * (0.5f * (y1 - y0)), fw * pw);
                }
                b.close_batch(mk, look.concrete, MAT_CONCRETE);
            });
        // ── W-beam: beam on blockouts and steel posts, turned down to the ground over the last 25 ft ──
        for (const Span& k : E.rail[w])
            each_tile(k, [&](double a, double z) {
                const double re = static_cast<double>(m(ed::kRailEnd));
                const auto   f  = [&](double s) {
                    return static_cast<float>(std::clamp(std::min(s - k.first, k.second - s) / re, 0.0, 1.0));
                };
                const float D = m(ed::kRailDepth), top = m(ed::kRailTop);
                const auto  base = [&](double s) {
                    const float Eo = al.outer_edge(al.sample(s));
                    return Vec2(Eo + m(ed::kRailOffset), al.surface_y(s, sg * Eo) - D + top * f(s));
                };
                const auto ss =
                    stations(a, z, sp, {k.first + re, k.second - re, k.first + 0.5 * re, k.second - 0.5 * re});
                // AASHTO M180 W profile from the rail bottom: flanges bent back, two crests at the face, valley between
                const float       c = m(ed::kRailCrest);
                std::vector<Vec2> pr{{0.6f * c, 0.0f},  {0.0f, 0.16f * D}, {0.0f, 0.36f * D}, {c, 0.5f * D},
                                     {0.0f, 0.64f * D}, {0.0f, 0.84f * D}, {0.6f * c, D}};
                const uint32_t    mk = b.batch_mark();
                sweep(b, al, ss, side, pr, base, ox, oz, m(1.0f), a);  // back face (up the profile = outward)
                std::reverse(pr.begin(), pr.end());
                sweep(b, al, ss, side, pr, base, ox, oz, m(1.0f), a);  // traffic face
                const double ps = static_cast<double>(ft(ed::kPostSpacingFt));
                for (double s = std::ceil(a / ps) * ps; s < z; s += ps) {
                    const auto  sm = al.sample(s);
                    const Vec2  bs = base(s);
                    const Vec3  r  = Vec3(-sm.tz, 0.0f, sm.tx) * sg, fw(sm.tx, 0.0f, sm.tz);
                    const float lb = bs.x + c + 0.5f * m(ed::kBlockout),
                                lp = bs.x + c + m(ed::kBlockout + 0.5f * ed::kPost);
                    const float yT = corridor_verge_y(al, paths, side, s, lp), y1 = bs.y + D - m(0.05f);
                    if (y1 < yT + m(0.15f)) continue;  // the turned-down end is posted by its anchor only
                    const float y0 = yT - m(0.3f), hp = 0.5f * m(ed::kPost);
                    cmesh::obox(b, cmesh::at(al, s, sg * lp, 0.5f * (y0 + y1), ox, oz), r * hp,
                                kUp * (0.5f * (y1 - y0)), fw * hp);
                    cmesh::obox(b, cmesh::at(al, s, sg * lb, bs.y + 0.5f * D, ox, oz), r * (0.5f * m(ed::kBlockout)),
                                kUp * m(0.18f), fw * m(0.075f));
                }
                b.close_batch(mk, kSteel, MAT_METAL);
            });
    }
}

}  // namespace swish
