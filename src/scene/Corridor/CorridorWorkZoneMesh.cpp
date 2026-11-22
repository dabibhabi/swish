#include "CorridorEdges.h"
#include "CorridorSigns.h"
#include "CorridorWorkZone.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/container/VectorOps.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace swish {

namespace {

namespace wz = road::workzone;
using util::container::filled;
using util::units::ft;
using util::units::kPi;
using util::units::m;

const Vec3 kUp(0.0f, 1.0f, 0.0f);
float      inch(float v) { return ft(v / 12.0f); }

// One draw per pass per cluster. Colours are linear tints over MAT_DEFAULT's white texel unless noted.
enum Pass : int { Body, Orange, White, Black, Steel, Concrete, Yellow, Face, Back, Lamp, Light, Dirt, kPasses };
struct PassLook {
    Vec4       col;
    MaterialId mat;
};
const PassLook kLook[kPasses] = {
    {{0.62f, 0.17f, 0.04f, 1.0f}, MAT_DEFAULT},     // weathered orange plastic, sun-faded
    {{0.92f, 0.24f, 0.03f, 1.0f}, MAT_DEFAULT},     // orange retroreflective sheeting
    {{0.86f, 0.86f, 0.83f, 1.0f}, MAT_DEFAULT},     // white sheeting
    {{0.035f, 0.035f, 0.035f, 1.0f}, MAT_DEFAULT},  // rubber bases, arrow-board face, wheels
    {{0.55f, 0.56f, 0.58f, 1.0f}, MAT_METAL},       // galvanised posts, frames
    {{0.62f, 0.60f, 0.56f, 1.0f}, MAT_CONCRETE},    // dirty precast barrier
    {{0.92f, 0.62f, 0.03f, 1.0f}, MAT_DEFAULT},     // sand barrels
    {{1.0f, 1.0f, 1.0f, 1.0f}, MAT_SIGN_KIT},       // sign faces (atlas)
    {{0.42f, 0.43f, 0.45f, 1.0f}, MAT_METAL},       // sign backs
    {{0.16f, 0.12f, 0.06f, 1.0f}, MAT_DEFAULT},     // arrow lamps (unlit lens; the flasher drives them)
    {{0.08f, 0.05f, 0.01f, 1.0f}, MAT_DEFAULT},     // Type A warning lenses
    {{0.42f, 0.38f, 0.33f, 1.0f}, MAT_DIRT},        // millings pile
};

// Quad whose front faces n; planar UVs (metres), tangent along p1 - p0.
void quad(MeshBuilder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, const Vec3& n) {
    if (glm::dot(glm::cross(p1 - p0, p2 - p0), n) < 0.0f) std::swap(p1, p2);
    const Vec3 t  = glm::length(p1 - p0) > 1e-3f ? glm::normalize(p1 - p0) : glm::normalize(p3 - p0);
    const Vec3 bt = glm::cross(n, t);
    const auto uv = [&](const Vec3& p) { return Vec2(glm::dot(p, t), glm::dot(p, bt)) / m(1.0f); };
    const Vec4 tan(t, 1.0f);
    b.push_quad({p0, n, uv(p0), tan}, {p1, n, uv(p1), tan}, {p2, n, uv(p2), tan}, {p3, n, uv(p3), tan},
                QuadWinding::Fan);
}

// Surface of revolution about axis a (unit) from c: radius r0 at h0 to r1 at h1, n sides; optional end discs.
void lathe(MeshBuilder& b, const Vec3& c, const Vec3& a, float h0, float r0, float h1, float r1, int n = 12,
           bool cap0 = false, bool cap1 = false) {
    const Vec3 u = glm::normalize(glm::cross(a, std::abs(a.y) > 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : kUp));
    const Vec3 v = glm::cross(a, u);
    for (int k = 0; k < n; ++k) {
        const float t0 = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(n);
        const float t1 = 2.0f * kPi * static_cast<float>(k + 1) / static_cast<float>(n);
        const Vec3  e0 = u * std::cos(t0) + v * std::sin(t0), e1 = u * std::cos(t1) + v * std::sin(t1);
        // smooth shading: each vertex keeps its own radial normal (round plastic, not facets)
        const auto nrm  = [&](const Vec3& e) { return glm::normalize(e * (h1 - h0) + a * (r0 - r1)); };
        Vec3       p[4] = {c + a * h0 + e0 * r0, c + a * h0 + e1 * r0, c + a * h1 + e0 * r1, c + a * h1 + e1 * r1};
        Vec3       q[4] = {nrm(e0), nrm(e1), nrm(e0), nrm(e1)};
        if (glm::dot(glm::cross(p[1] - p[0], p[2] - p[0]), q[0] + q[1]) < 0.0f)
            std::swap(p[1], p[2]), std::swap(q[1], q[2]);
        const Vec4 tan(glm::normalize(p[1] - p[0]), 1.0f);
        const auto V = [&](int i) {
            return Vertex{p[i], q[i], Vec2(static_cast<float>(k) / static_cast<float>(n), glm::dot(p[i] - c, a)), tan};
        };
        b.push_quad(V(0), V(1), V(2), V(3), QuadWinding::Fan);
        if (cap0) quad(b, c + a * h0, c + a * h0 + e0 * r0, c + a * h0 + e1 * r0, c + a * h0 + e1 * r0, -a);
        if (cap1) quad(b, c + a * h1, c + a * h1 + e0 * r1, c + a * h1 + e1 * r1, c + a * h1 + e1 * r1, a);
    }
}

// Item frame: local origin, facing f, viewer's right r (cross(up, f)).
struct Fr {
    Vec3 o, f, r;
};
Fr frame_of(const WzItem& it, double ox, double oz) {
    const Vec3 f = glm::normalize(Vec3(it.fx, 0.0f, it.fz));
    return {Vec3(static_cast<float>(it.x - ox), it.y, static_cast<float>(it.z - oz)), f, glm::cross(kUp, f)};
}

// 36 in drum: rubber base ring, orange body, four 6 in bands from the top (orange first), closed top.
void drum(MeshBuilder& b, int pass, const Vec3& c, const Vec3& a) {
    const float H = inch(wz::kDrumHIn), R = 0.5f * inch(wz::kDrumDIn) - m(0.005f), rb = R + m(0.07f);
    const auto  rad = [&](float h) { return R - m(0.04f) * h / H; };
    const float bd  = inch(wz::kBandIn);
    if (pass == Black) {
        lathe(b, c, a, 0.0f, rb, m(0.10f), rb, 12, true, false);
        lathe(b, c, a, m(0.10f), rb, m(0.10f), rad(m(0.10f)), 12);
        return;
    }
    // [h0, h1, pass] top down: 3 in body, O W O W bands, body
    const float top = H - inch(3.0f);
    const struct {
        float h0, h1;
        int   p;
    } segs[] = {{m(0.10f), top - 4.0f * bd, Body},
                {top - 4.0f * bd, top - 3.0f * bd, White},
                {top - 3.0f * bd, top - 2.0f * bd, Orange},
                {top - 2.0f * bd, top - bd, White},
                {top - bd, top, Orange},
                {top, H, Body}};
    for (const auto& s : segs)
        if (s.p == pass) {
            const float lift = s.p == Body ? 0.0f : m(0.003f);  // sheeting stands proud of the body
            lathe(b, c, a, s.h0, rad(s.h0) + lift, s.h1, rad(s.h1) + lift, 20, false, s.h1 >= H);
        }
}

// 28 in cone: black base, orange body, 6 in band 3.5 in under the top, 4 in band 2 in below it.
void cone(MeshBuilder& b, int pass, const Vec3& c, const Vec3& a, const Vec3& f, bool base) {
    const float H = inch(wz::kConeHIn), bh = m(0.03f);
    const auto  rad = [&](float h) { return m(0.15f) - m(0.125f) * (h - bh) / (H - bh); };
    if (pass == Black && base)
        cmesh::obox(b, c + a * (0.5f * bh), glm::cross(a, f) * (0.5f * inch(wz::kConeBaseIn)), a * (0.5f * bh),
                    f * (0.5f * inch(wz::kConeBaseIn)));
    const float w0 = H - inch(9.5f), w1 = H - inch(3.5f), v0 = H - inch(15.5f), v1 = H - inch(11.5f);
    const struct {
        float h0, h1;
        int   p;
    } segs[] = {{bh, v0, Body}, {v0, v1, White}, {v1, w0, Body}, {w0, w1, White}, {w1, H, Body}};
    for (const auto& s : segs)
        if (s.p == pass) lathe(b, c, a, s.h0, rad(s.h0), s.h1, rad(s.h1), 10);
}

// Type III barricade: A-frame legs, three 8 in rails striped orange / white at 45 degrees down to the centre.
void barricade(MeshBuilder& b, int pass, const Fr& F, float W, WzFace face) {
    const float H = wz::kBarricadeH, rh = inch(wz::kRailIn), th = m(0.015f);
    const float hw = 0.5f * W;
    if (pass == Steel)
        for (const float x : {-0.42f * W, 0.42f * W})
            for (const float s : {-1.0f, 1.0f})
                cmesh::rod(b, F.o + F.r * x + kUp * H, F.o + F.r * x + F.f * (s * m(0.38f)), m(0.02f));
    if (pass == Black)  // sandbags on the feet (MUTCD 6K.07 ballast)
        for (const float x : {-0.42f * W, 0.42f * W})
            cmesh::obox(b, F.o + F.r * x + kUp * m(0.06f), F.r * m(0.22f), kUp * m(0.06f), F.f * m(0.30f));
    for (int k = 0; k < 3; ++k) {
        const float yc = H - 0.5f * rh - static_cast<float>(k) * m(0.40f);
        const Vec3  c  = F.o + kUp * yc;
        if (pass == White) cmesh::obox(b, c, F.r * hw, kUp * (0.5f * rh), F.f * th);
        if (pass != Orange) continue;
        const float sw = inch(wz::kBandIn) * std::sqrt(2.0f);  // 6 in stripes at 45 degrees, measured across
        for (const float q : {-1.0f, 1.0f})
            for (int i = 0; 2 * i * sw < hw; ++i) {
                const float x0 = 2.0f * i * sw, x1 = x0 + sw;
                const auto  X = [&](float x) { return q * std::clamp(x, 0.0f, hw); };
                for (const float s : {1.0f, -1.0f}) {
                    const Vec3 n = F.f * s, off = n * (th + m(0.002f));
                    quad(b, c + off + F.r * X(x0) - kUp * (0.5f * rh), c + off + F.r * X(x1) - kUp * (0.5f * rh),
                         c + off + F.r * X(x0 + rh) + kUp * (0.5f * rh), c + off + F.r * X(x1 + rh) + kUp * (0.5f * rh),
                         n);
                }
            }
    }
    if (pass == Light)  // Type A lens on the left leg, 30 in+ up
        cmesh::obox(b, F.o + F.r * (-0.42f * W) + kUp * (H + m(0.12f)), F.r * m(0.09f), kUp * m(0.09f), F.f * m(0.04f));
    if (face == WzFace::None) return;
    const float fw = m(1.219f), fh = m(0.762f);
    const Vec3  fc = F.o + kUp * (H + m(0.08f) + 0.5f * fh) + F.f * m(0.03f);
    if (pass == Steel)
        for (const float x : {-0.3f * fw, 0.3f * fw})
            cmesh::rod(b, F.o + F.r * x + kUp * (H - rh), fc + F.r * x - F.f * m(0.03f), m(0.02f));
    if (pass == Face)
        sign_quad(b, fc + F.f * m(0.008f), F.r * (0.5f * fw), kUp * (0.5f * fh), F.f,
                  sign_atlas::kWorkZone[static_cast<int>(face)], sign_atlas::kKitW, sign_atlas::kKitH);
    if (pass == Back) cmesh::obox(b, fc, F.r * (0.5f * fw), kUp * (0.5f * fh), F.f * m(0.006f));
}

// Roadside sign on two posts, bottom at 7 ft; diamonds turned 45 degrees.
void sign(MeshBuilder& b, int pass, const Fr& F, WzFace face) {
    const bool  dia = face <= WzFace::LaneEnds;
    const float w = m(1.219f), h = face == WzFace::Speeding      ? m(2.134f)
                                   : face == WzFace::EndRoadWork ? m(0.610f)
                                   : dia                         ? w
                                                                 : m(0.762f);
    const float lift = ft(wz::kSignLiftFt), half = dia ? 0.7071f * w : 0.5f * h;
    const Vec3  c  = F.o + kUp * (lift + half);
    const Vec3  hx = F.r * (0.5f * w), hy = kUp * (0.5f * h);
    const Vec3  r = dia ? (hx + hy) * 0.7071f : hx, u = dia ? (hy - hx) * 0.7071f : hy;
    if (pass == Steel)
        for (const float x :
             {-0.3f, 0.3f}) {  // square perforated posts from 0.3 m below grade to just under the sign top
            const float top = lift + 1.8f * half, bot = -m(0.3f);
            cmesh::obox(b, F.o + F.r * (x * w) + kUp * (0.5f * (top + bot)) - F.f * m(0.04f), F.r * m(0.032f),
                        kUp * (0.5f * (top - bot)), F.f * m(0.032f));
        }
    if (pass == Face)
        sign_quad(b, c + F.f * m(0.008f), r, u, F.f, sign_atlas::kWorkZone[static_cast<int>(face)], sign_atlas::kKitW,
                  sign_atlas::kKitH);
    if (pass == Back) cmesh::obox(b, c, r, u, F.f * m(0.006f));
}

// Type C arrow board on a two-wheel trailer: 96 x 48 in black face, bottom 7 ft up, 15 amber lamps (arrow left).
void arrow(MeshBuilder& b, int pass, const Fr& F) {
    const float bw = 0.5f * inch(wz::kBoardWIn), bh = 0.5f * inch(wz::kBoardHIn);
    const Vec3  bc = F.o + kUp * (ft(wz::kBoardLiftFt) + bh);
    if (pass == Body) {  // trailer: chassis, tongue, solar-panel frame
        cmesh::obox(b, F.o + kUp * m(0.55f), F.r * m(0.75f), kUp * m(0.12f), F.f * m(1.1f));
        cmesh::obox(b, F.o + kUp * m(0.50f) + F.f * m(1.7f), F.r * m(0.06f), kUp * m(0.06f), F.f * m(0.6f));
    }
    if (pass == Steel) {
        const float y0 = m(0.67f), y1 = bc.y - F.o.y;  // chassis top to board centre
        cmesh::obox(b, F.o + kUp * (0.5f * (y0 + y1)) - F.f * m(0.2f), F.r * m(0.08f), kUp * (0.5f * (y1 - y0)),
                    F.f * m(0.08f));
        for (const float s : {-1.0f, 1.0f})  // stabiliser jacks
            cmesh::rod(b, F.o + F.r * (s * m(0.7f)) + kUp * m(0.5f) - F.f * m(0.9f),
                       F.o + F.r * (s * m(0.7f)) - F.f * m(0.9f), m(0.03f));
    }
    if (pass == Black) {
        cmesh::obox(b, bc - F.f * m(0.05f), F.r * bw, kUp * bh, F.f * m(0.06f));
        for (const float s : {-1.0f, 1.0f})
            cmesh::obox(b, F.o + F.r * (s * m(0.85f)) + kUp * m(0.33f), F.r * m(0.1f), kUp * m(0.33f), F.f * m(0.33f));
        const Vec3 pc = bc + kUp * (bh + m(0.45f)) - F.f * m(0.15f), tilt = glm::normalize(kUp + F.f * 0.6f);
        cmesh::obox(b, pc, F.r * m(0.75f), tilt * m(0.02f), glm::cross(F.r, tilt) * m(0.55f));
    }
    if (pass != Lamp) return;
    static constexpr float kL[15][2] = {{-1.0f, 0.0f},   {-0.75f, 0.0f},  {-0.5f, 0.0f},   {-0.25f, 0.0f},
                                        {0.0f, 0.0f},    {0.25f, 0.0f},   {0.5f, 0.0f},    {0.75f, 0.0f},
                                        {1.0f, 0.0f},    {-0.8f, 0.19f},  {-0.8f, -0.19f}, {-0.6f, 0.37f},
                                        {-0.6f, -0.37f}, {-0.42f, 0.52f}, {-0.42f, -0.52f}};
    const float            s         = m(0.075f);
    for (const auto& l : kL) {
        const Vec3 c = bc + F.r * m(l[0]) + kUp * m(l[1]) + F.f * m(0.015f);
        quad(b, c - F.r * s - kUp * s, c + F.r * s - kUp * s, c - F.r * s + kUp * s, c + F.r * s + kUp * s, F.f);
    }
}

// Portable concrete barrier segment (NJ shape) along the item's right axis; tapered ends drop to 6 in.
void wall(MeshBuilder& b, const WzItem& it, const Fr& F) {
    const auto        nj = nj_profile();
    std::vector<Vec2> pr(nj.begin(), nj.end());
    pr.front().y = pr.back().y = -m(0.05f);
    const float lo             = inch(wz::kTcbEndIn) / inch(wz::kTcbHIn);
    Vec3        ring[2][8];
    for (int e = 0; e < 2; ++e) {
        const float sgn = e ? 1.0f : -1.0f, k = (it.taper & (e ? 2 : 1)) ? lo : 1.0f;
        const Vec3  c = F.o + F.r * (sgn * 0.5f * it.len) + kUp * (sgn * 0.5f * it.dy);
        for (size_t i = 0; i < pr.size(); ++i)
            ring[e][i] = c + F.f * pr[i].x + kUp * (pr[i].y > 0.0f ? pr[i].y * k : pr[i].y);
    }
    for (size_t i = 0; i + 1 < pr.size(); ++i) {
        const Vec2 d = pr[i + 1] - pr[i];
        const Vec3 n = glm::normalize(F.f * d.y - kUp * d.x);
        quad(b, ring[0][i], ring[1][i], ring[0][i + 1], ring[1][i + 1], n);
    }
    for (int e = 0; e < 2; ++e)
        for (size_t k = 0, j = pr.size() - 1; k + 1 < j - 1; ++k, --j)
            quad(b, ring[e][k], ring[e][k + 1], ring[e][j], ring[e][j - 1], F.r * (e ? 1.0f : -1.0f));
}

void emit_item(MeshBuilder& b, int pass, const WzItem& it, double ox, double oz) {
    const Fr F = frame_of(it, ox, oz);
    switch (it.kind) {
        case WzKind::Drum:
            drum(b, pass, F.o, kUp);
            break;
        case WzKind::DrumDown: {  // on its side, rolled by spin
            const Vec3 a = F.f * std::cos(it.spin) + F.r * std::sin(it.spin);
            drum(b, pass, F.o + kUp * (0.5f * inch(wz::kDrumDIn) + m(0.05f)) - a * (0.5f * inch(wz::kDrumHIn)), a);
            break;
        }
        case WzKind::Cone:
            cone(b, pass, F.o, kUp, F.f, true);
            break;
        case WzKind::ConeDown: {
            const Vec3 a = F.f * std::cos(it.spin) + F.r * std::sin(it.spin);
            cone(b, pass, F.o + kUp * m(0.1f) - a * (0.5f * inch(wz::kConeHIn)), glm::normalize(a + kUp * 0.15f), F.f,
                 false);
            break;
        }
        case WzKind::Barricade:
            barricade(b, pass, F, it.len, it.face);
            break;
        case WzKind::Sign:
            sign(b, pass, F, it.face);
            break;
        case WzKind::Arrow:
            arrow(b, pass, F);
            break;
        case WzKind::Wall:
            if (pass == Concrete) wall(b, it, F);
            break;
        case WzKind::Barrel:
            if (pass == Yellow)
                lathe(b, F.o, kUp, 0.0f, 0.5f * wz::kBarrelD, m(wz::kBarrelH), 0.5f * wz::kBarrelD - m(0.03f));
            if (pass == Black)
                lathe(b, F.o, kUp, m(wz::kBarrelH), 0.5f * wz::kBarrelD - m(0.01f), m(wz::kBarrelH) + m(0.04f),
                      0.5f * wz::kBarrelD - m(0.05f), 12, false, true);
            break;
        case WzKind::Pile:
            if (pass == Dirt) {  // low frustum of millings
                const Vec3 a = F.f * std::cos(it.spin) + F.r * std::sin(it.spin), c = glm::cross(kUp, a);
                Vec3       lo4[4], hi4[4];
                for (int k = 0; k < 4; ++k) {
                    const float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
                    lo4[k] = F.o + a * (sx * m(2.2f)) + c * (sz * m(1.4f)) - kUp * m(0.05f);
                    hi4[k] = F.o + a * (sx * m(0.6f)) + c * (sz * m(0.3f)) + kUp * m(0.8f);
                }
                const int e[4][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}};
                for (const auto& k : e) {
                    const Vec3 mid = 0.25f * (lo4[k[0]] + lo4[k[1]] + hi4[k[0]] + hi4[k[1]]) - F.o;
                    quad(b, lo4[k[0]], lo4[k[1]], hi4[k[0]], hi4[k[1]], glm::normalize(Vec3(mid.x, m(0.6f), mid.z)));
                }
                quad(b, hi4[0], hi4[1], hi4[2], hi4[3], kUp);
            }
            break;
    }
}

}  // namespace

