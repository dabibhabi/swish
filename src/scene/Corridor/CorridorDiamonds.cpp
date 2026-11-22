#include "CorridorDiamonds.h"

#include "CorridorBridges.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace swish {

using util::container::filled;
using util::units::ft;
using util::units::m;

namespace cmesh {

Vec3 at(const CorridorAlignment& al, double s, float lat, float y, double ox, double oz) {
    const auto sm = al.sample(s);
    return Vec3(static_cast<float>(sm.x - ox + static_cast<double>(-sm.tz * lat)), y,
                static_cast<float>(sm.z - oz + static_cast<double>(sm.tx * lat)));
}

void face(MeshBuilder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, const Vec3& n, float tile) {
    if (glm::dot(glm::cross(p1 - p0, p2 - p0), n) < 0.0f) {
        std::swap(p0, p1);
        std::swap(p2, p3);
    }
    const Vec3 t = glm::normalize(p1 - p0), bt = glm::normalize(p2 - p0);
    const auto uv = [&](const Vec3& p) { return Vec2(glm::dot(p - p0, t) / tile, glm::dot(p - p0, bt) / tile); };
    const Vec4 tan(t, 1.0f);
    b.push_quad({p0, n, uv(p0), tan}, {p1, n, uv(p1), tan}, {p2, n, uv(p2), tan}, {p3, n, uv(p3), tan},
                QuadWinding::Fan);
}

void soffit(MeshBuilder& b, const std::vector<Vec3>& L, const std::vector<Vec3>& R, const Vec4& col, MaterialId mat,
            float tile) {
    const uint32_t mk = b.batch_mark();
    const Vec3     dn(0.0f, -1.0f, 0.0f);
    for (size_t i = 0; i + 1 < std::min(L.size(), R.size()); ++i) {
        const bool  up = glm::cross(R[i] - L[i], L[i + 1] - L[i]).y > 0.0f;  // Strip winding: (L R L1) faces along this
        const Vec3 &a = up ? R[i] : L[i], &c = up ? L[i] : R[i], &a1 = up ? R[i + 1] : L[i + 1],
                   &c1 = up ? L[i + 1] : R[i + 1];
        const Vec4 tan(glm::normalize(a1 - a + Vec3(1e-6f, 0.0f, 0.0f)), 1.0f);
        const auto v = [&](const Vec3& p) { return Vertex{p, dn, Vec2(p.x / tile, p.z / tile), tan}; };
        b.push_quad(v(a), v(c), v(a1), v(c1), QuadWinding::Strip);
    }
    b.close_batch(mk, col, mat);
}

// Six outward faces of a hexahedron; corner index bits: 1 = a1, 2 = t1, 4 = top.
void hexa(MeshBuilder& b, const std::array<Vec3, 8>& c, float tile) {
    Vec3 mid(0.0f);
    for (const Vec3& p : c) mid += 0.125f * p;
    const int faces[6][4] = {{0, 2, 4, 6}, {1, 3, 5, 7}, {0, 1, 4, 5}, {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}};
    for (const auto& f : faces) {
        const Vec3 fc = 0.25f * (c[f[0]] + c[f[1]] + c[f[2]] + c[f[3]]);
        Vec3       n  = glm::cross(c[f[1]] - c[f[0]], c[f[2]] - c[f[0]]);
        if (glm::dot(n, fc - mid) < 0.0f) n = -n;
        face(b, c[f[0]], c[f[1]], c[f[2]], c[f[3]], glm::normalize(n), tile);
    }
}

std::vector<std::pair<double, double>> minus(double lo, double hi, std::vector<std::pair<double, double>> holes) {
    std::sort(holes.begin(), holes.end());
    std::vector<std::pair<double, double>> out;
    for (const auto& h : holes) {
        if (h.second <= lo || h.first >= hi) continue;
        if (h.first > lo) out.push_back({lo, h.first});
        lo = std::max(lo, h.second);
    }
    if (hi > lo) out.push_back({lo, hi});
    return out;
}

void obox(MeshBuilder& b, const Vec3& c, const Vec3& hx, const Vec3& hy, const Vec3& hz) {
    std::array<Vec3, 8> k;
    for (int i = 0; i < 8; ++i) k[i] = c + (i & 1 ? hx : -hx) + (i & 2 ? hz : -hz) + (i & 4 ? hy : -hy);
    hexa(b, k);
}

void rod(MeshBuilder& b, const Vec3& p, const Vec3& q, float w) {
    const Vec3 d = q - p, ax = glm::normalize(d);
    const Vec3 s =
        glm::normalize(glm::cross(ax, std::abs(ax.y) > 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f)));
    obox(b, 0.5f * (p + q), 0.5f * d, s * w, glm::cross(ax, s) * w);
}

void pole(MeshBuilder& b, const Vec3& base, const Vec3& top, float rad) {
    for (int k = 0; k < 8; ++k) {
        const float a0 = static_cast<float>(k) * util::units::kPi / 4.0f, a1 = a0 + util::units::kPi / 4.0f;
        const Vec3  e0(std::cos(a0) * rad, 0.0f, std::sin(a0) * rad), e1(std::cos(a1) * rad, 0.0f, std::sin(a1) * rad);
        face(b, base + e0, base + e1, top + e0, top + e1, glm::normalize(e0 + e1));
    }
}

}  // namespace cmesh

