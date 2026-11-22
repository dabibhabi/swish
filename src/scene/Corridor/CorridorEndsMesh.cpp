#include "CorridorEnds.h"
#include "CorridorSigns.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "scene/RoadScene/RoadParams.h"
#include "utils/container/VectorOps.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace swish {

using cmesh::face;
using cmesh::obox;
using util::container::filled;
using util::units::m;
namespace E = road::ends;

namespace {

const Vec3 kUp(0.0f, 1.0f, 0.0f);

// Corridor (s, lat, y) to segment-local; past the last station it runs on along the end tangent.
Vec3 atx(const CorridorAlignment& al, double s, float lat, float y, double ox, double oz) {
    const double T = al.total_length(), ds = std::max(0.0, s - T);
    const auto   sm = al.sample(std::min(s, T));
    return Vec3(static_cast<float>(sm.x - ox + static_cast<double>(sm.tx) * ds - static_cast<double>(sm.tz * lat)), y,
                static_cast<float>(sm.z - oz + static_cast<double>(sm.tz) * ds + static_cast<double>(sm.tx * lat)));
}

// Flat quad facing n with world-planar UVs, so neighbouring quads share one texture field (no seams).
void flat(MeshBuilder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float tile, const Vec3& n = kUp) {
    if (glm::dot(glm::cross(p1 - p0, p2 - p0), n) < 0.0f) {
        std::swap(p0, p1);
        std::swap(p2, p3);
    }
    const auto uv = [&](const Vec3& p) { return Vec2(p.x / tile, p.z / tile); };
    const Vec4 tan(1.0f, 0.0f, 0.0f, 1.0f);
    b.push_quad({p0, n, uv(p0), tan}, {p1, n, uv(p1), tan}, {p2, n, uv(p2), tan}, {p3, n, uv(p3), tan},
                QuadWinding::Fan);
}

// Frame point: lateral along the (banked) frame, height straight up.
Vec3 fp(const SweepFrame& f, float lat, float y) { return f.center + f.right * lat + kUp * y; }

// Board on posts beside the road facing `n`; bottom `lift` above the road. Diamond = turned 45 degrees.
struct Board {
    Vec3  base, n;  // foot at the board centre line, facing direction (horizontal)
    float w, h, lift;
    bool  diamond;
    int   cell;  // D3: sign_atlas::kBoards face
};

void boards(MeshBuilder& b, std::vector<DrawCall>& draws, const std::vector<Board>& v) {
    if (v.empty()) return;
    const Vec4 steel(0.55f, 0.56f, 0.58f, 1.0f), back(0.42f, 0.43f, 0.45f, 1.0f);
    uint32_t   mk = b.batch_mark();
    for (const Board& k : v) {
        const Vec3  side = glm::normalize(glm::cross(kUp, k.n));
        const float top = k.lift + k.h, post = m(0.05f);
        for (const float o : k.diamond ? std::vector<float>{0.0f} : std::vector<float>{-0.3f * k.w, 0.3f * k.w})
            obox(b, k.base + side * o + kUp * (0.5f * top - m(0.25f)), side * post, kUp * (0.5f * top + m(0.25f)),
                 k.n * post);
    }
    b.close_batch(mk, steel, MAT_METAL);
    for (const bool front : {true, false}) {
        mk = b.batch_mark();
        for (const Board& k : v) {
            const Vec3 side = glm::normalize(glm::cross(kUp, k.n));
            const Vec3 c    = k.base + kUp * (k.lift + 0.5f * k.h) +
                              k.n * (front ? m(0.075f) : m(0.06f));  // face just past the back box
            const Vec3 hx = side * (0.5f * k.w), hy = kUp * (0.5f * k.h);
            const Vec3 r = k.diamond ? (hx + hy) * 0.7071f : hx, u = k.diamond ? (hy - hx) * 0.7071f : hy;
            if (front)  // D3: text face from the kit atlas (W2-6 content is pre-rotated for the turned quad)
                sign_quad(b, c, r, u, k.n, sign_atlas::kBoards[k.cell], sign_atlas::kKitW, sign_atlas::kKitH);
            else
                obox(b, c, r, u, k.n * m(0.012f));
        }
        if (b.close_batch(mk, front ? Vec4(1.0f) : back, front ? MAT_SIGN_KIT : MAT_METAL) && front) {
            draws.back().retro        = 1.0f;  // sign sheeting is retroreflective
            draws.back().roughnessMul = road::signs::kSheen;
            draws.back().metalness    = 0.0f;
        }
    }
}

}  // namespace