void emit_work_zone(MeshBuilder& b, std::vector<DrawCall>& draws, std::vector<SignalLens>& flash, const WorkZone& wz,
                    double ox, double oz, const CorridorLook&) {
    // 300 m clusters along the piece: per-draw culling keeps far drums and signs out of the frame
    std::map<long, std::vector<const WzItem*>> cl;
    for (const WzItem& it : wz.items) cl[static_cast<long>(std::floor(it.s / m(300.0)))].push_back(&it);
    for (const auto& [key, items] : cl)
        for (int p = 0; p < kPasses; ++p) {
            const uint32_t mk = b.batch_mark();
            for (const WzItem* it : items) emit_item(b, p, *it, ox, oz);
            if (!b.close_batch(mk, kLook[p].col, kLook[p].mat)) continue;
            DrawCall& d = draws.back();
            d.metalness = kLook[p].mat == MAT_METAL ? -1.0f : 0.0f;
            if (p == Orange || p == White) d.retro = 1.0f;  // retroreflective sheeting
            if (p == Body) d.roughnessMul = 1.4f;           // chalky UV-aged plastic
            if (p == Face) d.retro = 1.0f, d.roughnessMul = road::signs::kSheen;
            d.selfLit = p == Lamp || p == Light;  // active devices burn by day too
            if (p == Lamp || p == Light)
                flash.push_back({static_cast<uint32_t>(draws.size() - 1), static_cast<uint8_t>(p == Lamp ? 2 : 3), 1});
        }
}