namespace {

using Span = std::pair<double, double>;
using cmesh::at;
using cmesh::face;
using cmesh::hexa;
using cmesh::minus;
using cmesh::obox;
using cmesh::pole;
using cmesh::rod;

void mark_retro(std::vector<DrawCall>& draws, size_t first) {
    for (size_t i = first; i < draws.size(); ++i) draws[i].retro = 1.0f;
}

double s_at(const CorridorPath& p, float u) { return p.s0 + static_cast<double>(u) * (p.s1 - p.s0); }

// ── C3a props (obox, rod, pole live in cmesh) ──
// 12-gon lens facing n, as 6 convex quads (centre + 3 rim points).
void disc(MeshBuilder& b, const Vec3& c, const Vec3& side, const Vec3& n, float rad) {
    const Vec3 up  = glm::cross(n, side);
    const auto rim = [&](int k) {
        const float a = static_cast<float>(k) * util::units::kPi / 6.0f;
        return c + (side * std::cos(a) + up * std::sin(a)) * rad;
    };
    for (int j = 0; j < 6; ++j) face(b, c, rim(2 * j), rim(2 * j + 2), rim(2 * j + 1), n);
}

// Span-wire signal per service road x crossroad (out[i] > 0): strain poles on the diagonal corners, a sagging
// messenger wire, 3-section heads hung over each approach lane. Lenses batch per (phase group, colour).
template <class QF>
void emit_signals(MeshBuilder& b, std::vector<DrawCall>& draws, const QF& Q, const Vec3& f, const Vec3& r,
                  const float out[2], float in, float hc, float lane, std::vector<SignalLens>& lenses) {
    struct Head {
        Vec3 top, n;
        int  g;
    };
    const Vec3        up(0.0f, 1.0f, 0.0f);
    const float       S = m(diamond::kSection), W = m(diamond::kHeadW), D = m(diamond::kHeadD), V = m(diamond::kVisor);
    std::vector<Head> heads;
    uint32_t          mk = b.batch_mark();
    for (const int i : {0, 1}) {
        if (out[i] <= 0.0f) continue;
        const float sgn = i ? 1.0f : -1.0f, gut = m(diamond::kGutter);
        // P upstream on the inner grass, Q downstream behind the walk
        const float pa = -sgn * (hc + m(1.5f)), pt = sgn * (in - m(1.5f));
        const float qa = sgn * (hc + m(diamond::kCurbW + diamond::kSidewalk + 0.6f)), qt = sgn * (out[i] + m(2.0f));
        const auto  wire = [&](float u) {
            return Q(pa + u * (qa - pa), pt + u * (qt - pt),
                     m(diamond::kSpanH) - 4.0f * m(diamond::kSpanSag) * u * (1.0f - u));
        };
        pole(b, Q(pa, pt, -m(0.3f)), Q(pa, pt, m(diamond::kPoleH)), m(0.13f));
        pole(b, Q(qa, qt, -m(0.3f)), Q(qa, qt, m(diamond::kPoleH)), m(0.13f));
        for (int k = 0; k < 10; ++k)
            rod(b, wire(0.1f * static_cast<float>(k)), wire(0.1f * static_cast<float>(k + 1)), m(0.012f));
        const auto hang = [&](float u, const Vec3& n, int g) {
            const Vec3 w = wire(u), top = w - up * m(0.35f);
            rod(b, w, top, m(0.02f));
            heads.push_back({top, n, g});
        };
        const int lanes = std::max(1, static_cast<int>(std::lround((out[i] - in - 2.0f * gut) / lane)));
        for (int k = 0; k < lanes; ++k)  // service road: faces upstream traffic (it runs along sgn * f)
            hang((sgn * (in + gut + lane * (static_cast<float>(k) + 0.5f)) - pt) / (qt - pt), -sgn * f, 0);
        for (int k = 0; k < 2; ++k) {
            const float a = lane * (static_cast<float>(k) + 0.5f);
            hang((sgn * a - pa) / (qa - pa), sgn * r, 1);    // inbound from the stub
            hang((-sgn * a - pa) / (qa - pa), -sgn * r, 1);  // outbound from the deck
        }
    }
    b.close_batch(mk, Vec4(0.55f, 0.56f, 0.58f, 1.0f), MAT_METAL);  // galvanised steel
    if (heads.empty()) return;

    mk = b.batch_mark();
    for (const Head& h : heads) {
        const Vec3 side = glm::normalize(glm::cross(up, h.n));
        obox(b, h.top - up * (1.5f * S), side * (0.5f * W), up * (1.5f * S), h.n * (0.5f * D));
        for (int k = 0; k < 3; ++k) {  // tunnel visors: top plate + side cheeks
            const Vec3 cs = h.top - up * (S * (static_cast<float>(k) + 0.5f)) + h.n * (0.5f * D + 0.5f * V);
            obox(b, cs + up * (0.47f * S), side * (0.5f * W - m(0.01f)), up * m(0.008f), h.n * (0.5f * V));
            for (const float sd : {-1.0f, 1.0f})
                obox(b, cs + side * (sd * (0.5f * W - m(0.02f))) + up * (0.15f * S), side * m(0.008f), up * (0.32f * S),
                     h.n * (0.5f * V));
        }
    }
    b.close_batch(mk, Vec4(0.035f, 0.035f, 0.035f, 1.0f), MAT_DEFAULT);  // black polycarbonate heads

    for (int g = 0; g < 2; ++g)
        for (int col = 0; col < 3; ++col) {
            mk = b.batch_mark();
            for (const Head& h : heads)
                if (h.g == g)
                    disc(b, h.top - up * (S * (static_cast<float>(col) + 0.5f)) + h.n * (0.5f * D + m(0.004f)),
                         glm::normalize(glm::cross(up, h.n)), h.n, m(diamond::kLensR));
            if (b.close_batch(mk, signal_lens_color(col, false), MAT_DEFAULT))
                lenses.push_back(
                    {static_cast<uint32_t>(draws.size() - 1), static_cast<uint8_t>(g), static_cast<uint8_t>(col)});
        }
}

}  // namespace