void emit_tunnel(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double from, double to,
                 double ox, double oz, const CorridorLook& look) {
    const CorridorEnds& e = al.ends();
    if (e.portal <= from) return;
    const CorridorHeader& h  = al.header();
    const float           ry = h.roadY, W = m(E::kWalkW), wh = m(E::kWalkH), ch = m(E::kCeilH);
    const float           rep = m(E::kTileRepeat);
    // white glazed tile with New York blue and gold stripes (2017-18 re-tiling; stripe layout unverified)
    const Vec4   white(0.92f, 0.91f, 0.88f, 1.0f), blue(0.02f, 0.07f, 0.28f, 1.0f), gold(0.85f, 0.55f, 0.06f, 1.0f);
    const Vec4   walk(0.62f, 0.61f, 0.58f, 1.0f), ceilCol(0.58f, 0.58f, 0.56f, 1.0f), lensCol(1.0f, 0.96f, 0.9f, 1.0f);
    const Vec4   steel(0.55f, 0.56f, 0.58f, 1.0f);
    const double tile = static_cast<double>(h.tileLength), end = std::min(to, e.portal);

    for (double a = from; a < end - 1.0; a += tile) {
        const double z = std::min(a + tile, end);
        const auto   f = corridor_mainline_frames(al, a, z, ox, oz);
        const size_t n = f.size();
        if (n < 2) continue;
        const float lanes = al.sample(a).lanes;  // shortcut: constant per tile; fine while the tube keeps its lanes
        const float pi = al.paint_inner(lanes), po = al.paint_outer(lanes), tin = pi - W, tout = po + W;
        for (const float sg : {1.0f, -1.0f}) {
            const auto band = [&](float t0, float t1, float y, const Vec4& c) {
                b.emit_sweep_band(f, filled(n, std::min(sg * t0, sg * t1)), filled(n, std::max(sg * t0, sg * t1)), y, c,
                                  MAT_CONCRETE, look.concreteTile);
            };
            band(tin, pi, ry + wh, walk);  // safety walks
            band(po, tout, ry + wh, walk);
            b.emit_sweep_wall(f, sg * pi, ry, wh, sg > 0.0f, walk, MAT_CONCRETE, look.concreteTile);  // curb faces
            b.emit_sweep_wall(f, sg * po, ry, wh, sg < 0.0f, walk, MAT_CONCRETE, look.concreteTile);
            for (const auto& [t, right] : {std::pair{tin, sg > 0.0f}, std::pair{tout, sg < 0.0f}}) {
                const float y0 = ry + wh, up = E::kBandLo + E::kBandH + E::kGoldH;
                b.emit_sweep_wall(f, sg * t, y0, m(E::kBandLo), right, white, MAT_TILE, rep);
                b.emit_sweep_wall(f, sg * t, y0 + m(E::kBandLo), m(E::kBandH), right, blue, MAT_TILE, rep);
                b.emit_sweep_wall(f, sg * t, y0 + m(E::kBandLo + E::kBandH), m(E::kGoldH), right, gold, MAT_TILE, rep);
                b.emit_sweep_wall(f, sg * t, y0 + m(up), ch - wh - m(up), right, white, MAT_TILE, rep);
            }
            uint32_t mk = b.batch_mark();  // ceiling, facing down
            for (size_t i = 0; i + 1 < n; ++i)
                flat(b, fp(f[i], sg * tin, ry + ch), fp(f[i], sg * tout, ry + ch), fp(f[i + 1], sg * tin, ry + ch),
                     fp(f[i + 1], sg * tout, ry + ch), look.concreteTile, -kUp);
            b.close_batch(mk, ceilCol, MAT_CONCRETE);
        }
        // Luminaire rows: two continuous LED rows per tube near the wall heads, dark lead-in at the closed end.
        const double la = std::max(a, static_cast<double>(m(E::kLightStart)));
        if (z - la > 1.0) {
            const auto  g  = corridor_mainline_frames(al, la, z, ox, oz);
            const float hw = 0.5f * m(E::kStripW), yl = ry + ch - m(0.10f);
            const float rows[2] = {tin + m(E::kStripInset), tout - m(E::kStripInset)};
            uint32_t    mk      = b.batch_mark();
            for (const float sg : {1.0f, -1.0f})
                for (const float t : rows)
                    for (size_t i = 0; i + 1 < g.size(); ++i)
                        flat(b, fp(g[i], sg * (t - hw), yl), fp(g[i], sg * (t + hw), yl),
                             fp(g[i + 1], sg * (t - hw), yl), fp(g[i + 1], sg * (t + hw), yl), m(1.0f), -kUp);
            if (b.close_batch(mk, lensCol, MAT_DEFAULT)) {
                draws.back().emissiveNits = E::kStripNits;
                draws.back().selfLit      = true;
            }
            for (const float sg : {1.0f, -1.0f})  // housings
                for (const float t : rows) {
                    b.emit_sweep_wall(g, sg * (t - hw), yl, m(0.10f), false, steel, MAT_METAL);
                    b.emit_sweep_wall(g, sg * (t + hw), yl, m(0.10f), true, steel, MAT_METAL);
                }
        }
        if (a <= 0.0) {  // closed west end: the tube runs on into the dark
            uint32_t mk = b.batch_mark();
            for (const float sg : {1.0f, -1.0f})
                face(b, fp(f[0], sg * tin, ry), fp(f[0], sg * tout, ry), fp(f[0], sg * tin, ry + ch),
                     fp(f[0], sg * tout, ry + ch), glm::normalize(f[1].center - f[0].center));
            b.close_batch(mk, Vec4(0.01f, 0.01f, 0.01f, 1.0f), MAT_DEFAULT);
        }
    }

    // ── Portal: headwall with two openings, coping, grassed cover, 2:1 slopes behind wing walls ──
    const double P = e.portal;
    if (P >= from && P < to) {
        const auto  sm = al.sample(P);
        const Vec3  fwd(sm.tx, 0.0f, sm.tz), right(-sm.tz, 0.0f, sm.tx);
        const float lanes = sm.lanes, pi = al.paint_inner(lanes), po = al.paint_outer(lanes);
        const float tin = pi - W, tout = po + W, X = tout + m(E::kPortalOut), H = m(E::kHeadH);
        const float Wg = al.outer_edge(lanes) + h.grassExtent, dg = h.grassY - h.roadY;
        const auto  Q = [&](double s, float lat, float y) {
            return cmesh::at(al, s, lat, al.surface_y(s, lat) + y, ox, oz);
        };
        uint32_t   mk    = b.batch_mark();
        const auto front = [&](float l0, float l1, float y0, float y1) {
            face(b, Q(P, l0, y0), Q(P, l1, y0), Q(P, l0, y1), Q(P, l1, y1), fwd);
        };
        front(-X, X, ch, H);         // over both openings
        front(-tin, tin, 0.0f, ch);  // pier between the tubes
        front(tout, X, 0.0f, ch);
        front(-X, -tout, 0.0f, ch);
        for (const float sg : {1.0f, -1.0f}) {  // wing walls, top falling with the slope behind them
            const int K = 6;
            for (int k = 0; k < K; ++k) {
                const double s0 = P + static_cast<double>(m(E::kWingLen)) * k / K;
                const double s1 = P + static_cast<double>(m(E::kWingLen)) * (k + 1) / K;
                const float  t0 = H + (m(0.3f) - H) * static_cast<float>(k) / K;
                const float  t1 = H + (m(0.3f) - H) * static_cast<float>(k + 1) / K;
                face(b, Q(s0, sg * X, -m(0.5f)), Q(s1, sg * X, -m(0.5f)), Q(s0, sg * X, t0), Q(s1, sg * X, t1),
                     -sg * right);
            }
        }
        b.close_batch(mk, look.concrete, MAT_CONCRETE);
        mk = b.batch_mark();
        obox(b, Q(P, 0.0f, H + m(0.15f)) - fwd * m(0.3f), right * (X + m(0.2f)), kUp * m(0.15f), fwd * m(0.45f));
        b.close_batch(mk, look.concrete * Vec4(0.8f, 0.8f, 0.8f, 1.0f), MAT_CONCRETE);  // coping

        mk                = b.batch_mark();  // grassed cover over the tubes and the slopes down to grade
        const double step = static_cast<double>(m(10.0f));
        for (double s = P - static_cast<double>(m(E::kCapLen)); s < P - 1.0; s += step) {
            const double s1 = std::min(s + step, P);
            const float  y0 = al.surface_y(s, 0.0f) + H, y1 = al.surface_y(s1, 0.0f) + H;
            flat(b, cmesh::at(al, s, -Wg, y0, ox, oz), cmesh::at(al, s, Wg, y0, ox, oz),
                 cmesh::at(al, s1, -Wg, y1, ox, oz), cmesh::at(al, s1, Wg, y1, ox, oz), look.grassTile);
        }
        for (const float sg : {1.0f, -1.0f}) {
            const double sT = P + static_cast<double>(m(E::kWingLen));
            const float  yT = al.surface_y(P, 0.0f) + H, yB = al.ground_y(sT) + dg - m(0.05f);
            const Vec3   p0 = cmesh::at(al, P, sg * X, yT, ox, oz), p1 = cmesh::at(al, P, sg * Wg, yT, ox, oz);
            const Vec3   p2 = cmesh::at(al, sT, sg * X, yB, ox, oz), p3 = cmesh::at(al, sT, sg * Wg, yB, ox, oz);
            Vec3         nn = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            flat(b, p0, p1, p2, p3, look.grassTile, nn.y < 0.0f ? -nn : nn);
        }
        b.close_batch(mk, look.grass, MAT_GRASS);
    }

    // D6: no toll gantry; Queens plaza gone since cashless tolling (2017-01-10), readers at the Manhattan portals
}

