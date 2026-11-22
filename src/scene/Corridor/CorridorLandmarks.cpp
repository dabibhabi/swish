#include "CorridorLandmarks.h"

#include "CorridorEdges.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace swish {

namespace {

namespace lm = road::landmarks;
using cmesh::face;
using util::units::kPi;
using util::units::kRadPerDeg;
using util::units::m;

const Vec3 kUp(0.0f, 1.0f, 0.0f);

// n-sided prism, base centre c, radius r, height h, with caps (degenerate-quad fans).
void drum(MeshBuilder& b, const Vec3& c, float r, float h, int n) {
    const Vec3 t = c + kUp * h;
    for (int k = 0; k < n; ++k) {
        const float a0 = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(n), a1 = a0 + 2.0f * kPi / n;
        const Vec3  e0(std::cos(a0) * r, 0.0f, std::sin(a0) * r), e1(std::cos(a1) * r, 0.0f, std::sin(a1) * r);
        face(b, c + e0, c + e1, t + e0, t + e1, glm::normalize(e0 + e1));
        face(b, t, t, t + e0, t + e1, kUp);
        face(b, c, c, c + e0, c + e1, -kUp);
    }
}

// Unisphere: ribbed globe (meridians, parallels), continent plates, three orbit rings, tripod. Steel batch.
void unisphere(MeshBuilder& b, const Vec3& g) {
    const float R = m(lm::kGlobeR), w = m(lm::kGlobeRib), t = lm::kGlobeTilt * kRadPerDeg;
    const Vec3  C = g + kUp * (m(lm::kGlobeTop) - R);
    const Vec3  ax(std::sin(t), std::cos(t), 0.0f), e1(std::cos(t), -std::sin(t), 0.0f), e2(0.0f, 0.0f, 1.0f);
    const auto  P = [&](float la, float lo, float r) {
        return C + (e1 * (std::cos(la) * std::cos(lo)) + e2 * (std::cos(la) * std::sin(lo)) + ax * std::sin(la)) * r;
    };
    const float d  = kRadPerDeg;
    uint32_t    mk = b.batch_mark();
    for (int k = 0; k < lm::kMeridians; ++k) {
        const float lo = 2.0f * kPi * static_cast<float>(k) / lm::kMeridians;
        for (int j = 0; j < 18; ++j)
            cmesh::rod(b, P((-90.0f + 10.0f * j) * d, lo, R), P((-80.0f + 10.0f * j) * d, lo, R), w);
    }
    for (int j = 0; j < lm::kParallels; ++j) {
        const float la = (-75.0f + 150.0f * static_cast<float>(j) / (lm::kParallels - 1)) * d;
        for (int k = 0; k < 36; ++k) cmesh::rod(b, P(la, 10.0f * k * d, R), P(la, 10.0f * (k + 1) * d, R), w);
    }
    // orbit rings (Gagarin, Glenn, Telstar), each in its own tilted plane
    for (int i = 0; i < 3; ++i) {
        const float a = (35.0f + 50.0f * i) * d, q = (20.0f + 120.0f * i) * d;
        const Vec3  u = glm::normalize(e1 * std::cos(q) + e2 * std::sin(q)),
                    v = glm::cross(ax, u) * std::cos(a) + ax * std::sin(a);
        const float r = lm::kRingR * R;
        for (int k = 0; k < 48; ++k) {
            const float b0 = 2.0f * kPi * k / 48.0f, b1 = 2.0f * kPi * (k + 1) / 48.0f;
            cmesh::rod(b, C + (u * std::cos(b0) + v * std::sin(b0)) * r, C + (u * std::cos(b1) + v * std::sin(b1)) * r,
                       0.6f * w);
        }
    }
    // tripod: three raked legs from the pool floor to the south cap, and a central column
    const Vec3 foot = P(-90.0f * d, 0.0f, R * 0.8f);
    for (int k = 0; k < 3; ++k) {
        const float a = 2.0f * kPi * k / 3.0f;
        cmesh::rod(b, g + Vec3(std::cos(a), 0.0f, std::sin(a)) * m(7.0f), foot, m(1.0f));
    }
    cmesh::pole(b, g, foot, m(1.2f));
    b.close_batch(mk, Vec4(0.78f, 0.79f, 0.8f, 1.0f), MAT_METAL);

    // continents: 10 deg plates just inside the ribs, both faces (seen through the lattice from behind)
    struct Box {
        int la0, la1, lo0, lo1;
    };
    static constexpr Box kLand[] = {{20, 70, -160, -60}, {-50, 10, -80, -40}, {40, 70, -10, 40},
                                    {-30, 30, -10, 50},  {10, 70, 40, 140},   {-40, -20, 120, 150}};
    mk                           = b.batch_mark();
    for (const Box& x : kLand)
        for (int la = x.la0; la < x.la1; la += 10)
            for (int lo = x.lo0; lo < x.lo1; lo += 10) {
                const float r  = 0.99f * R;
                const Vec3  p0 = P(la * d, lo * d, r), p1 = P(la * d, (lo + 10) * d, r);
                const Vec3  p2 = P((la + 10) * d, lo * d, r), p3 = P((la + 10) * d, (lo + 10) * d, r);
                const Vec3  n = glm::normalize(0.25f * (p0 + p1 + p2 + p3) - C);
                face(b, p0, p1, p2, p3, n);
                face(b, p0, p1, p2, p3, -n);
            }
    b.close_batch(mk, Vec4(0.7f, 0.71f, 0.72f, 1.0f), MAT_METAL);
}

// NYS Pavilion: three observation towers with deck drums, Tent of Tomorrow column ring and roof ring.
void pavilion(MeshBuilder& b, const Vec3& g, const Vec3& ax, const Vec3& cx, const CorridorLook& look) {
    uint32_t mk = b.batch_mark();
    for (int i = 0; i < 3; ++i) {
        const Vec3  c = g - ax * m(40.0f + 22.0f * i) + cx * m(i == 1 ? 14.0f : -6.0f);
        const float H = m(lm::kTowerH[i]);
        for (int k = 0; k < 4; ++k) {  // four slender legs round an elevator core
            const float a = 0.5f * kPi * k + 0.25f * kPi;
            cmesh::pole(b, c + Vec3(std::cos(a), 0.0f, std::sin(a)) * m(lm::kTowerR), c + kUp * H, m(0.45f));
        }
        cmesh::pole(b, c, c + kUp * H, m(1.2f));
        drum(b, c + kUp * (H - m(2.5f)), m(lm::kDeckR[i]), m(2.5f), 16);
        drum(b, c + kUp * (H - m(4.5f)), 0.6f * m(lm::kDeckR[i]), m(2.0f), 12);
    }
    const Vec3 tc = g + ax * m(25.0f);
    for (int k = 0; k < 16; ++k) {
        const float a = 2.0f * kPi * k / 16.0f;
        const Vec3  p = tc + ax * (std::cos(a) * m(lm::kTentA)) + cx * (std::sin(a) * m(lm::kTentB));
        cmesh::pole(b, p, p + kUp * m(lm::kTentH), m(0.9f));
    }
    b.close_batch(mk, look.concrete, MAT_CONCRETE);
    mk = b.batch_mark();  // roof ring, repainted yellow (2015, from memory)
    for (int k = 0; k < 48; ++k) {
        const float a0 = 2.0f * kPi * k / 48.0f, a1 = 2.0f * kPi * (k + 1) / 48.0f;
        const auto  E = [&](float a) {
            return tc + ax * (std::cos(a) * m(lm::kTentA)) + cx * (std::sin(a) * m(lm::kTentB)) + kUp * m(lm::kTentH);
        };
        cmesh::rod(b, E(a0), E(a1), m(1.4f));
    }
    b.close_batch(mk, Vec4(0.85f, 0.68f, 0.2f, 1.0f), MAT_METAL);
}

// Queens vent building: brick tower with an Art Deco setback and coping, on a Borden Ave street pad over the tube.
void vent_tower(MeshBuilder& b, const CorridorAlignment& al, double s, float lat, double ox, double oz,
                const CorridorLook& look) {
    namespace E              = road::ends;
    const CorridorHeader& h  = al.header();
    const double          P  = al.ends().portal;
    const float           Wg = al.outer_edge(al.sample(s)) + h.grassExtent;
    const double          a = s - m(lm::kPadPast), z = P - m(E::kCapLen);
    uint32_t              mk = b.batch_mark();  // street pad at the cap's level, from the cap back past the tower
    for (double q = a; q < z - 1.0; q += m(10.0f)) {
        const double q1 = std::min(q + m(10.0f), z);
        const float  y0 = al.surface_y(q, 0.0f) + m(E::kHeadH), y1 = al.surface_y(q1, 0.0f) + m(E::kHeadH);
        face(b, cmesh::at(al, q, -Wg, y0, ox, oz), cmesh::at(al, q, Wg, y0, ox, oz), cmesh::at(al, q1, -Wg, y1, ox, oz),
             cmesh::at(al, q1, Wg, y1, ox, oz), kUp, look.asphaltTile);
    }
    b.close_batch(mk, look.asphalt, MAT_ASPHALT);

    const auto sm = al.sample(s);
    const Vec3 ax(sm.tx, 0.0f, sm.tz), cx(-sm.tz, 0.0f, sm.tx);
    const Vec3 g   = cmesh::at(al, s, lat, al.surface_y(s, 0.0f) + m(E::kHeadH), ox, oz);
    const auto box = [&](float L, float W, float y0, float y1) {
        std::array<Vec3, 8> c;
        for (int k = 0; k < 8; ++k)
            c[k] = g + ax * ((k & 1 ? 0.5f : -0.5f) * L) + cx * ((k & 2 ? 0.5f : -0.5f) * W) + kUp * (k & 4 ? y1 : y0);
        return c;
    };
    const float L = m(lm::kVentL), W = m(lm::kVentW), H = m(lm::kVentH), st = m(lm::kVentStep), in = m(lm::kVentInset);
    mk = b.batch_mark();
    cmesh::hexa(b, box(L, W, -m(0.5f), H - st), m(road::edges::kBrickTile));
    cmesh::hexa(b, box(L - 2.0f * in, W - 2.0f * in, H - st, H), m(road::edges::kBrickTile));
    for (int k = -2; k <= 2; ++k)  // brick piers up the long faces
        for (const float sg : {-1.0f, 1.0f}) {
            std::array<Vec3, 8> c = box(m(1.2f), m(0.6f), -m(0.5f), H - st + m(1.5f));
            for (Vec3& p : c) p += ax * (0.2f * k * L) + cx * (sg * 0.5f * W);
            cmesh::hexa(b, c, m(road::edges::kBrickTile));
        }
    b.close_batch(mk, Vec4(0.95f, 0.62f, 0.42f, 1.0f), MAT_BRICK);  // orange brick
    mk = b.batch_mark();                                            // limestone coping on each stage
    cmesh::hexa(b, box(L + m(0.4f), W + m(0.4f), H - st - m(0.6f), H - st));
    cmesh::hexa(b, box(L - 2.0f * in + m(0.4f), W - 2.0f * in + m(0.4f), H - m(0.6f), H + m(0.1f)));
    b.close_batch(mk, Vec4(0.86f, 0.83f, 0.76f, 1.0f), MAT_CONCRETE);
}

}  // namespace