namespace {
// One verge cross-section at s: mainline edge E (height yA), gore to the ramp inner edge lb (yb), ramp outer edge lc
// (yc), strip top `top` at level ground (yD). No ramp: lb = lc = E.
struct VergeRow {
    float E, yA, lb, yb, lc, yc, top, yD, ditch;
};

VergeRow verge_row(const CorridorAlignment& al, const std::vector<const CorridorPath*>& ramps, int side, double s) {
    const float dg = al.header().grassY - al.header().roadY, sg = static_cast<float>(side);
    const float E = al.outer_edge(al.sample(s)), top = E + al.sr_strip();
    const float yA = al.surface_y(s, sg * E) + dg, yD = al.ground_y(s) + dg;
    VergeRow    r{E, yA, E, yA, E, yA, top, yD, al.edges().ditch};
    for (const CorridorPath* p : ramps) {
        if (s < p->s0 || s > p->s1) continue;
        const PathPoint pt = corridor_path_at(al, *p, s);
        const float     in = std::abs(pt.lat) - p->halfWidth, out = std::abs(pt.lat) + p->halfWidth;
        if (in > E) {
            r.lb = std::min(in, top);
            r.yb = pt.y + dg;
        }
        r.lc = std::min(out, top);
        r.yc = out < top ? pt.y + dg : yD;
    }
    return r;
}

// D2 ditch: 1V:6H foreslope, flat bottom, then a backslope rounded at toe and top; d = distance out from lc.
float ditch_y(const VergeRow& r, float d) {
    const float W  = r.top - r.lc;
    const float dd = r.ditch * std::clamp((W - m(4.0f)) / m(2.0f), 0.0f, 1.0f);  // no room: no ditch
    const float fo = dd * road::edges::kForeslope, x0 = fo + (dd > 0.0f ? m(road::edges::kDitchFlat) : 0.0f);
    if (d <= fo && fo > 0.0f) return r.yc - dd * d / fo;
    if (d <= x0) return r.yc - dd;
    const float t = std::clamp((d - x0) / std::max(1.0f, W - x0), 0.0f, 1.0f), lo = r.yc - dd;
    return lo + (r.yD - lo) * (0.5f * t + 0.5f * t * t * (3.0f - 2.0f * t));
}

// Lateral distances from lc of the ditch rows: foreslope foot, flat end, six backslope rows.
std::array<float, 9> ditch_rows(const VergeRow& r) {
    const float          W = r.top - r.lc, dd = r.ditch * std::clamp((W - m(4.0f)) / m(2.0f), 0.0f, 1.0f);
    const float          fo = dd * road::edges::kForeslope, x0 = fo + (dd > 0.0f ? m(road::edges::kDitchFlat) : 0.0f);
    std::array<float, 9> d{0.0f, fo, x0};
    for (int k = 1; k <= 6; ++k) d[2 + k] = x0 + (W - x0) * static_cast<float>(k) / 6.0f;
    return d;
}

std::vector<const CorridorPath*> side_ramps(const std::vector<CorridorPath>& paths, int side, double a, double b) {
    std::vector<const CorridorPath*> v;
    for (const CorridorPath& p : paths)
        if (p.kind == CorridorPath::Kind::Ramp && p.side == side && p.s1 > a && p.s0 < b) v.push_back(&p);
    return v;
}
}  // namespace

float corridor_far_ground(const CorridorAlignment& al, int side, double s) {
    float far = al.edges().far;
    if (far <= 0.0f) return 0.0f;
    for (const CorridorBridge& b : al.bridges())
        if (b.kind == CrossingKind::LieOver && std::abs(s - b.s) < b.half + m(road::dressing::kApproach)) return 0.0f;
    const double                    d = m(50.0f);
    const CorridorAlignment::Sample a = al.sample(s - d), c = al.sample(s + d);
    const float turn = a.tx * c.tz - a.tz * c.tx;  // sin of the heading change; > 0 = curving right (EB)
    const float in   = al.outer_edge(al.sample(s)) + al.header().grassExtent;
    if (turn * static_cast<float>(side) > 1e-4f)  // this side is the inside of the curve
        far = std::min(far, std::max(0.0f, road::dressing::kFarCurve * static_cast<float>(2.0 * d) / turn - in));
    return far;
}

float corridor_verge_y(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, int side, double s,
                       float lat) {
    const VergeRow r = verge_row(al, side_ramps(paths, side, s, s), side, s);
    if (lat <= r.lb) return r.lb > r.E ? r.yA + (r.yb - r.yA) * (lat - r.E) / (r.lb - r.E) : r.yA;
    if (lat <= r.lc) return r.yc;
    return lat < r.top ? ditch_y(r, lat - r.lc) : r.yD;
}