void emit_road_end(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, double from, double to,
                   double ox, double oz, const CorridorLook& look) {
    const CorridorEnds& e = al.ends();
    if (!al.has_ends()) return;
    const CorridorHeader& h = al.header();

    // ── Blank sign boards (D3 writes the text): EB approach on the right, WB departure on its right ──
    {
        struct Spec {
            float before, w, hgt;
            int   side;
            bool  warn;
        };
        // END 495 1 MILE, EXIT 73 CR 58 1/2 MILE, roundabout ahead (W2-6), Riverhead / Orient, I-495 WEST New York.
        // D3: the first two stand at their REAL-mile distance (the gantries' rule); before < 0 = that many real miles.
        static constexpr Spec kSigns[] = {{-1.0f, 4.3f, 2.4f, 1, false},
                                          {-0.5f, 4.3f, 3.0f, 1, false},
                                          {250.0f, 1.2f, 1.2f, 1, true},
                                          {150.0f, 3.6f, 2.4f, 1, false},
                                          {250.0f, 3.6f, 2.4f, -1, false}};
        std::vector<Board>    v;
        for (int i = 0; i < 5; ++i) {
            const Spec&  k = kSigns[i];
            const double s = e.ringC - (k.before < 0.0f ? -k.before * sign_wu_per_mile(al) : m(k.before));
            if (s < from || s >= to) continue;
            const auto  sm  = al.sample(s);
            const float lat = static_cast<float>(k.side) * (al.outer_edge(sm.lanes) + m(2.5f));
            const Vec3  fwd(sm.tx, 0.0f, sm.tz);
            const Board bd{cmesh::at(al, s, lat, al.surface_y(s, lat), ox, oz),
                           -static_cast<float>(k.side) * fwd,
                           m(k.w),
                           m(k.hgt),
                           m(k.warn ? 1.5f : 2.1f),
                           k.warn,
                           i};
            v.push_back(bd);
        }
        if (e.ringC >= from && e.ringC < to) {  // W1-8 chevrons on the island nose, facing the entries
            const auto sm = al.sample(e.ringC);
            const Vec3 fwd(sm.tx, 0.0f, sm.tz);
            for (const float lat : {m(1.2f), -m(1.2f)}) {
                const double s = e.ringC - static_cast<double>(e.ringRi - m(1.5f));
                v.push_back({cmesh::at(al, s, lat, al.surface_y(s, lat) + m(E::kIslandH), ox, oz), -fwd, m(0.75f),
                             m(0.9f), m(0.9f), false, 5});
            }
        }
        boards(b, draws, v);
    }
    if (e.ringC < from || e.ringC >= to) return;

    // ── Roundabout ──
    const float Ro = e.ringRo, Ri = e.ringRi, lift = E::kRingLiftWu, my = lift + h.markingLift;
    const float laneEnd = al.sample(e.mainEnd).lanes, edge = al.outer_edge(laneEnd);
    const int   N  = 96;
    const auto  th = [&](int k) { return 2.0f * util::units::kPi * static_cast<float>(k) / N; };
    const auto  R  = [&](float r, float a, float y) {
        const double s = e.ringC + static_cast<double>(r * std::cos(a));
        const float  l = r * std::sin(a);
        return atx(al, s, l, al.surface_y(s, l) + y, ox, oz);
    };
    // west entry: no curb, line or barrier where the approach pavement joins
    const auto opening = [&](float a, float r) {
        return std::cos(a) < 0.0f && std::abs(r * std::sin(a)) < edge + m(0.5f);
    };
    const auto ring = [&](float r0, float r1, float y0, float y1, bool skipOpen) {
        for (int k = 0; k < N; ++k) {
            const float a = th(k), c = th(k + 1);
            if (skipOpen && (opening(a, r1) || opening(c, r1))) continue;
            flat(b, R(r0, a, y0), R(r1, a, y1), R(r0, c, y0), R(r1, c, y1), m(2.0f));
        }
    };
    const auto wall = [&](float r, float y0, float y1, float out, bool skipOpen) {  // out +1 faces away from the centre
        for (int k = 0; k < N; ++k) {
            const float a = th(k), c = th(k + 1);
            if (skipOpen && (opening(a, r) || opening(c, r))) continue;
            const Vec3 n = glm::normalize(R(r + 1.0f, 0.5f * (a + c), 0.0f) - R(r, 0.5f * (a + c), 0.0f)) * out;
            face(b, R(r, a, y0), R(r, c, y0), R(r, a, y1), R(r, c, y1), Vec3(n.x, 0.0f, n.z));
        }
    };
    uint32_t mk = b.batch_mark();  // circulatory roadway, world-planar UVs like the asphalt tiles
    for (int k = 0; k < N; ++k) {
        const float a = th(k), c = th(k + 1);
        const auto  P = [&](float r, float t) {
            const Vec3 p = R(r, t, lift);
            return Vertex{p, kUp, Vec2(p.x, p.z) / look.asphaltTile, Vec4(1.0f, 0.0f, 0.0f, 1.0f)};
        };
        Vertex v0 = P(Ri, a), v1 = P(Ro, a), v2 = P(Ri, c), v3 = P(Ro, c);
        if (glm::dot(glm::cross(v1.position - v0.position, v2.position - v0.position), kUp) < 0.0f) {
            std::swap(v0, v1);
            std::swap(v2, v3);
        }
        b.push_quad(v0, v1, v2, v3, QuadWinding::Fan);
    }
    b.close_batch(mk, look.asphalt, MAT_ASPHALT);

    // Central island: mountable truck apron, curb, grassed mound.
    const float Rg = Ri - m(E::kApronW), ia = lift + m(E::kApronH), ih = m(E::kIslandH);
    mk = b.batch_mark();
    ring(Rg, Ri, ia, ia, false);
    wall(Ri, lift - m(0.02f), ia, 1.0f, false);
    wall(Rg, ia, ih, 1.0f, false);
    b.close_batch(mk, look.concrete, MAT_CONCRETE);
    mk = b.batch_mark();
    for (int j = 0; j < 5; ++j) {
        const float r0    = Rg * (1.0f - static_cast<float>(j) / 5.0f),
                    r1    = Rg * (1.0f - static_cast<float>(j + 1) / 5.0f);
        const auto  mound = [&](float r) { return ih + m(0.6f) * (1.0f - (r / Rg) * (r / Rg)); };
        ring(std::max(r1, m(0.05f)), r0, mound(std::max(r1, m(0.05f))), mound(r0), false);
    }
    b.close_batch(mk, look.grass, MAT_GRASS);

    // Paint: yellow along the island (left of circulating traffic), white outer edge, yield line at the EB entry.
    const size_t firstPaint = draws.size();
    mk                      = b.batch_mark();
    ring(Ri + m(0.15f), Ri + m(0.30f), my, my, false);
    b.close_batch(mk, look.yellow, MAT_DEFAULT);
    mk = b.batch_mark();
    ring(Ro - m(0.45f), Ro - m(0.30f), my, my, true);
    {
        const float pi = al.paint_inner(laneEnd), po = al.paint_outer(laneEnd), tw = m(0.3f);
        for (float t = pi + m(0.2f); t + tw < po; t += m(0.6f)) {  // MUTCD shark teeth, pointing at approaching traffic
            const auto edgeS = [&](float l) {
                return e.ringC - std::sqrt(static_cast<double>(Ro * Ro - l * l)) - static_cast<double>(m(0.6f));
            };
            const double sb = edgeS(t + 0.5f * tw), apex = sb - static_cast<double>(m(0.45f));
            const auto   Y  = [&](double s, float l) { return al.surface_y(s, l) + h.markingLift; };
            const Vec3   q0 = cmesh::at(al, sb, t, Y(sb, t), ox, oz),
                         q1 = cmesh::at(al, sb, t + tw, Y(sb, t + tw), ox, oz);
            const Vec3   q2 = cmesh::at(al, apex, t + 0.5f * tw, Y(apex, t + 0.5f * tw), ox, oz);
            flat(b, q0, q1, q2, q2, m(1.0f));
        }
    }
    b.close_batch(mk, look.white, MAT_DEFAULT);
    for (size_t i = firstPaint; i < draws.size(); ++i) draws[i].retro = 1.0f;

    // Outer curb and a concrete barrier round everything but the entry: the car cannot leave the ring.
    mk = b.batch_mark();
    ring(Ro, Ro + m(0.15f), lift + m(0.15f), lift + m(0.15f), true);
    wall(Ro, lift - m(0.02f), lift + m(0.15f), -1.0f, true);
    const float r0 = Ro + m(E::kBarrierGap), r1 = r0 + m(0.6f), bh = h.barrierHeight;
    for (int k = 0; k < N; ++k) {
        const float a = th(k), c = th(k + 1);
        if (opening(a, r1) || opening(c, r1)) continue;
        cmesh::hexa(b,
                    {R(r0, a, -m(0.05f)), R(r0, c, -m(0.05f)), R(r1, a, -m(0.05f)), R(r1, c, -m(0.05f)),
                     R(r0 + m(0.2f), a, bh), R(r0 + m(0.2f), c, bh), R(r1 - m(0.2f), a, bh), R(r1 - m(0.2f), c, bh)});
    }
    // Splitter island from the barrier end to the ring, crash cushion on its downstream nose.
    {
        const float  w  = al.paint_inner(laneEnd) - m(0.3f);
        const double s0 = e.barrierEnd, s1 = e.ringC - static_cast<double>(Ro);
        const auto S = [&](double s, float l, float y) { return cmesh::at(al, s, l, al.surface_y(s, l) + y, ox, oz); };
        cmesh::hexa(b, {S(s0, -w, -m(0.02f)), S(s1, -w, -m(0.02f)), S(s0, w, -m(0.02f)), S(s1, w, -m(0.02f)),
                        S(s0, -w + m(0.05f), m(0.15f)), S(s1, -w + m(0.05f), m(0.15f)), S(s0, w - m(0.05f), m(0.15f)),
                        S(s1, w - m(0.05f), m(0.15f))});
    }
    b.close_batch(mk, look.concrete, MAT_CONCRETE);
    {
        const double s  = e.barrierEnd + static_cast<double>(m(1.75f));
        const auto   sm = al.sample(s);
        const Vec3   fwd(sm.tx, 0.0f, sm.tz), right(-sm.tz, 0.0f, sm.tx);
        mk = b.batch_mark();
        obox(b, cmesh::at(al, s, 0.0f, al.surface_y(s, 0.0f) + m(0.15f + 0.43f), ox, oz), right * m(0.3f),
             kUp * m(0.43f), fwd * m(1.75f));
        if (b.close_batch(mk, Vec4(0.95f, 0.72f, 0.02f, 1.0f), MAT_DEFAULT)) draws.back().retro = 1.0f;
    }

    // Level grass from where the mainline verge stops to past the last station.
    {
        const float  Wg = edge + h.grassExtent, dg = h.grassY - h.roadY;
        const double step = static_cast<double>(m(10.0f)),
                     sEnd = al.total_length() + static_cast<double>(m(E::kGrassPast));
        mk                = b.batch_mark();
        for (double s = e.mainEnd; s < sEnd - 1.0; s += step) {
            const double s1 = std::min(s + step, sEnd);
            const float  y0 = al.ground_y(s) + dg, y1 = al.ground_y(s1) + dg;
            flat(b, atx(al, s, -Wg, y0, ox, oz), atx(al, s, Wg, y0, ox, oz), atx(al, s1, -Wg, y1, ox, oz),
                 atx(al, s1, Wg, y1, ox, oz), look.grassTile);
        }
        b.close_batch(mk, look.grass, MAT_GRASS);
    }
}

}  // namespace swish
