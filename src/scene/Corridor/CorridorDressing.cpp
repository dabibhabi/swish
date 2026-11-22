#include "CorridorDressing.h"

#include "CorridorEdges.h"
#include "CorridorLandmarks.h"
#include "CorridorSigns.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace swish {

namespace {

namespace dr = road::dressing;
using cmesh::at;
using util::units::m;

const Vec3  kUp(0.0f, 1.0f, 0.0f);
const float kTexel = 1.0f / 2048.0f, kRow = 0.125f;  // atlas texel, one 256 px band

// Stateless hash to [0, 1) (murmur3 finaliser): a tile's placement never depends on its neighbours.
float rnd(int a, int b, int c) {
    uint32_t x = static_cast<uint32_t>(a) * 0x9E3779B1u ^ static_cast<uint32_t>(b) * 0x85EBCA77u ^
                 static_cast<uint32_t>(c) * 0xC2B2AE3Du;
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

// Bilinear value noise on a grid of `cell` (WU): groves and clearings instead of an even scatter.
float grove(double s, float lat, int side) {
    const double gs = s / static_cast<double>(m(60.0f)), gl = static_cast<double>(lat) / m(60.0f);
    const int    i = static_cast<int>(std::floor(gs)), j = static_cast<int>(std::floor(gl));
    const float  u = static_cast<float>(gs - i), v = static_cast<float>(gl - j);
    const auto   h = [&](int a, int b) { return rnd(a, b, 977 + side); };
    return (h(i, j) * (1 - u) + h(i + 1, j) * u) * (1 - v) + (h(i, j + 1) * (1 - u) + h(i + 1, j + 1) * u) * v;
}

// One quad of a batch with explicit UVs and normals; front face along `front` (cmesh::face winding).
void uvquad(MeshBuilder& b, std::array<Vec3, 4> p, std::array<Vec2, 4> uv, std::array<Vec3, 4> n, const Vec3& front) {
    if (glm::dot(glm::cross(p[1] - p[0], p[2] - p[0]), front) < 0.0f) {
        std::swap(p[0], p[1]);
        std::swap(p[2], p[3]);
        std::swap(uv[0], uv[1]);
        std::swap(uv[2], uv[3]);
        std::swap(n[0], n[1]);
        std::swap(n[2], n[3]);
    }
    const Vec4 tan(glm::normalize(p[1] - p[0]), 1.0f);
    b.push_quad({p[0], n[0], uv[0], tan}, {p[1], n[1], uv[1], tan}, {p[2], n[2], uv[2], tan}, {p[3], n[3], uv[3], tan},
                QuadWinding::Fan);
}

// Facade a -> c (ground corners), height h (WU), outward n: band modules stacked, the top one cut from the band's
// bottom so floors line up.
void facade(MeshBuilder& b, const Vec3& a, const Vec3& c, float h, const Vec3& n, const dr::BldgSpec& sp, float u0,
            float u1) {
    const float band = m(sp.bandH), vb = (static_cast<float>(sp.row) + 1.0f) * kRow - kTexel;
    for (float y = 0.0f; y < h - 1.0f; y += band) {
        const float t = std::min(band, h - y), vt = vb - (kRow - 2.0f * kTexel) * t / band;
        uvquad(b, {a + kUp * y, c + kUp * y, a + kUp * (y + t), c + kUp * (y + t)},
               {Vec2(u0, vb), Vec2(u1, vb), Vec2(u0, vt), Vec2(u1, vt)}, {n, n, n, n}, n);
    }
}

// Roof quad over the row-7 strip [ua, ub] (shingle 0..0.5, membrane 0.5..0.75); p0 p1 eave, p2 p3 ridge.
void roof(MeshBuilder& b, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float ua, float ub) {
    Vec3 n = glm::cross(p1 - p0, (p2 + p3) * 0.5f - p0);
    if (glm::length(n) < 1e-6f) return;
    n              = glm::normalize(n.y < 0.0f ? -n : n);
    const float vt = 7.0f * kRow + kTexel, vb = 1.0f - kTexel;
    uvquad(b, {p0, p1, p2, p3}, {Vec2(ua, vb), Vec2(ub, vb), Vec2(ua, vt), Vec2(ub, vt)}, {n, n, n, n}, n);
}

// A building on `base` (ground centre), long axis `ax`, `cx` outward from the road. `pick` in [0, 1) varies the look.
void building(MeshBuilder& b, dr::Bldg type, const Vec3& base, const Vec3& ax, const Vec3& cx, int floors, float pick) {
    const dr::BldgSpec& sp = dr::kBldg[static_cast<int>(type)];
    const float         L = 0.5f * m(sp.len), D = 0.5f * m(sp.dep), H = static_cast<float>(floors) * m(sp.floorH);
    const Vec3 c[4] = {base - ax * L - cx * D, base + ax * L - cx * D, base + ax * L + cx * D, base - ax * L + cx * D};
    const Vec3 n[4] = {-cx, ax, cx, -ax};
    const bool hip  = type == dr::Bldg::House || type == dr::Bldg::Barn;
    for (int k = 0; k < 4; ++k) {
        const float w = (k % 2 ? sp.dep : sp.len);
        float       u0, u1;
        if (type == dr::Bldg::House) {  // one house back per variant cell; ends take part of it
            u0 = std::floor(pick * 4.0f) * 0.25f + 2.0f * kTexel;
            u1 = u0 + (0.25f - 4.0f * kTexel) * w / sp.len;
        } else if (type == dr::Bldg::Barn) {
            u0 = 0.75f + 2.0f * kTexel;
            u1 = 1.0f - 2.0f * kTexel;
        } else {
            u0 = pick * 7.0f;
            u1 = u0 + w / sp.texW;
        }
        facade(b, c[k], c[(k + 1) % 4], H, n[k], sp, u0, u1);
    }
    const Vec3 top = kUp * H;
    if (!hip) {
        roof(b, c[0] + top, c[1] + top, c[3] + top, c[2] + top, 0.5f + 2.0f * kTexel, 0.75f - 2.0f * kTexel);
        return;
    }
    const float e = m(dr::kEave), rise = m(type == dr::Bldg::Barn ? dr::kBarnRise : dr::kHouseRise);
    const Vec3  E[4] = {c[0] + top - ax * e - cx * e, c[1] + top + ax * e - cx * e, c[2] + top + ax * e + cx * e,
                        c[3] + top - ax * e + cx * e};
    const Vec3  r0   = base + top + kUp * rise - ax * std::max(0.0f, L - D),
                r1   = base + top + kUp * rise + ax * std::max(0.0f, L - D);
    roof(b, E[0], E[1], r0, r1, 0.0f, 0.5f);
    roof(b, E[2], E[3], r1, r0, 0.0f, 0.5f);
    roof(b, E[1], E[2], r1, r1, 0.0f, 0.25f);
    roof(b, E[3], E[0], r0, r0, 0.0f, 0.25f);
}

// Crossed double-sided cards, seen only from the road: every vertex leans toward it (`toRoad`), more upward at the
// top. One shared normal per height means no dark fin where a card is edge-on (radial or face normals made one).
void tree(MeshBuilder& b, const Vec3& base, int sp, float H, float yaw, const Vec3& toRoad) {
    const float W  = H * dr::kTreeAspect[sp];
    const float u0 = static_cast<float>(sp) * 0.25f + 4.0f * kTexel, u1 = u0 + 0.25f - 8.0f * kTexel;
    const float vt = 4.0f * kTexel, vb = 0.25f - kTexel;
    const Vec3  lo = glm::normalize(toRoad * 0.8f + kUp * 0.35f), hi = glm::normalize(toRoad * 0.5f + kUp * 0.85f);
    for (int k = 0; k < 2; ++k) {
        const float               a = yaw + static_cast<float>(k) * 0.5f * util::units::kPi;
        const Vec3                e(std::cos(a), 0.0f, std::sin(a)), f(-e.z, 0.0f, e.x);
        const std::array<Vec3, 4> p{base - e * (0.5f * W), base + e * (0.5f * W), base - e * (0.5f * W) + kUp * H,
                                    base + e * (0.5f * W) + kUp * H};
        const std::array<Vec2, 4> uv{Vec2(u0, vb), Vec2(u1, vb), Vec2(u0, vt), Vec2(u1, vt)};
        uvquad(b, p, uv, {lo, lo, hi, hi}, f);
        uvquad(b, p, uv, {lo, lo, hi, hi}, -f);
    }
}

// What a tile side must keep clear of: its ramps / service roads, crossroad aprons and bridge approaches.
struct Clearance {
    const CorridorAlignment&         al;
    std::vector<const CorridorPath*> paths;
    int                              side;

    // Outermost paved |lateral| of this side's paths at s (0 when none).
    float path_out(double s) const {
        float o = 0.0f;
        for (const CorridorPath* p : paths)
            if (s >= p->s0 && s <= p->s1) o = std::max(o, std::abs(corridor_path_at(al, *p, s).lat) + p->halfWidth);
        return o;
    }
    // Nearest |lateral| allowed at s: `in` past the shoulder edge and `pad` past any path.
    float inner(double s, float in, float pad) const {
        const float po = path_out(s);
        return std::max(al.outer_edge(al.sample(s)) + in, po > 0.0f ? po + pad : 0.0f);
    }
    // Farthest |lateral| with ground under it.
    float outer(double s) const {
        return al.outer_edge(al.sample(s)) + al.header().grassExtent + corridor_far_ground(al, side, s);
    }
    // A point (s, |lat|) on a cross street: crossroad stub + apron, or an overpass approach / LIE-over trench.
    bool on_cross(double s, float lat) const {
        const double pad = m(dr::kCrossPad);
        for (const CorridorCrossroad& c : al.crossroads())
            if (std::abs(s - c.s) < m(diamond::kApronHalf) + pad &&
                lat < std::max(path_out(c.s), al.outer_edge(al.sample(c.s)) + al.sr_strip()) + al.cross_stub() + pad)
                return true;
        for (const CorridorBridge& br : al.bridges()) {
            const float  t = static_cast<float>(side) * lat;
            const double a = s - br.s - static_cast<double>(t * std::tan(br.skew));
            if (std::abs(a) < br.halfA + pad && lat < br.reach[side > 0] + m(dr::kApproach)) return true;
        }
        return false;
    }
};

double route_lo(const CorridorAlignment& al) { return al.ends().portal + m(60.0f); }
double route_hi(const CorridorAlignment& al) { return al.main_end() - m(60.0f); }

}  // namespace

const dr::Kit& dress_kit(const CorridorAlignment& al, double s) {
    const dr::Kit* k = &dr::kKits[0];
    for (const dr::Kit& x : dr::kKits) {
        const double xs = exit_s(al, x.fromExit);
        if (xs >= 0.0 && xs <= s) k = &x;
    }
    return *k;
}

void emit_lamps(MeshBuilder& b, std::vector<DrawCall>& draws, std::vector<SpotLightDesc>& lamps,
                const CorridorAlignment& al, const std::vector<CorridorPath>& paths, const CorridorSigns* signs,
                double s0, double s1, double ox, double oz, const CorridorLook& look) {
    namespace ul             = util::light;
    const CorridorHeader& h  = al.header();
    const float           ry = h.roadY;
    const auto near_deck     = [&](double st) {  // the pole would pierce a crossroad deck or stand under an overpass
        return std::any_of(al.crossroads().begin(), al.crossroads().end(),
                           [&](const CorridorCrossroad& c) { return std::abs(c.s - st) < m(25.0f); }) ||
               std::any_of(al.bridges().begin(), al.bridges().end(), [&](const CorridorBridge& br) {
                   return br.kind != CrossingKind::LieOver && std::abs(br.s - st) < br.halfA + m(6.0f);
               });
    };
    const auto off_route = [&](double st) {
        return al.in_turnaround(st) || near_deck(st) ||
               (al.has_ends() && (st < al.ends().portal + m(80.0f) || st > al.ends().barrierEnd));
    };
    struct Pole {
        Vec3     base, top;
        Vec3     arm[2];  // unit arm directions (count `arms`)
        int      arms;
        dr::Lamp kind;
    };
    std::vector<Pole>                  poles;
    std::vector<std::pair<Vec3, Vec3>> pedestals;  // NJ-top pole: barrier foot centre, right
    const auto right_of = [](const CorridorAlignment::Sample& sm) { return Vec3(-sm.tz, 0.0f, sm.tx); };

    // Queens median twin arms on the L6.1 grid (D3 gantry posts already dodge it).
    const double med = m(kCorridorLampSpacingM);
    for (double st = std::ceil(s0 / med) * med; st < s1; st += med) {
        if (off_route(st) || al.median_extra(st) > 0.0f || dress_kit(al, st).lamp != dr::Lamp::MedianTwin) continue;
        const auto sm = al.sample(st);
        const Vec3 base(static_cast<float>(sm.x - ox), ry + sm.y - al.dip(st), static_cast<float>(sm.z - oz));
        poles.push_back(
            {base, base + kUp * m(kCorridorLampHeightM), {right_of(sm), -right_of(sm)}, 2, dr::Lamp::Cobra});
        if (al.edges().jersey) pedestals.push_back({at(al, st, 0.0f, al.surface_y(st, 0.0f), ox, oz), right_of(sm)});
    }
    // Outer singles: Queens between the median rows, Nassau / west Suffolk on the right shoulder (staggered per side).
    for (const int side : {+1, -1}) {
        for (const bool queens : {true, false}) {
            if (!al.has_ends()) break;  // no exit table: the L6.1 median row only
            const double step = m(queens ? dr::kQueensOuter : dr::kShoulderSpacing);
            const double off  = queens ? m(30.0f) : (side > 0 ? 0.0 : 0.5 * step);
            for (double st = std::ceil((s0 - off) / step) * step + off; st < s1; st += step) {
                if (st < s0 || off_route(st)) continue;
                const dr::Lamp kind = dress_kit(al, st).lamp;
                if (queens != (kind == dr::Lamp::MedianTwin) || kind == dr::Lamp::None) continue;
                const auto  sm        = al.sample(st);
                const float lat       = al.outer_edge(sm) + m(dr::kPoleOut);
                const bool  hit       = std::any_of(paths.begin(), paths.end(), [&](const CorridorPath& p) {
                    if (p.side != side || st < p.s0 - m(5.0f) || st > p.s1 + m(5.0f)) return false;
                    const float c = std::abs(corridor_path_at(al, p, st).lat);
                    return lat > c - p.halfWidth - m(3.0f) && lat < c + p.halfWidth + m(3.0f);
                });
                const auto  near_sign = [&](const auto& v) {
                    return std::any_of(v.begin(), v.end(), [&](const auto& g) {
                        return g.side == side && std::abs(g.s - st) < m(dr::kSignClear + road::signs::kColumnGap);
                    });
                };
                if (hit || (signs && (near_sign(signs->gantries) || near_sign(signs->gores) ||
                                      near_sign(signs->markers) || near_sign(signs->boards))))
                    continue;
                const float sg   = static_cast<float>(side);
                const Vec3  base = at(al, st, sg * lat, corridor_verge_y(al, paths, side, st, lat), ox, oz);
                poles.push_back({base,
                                 base + kUp * m(dr::kPoleH),
                                 {-right_of(sm) * sg, Vec3(0.0f)},
                                 1,
                                 queens ? dr::Lamp::Cobra : kind});
            }
        }
    }
    if (poles.empty()) return;

    // Steel: octagonal pole, upswept arm (two rods) per arm.
    const auto arm_end  = [](const Pole& p, int k, float reach) { return p.top + p.arm[k] * reach + kUp * m(0.55f); };
    const auto reach_of = [](const Pole& p) { return m(p.arms == 2 ? 3.0f : dr::kArmReach); };
    uint32_t   mk       = b.batch_mark();
    for (const Pole& p : poles) {
        cmesh::pole(b, p.base, p.top + kUp * m(0.1f), m(0.11f));
        for (int k = 0; k < p.arms; ++k) {
            const float R = reach_of(p);
            const Vec3  a = p.top - kUp * m(0.5f), q = p.top + p.arm[k] * (0.45f * R) + kUp * m(0.35f);
            cmesh::rod(b, a, q, m(0.045f));
            cmesh::rod(b, q, arm_end(p, k, R), m(0.04f));
        }
    }
    b.close_batch(mk, Vec4(0.45f, 0.46f, 0.48f, 1.0f), MAT_METAL);

    // Heads: cobra (long housing, drop lens) or shoebox (flat box, flat lens).
    struct Head {
        Vec3     c, along, perp;
        dr::Lamp kind;
    };
    std::vector<Head> heads;
    for (const Pole& p : poles)
        for (int k = 0; k < p.arms; ++k) {
            const Vec3 d = p.arm[k], perp = glm::normalize(glm::cross(d, kUp));
            heads.push_back({arm_end(p, k, reach_of(p)) + d * m(0.32f) - kUp * m(0.06f), d, perp, p.kind});
        }
    mk = b.batch_mark();
    for (const Head& hd : heads) {
        const bool box = hd.kind == dr::Lamp::Shoebox;
        cmesh::obox(b, hd.c, hd.along * m(box ? 0.32f : 0.38f), kUp * m(box ? 0.08f : 0.09f),
                    hd.perp * m(box ? 0.30f : 0.16f));
    }
    b.close_batch(mk, Vec4(0.55f, 0.56f, 0.58f, 1.0f), MAT_METAL);
    mk = b.batch_mark();
    for (const Head& hd : heads) {
        if (hd.kind == dr::Lamp::Shoebox)
            cmesh::obox(b, hd.c - kUp * m(0.085f), hd.along * m(0.28f), kUp * m(0.006f), hd.perp * m(0.26f));
        else
            cmesh::obox(b, hd.c - kUp * m(0.13f), hd.along * m(0.25f), kUp * m(0.05f), hd.perp * m(0.12f));
    }
    if (b.close_batch(mk, Vec4(0.95f, 0.9f, 0.8f, 1.0f), MAT_DEFAULT)) draws.back().emissiveNits = 20000.0f;

    // D2: an NJ-top pole stands on a widened block.
    mk             = b.batch_mark();
    const float hh = 0.5f * util::units::ft(road::edges::kNjHeightIn / 12.0f);
    for (const auto& [c, r] : pedestals)
        cmesh::obox(b, c + kUp * hh, r * m(road::edges::kPedestalHalf), kUp * hh,
                    glm::cross(kUp, r) * (0.5f * m(road::edges::kPedestalLen)));
    b.close_batch(mk, look.concrete, MAT_CONCRETE);

    // One downward sodium spot per pole: the twin's two 16,000 lm heads as one source at the pole.
    const Vec3  sodium = ul::kelvin_to_rgb(2100.0f);
    const float half   = 70.0f * util::units::kRadPerDeg;
    for (const Pole& p : poles) {
        SpotLightDesc l{};
        l.position  = p.arms == 2 ? p.top - kUp * m(0.3f) : arm_end(p, 0, reach_of(p)) - kUp * m(0.3f);
        l.direction = Vec3(0.0f, -1.0f, 0.0f);
        l.color     = sodium;
        l.intensity = ul::cone_candela_from_lumens(kCorridorLampLumens * (p.arms == 2 ? 1.0f : 0.5f), half);
        l.range     = m(60.0f);
        l.cosInner  = std::cos(55.0f * util::units::kRadPerDeg);
        l.cosOuter  = std::cos(75.0f * util::units::kRadPerDeg);
        lamps.push_back(l);
    }
}

void emit_dressing(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al,
                   const std::vector<CorridorPath>& paths, double ts, double te, double ox, double oz) {
    const float dens = al.edges().dress;
    if (dens <= 0.0f || !al.has_ends()) return;
    const double lo = std::max(ts, route_lo(al)), hi = std::min(te, route_hi(al));
    if (hi - lo < m(1.0f)) return;
    // shortcut: kit snaps to 300 m tiles (taken at the middle), like D1 pavement; split tiles for exact seams.
    const dr::Kit& kit = dress_kit(al, 0.5 * (ts + te));
    const float    dg  = al.header().grassY - al.header().roadY;

    for (const int side : {+1, -1}) {
        Clearance cl{al, {}, side};
        for (const CorridorPath& p : paths)
            if (p.side == side && p.s1 > lo - m(200.0f) && p.s0 < hi + m(200.0f)) cl.paths.push_back(&p);
        const float sg = static_cast<float>(side);
        struct Rect {
            double s0, s1;
            float  l0, l1;
        };
        std::vector<Rect> rects;

        // ── Buildings: lots on a grid along the road, rows outward from the inner limit ──
        uint32_t mk = b.batch_mark();
        for (int which = 0; which < 2; ++which) {
            const dr::Bldg type = which ? kit.b2 : kit.b1;
            const float    rate = (which ? kit.rate2 : kit.rate1) * dens;
            if (type == dr::Bldg::None || rate <= 0.0f) continue;
            const dr::BldgSpec& sp   = dr::kBldg[static_cast<int>(type)];
            const double        lotS = m(sp.lotS);
            const float         L = 0.5f * m(sp.len), D = 0.5f * m(sp.dep), pad = m(4.0f);
            for (double sa = std::ceil(lo / lotS) * lotS; sa < hi; sa += lotS) {
                const int    slot = static_cast<int>(std::llround(sa / lotS));
                const double s    = sa + (rnd(slot, side, 11 + which) - 0.5) * 0.3 * lotS;
                if (s - L < lo || s + L > hi) continue;
                float in = 0.0f;
                for (const double q : {s - L, s, s + L})
                    in = std::max(in, cl.inner(q, m(dr::kBldgIn), m(dr::kBldgPathClear)));
                const float out = std::min({cl.outer(s - L), cl.outer(s), cl.outer(s + L)});
                for (int row = 0; row < 12; ++row) {
                    const float lat =
                        in + D + static_cast<float>(row) * m(sp.lotT) + rnd(slot, row, 23 + side) * m(6.0f);
                    if (lat + D > out - m(5.0f)) break;
                    if (rnd(slot, row * 2 + (side > 0), 31 + static_cast<int>(type)) >= rate) continue;
                    const Rect r{s - L - pad, s + L + pad, lat - D - pad, lat + D + pad};
                    const bool clash = std::any_of(rects.begin(), rects.end(),
                                                   [&](const Rect& o) {
                                                       return r.s0 < o.s1 && o.s0 < r.s1 && r.l0 < o.l1 && o.l0 < r.l1;
                                                   }) ||
                                       cl.on_cross(s - L, lat - D) || cl.on_cross(s + L, lat - D) ||
                                       cl.on_cross(s, lat) || cl.on_cross(s - L, lat + D) ||
                                       cl.on_cross(s + L, lat + D);
                    if (clash) continue;
                    rects.push_back(r);
                    const auto sm = al.sample(s);
                    const Vec3 ax = Vec3(sm.tx, 0.0f, sm.tz), cx = Vec3(-sm.tz, 0.0f, sm.tx) * sg;
                    const int  span = std::max(0, kit.floorsMax - kit.floorsMin);
                    int        floors =
                        type == dr::Bldg::House ? 2
                        : (type == dr::Bldg::Warehouse || type == dr::Bldg::Barn)
                            ? 1
                            : kit.floorsMin + static_cast<int>(rnd(slot, row, 41) * static_cast<float>(span + 1));
                    building(b, type, at(al, s, sg * lat, al.ground_y(s) + dg - m(dr::kSink), ox, oz), ax, cx, floors,
                             rnd(slot, row, 53 + side));
                }
            }
        }
        b.close_batch(mk, Vec4(1.0f), MAT_DRESSING);

        // ── D6 headstones: straight rows from the tree line out kGraveDeep; tablets face the road ──
        if (const float gd = kit.graves * al.edges().graves; gd > 0.0f) {
            mk              = b.batch_mark();
            const double gs = m(dr::kGraveS);
            for (double sa = std::ceil(lo / gs) * gs; sa < hi; sa += gs) {
                const int   is  = static_cast<int>(std::llround(sa / gs));
                const float in  = cl.inner(sa, m(dr::kTreeIn), m(dr::kPathClear));
                const float out = std::min(cl.outer(sa) - m(3.0f), in + m(dr::kGraveDeep));
                const auto  sm  = al.sample(sa);
                const Vec3  ax(sm.tx, 0.0f, sm.tz), cx = Vec3(-sm.tz, 0.0f, sm.tx) * sg;
                for (int il = 0; in + static_cast<float>(il) * m(dr::kGraveT) < out; ++il) {
                    const float lat = in + static_cast<float>(il) * m(dr::kGraveT);
                    if (rnd(is, il, 101 + side) >= gd || cl.on_cross(sa, lat)) continue;
                    const float pick = rnd(is, il, 107 + side), k = 0.75f + 0.5f * rnd(is, il, 109 + side);
                    const Vec3  g = at(al, sa, sg * lat, al.ground_y(sa) + dg - m(0.1f), ox, oz);
                    if (pick < dr::kObeliskShare) {
                        const float w = 0.5f * m(dr::kObelisk[0]), H = k * m(dr::kObelisk[1]);
                        cmesh::obox(b, g + kUp * m(0.25f), ax * (1.6f * w), kUp * m(0.25f), cx * (1.6f * w));  // plinth
                        cmesh::obox(b, g + kUp * (m(0.5f) + 0.5f * H), ax * w, kUp * (0.5f * H), cx * w);
                    } else {
                        const float* d = pick < dr::kObeliskShare + dr::kDieShare ? dr::kDie : dr::kTablet;
                        cmesh::obox(b, g + kUp * (0.5f * k * m(d[1])), ax * (0.5f * m(d[0])),
                                    kUp * (0.5f * k * m(d[1])), cx * (0.5f * m(d[2])));
                    }
                }
            }
            b.close_batch(mk, Vec4(0.72f, 0.72f, 0.7f, 1.0f), MAT_CONCRETE);  // weathered granite
        }

        // ── Trees: one chance per kCell square, thinned by a grove noise; never in a building lot ──
        const auto sightline = [&](double s, float lat) {  // D6: lawn wedge out to a landmark on this side
            namespace lm = road::landmarks;
            return al.edges().landmarks &&
                   std::any_of(std::begin(lm::kSpots), std::end(lm::kSpots), [&](const auto& sp) {
                       const float L = m(std::abs(sp.lat)), f = std::max(0.0f, 1.0f - lat / L);
                       return sp.clear > 0.0f && (sp.lat < 0.0f) == (side < 0) && lat < L &&
                              std::abs(s - landmark_s(al, sp)) < m(sp.clear + lm::kSightBase * f);
                   });
        };
        mk                = b.batch_mark();
        const double cell = m(dr::kCell);
        float        wsum = 0.0f;
        for (const float w : kit.species) wsum += w;
        for (double sc = std::ceil(lo / cell) * cell; sc < hi && wsum > 0.0f; sc += cell) {
            const int   is   = static_cast<int>(std::llround(sc / cell));
            const float in   = cl.inner(sc, m(dr::kTreeIn), m(dr::kPathClear));
            const float out  = cl.outer(sc) - m(3.0f);
            const float edge = al.outer_edge(al.sample(sc));
            for (int il = 0; il < 80; ++il) {
                const float lat0 = edge + static_cast<float>(il) * m(dr::kCell);
                if (lat0 > out) break;
                const double s   = sc + rnd(is, il, 61 + side) * cell;
                const float  lat = lat0 + rnd(is, il, 67 + side) * m(dr::kCell);
                if (lat < in || lat > out || s < lo || s > hi) continue;
                if (rnd(is, il, 71 + side) >= kit.trees * dens * (0.3f + 1.4f * grove(s, lat, side))) continue;
                if (cl.on_cross(s, lat) || sightline(s, lat)) continue;
                if (std::any_of(rects.begin(), rects.end(),
                                [&](const Rect& o) { return s > o.s0 && s < o.s1 && lat > o.l0 && lat < o.l1; }))
                    continue;
                float pick = rnd(is, il, 73 + side) * wsum;
                int   sp   = 0;
                while (sp < 3 && pick >= kit.species[sp]) pick -= kit.species[sp++];
                const float H = m(dr::kTreeH[sp][0] + (dr::kTreeH[sp][1] - dr::kTreeH[sp][0]) * rnd(is, il, 79 + side));
                const auto  sm = al.sample(s);
                tree(b, at(al, s, sg * lat, al.ground_y(s) + dg - m(dr::kSink), ox, oz), sp, H,
                     rnd(is, il, 83 + side) * util::units::kPi, Vec3(sm.tz, 0.0f, -sm.tx) * sg);
            }
        }
        // D6: street-tree row along the service roads' outer curb (Melville South Service Road)
        for (const CorridorPath* p : cl.paths) {
            if (kit.srTrees <= 0.0f || p->kind != CorridorPath::Kind::Service || wsum <= 0.0f) continue;
            const double st = m(kit.srTrees);
            for (double s = std::ceil(std::max(lo, p->s0) / st) * st; s < std::min(hi, p->s1); s += st) {
                const PathPoint q   = corridor_path_at(al, *p, s);
                const float     lat = std::abs(q.lat) + p->halfWidth + m(diamond::kCurbW + dr::kSrTreeOut);
                const int       k   = static_cast<int>(std::llround(s / st));
                if (lat > cl.outer(s) || cl.on_cross(s, lat) || rnd(k, side, 89) < 0.12f) continue;  // a few gaps
                const int   sp = kit.species[0] >= kit.species[1] ? 0 : 1;
                const float H  = m(dr::kTreeH[sp][0] + (dr::kTreeH[sp][1] - dr::kTreeH[sp][0]) * rnd(k, side, 97));
                const auto  sm = al.sample(s);
                tree(b, at(al, s, sg * lat, q.y - m(dr::kSink), ox, oz), sp, H, rnd(k, side, 93) * util::units::kPi,
                     Vec3(sm.tz, 0.0f, -sm.tx) * sg);
            }
        }
        if (b.close_batch(mk, Vec4(1.0f), MAT_DRESSING)) draws.back().noShadow = true;
    }
}

CorridorSegment skyline_segment(const CorridorAlignment& al) {
    CorridorSegment seg;
    if (!al.has_ends()) return seg;
    // Midtown: past the tube's Manhattan end (station 0), on the line from the Queens portal through it.
    const auto   p0 = al.sample(0.0), pq = al.sample(al.ends().portal);
    const double dx = p0.x - pq.x, dz = p0.z - pq.z, d = std::hypot(dx, dz);
    seg.originX = p0.x + dx / d * m(dr::kSkyBack);
    seg.originZ = p0.z + dz / d * m(dr::kSkyBack);
    MeshBuilder    b(seg.mesh, seg.draws);
    const float    W = 0.5f * m(dr::kSkyW), H = m(dr::kSkyH), y = al.ground_y(0.0);
    const Vec3     n(0.0f, 0.0f, 1.0f);
    const uint32_t mk = b.batch_mark();
    uvquad(b, {Vec3(-W, y, 0.0f), Vec3(W, y, 0.0f), Vec3(-W, y + H, 0.0f), Vec3(W, y + H, 0.0f)},
           {Vec2(0.0f, 1.0f), Vec2(1.0f, 1.0f), Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f)}, {n, n, n, n}, n);
    b.close_batch(mk, Vec4(1.0f), MAT_SKYLINE);
    seg.draws.back().noShadow = true;
    seg.boundsRadius          = W;
    seg.startStation = seg.endStation = 0.0;
    return seg;
}

bool skyline_model(const Vec3& cam, const Vec3& anchor, Mat4& model) {
    const Vec3  d = Vec3(cam.x - anchor.x, 0.0f, cam.z - anchor.z);
    const float R = glm::length(d);
    if (R > m(dr::kSkyMaxR) || R < 1.0f) return false;
    const float k = std::min(1.0f, m(dr::kSkyDist) / R);
    // final = cam + k (anchor + Ry v - cam): translate by (cam - anchor)(1 - k) on top of the instance offset
    model = glm::translate(Mat4(1.0f), (cam - anchor) * (1.0f - k)) * glm::scale(Mat4(1.0f), Vec3(k)) *
            glm::rotate(Mat4(1.0f), std::atan2(d.x, d.z), kUp);
    return true;
}

}  // namespace swish