void emit_corridor_verge(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                         const std::vector<CorridorPath>& paths, int side, double ts, double te, double ox, double oz,
                         const CorridorLook& look) {
    const CorridorHeader&                  h     = al.header();
    const float                            sg    = static_cast<float>(side);
    const std::vector<const CorridorPath*> ramps = side_ramps(paths, side, ts, te);

    const double        sp = static_cast<double>(h.stationSpacing);
    std::vector<double> ss;
    const int           n = std::max(2, static_cast<int>(std::llround((te - ts) / sp)) + 1);
    for (int i = 0; i < n; ++i) ss.push_back(std::min(ts + static_cast<double>(i) * sp, te));
    // 1 WU either side of each ramp end: the slope switches shape there without a sliver
    for (const CorridorPath* r : ramps)
        for (const double e : {r->s0 - 1.0, r->s0, r->s1, r->s1 + 1.0})
            if (e > ts && e < te) ss.push_back(e);
    std::sort(ss.begin(), ss.end());
    ss.erase(std::unique(ss.begin(), ss.end()), ss.end());

    // rows across: A (edge), B (ramp inner), C + ditch rows (ramp outer .. strip top), G (grass extent)
    std::vector<std::vector<Vec3>> rows(14);
    for (const double s : ss) {
        const VergeRow r = verge_row(al, ramps, side, s);
        rows[0].push_back(at(al, s, sg * r.E, r.yA, ox, oz));
        rows[1].push_back(at(al, s, sg * r.lb, r.yb, ox, oz));
        const auto d = ditch_rows(r);
        for (size_t k = 0; k < d.size(); ++k)
            rows[2 + k].push_back(at(al, s, sg * (r.lc + d[k]), r.lc + d[k] < r.top ? ditch_y(r, d[k]) : r.yD, ox, oz));
        rows[11].push_back(at(al, s, sg * (r.E + h.grassExtent), r.yD, ox, oz));
        // D5: far ground, a step down under the grass edge (aprons win the depth test); N7: network roads have the
        // network ground instead (CorridorGround.h)
        if (al.edges().far > 0.0f && !al.has_terrain()) {
            const float y = r.yD - m(road::dressing::kFarSink), g = r.E + h.grassExtent;
            rows[12].push_back(at(al, s, sg * (g - m(1.0f)), y, ox, oz));
            rows[13].push_back(at(al, s, sg * (g + corridor_far_ground(al, side, s)), y, ox, oz));
        }
    }
    // one draw per tile side: the gore, ditch and cut slope, then level grass
    const uint32_t mk = b.batch_mark();
    const size_t   d0 = draws.size();
    for (size_t k = 0; k + 1 < rows.size(); ++k) {
        if (k == 1 || k == 11 || rows[k + 1].empty()) continue;  // B..C is the ramp itself; 11..12 is the D5 step
        if (side > 0)
            b.emit_strip(rows[k], rows[k + 1], look.grass, MAT_GRASS, look.grassTile);
        else
            b.emit_strip(rows[k + 1], rows[k], look.grass, MAT_GRASS, look.grassTile);
    }
    draws.resize(d0);
    b.close_batch(mk, look.grass, MAT_GRASS);
}