double landmark_s(const CorridorAlignment& al, const lm::Spot& sp) {
    const double a = exit_s(al, sp.exit), z = exit_s(al, sp.exit, true);
    return a < 0.0 ? -1.0 : 0.5 * (a + z) + m(sp.along);
}

void landmark_styles(std::vector<CorridorCrossroad>& cross) {
    for (CorridorCrossroad& c : cross)
        for (const lm::Style& x : lm::kStyles)
            if (c.exitNo == x.exit) c.style = static_cast<BridgeStyle>(x.style);
}

void emit_landmarks(MeshBuilder& b, const CorridorAlignment& al, double from, double to, double ox, double oz,
                    const CorridorLook& look) {
    if (!al.edges().landmarks || !al.has_ends()) return;
    const float dg = al.header().grassY - al.header().roadY;
    for (const lm::Spot& sp : lm::kSpots) {
        const double s = landmark_s(al, sp);
        if (s < from || s >= to) continue;
        const auto sm = al.sample(s);
        const Vec3 ax(sm.tx, 0.0f, sm.tz), cx = Vec3(-sm.tz, 0.0f, sm.tx) * (sp.lat < 0.0f ? -1.0f : 1.0f);
        const Vec3 g = cmesh::at(al, s, m(sp.lat), al.ground_y(s) + dg, ox, oz);
        switch (sp.kind) {
            case lm::Prop::Unisphere:
                unisphere(b, g);
                break;
            case lm::Prop::Pavilion:
                pavilion(b, g, ax, cx, look);
                break;
            case lm::Prop::VentTower:
                vent_tower(b, al, s, m(sp.lat), ox, oz, look);
                break;
        }
    }
}

}  // namespace swish