void emit_work_zone_tail(MeshBuilder& b, std::vector<DrawCall>&, const CorridorAlignment& al, const WorkZone& wz,
                         double ox, double oz, const CorridorLook& look) {
    if (wz.tailLen <= 0.0f) return;
    const CorridorHeader& h  = al.header();
    const auto            sm = al.sample(wz.tailFrom);
    const double          L = wz.tailLen, step = m(100.0);
    std::vector<Vec3>     cl, tan;
    std::vector<float>    bank;
    const int             n = static_cast<int>(std::ceil(L / step));
    for (int i = 0; i <= n; ++i) {  // stations rising in s either way, so the bands face up
        const double e = wz.end ? L * i / n : -L + L * i / n;
        cl.emplace_back(static_cast<float>(sm.x + sm.tx * e - ox), sm.y, static_cast<float>(sm.z + sm.tz * e - oz));
        tan.emplace_back(sm.tx, 0.0f, sm.tz);
        bank.push_back(sm.bank);
    }
    const auto  f = build_sweep_frames(cl, tan, bank);
    const auto  k = f.size();
    const float o = al.outer_edge(sm), g = o + h.grassExtent, bw = 0.5f * h.barrierWidth;
    b.emit_sweep_band(f, filled(k, -o), filled(k, o), h.roadY, look.asphalt, MAT_ASPHALT, look.asphaltTile);
    b.emit_sweep_band(f, filled(k, o), filled(k, g), h.grassY, look.grass, MAT_GRASS, look.grassTile);
    b.emit_sweep_band(f, filled(k, -g), filled(k, -o), h.grassY, look.grass, MAT_GRASS, look.grassTile);
    b.emit_sweep_band(f, filled(k, -bw), filled(k, bw), h.roadY + h.barrierHeight, look.concrete, MAT_CONCRETE,
                      look.concreteTile);
    b.emit_sweep_wall(f, bw, h.roadY, h.barrierHeight, true, look.concrete, MAT_CONCRETE, look.concreteTile);
    b.emit_sweep_wall(f, -bw, h.roadY, h.barrierHeight, false, look.concrete, MAT_CONCRETE, look.concreteTile);
}

}  // namespace swish