void emit_diamond_path(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorPath& p,
                       const std::vector<CorridorPath>& paths, double from, double to, double ox, double oz,
                       const CorridorLook& look) {
    const double a = std::max(from, p.s0), z = std::min(to, p.s1);
    if (z - a < 1.0) return;
    const CorridorHeader& h  = al.header();
    const float           sg = static_cast<float>(p.side), hw = p.halfWidth, my = h.markingLift;
    const float           lw = m(diamond::kLine), gut = m(diamond::kGutter), S = al.sr_strip();
    const float           cw = m(diamond::kCurbW), ch = m(diamond::kCurbH), dg = h.grassY - h.roadY;
    const auto            fr  = [&](double s0, double s1) { return corridor_path_frames(al, p, s0, s1, ox, oz); };
    const auto            all = fr(a, z);
    if (all.size() < 2) return;
    // q = offset from the path centre, positive away from the mainline; frame lateral = side * q
    const auto band = [&](const std::vector<SweepFrame>& f, float q0, float q1, float y, const Vec4& col,
                          MaterialId mat = MAT_DEFAULT, float tile = 0.0f) {
        const float l0 = sg * q0, l1 = sg * q1;
        b.emit_sweep_band(f, filled(f.size(), std::min(l0, l1)), filled(f.size(), std::max(l0, l1)), y, col, mat, tile);
    };
    const auto wall = [&](const std::vector<SweepFrame>& f, float q, float y0, float hgt, bool facingOut) {
        b.emit_sweep_wall(f, sg * q, y0, hgt, (sg > 0.0f) == facingOut, look.concrete, MAT_CONCRETE, look.concreteTile);
    };
    band(all, -hw, hw, 0.0f, look.asphalt, MAT_ASPHALT, look.asphaltTile);

    if (p.kind == CorridorPath::Kind::Service) {
        // inner curb gaps where a ramp crosses it (outer edge reaches the curb .. inner edge clears it)
        std::vector<Span> gaps;
        for (const CorridorPath& r : paths) {
            if (r.kind != CorridorPath::Kind::Ramp || r.side != p.side || r.s1 < p.s0 || r.s0 > p.s1) continue;
            const double s0 = s_at(r, ramp_u_from_offset(r, S - r.halfWidth));
            const double s1 = s_at(r, ramp_u_from_offset(r, S + cw + r.halfWidth));
            gaps.push_back({std::min(s0, s1), std::max(s0, s1)});
        }
        for (const Span& k : minus(a, z, gaps)) {
            const auto f = fr(k.first, k.second);
            band(f, -hw - cw, -hw, ch, look.concrete, MAT_CONCRETE, look.concreteTile);
            wall(f, -hw, 0.0f, ch, true);
            wall(f, -hw - cw, dg, ch - dg, false);
        }
        band(all, hw, hw + cw, ch, look.concrete, MAT_CONCRETE, look.concreteTile);
        wall(all, hw, 0.0f, ch, false);
        wall(all, hw + cw, dg, ch - dg, true);

        const size_t paint = draws.size();
        // MUTCD 3A.05: left edge of a one-way roadway is yellow; the left (travel) side is the mainline side
        band(all, -hw + gut, -hw + gut + lw, my, look.yellow);
        band(all, hw - gut - lw, hw - gut, my, look.white);
        const float lane = h.laneWidth, cyc = h.dashLength + h.dashGap;
        const float phase = -static_cast<float>(std::fmod(a - p.s0, static_cast<double>(cyc)));
        for (int k = 1; k < p.lanes; ++k) {
            const float q = -hw + gut + static_cast<float>(k) * lane, l0 = sg * (q - 0.5f * lw),
                        l1 = sg * (q + 0.5f * lw);
            b.emit_sweep_dashes(all, std::min(l0, l1), std::max(l0, l1), my, h.dashLength, h.dashGap, phase,
                                look.white);
        }
        // Approach to a crossroad (upstream end in travel direction): crosswalk, then a 24 in stop bar 4 ft before it.
        const double tuck = static_cast<double>(corridor_cross_half(al) - m(0.5f)), near = static_cast<double>(m(1.0f));
        for (const CorridorCrossroad& c : al.crossroads()) {
            const bool eb = p.side > 0 && std::abs(p.s1 - (c.s - tuck)) < near;
            const bool wb = p.side < 0 && std::abs(p.s0 - (c.s + tuck)) < near;
            if (!eb && !wb) continue;
            const double dir  = eb ? -1.0 : 1.0;  // upstream
            const double edge = (eb ? p.s1 : p.s0) - dir * static_cast<double>(m(0.5f));
            const auto   seg  = [&](float d0, float d1) {
                const double x = edge + dir * static_cast<double>(m(d0)), y = edge + dir * static_cast<double>(m(d1));
                return fr(std::max(a, std::min(x, y)), std::min(z, std::max(x, y)));
            };
            const auto walk = seg(1.0f, 4.05f);  // 10 ft crosswalk, 1 m off the crossroad edge
            for (float q = -hw + gut; q + m(0.61f) <= hw - gut + 1.0f; q += m(1.22f))
                band(walk, q, q + m(0.61f), my, look.white);  // 24 in continental bars
            band(seg(4.05f + 1.22f, 4.05f + 1.22f + diamond::kStopBar), -hw + gut, hw - gut, my, look.white);
        }
        mark_retro(draws, paint);
        return;
    }
    if (p.kind != CorridorPath::Kind::Ramp) return;

    // Ramp: mainline end sm, nose sn (inner edge leaves the shoulder), touch st (outer edge reaches the curb).
    const bool   mainAtS0 = p.g0 == 0.0f;
    const double sm = mainAtS0 ? p.s0 : p.s1, ssr = mainAtS0 ? p.s1 : p.s0;
    const double sn = s_at(p, ramp_u_from_offset(p, hw));
    const double st = s_at(p, ramp_u_from_offset(p, S - hw));
    const auto clip = [&](double x, double y) { return fr(std::max(a, std::min(x, y)), std::min(z, std::max(x, y))); };
    const size_t paint = draws.size();
    band(clip(sm, sn), -hw, -hw + m(diamond::kGoreLine), my, look.white);  // channelizing line along the gore
    band(clip(sn, ssr), -hw, -hw + lw, my, look.yellow);
    band(clip(sm, st), hw - lw, hw, my, look.white);

    // Gore chevrons: 12 in diagonal stripes every 15 m between the mainline edge line and the channelizing line,
    // slanted so the ramp end of each stripe leans toward the nose.
    const double   dir    = sn > sm ? 1.0 : -1.0;
    const float    w      = m(0.45f);
    const auto     edgeIn = [&](double s) { return std::abs(corridor_path_at(al, p, s).lat) - hw; };
    const uint32_t mk     = b.batch_mark();
    for (double s = sm + dir * static_cast<double>(m(15.0f)); dir * (sn - s) > 0.0;
         s += dir * static_cast<double>(m(15.0f))) {
        if (s < from || s >= to) continue;
        const float la = al.paint_outer(al.sample(s));
        const float d  = edgeIn(s) - la;
        if (d < m(0.3f)) continue;
        const double s2 = s + dir * static_cast<double>(d);
        const auto   P  = [&](double x, float lat) {
            return at(al, x, sg * lat, al.surface_y(x, sg * lat) + my + 1.0f, ox, oz);
        };
        face(b, P(s, la), P(s + w, la), P(s2, edgeIn(s2)), P(s2 + w, edgeIn(s2 + w)), Vec3(0.0f, 1.0f, 0.0f));
    }
    b.close_batch(mk, look.white, MAT_DEFAULT);
    mark_retro(draws, paint);
}

void emit_crossroad(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                    const CorridorCrossroad& c, const std::vector<CorridorPath>& paths, double ox, double oz,
                    const CorridorLook& look, std::vector<SignalLens>& lenses) {
    const CorridorHeader& h  = al.header();
    const auto            sm = al.sample(c.s);
    const Vec3            O(static_cast<float>(sm.x - ox), 0.0f, static_cast<float>(sm.z - oz));
    const Vec3            f(sm.tx, 0.0f, sm.tz), r(-sm.tz, 0.0f, sm.tx), up(0.0f, 1.0f, 0.0f);
    const double          sp = static_cast<double>(h.stationSpacing);
    const float           gr = (al.ground_y(c.s + sp) - al.ground_y(c.s - sp)) / static_cast<float>(2.0 * sp);
    const float           y0 = al.ground_y(c.s) + 2.0f;  // 2 WU over the service road it overlaps at the tuck

    const float hc = corridor_cross_half(al), E = al.outer_edge(sm), span = E + al.sr_strip();
    const float T = E + h.grassExtent, cw = m(diamond::kCurbW), ch = m(diamond::kCurbH), pw = m(0.45f);
    const float gut = m(diamond::kGutter), lw = m(diamond::kLine), my = h.markingLift, deck = m(diamond::kDeck);
    const float dg = h.grassY - h.roadY, in = span + cw, lane = h.laneWidth;

    // service-road pavement across the crossroad on each side (t-span), where one meets it; out[0] t < 0, out[1] t > 0
    std::vector<Span> sr;
    float             out[2] = {0.0f, 0.0f};
    for (const int side : {-1, +1}) {
        const float o = crossroad_sr_outer(al, c.s, paths, side);
        out[side > 0] = o;
        if (o > 0.0f) sr.push_back(side > 0 ? Span{in, o} : Span{-o, -in});
    }

    // C3a: stub past each service road; a crest over its last stretch hides the end from road level
    float end[2];
    for (const int i : {0, 1}) end[i] = al.cross_stub() > 0.0f && out[i] > 0.0f ? out[i] + al.cross_stub() : T;
    const auto d0     = [&](int i) { return std::max(T, end[i] - m(diamond::kStubFade)); };
    const auto d1     = [&](int i) { return std::max(d0(i) + m(1.0f), end[i] - m(diamond::kStubCrest)); };
    const auto smooth = [](float u) {
        u = std::clamp(u, 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    };
    const auto prof = [&](float t) {
        const int   i = t > 0.0f ? 1 : 0;
        const float d = std::abs(t), H = m(diamond::kCrestH);
        if (end[i] <= T || d <= d0(i)) return 0.0f;
        if (d <= d1(i)) return H * smooth((d - d0(i)) / (d1(i) - d0(i)));
        return H - (H + m(diamond::kStubDrop)) * smooth((d - d1(i)) / std::max(m(1.0f), end[i] - d1(i)));
    };
    std::vector<float> knots;  // where the profile bends: every 2.5 m over the crest
    for (const int i : {0, 1})
        if (end[i] > T)
            for (float d = d0(i); d < end[i]; d += m(2.5f)) knots.push_back(i ? d : -d);
    std::sort(knots.begin(), knots.end());
    const auto cuts = [&](float t0, float t1) {
        std::vector<Span> v;
        float             lo = t0;
        for (const float k : knots)
            if (k > lo && k < t1) {
                v.push_back({lo, k});
                lo = k;
            }
        v.push_back({lo, t1});
        return v;
    };

    // a along the mainline, t across it; the strip is straight and follows the ground grade along a
    const auto Q = [&](float a, float t, float dy) { return O + f * a + r * t + up * (y0 + gr * a + prof(t) + dy); };

    const auto slab = [&](float a0, float a1, float t0, float t1, float yb, float yt) {
        for (const Span& k : cuts(t0, t1)) {
            std::array<Vec3, 8> q;
            for (int i = 0; i < 8; ++i)
                q[i] = Q(i & 1 ? a1 : a0, static_cast<float>(i & 2 ? k.second : k.first), i & 4 ? yt : yb);
            hexa(b, q);
        }
    };
    const auto paint = [&](float a0, float a1, float t0, float t1, float lift = 0.0f) {
        for (const Span& k : cuts(t0, t1)) {
            const float u0 = static_cast<float>(k.first), u1 = static_cast<float>(k.second);
            face(b, Q(a0, u0, my + lift), Q(a1, u0, my + lift), Q(a0, u1, my + lift), Q(a1, u1, my + lift), up);
        }
    };
    // pavement / grass over the crest: plan-space UVs so the texture runs on across the cuts
    const auto ground = [&](float a0, float a1, float t0, float t1, float dy, float tile) {
        const Vec4 tan(f, 1.0f);
        for (const Span& k : cuts(t0, t1)) {
            const float u0 = static_cast<float>(k.first), u1 = static_cast<float>(k.second);
            const Vec3  p0 = Q(a1, u0, dy), p1 = Q(a0, u0, dy), p2 = Q(a1, u1, dy), p3 = Q(a0, u1, dy);
            const Vec3  n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            b.push_quad({p0, n, Vec2(a1, u0) / tile, tan}, {p1, n, Vec2(a0, u0) / tile, tan},
                        {p2, n, Vec2(a1, u1) / tile, tan}, {p3, n, Vec2(a0, u1) / tile, tan}, QuadWinding::Fan);
        }
    };

    uint32_t mk = b.batch_mark();
    ground(-hc, hc, -end[0], end[1], 0.0f, look.asphaltTile);
    b.close_batch(mk, look.asphalt, MAT_ASPHALT);

    mk                      = b.batch_mark();
    std::vector<Span> holes = sr;
    holes.push_back({-span, span});
    for (const float sgn : {-1.0f, 1.0f}) {
        // curbs off the deck, broken where a service road crosses
        for (const Span& k : minus(-end[0], end[1], holes))
            slab(sgn > 0 ? hc : -hc - cw, sgn > 0 ? hc + cw : -hc, static_cast<float>(k.first),
                 static_cast<float>(k.second), -m(0.1f), ch);
        // C3a: 5 ft walk behind the curb along each stub
        for (const int i : {0, 1}) {
            if (end[i] <= T) continue;
            const float t0 = out[i] + cw, a0 = sgn * (hc + cw), a1 = sgn * (hc + cw + m(diamond::kSidewalk));
            slab(std::min(a0, a1), std::max(a0, a1), i ? t0 : -end[i], i ? end[i] : -t0, dg - 2.0f - m(0.1f), ch);
        }
    }
    b.close_batch(mk, look.concrete, MAT_CONCRETE);
    // D4: deck in the crossroad's style (girders, bearings, caps, piers, F-shape parapets); piers at the median and
    // past both shoulders (D2: room behind the W-beam shield), median pier NJ toe + 1 cm
    const float             po      = E + m(road::edges::kPierOffset);
    const CorridorDistrict* dist    = al.district_at(c.s);
    const bool              suffolk = dist && std::strncmp(dist->name, "Suffolk", sizeof(dist->name)) == 0;
    const DeckSpec          dk{{O, f, r, gr, 0.0f},
                               c.style,
                               hc + pw,
                               y0 - 1.0f,
                               y0 - deck,
                               -span,
                               span,
                               {-po, 0.0f, po},
                               true,
                               !suffolk,  // chain-link on Queens / Nassau street bridges
                               dist && std::strncmp(dist->name, "Nassau", sizeof(dist->name)) == 0,
                               ft(road::edges::kNjBaseIn / 24.0f) + m(0.01f)};
    emit_deck(b, draws, dk, [&](float t) { return al.surface_y(c.s, t) - m(1.0f); }, look);

    mk = b.batch_mark();  // C3a: grass apron under each stub, 3 WU below the verge it tucks under
    for (const int i : {0, 1})
        if (end[i] > T) {
            const float A = m(diamond::kApronHalf), t0 = T - m(3.0f);
            ground(-A, A, i ? t0 : -end[i], i ? end[i] : -t0, dg - 5.0f, look.grassTile);
        }
    b.close_batch(mk, look.grass, MAT_GRASS);

    // centre and lane lines stop at the crosswalks (no paint over the bars)
    std::vector<Span> walk = sr;
    for (const Span& k : sr)
        walk.push_back(k.first > 0.0 ? Span{k.second + m(1.0f), k.second + m(4.05f)}
                                     : Span{k.first - m(4.05f), k.first - m(1.0f)});
    const size_t first = draws.size();
    mk                 = b.batch_mark();
    const float cyc    = h.dashLength + h.dashGap;
    for (const Span& k : minus(-end[0], end[1], sr)) {
        const float t0 = static_cast<float>(k.first), t1 = static_cast<float>(k.second);
        for (const float sgn : {-1.0f, 1.0f}) {
            const float e0 = sgn * (hc - gut - lw), e1 = sgn * (hc - gut);
            paint(std::min(e0, e1), std::max(e0, e1), t0, t1);  // edge lines
        }
    }
    for (const Span& k : minus(-end[0], end[1], walk)) {
        const float t0 = static_cast<float>(k.first), t1 = static_cast<float>(k.second);
        for (const float sgn : {-1.0f, 1.0f})
            for (float t = t0; t < t1; t += cyc)  // lane lines, 10 ft dash / 30 ft gap
                paint(sgn * lane - 0.5f * lw, sgn * lane + 0.5f * lw, t, std::min(t1, t + h.dashLength));
    }
    for (const Span& k : sr) {  // crosswalk on the outer side of each service road
        const float sgn = k.first > 0.0 ? 1.0f : -1.0f;
        const float t0  = sgn * (static_cast<float>(sgn > 0 ? k.second : -k.first) + m(1.0f));
        const float t1  = t0 + sgn * m(3.05f);
        for (float q = -hc + gut; q + m(0.61f) <= hc - gut; q += m(1.22f))
            paint(q, q + m(0.61f), std::min(t0, t1), std::max(t0, t1));
    }
    // C3a: 24 in stop bars, 4 ft before the crosswalk (inbound from the stub) or the service road (outbound);
    // right-hand traffic: inbound on side +-1 uses the a half with the same sign
    for (const int i : {0, 1}) {
        if (out[i] <= 0.0f) continue;
        const float sgn = i ? 1.0f : -1.0f, a0 = 1.5f * lw + m(0.15f), a1 = hc - gut;
        const float ti = out[i] + m(1.0f + 3.05f + 1.22f), to = in - m(1.22f) - m(diamond::kStopBar);
        const auto  bar = [&](float as, float t) {
            const float t0 = sgn * t, t1 = sgn * (t + m(diamond::kStopBar));
            paint(std::min(as * a0, as * a1), std::max(as * a0, as * a1), std::min(t0, t1), std::max(t0, t1), 1.0f);
        };
        bar(sgn, ti);
        bar(-sgn, to);
    }
    b.close_batch(mk, look.white, MAT_DEFAULT);

    mk = b.batch_mark();
    for (const Span& k : minus(-end[0], end[1], walk))
        for (const float sgn : {-1.0f, 1.0f})  // double yellow: two 4 in lines, 4 in apart
            paint(sgn > 0 ? 0.5f * lw : -1.5f * lw, sgn > 0 ? 1.5f * lw : -0.5f * lw, static_cast<float>(k.first),
                  static_cast<float>(k.second));
    b.close_batch(mk, look.yellow, MAT_DEFAULT);
    mark_retro(draws, first);

    emit_signals(b, draws, Q, f, r, out, in, hc, lane, lenses);
}

std::vector<SweepFrame> corridor_mainline_frames(const CorridorAlignment& al, double a, double b, double ox,
                                                 double oz) {
    const CorridorHeader& h  = al.header();
    const double          sp = static_cast<double>(h.stationSpacing);
    std::vector<double>   ss{a};
    for (double s = (std::floor(a / sp) + 1.0) * sp; s < b - 1.0; s += sp)  // the tiles' own stations
        if (s - a >= 1.0) ss.push_back(s);
    ss.push_back(b);
    std::vector<Vec3>  cl, tan;
    std::vector<float> banks;
    for (const double s : ss) {
        const auto sm = al.sample(s);
        cl.emplace_back(static_cast<float>(sm.x - ox), sm.y - al.dip(s), static_cast<float>(sm.z - oz));
        tan.emplace_back(sm.tx, 0.0f, sm.tz);
        if (h.version != CorridorHeader::VERSION_FLAT) banks.push_back(sm.bank);
    }
    return build_sweep_frames(cl, tan, banks);
}

void emit_hov(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorHov& hov,
              double from, double to, double ox, double oz, const CorridorLook& look) {
    if (hov.s1 <= hov.s0 || hov.s1 <= from || hov.s0 >= to) return;
    const CorridorHeader& h     = al.header();
    const float           lane1 = h.medianHalfWidth + h.innerShoulder + h.laneWidth;  // lane 1 | 2 line
    const float           my = h.roadY + h.markingLift, lw = m(diamond::kHovLine), hb = 0.5f * m(diamond::kHovBuffer);
    const float           cyc   = h.dashLength + h.dashGap;
    const size_t          first = draws.size();
    // per tile, as the mainline: per-draw culling stays tight
    for (double ts = from; ts < to - 1e-6; ts += static_cast<double>(h.tileLength)) {
        const double a = std::max(ts, hov.s0), z = std::min({ts + static_cast<double>(h.tileLength), to, hov.s1});
        if (z - a < 1.0) continue;
        for (const int w : {0, 1}) {
            const float sg    = w ? -1.0f : 1.0f;
            const auto  lines = [&](double s0, double s1, bool broken) {
                const auto f = corridor_mainline_frames(al, s0, s1, ox, oz);
                if (f.size() < 2) return;
                const float phase = -static_cast<float>(std::fmod(s0, static_cast<double>(cyc)));
                for (const float c : {lane1 - hb, lane1 + hb}) {
                    const float l0 = std::min(sg * (c - 0.5f * lw), sg * (c + 0.5f * lw));
                    const float l1 = std::max(sg * (c - 0.5f * lw), sg * (c + 0.5f * lw));
                    if (broken)  // MUTCD 3D.02 drawing C: wide broken lines where crossing is permitted
                        b.emit_sweep_dashes(f, l0, l1, my, h.dashLength, h.dashGap, phase, look.white);
                    else
                        b.emit_sweep_band(f, filled(f.size(), l0), filled(f.size(), l1), my, look.white);
                }
            };
            for (const Span& k : minus(a, z, hov.zones[w])) lines(k.first, k.second, false);
            for (const auto& zn : hov.zones[w])
                if (std::min(z, zn.second) - std::max(a, zn.first) > 1.0)
                    lines(std::max(a, zn.first), std::min(z, zn.second), true);
        }
    }
    // Diamonds: 6 in outline centred in the HOV lane (inner edge line .. buffer)
    const float    hw = 0.5f * ft(diamond::kDiamondWFt), hl = 0.5f * ft(diamond::kDiamondLFt);
    const float    ln = 0.5f * ft(diamond::kDiamondLineFt);
    const float    tc = 0.5f * (h.medianHalfWidth + h.innerShoulder + lane1 - hb - 0.5f * lw);
    const uint32_t mk = b.batch_mark();
    for (const int w : {0, 1}) {
        const float sg = w ? -1.0f : 1.0f;
        for (const double s : hov.diamonds[w]) {
            if (s < from || s >= to) continue;
            const auto P = [&](double ds, float lat) {
                return at(al, s + ds, sg * lat, al.surface_y(s + ds, sg * lat) + h.markingLift + 1.0f, ox, oz);
            };
            const std::array<std::pair<double, float>, 4> v{{{hl, tc}, {0.0, tc + hw}, {-hl, tc}, {0.0, tc - hw}}};
            for (int k = 0; k < 4; ++k) {
                const auto&  p  = v[k];
                const auto&  q  = v[(k + 1) % 4];
                const double es = q.first - p.first;
                const float  el = q.second - p.second, len = std::sqrt(static_cast<float>(es * es) + el * el);
                const double ns = static_cast<double>(-el / len * ln);
                const float  nl = static_cast<float>(es) / len * ln;
                face(b, P(p.first - ns, p.second - nl), P(p.first + ns, p.second + nl), P(q.first - ns, q.second - nl),
                     P(q.first + ns, q.second + nl), Vec3(0.0f, 1.0f, 0.0f));
            }
        }
    }
    b.close_batch(mk, look.white, MAT_DEFAULT);
    mark_retro(draws, first);
}

}  // namespace swish
