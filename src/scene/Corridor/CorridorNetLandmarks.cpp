#include "CorridorBridges.h"
#include "CorridorNetwork.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/light/Photometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace swish {

// N8 decision 9 landmarks (docs/endless-road.md N8, docs/queens-network-reference/landmarks.md). One segment per
// LMRK row, simple procedural boxes in the existing materials; sizes in road::netmarks. Debug only (the network).

namespace {

namespace nm = road::netmarks;
namespace nw = road::network;
using cmesh::face;
using cmesh::obox;
using nm::Kind;
using util::units::ft;
using util::units::kPi;
using util::units::m;

const Vec3 kUp(0.0f, 1.0f, 0.0f);
const Vec4 kStoneTint(1.0f, 0.95f, 0.88f, 1.0f);  // CorridorBridges kStone (granite ashlar tint)
const Vec4 kGlass(0.1f, 0.13f, 0.16f, 1.0f);

struct XZ {
    double x, z;
};

// Road point (s, lat), extended along the end tangent past the data (the Throgs Neck towers lie past the tail).
XZ world(const CorridorAlignment& al, double s, float lat) {
    const double c = std::clamp(s, 0.0, al.total_length()), d = s - c;
    const auto   sm = al.sample(c);
    return {sm.x + sm.tx * d - sm.tz * lat, sm.z + sm.tz * d + sm.tx * lat};
}

Kind kind_of(const NetworkLandmark& l) { return static_cast<Kind>(l.kind); }

// Footprint radius of a building landmark (0 = none): what must stay off other pavement.
float footprint(Kind k) {
    switch (k) {
        case Kind::Gas:
            return m(15.0f);
        case Kind::Citi:
            return m(nm::kCitiA + 10.0f);
        case Kind::Ashe:
            return m(nm::kAsheR + 10.0f);
        case Kind::Terminal:
            return m(0.5f * nm::kTermD + 10.0f);
        default:
            return 0.0f;
    }
}

// True when a disc of radius r at p is off every road's pavement and every ramp.
bool clear(const CorridorNetwork& net, const XZ& p, float r) {
    for (int k = 0; k < static_cast<int>(net.road_count()); ++k) {
        const CorridorAlignment& o = net.road(k);
        size_t                   h = 0;
        double                   s = 0.0;
        float                    t = 0.0f;
        o.project_global(p.x, p.z, h, s, t);
        if (s > 0.0 && s < o.total_length() && std::abs(t) - o.outer_edge(o.sample(s)) < r) return false;
    }
    for (const NetRamp& rp : net.ramps()) {
        if (p.x < rp.minX - r || p.x > rp.maxX + r || p.z < rp.minZ - r || p.z > rp.maxZ + r) continue;
        for (const NetRampPoint& q : rp.pts)
            if (std::hypot(q.x - p.x, q.z - p.z) - static_cast<double>(rp.halfWidth) < r) return false;
    }
    return true;
}

// The junction on `road` nearest station s, or nullptr.
const NetworkJunction* nearest_junction(const CorridorNetwork& net, int road, double s) {
    const NetworkJunction* best = nullptr;
    double                 d    = 1e300;
    for (const NetworkJunction& j : net.asset().junctions) {
        const double sj = j.roadA == road ? j.sA : j.roadB == road ? j.sB : 1e300;
        if (std::abs(sj - s) < d) d = std::abs(sj - s), best = &j;
    }
    return best;
}

struct Ctx {
    const CorridorNetwork&   net;
    const CorridorAlignment& al;
    const NetworkLandmark&   lm;
    float                    lat;  // landmark_lat
    const CorridorLook&      look;
    const StructureParams&   sp;
    CorridorSegment&         seg;
    MeshBuilder&             b;

    Vec3 P(double s, float lat, float y) const {
        const XZ w = world(al, s, lat);
        return Vec3(static_cast<float>(w.x - seg.originX), y, static_cast<float>(w.z - seg.originZ));
    }
    // Ground under a segment-local point: the N7 field, else the baked DEM.
    float G(const Vec3& p) const {
        if (!net.ground().valid()) return lm.y;
        const float h = net.ground().height(seg.originX + p.x, seg.originZ + p.z);
        return std::isfinite(h) ? h : lm.y;
    }
    // Everything `emit` pushes as one draw (false when empty); painted steel has no metal response.
    template <class F>
    bool run(const Vec4& col, MaterialId mat, F&& emit) const {
        const uint32_t mk = b.batch_mark();
        emit();
        if (!b.close_batch(mk, col, mat)) return false;
        if (mat == MAT_METAL) seg.draws.back().metalness = 0.0f;
        return true;
    }
    // run, glowing at `nits` (with the street lamps; self = always).
    template <class F>
    void glow(float nits, bool self, const Vec4& col, MaterialId mat, F&& emit) const {
        if (!run(col, mat, emit)) return;
        seg.draws.back().emissiveNits = nits;
        seg.draws.back().selfLit      = self;
    }
};

// Elliptic ring wall: semi-axes a (along T), c (along R) at r0 -> r1 (fractions) and heights y0 -> y1 over C.
void ring(MeshBuilder& b, const Vec3& C, const Vec3& T, const Vec3& R, float a, float c, float r0, float r1, float y0,
          float y1, int n, float tile, bool inward = false) {
    for (int k = 0; k < n; ++k) {
        const float a0 = 2.0f * kPi * k / n, a1 = 2.0f * kPi * (k + 1) / n;
        const Vec3  d0 = T * (std::cos(a0) * a) + R * (std::sin(a0) * c),
                    d1 = T * (std::cos(a1) * a) + R * (std::sin(a1) * c);
        const Vec3  e = glm::normalize(d0 + d1), up = kUp * (r0 - r1);  // tilt with the rake
        face(b, C + d0 * r0 + kUp * y0, C + d1 * r0 + kUp * y0, C + d0 * r1 + kUp * y1, C + d1 * r1 + kUp * y1,
             glm::normalize((inward ? -e : e) * std::max(0.1f, std::abs(y1 - y0)) + up * (a + c) * 0.5f), tile);
    }
}

// #1 GCP median service area: forecourt, canopy on columns over four pump islands, stone cottage kiosk with a
// hipped slate roof, lit LED price sign upstream; two canopy spots join the street lamps (lit at dusk).
void gas(const Ctx& c) {
    const double s  = c.lm.s[0];
    const auto   sm = c.al.sample(s);
    const float  sd = nm::kGasSide, E = c.al.outer_edge(sm), lat = c.lat;
    const Vec3   T(sm.tx, 0.0f, sm.tz), R = Vec3(-sm.tz, 0.0f, sm.tx) * sd;  // R: outward
    const Vec3   C0 = c.P(s, lat, 0.0f);
    const float  y = c.G(C0) + m(0.05f), L = 0.5f * m(nm::kCanopyL), W = 0.5f * m(nm::kCanopyW);
    const float  H = m(nm::kCanopyH), F = m(nm::kFascia);
    const Vec3   C = Vec3(C0.x, y, C0.z);
    c.run(c.look.asphalt, MAT_ASPHALT, [&] {  // forecourt from the shoulder edge out past the kiosk, on the ground
        const float far = E + m(nm::kGasOut + nm::kKioskBack + 8.0f);
        for (double q = s - m(55.0f); q < s + m(55.0f); q += m(10.0f)) {
            const Vec3 n0 = c.P(q, sd * (E + m(0.3f)), c.al.surface_y(q, sd * E) + m(0.03f)),
                       n1 = c.P(q + m(10.0f), sd * (E + m(0.3f)), c.al.surface_y(q + m(10.0f), sd * E) + m(0.03f));
            Vec3       f0 = c.P(q, sd * far, 0.0f), f1 = c.P(q + m(10.0f), sd * far, 0.0f);
            f0.y = std::max(c.G(f0), y - m(0.6f)) + m(0.1f), f1.y = std::max(c.G(f1), y - m(0.6f)) + m(0.1f);
            face(c.b, n0, n1, f0, f1, kUp, c.look.asphaltTile);
        }
    });
    const float isl[4] = {-0.75f * L, -0.25f * L, 0.25f * L, 0.75f * L};
    c.run(c.look.concrete, MAT_CONCRETE, [&] {  // pad and island curbs
        obox(c.b, C + kUp * m(0.06f), T * (L + m(3.0f)), kUp * m(0.06f), R * (W + m(3.0f)));
        for (const float a : isl) obox(c.b, C + T * a + kUp * m(0.2f), T * m(0.6f), kUp * m(0.15f), R * m(2.6f));
    });
    c.run(Vec4(0.93f, 0.93f, 0.91f, 1.0f), MAT_METAL, [&] {  // canopy deck, columns, pumps
        obox(c.b, C + kUp * (H + 0.5f * F), T * L, kUp * (0.5f * F), R * W);
        for (const float a : isl) {
            cmesh::pole(c.b, C + T * a, C + T * a + kUp * H, m(0.2f));
            for (const float o : {-1.4f, 1.4f})
                obox(c.b, C + T * a + R * m(o) + kUp * m(1.25f), T * m(0.35f), kUp * m(0.9f), R * m(0.5f));
        }
    });
    c.run(Vec4(0.05f, 0.33f, 0.3f, 1.0f), MAT_METAL, [&] {  // generic brand fascia band (no trademark)
        obox(c.b, C + kUp * (H + 0.55f * F), T * (L + m(0.03f)), kUp * (0.4f * F), R * (W + m(0.03f)));
        for (const float a : isl)  // pump toppers
            for (const float o : {-1.4f, 1.4f})
                obox(c.b, C + T * a + R * m(o) + kUp * m(2.25f), T * m(0.37f), kUp * m(0.12f), R * m(0.52f));
    });
    c.glow(nm::kCanopyNits, false, Vec4(1.0f, 0.97f, 0.9f, 1.0f), MAT_DEFAULT,
           [&] {  // recessed canopy luminaires
               for (const float a : isl)
                   for (const float o : {-0.5f, 0.5f})
                       obox(c.b, C + T * a + R * (o * W) + kUp * (H - m(0.01f)), T * m(0.6f), kUp * m(0.01f),
                            R * m(0.3f));
           });
    // kiosk: rough ashlar walls, hipped slate roof, lit shop window toward the road
    const Vec3  K  = C + R * m(nm::kKioskBack);
    const float kL = 0.5f * m(nm::kKioskL), kW = 0.5f * m(nm::kKioskW), kH = m(nm::kKioskH), e = m(0.4f);
    c.run(Vec4(0.72f, 0.64f, 0.55f, 1.0f), MAT_STONE,  // darker field stone (photo)
          [&] { obox(c.b, K + kUp * (0.5f * kH), T * kL, kUp * (0.5f * kH), R * kW); });
    c.run(Vec4(0.33f, 0.34f, 0.37f, 1.0f), MAT_DEFAULT, [&] {
        const Vec3  base = K + kUp * kH, a = T * (kL + e), w = R * (kW + e), rg = T * (kL - kW);
        const Vec3  top  = base + kUp * m(nm::kKioskRoof);
        const float rise = m(nm::kKioskRoof), run = kW + e;
        for (const float sg : {-1.0f, 1.0f}) {
            face(c.b, base - a + w * sg, base + a + w * sg, top - rg, top + rg,
                 glm::normalize(R * (sg * rise) + kUp * run));
            face(c.b, base + a * sg - w, base + a * sg + w, top + rg * sg, top + rg * sg,
                 glm::normalize(T * (sg * rise) + kUp * run));
        }
    });
    c.glow(250.0f, false, kGlass, MAT_METAL,
           [&] { obox(c.b, K - R * (kW + m(0.02f)) + kUp * m(1.4f), T * m(2.5f), kUp * m(0.9f), R * m(0.03f)); });
    // price sign upstream at the shoulder: pole, dark board, three rows of LED digits each face (self-lit)
    const Vec3 Pb = c.P(s - m(nm::kPriceUp), sd * (E + m(4.0f)), 0.0f);
    const Vec3 pb(Pb.x, c.G(Pb), Pb.z), pc = pb + kUp * (m(nm::kPriceH) - 0.5f * m(nm::kPriceTall));
    c.run(Vec4(0.55f, 0.56f, 0.58f, 1.0f), MAT_METAL, [&] { cmesh::pole(c.b, pb, pc, m(0.15f)); });
    c.run(Vec4(0.06f, 0.07f, 0.08f, 1.0f), MAT_METAL,
          [&] { obox(c.b, pc, T * m(0.15f), kUp * (0.5f * m(nm::kPriceTall)), R * (0.5f * m(nm::kPriceW))); });
    c.run(Vec4(0.05f, 0.33f, 0.3f, 1.0f), MAT_METAL, [&] {  // brand header
        obox(c.b, pc + kUp * (0.5f * m(nm::kPriceTall) - m(0.35f)), T * m(0.17f), kUp * m(0.33f),
             R * (0.5f * m(nm::kPriceW) + m(0.02f)));
    });
    c.glow(c.sp.signNits, true, Vec4(1.0f, 0.12f, 0.04f, 1.0f), MAT_DEFAULT,
           [&] {  // three price rows of four LED digits
               for (int r = 0; r < 3; ++r)
                   for (int d = 0; d < 4; ++d)
                       obox(c.b, pc + kUp * m(0.45f - 0.75f * r) + R * (m(0.42f) * (static_cast<float>(d) - 1.5f)),
                            T * m(0.17f), kUp * m(0.24f), R * m(0.16f));
           });
    const Vec3  cool = util::light::kelvin_to_rgb(4000.0f);
    const float half = 70.0f * util::units::kRadPerDeg;
    for (const float a : {-0.5f * L, 0.5f * L})
        c.seg.lamps.push_back({C + T * a + kUp * (H - m(0.1f)), -kUp, cool,
                               util::light::cone_candela_from_lumens(nm::kCanopyLumens, half), m(25.0f),
                               std::cos(55.0f * util::units::kRadPerDeg), std::cos(75.0f * util::units::kRadPerDeg)});
}

// #2 the GCP over the CIP: D4 stone segmental arches (one per carriageway) on the GCP's two faces, crown under its
// girders, ashlar piers and wing walls; the GCP's own steel shows through the arch, as in the photo.
void arch(const Ctx& c, int road) {
    const NetworkJunction* J = nearest_junction(c.net, road, c.lm.s[0]);
    if (!J) return;
    const bool               a  = J->roadA == road;
    const double             sc = a ? J->sA : J->sB, so = a ? J->sB : J->sA;
    const CorridorAlignment& up = c.net.road(a ? J->roadB : J->roadA);
    const float              yc = c.al.surface_y(sc, 0.0f), yg = up.surface_y(so, 0.0f);
    if (yg - yc < m(nw::kClear)) return;  // not over this road here
    const auto  sm = c.al.sample(sc), gm = up.sample(so);
    const Vec3  T(sm.tx, 0.0f, sm.tz), R(-sm.tz, 0.0f, sm.tx), G(gm.tx, 0.0f, gm.tz);
    const float sn = std::max(0.5f, std::abs(glm::dot(G, R)));
    Vec3        ea = glm::normalize(glm::cross(kUp, G));
    if (glm::dot(ea, T) < 0.0f) ea = -ea;
    const float A  = up.outer_edge(gm) + m(nw::kParapetW + 0.6f);  // just outside the GCP's parapets
    const float po = (c.al.outer_edge(sm) + m(1.0f)) / sn, wing = m(nm::kArchWing);
    const float rise = arch_rise(po);
    const float spring =
        std::max(yc + ft(nm::kHaunchFt), yg - m(nw::kRoadDeck + nw::kGirderD + 0.2f) - rise - m(nm::kArchRing));
    const DeckFrame F{c.P(sc, 0.0f, 0.0f), ea, G};
    emit_arch(c.b, c.seg.draws, F, A, {-po, 0.0f, po}, spring, yg, -po - wing, po + wing, c.look);
    c.run(kStoneTint, MAT_STONE, [&] {
        for (const float t : {-po, 0.0f, po}) {
            const float y0 = std::min(yc, c.G(F.P(0.0f, t, 0.0f))) - m(1.0f);
            obox(c.b, F.P(0.0f, t, 0.5f * (y0 + spring)), ea * A, kUp * (0.5f * (spring - y0)),
                 G * m(t == 0.0f ? 0.5f : 0.8f));
        }
        for (const float sg : {-1.0f, 1.0f})  // wing walls past the outer piers, both faces
            for (const float sa : {-1.0f, 1.0f}) {
                const float t0 = sg * po, t1 = sg * (po + wing);
                const float y0 = std::min(yc, c.G(F.P(sa * A, t1, 0.0f))) - m(1.0f);
                face(c.b, F.P(sa * A, t0, y0), F.P(sa * A, t1, y0), F.P(sa * A, t0, yg), F.P(sa * A, t1, yg), ea * sa,
                     m(road::bridges::kStoneTile));
            }
    });
}

// The OSM crossing on the landmark's road nearest its station within kSnap, or nullptr.
const CorridorCrossing* snap_crossing(const Ctx& c) {
    const CorridorCrossing* best = nullptr;
    double                  d    = m(nm::kSnap);
    for (const CorridorCrossing& x : c.al.crossings())
        if (std::abs(x.station - c.lm.s[0]) < d) d = std::abs(x.station - c.lm.s[0]), best = &x;
    return best;
}

// #3 Astoria Blvd station: along 31st St over its street bridge (emit_overpass draws that), dark green steel bents
// past the road edges, platform with a red-brown gable canopy, mezzanine under it.
void astoria(const Ctx& c) {
    const CorridorCrossing* x = snap_crossing(c);
    if (!x) return;
    const double s  = x->station;
    const auto   sm = c.al.sample(s);
    const Vec3   T(sm.tx, 0.0f, sm.tz), R(-sm.tz, 0.0f, sm.tx);
    const float  sk = std::clamp(x->skew, -0.7f, 0.7f);
    const Vec3   A = glm::normalize(R * std::cos(sk) + T * std::sin(sk)), Wd = glm::normalize(glm::cross(kUp, A));
    const float  road = c.al.surface_y(s, 0.0f), gnd = c.al.terrain_y(s);
    // CorridorViaduct emit_overpass: the street deck's top
    const float top  = std::max(gnd + m(c.sp.wallAbove) + m(0.3f), road + ft(16.0f) + m(1.4f));
    const float plat = top + m(nm::kStaRise), half = 0.5f * m(nm::kStaLen), hw = 0.5f * m(nm::kStaW);
    const float edge = (c.al.outer_edge(sm) + m(3.0f)) / std::cos(sk);
    const Vec3  O    = c.P(s, 0.0f, 0.0f);
    c.run(Vec4(0.17f, 0.27f, 0.22f, 1.0f), MAT_METAL, [&] {  // track girders, bents, canopy posts
        for (const float w : {-0.3f * hw, 0.3f * hw})
            obox(c.b, O + A * 0.0f + Wd * w + kUp * (plat - m(1.1f)), A * half, kUp * m(0.8f), Wd * m(0.35f));
        for (float a = edge; a <= half; a += m(nm::kStaCol))
            for (const float sg : {-1.0f, 1.0f}) {
                const Vec3 p = O + A * (sg * a);
                for (const float w : {-hw + m(1.0f), hw - m(1.0f)}) {
                    const Vec3 f = p + Wd * w;
                    cmesh::rod(c.b, Vec3(f.x, c.G(f) - m(0.5f), f.z), Vec3(f.x, plat - m(1.9f), f.z), m(0.4f));
                }
                obox(c.b, p + kUp * (plat - m(2.2f)), A * m(0.4f), kUp * m(0.4f), Wd * hw);  // bent cap
            }
        for (float a = -half + m(4.0f); a < half; a += m(8.0f))
            for (const float w : {-hw + m(0.6f), hw - m(0.6f)})
                cmesh::rod(c.b, O + A * a + Wd * w + kUp * plat, O + A * a + Wd * w + kUp * (plat + m(nm::kStaRoof)),
                           m(0.12f));
    });
    c.run(c.look.concrete, MAT_CONCRETE, [&] {  // platform slab, windscreens, mezzanine walls
        obox(c.b, O + kUp * (plat - m(0.15f)), A * half, kUp * m(0.25f), Wd * hw);
        for (const float sg : {-1.0f, 1.0f})
            obox(c.b, O + Wd * (sg * (hw - m(0.1f))) + kUp * (plat + m(0.6f)), A * half, kUp * m(0.6f), Wd * m(0.08f));
        obox(c.b, O + kUp * (top + m(1.0f) + 0.5f * m(nm::kMezzH)), A * (0.5f * m(nm::kMezzLen)),
             kUp * (0.5f * m(nm::kMezzH)), Wd * (hw - m(1.5f)));
    });
    c.run(Vec4(0.47f, 0.18f, 0.13f, 1.0f), MAT_METAL, [&] {  // red-brown canopy roof and mezzanine eave
        const float y0 = plat + m(nm::kStaRoof), y1 = y0 + m(1.0f);
        for (const float sg : {-1.0f, 1.0f})
            for (const float fc : {-1.0f, 1.0f})  // both faces: it is seen from the road below
                face(c.b, O - A * half + Wd * (sg * (hw + m(0.4f))) + kUp * y0,
                     O + A * half + Wd * (sg * (hw + m(0.4f))) + kUp * y0, O - A * half + kUp * y1,
                     O + A * half + kUp * y1, glm::normalize(Wd * sg + kUp * 7.0f) * fc);
        obox(c.b, O + kUp * (top + m(1.15f) + m(nm::kMezzH)), A * (0.5f * m(nm::kMezzLen) + m(0.5f)), kUp * m(0.15f),
             Wd * (hw - m(1.0f)));
    });
    c.glow(200.0f, false, kGlass, MAT_METAL,
           [&] {  // mezzanine windows, warm at night
               for (const float sg : {-1.0f, 1.0f})
                   obox(c.b, O + Wd * (sg * (hw - m(1.47f))) + kUp * (top + m(1.0f) + 0.55f * m(nm::kMezzH)),
                        A * (0.45f * m(nm::kMezzLen)), kUp * (0.25f * m(nm::kMezzH)), Wd * m(0.05f));
           });
}

// #4 Jamaica Yard bridge: two riveted plate-girder spans (12 ft 7 in posted) on a granite centre pier and
// abutments, stiffener panels with rust streaks, cream railing; it replaces the yard-lead crossings it covers.
void jamaica(const Ctx& c) {
    double lo = c.lm.s[0], hi = c.lm.s[0], sk0 = 0.0, best = 1e300;
    for (const CorridorCrossing& x : c.al.crossings())
        if (std::abs(x.station - c.lm.s[0]) < m(nm::kYardReplace)) {
            lo = std::min(lo, x.station - 0.5 * x.width), hi = std::max(hi, x.station + 0.5 * x.width);
            if (std::abs(x.station - c.lm.s[0]) < best) best = std::abs(x.station - c.lm.s[0]), sk0 = x.skew;
        }
    const double s  = 0.5 * (lo + hi);
    const auto   sm = c.al.sample(s);
    const Vec3   T(sm.tx, 0.0f, sm.tz), R(-sm.tz, 0.0f, sm.tx);
    const float  sk = std::clamp(static_cast<float>(sk0), -0.7f, 0.7f);
    const Vec3   A = glm::normalize(R * std::cos(sk) + T * std::sin(sk)), Wd = glm::normalize(glm::cross(kUp, A));
    const float  hw   = std::max(0.5f * static_cast<float>(hi - lo), m(8.0f));
    const float  road = c.al.surface_y(s, 0.0f), soff = road + ft(nm::kYardClearFt), Gd = m(nm::kYardGirder);
    const float  top = soff + Gd, L = (c.al.outer_edge(sm) + m(2.5f)) / std::cos(sk);
    const float  pier = std::max(m(0.3f), c.al.inner_edge(sm.lanes) - m(0.15f));
    const Vec3   O    = c.P(s, 0.0f, 0.0f);
    const float  base = std::min(road, c.al.terrain_y(s)) - m(1.0f);
    c.run(kStoneTint, MAT_STONE, [&] {
        obox(c.b, O + kUp * (0.5f * (base + soff)), A * pier, kUp * (0.5f * (soff - base)), Wd * (hw + m(0.6f)));
        for (const float sg : {-1.0f, 1.0f}) {
            const float ab = m(nm::kYardAbut), y0 = std::min(base, c.G(O + A * (sg * (L + ab)))) - m(0.5f);
            obox(c.b, O + A * (sg * (L + 0.5f * ab)) + kUp * (0.5f * (y0 + top + m(0.4f))), A * (0.5f * ab),
                 kUp * (0.5f * (top + m(0.4f) - y0)), Wd * (hw + m(1.5f)));
        }
    });
    c.run(c.look.concrete, MAT_CONCRETE, [&] {  // deck slab, soffit faces down
        obox(c.b, O + kUp * (top - m(0.25f)), A * L, kUp * m(0.25f), Wd * hw);
    });
    const auto panel = [&](int k, int side) { return (k * 7 + side * 3) % 10 < 3; };  // rust-streaked panels
    c.run(Vec4(0.34f, 0.45f, 0.36f, 1.0f), MAT_METAL, [&] {
        for (const float sg : {-1.0f, 1.0f}) {
            const Vec3 f = O + Wd * (sg * hw);
            obox(c.b, f + kUp * (soff + 0.5f * Gd), A * L, kUp * (0.5f * Gd), Wd * m(0.03f));  // web
            for (const float y : {soff, top})                                                  // flanges
                obox(c.b, f + kUp * y, A * L, kUp * m(0.03f), Wd * m(0.25f));
            int k = 0;
            for (float a = -L; a <= L; a += m(nm::kStiffEvery), ++k)  // stiffeners
                if (!panel(k, sg > 0.0f))
                    obox(c.b, f + Wd * (sg * m(0.06f)) + A * a + kUp * (soff + 0.5f * Gd), A * m(0.08f),
                         kUp * (0.48f * Gd), Wd * m(0.05f));
        }
        for (float w = -hw + m(2.5f); w < hw - m(1.0f); w += m(2.5f))  // interior girders
            obox(c.b, O + Wd * w + kUp * (soff + 0.5f * Gd), A * L, kUp * (0.5f * Gd), Wd * m(0.03f));
    });
    c.run(Vec4(0.44f, 0.31f, 0.22f, 1.0f), MAT_METAL, [&] {
        for (const float sg : {-1.0f, 1.0f}) {
            int k = 0;
            for (float a = -L; a + m(nm::kStiffEvery) <= L; a += m(nm::kStiffEvery), ++k)
                if (panel(k, sg > 0.0f))
                    obox(c.b,
                         O + Wd * (sg * (hw + m(0.04f))) + A * (a + 0.5f * m(nm::kStiffEvery)) +
                             kUp * (soff + 0.6f * Gd),
                         A * (0.45f * m(nm::kStiffEvery)), kUp * (0.35f * Gd), Wd * m(0.01f));
        }
    });
    c.run(Vec4(0.95f, 0.78f, 0.1f, 1.0f), MAT_DEFAULT, [&] {  // yellow clearance plates, one per span and face
        for (const float sg : {-1.0f, 1.0f})
            for (const float sa : {-0.5f, 0.5f})
                obox(c.b, O + Wd * (sg * (hw + m(0.06f))) + A * (sa * L) + kUp * (soff + 0.5f * Gd), A * m(0.45f),
                     kUp * m(0.25f), Wd * m(0.01f));
    });
    c.run(Vec4(0.82f, 0.77f, 0.62f, 1.0f), MAT_METAL, [&] {  // walkway railing on top
        for (const float sg : {-1.0f, 1.0f}) {
            const Vec3 f = O + Wd * (sg * (hw - m(0.1f)));
            for (const float y : {0.55f, 1.1f}) obox(c.b, f + kUp * (top + m(y)), A * L, kUp * m(0.04f), Wd * m(0.04f));
            for (float a = -L; a <= L; a += m(2.0f))
                obox(c.b, f + A * a + kUp * (top + m(0.55f)), A * m(0.04f), kUp * m(0.55f), Wd * m(0.04f));
        }
    });
}

// #5 LaGuardia perimeter: white concrete wall, chain-link (posts, rails, wire courses) kFenceOut past the edge, off
// any ramp; airport side = the side of the first point.
void fence(const Ctx& c) {
    const double                     s0 = std::min(c.lm.s[0], c.lm.s[1]), s1 = std::max(c.lm.s[0], c.lm.s[1]);
    const float                      sg = c.lm.lat[0] < 0.0f ? -1.0f : 1.0f, step = m(nm::kFencePost);
    std::vector<std::array<Vec3, 2>> runs;  // (ground point, its outward normal) per post
    for (double q = s0; q <= s1; q += step) {
        const float lat = sg * (c.al.outer_edge(c.al.sample(q)) + m(nm::kFenceOut));
        const XZ    w   = world(c.al, q, lat);
        Vec3        p   = c.P(q, lat, 0.0f);
        p.y             = c.G(p);
        const bool on   = c.net.ground().clearance(w.x, w.z, c.lm.road) > m(3.0f);
        runs.push_back({p, on ? Vec3(1.0f) : Vec3(0.0f)});
    }
    const auto each = [&](auto f) {
        for (size_t i = 0; i + 1 < runs.size(); ++i)
            if (runs[i][1].x > 0.0f && runs[i + 1][1].x > 0.0f) f(runs[i][0], runs[i + 1][0], i);
    };
    const float wall = m(nm::kFenceWall), H = m(nm::kFenceH);
    c.run(Vec4(0.86f, 0.86f, 0.83f, 1.0f), MAT_CONCRETE, [&] {
        each([&](const Vec3& a, const Vec3& b, size_t) {
            const Vec3 n = glm::normalize(glm::cross(b - a, kUp)) * -sg;
            const Vec3 o = n * m(0.2f);
            for (const float k : {-1.0f, 1.0f})
                face(c.b, a + o * k - kUp * m(0.4f), b + o * k - kUp * m(0.4f), a + o * k + kUp * wall,
                     b + o * k + kUp * wall, n * k, c.look.concreteTile);
            face(c.b, a - o + kUp * wall, b - o + kUp * wall, a + o + kUp * wall, b + o + kUp * wall, kUp);
        });
    });
    c.run(Vec4(0.6f, 0.62f, 0.63f, 1.0f), MAT_METAL, [&] {
        each([&](const Vec3& a, const Vec3& b, size_t) {
            cmesh::rod(c.b, a + kUp * wall, a + kUp * (wall + H + m(0.4f)), m(0.04f));
            for (float y = wall + m(0.6f); y <= wall + H + 0.01f; y += m(0.6f))
                cmesh::rod(c.b, a + kUp * y, b + kUp * y, y > wall + H - m(0.05f) ? m(0.03f) : m(0.015f));
        });
    });
}

// #5 runway (OSM end to end): asphalt strip on the ground, white centreline dashes, edge lights, green thresholds.
void runway(const Ctx& c) {
    const Vec3  a = c.P(c.lm.s[0], c.lm.lat[0], 0.0f), z = c.P(c.lm.s[1], c.lm.lat[1], 0.0f);
    const Vec3  d = Vec3(z.x - a.x, 0.0f, z.z - a.z);
    const float L = glm::length(d), hw = 0.5f * m(nm::kRunwayW);
    if (L < m(100.0f)) return;
    const Vec3 u = d / L, v = glm::cross(kUp, u);
    const auto at = [&](float t, float w, float dy) {
        Vec3 p = a + u * t + v * w;
        p.y    = c.G(p) + dy;
        return p;
    };
    c.run(c.look.asphalt, MAT_ASPHALT, [&] {
        for (float t = 0.0f; t < L; t += m(50.0f)) {
            const float t1 = std::min(L, t + m(50.0f));
            face(c.b, at(t, -hw, m(0.15f)), at(t, hw, m(0.15f)), at(t1, -hw, m(0.15f)), at(t1, hw, m(0.15f)), kUp,
                 c.look.asphaltTile);
        }
    });
    c.run(c.look.white, MAT_DEFAULT, [&] {
        for (float t = m(60.0f); t + m(36.0f) < L - m(60.0f); t += m(60.0f)) {
            const Vec3 p = at(t + m(18.0f), 0.0f, m(0.17f));
            obox(c.b, p, u * m(18.0f), kUp * m(0.01f), v * m(0.45f));
        }
    });
    c.glow(nm::kRunwayNits, false, Vec4(1.0f, 0.92f, 0.75f, 1.0f), MAT_DEFAULT, [&] {
        for (float t = 0.0f; t <= L; t += m(nm::kRunwayLight))
            for (const float w : {-hw - m(1.0f), hw + m(1.0f)})
                obox(c.b, at(t, w, m(0.45f)), u * m(0.15f), kUp * m(0.15f), v * m(0.15f));
    });
    c.glow(nm::kRunwayNits, false, Vec4(0.3f, 1.0f, 0.4f, 1.0f), MAT_DEFAULT, [&] {
        for (const float t : {-m(1.0f), L + m(1.0f)})
            for (float w = -hw; w <= hw + 1.0f; w += m(3.0f))
                obox(c.b, at(t, w, m(0.45f)), u * m(0.15f), kUp * m(0.15f), v * m(0.15f));
    });
}

// #5 Terminal B: long grey metal-panel hall with a glass curtain wall, a glass block over it, the tower, three jets
// on the apron between it and the fence (tail fins over the fence, as in the photo).
void terminal(const Ctx& c) {
    const double s  = c.lm.s[0];
    const auto   sm = c.al.sample(s);
    const float  sg = c.lm.lat[0] < 0.0f ? -1.0f : 1.0f;
    const Vec3   T(sm.tx, 0.0f, sm.tz), R = Vec3(-sm.tz, 0.0f, sm.tx) * sg;  // R: away from the road
    Vec3         C = c.P(s, c.lat, 0.0f);
    C.y            = c.G(C);
    const float L = 0.5f * m(nm::kTermL), D = 0.5f * m(nm::kTermD), H = m(nm::kTermH);
    c.run(Vec4(0.62f, 0.64f, 0.66f, 1.0f), MAT_METAL, [&] {
        obox(c.b, C + kUp * (0.5f * H - m(1.0f)), T * L, kUp * (0.5f * H + m(1.0f)), R * D);
        obox(c.b, C + kUp * (H + m(0.6f)), T * (L + m(4.0f)), kUp * m(0.6f), R * (D + m(4.0f)));  // roof overhang
    });
    c.glow(150.0f, false, kGlass, MAT_METAL, [&] {
        obox(c.b, C - R * (D + m(0.05f)) + kUp * (0.55f * H), T * (L - m(10.0f)), kUp * (0.35f * H), R * m(0.05f));
        obox(c.b, C + kUp * (H + m(9.0f)), T * m(30.0f), kUp * m(8.0f), R * m(20.0f));
    });
    const Vec3 tw = C + T * (L + m(70.0f)) + R * m(20.0f);
    c.run(c.look.concrete, MAT_CONCRETE, [&] {
        ring(c.b, tw, T, R, m(4.0f), m(4.0f), 1.0f, 1.0f, -m(1.0f), m(nm::kTowerH), 12, c.look.concreteTile);
        ring(c.b, tw, T, R, m(nm::kCabR), m(nm::kCabR), 1.0f, 0.4f, m(nm::kTowerH + 6.0f), m(nm::kTowerH + 8.0f), 16,
             c.look.concreteTile);
    });
    c.glow(300.0f, false, kGlass, MAT_METAL, [&] {
        ring(c.b, tw, T, R, m(nm::kCabR), m(nm::kCabR), 0.8f, 1.0f, m(nm::kTowerH), m(nm::kTowerH + 6.0f), 16, 4000.0f);
    });
    const auto jet = [&](const Vec3& p, auto part) {                            // nose toward the terminal, along R
        part(p + kUp * m(3.0f), R * m(19.0f), kUp * m(2.0f), T * m(2.0f), 0);   // fuselage
        part(p + kUp * m(2.5f), R * m(3.0f), kUp * m(0.25f), T * m(17.0f), 0);  // wings
        part(p - R * m(16.5f) + kUp * m(8.5f), R * m(2.6f), kUp * m(3.8f), T * m(0.25f), 1);  // fin
        part(p - R * m(17.0f) + kUp * m(5.0f), R * m(1.5f), kUp * m(0.15f), T * m(6.0f), 0);  // stabiliser
    };
    for (const int fin : {0, 1})
        c.run(fin ? Vec4(0.08f, 0.14f, 0.35f, 1.0f) : Vec4(0.9f, 0.9f, 0.92f, 1.0f), MAT_METAL, [&] {
            for (const float a : {-0.5f * L, 0.0f, 0.5f * L}) {
                Vec3 p = C + T * a - R * (D + m(24.0f));
                p.y    = c.G(p);
                jet(p, [&](const Vec3& q, const Vec3& hx, const Vec3& hy, const Vec3& hz, int f) {
                    if (f == fin) obox(c.b, q, hx, hy, hz);
                });
            }
        });
}

// #6 a stadium bowl: facade ring (brick arches for Citi Field, ribbed pale concrete for Ashe), upper ring, raked
// seating inside, field, light masts with lamp banks (lit with the street lamps).
void bowl(const Ctx& c, bool citi) {
    const double s  = c.lm.s[0];
    const auto   sm = c.al.sample(s);
    const Vec3   T(sm.tx, 0.0f, sm.tz), R(-sm.tz, 0.0f, sm.tx);
    Vec3         C = c.P(s, c.lat, 0.0f);
    C.y            = c.G(C);
    const float A = m(citi ? nm::kCitiA : nm::kAsheR), B = m(citi ? nm::kCitiB : nm::kAsheR);
    const float H = m(citi ? nm::kCitiH : nm::kAsheH), H2 = citi ? m(nm::kCitiTop) : H + m(nm::kAsheRoof);
    const int   n = 48;
    c.run(citi ? Vec4(0.78f, 0.45f, 0.36f, 1.0f) : Vec4(0.86f, 0.84f, 0.8f, 1.0f), citi ? MAT_BRICK : MAT_CONCRETE,
          [&] {
              ring(c.b, C, T, R, A, B, 1.0f, 1.0f, -m(2.0f), H, n,
                   citi ? m(road::edges::kBrickTile) : c.look.concreteTile);
              if (!citi)  // vertical ribs
                  for (int k = 0; k < n; ++k) {
                      const float a = 2.0f * kPi * k / n;
                      const Vec3  d = T * (std::cos(a) * A) + R * (std::sin(a) * B), e = glm::normalize(d);
                      obox(c.b, C + d + e * m(0.6f) + kUp * (0.5f * H), e * m(0.6f), kUp * (0.5f * H),
                           glm::cross(kUp, e) * m(0.5f));
                  }
          });
    if (citi)
        c.run(Vec4(0.86f, 0.82f, 0.72f, 1.0f), MAT_CONCRETE, [&] {  // limestone arcade band, dark arch openings
            ring(c.b, C, T, R, A + m(0.1f), B + m(0.1f), 1.0f, 1.0f, m(9.0f), m(10.5f), n, c.look.concreteTile);
        });
    c.run(citi ? Vec4(0.14f, 0.16f, 0.19f, 1.0f) : Vec4(0.95f, 0.95f, 0.96f, 1.0f), MAT_METAL, [&] {
        ring(c.b, C, T, R, A, B, 0.97f, citi ? 0.97f : 0.82f, H, H2, n, 4000.0f);        // upper ring / white roof
        if (citi) ring(c.b, C, T, R, A, B, 0.97f, 0.86f, H2, H2 + m(1.5f), n, 4000.0f);  // canopy lip
        const int masts = 6;
        for (int k = 0; k < masts; ++k) {
            const float a = 2.0f * kPi * (k + 0.5f) / masts, r = citi ? 0.9f : 1.2f;
            const Vec3  d   = T * (std::cos(a) * A * r) + R * (std::sin(a) * B * r);
            const float top = citi ? m(nm::kCitiMast) : H2 + m(18.0f);
            cmesh::pole(c.b, C + d + kUp * (citi ? H2 : -m(1.0f)), C + d + kUp * top, m(0.6f));
        }
    });
    c.run(citi ? Vec4(0.12f, 0.3f, 0.22f, 1.0f) : Vec4(0.12f, 0.22f, 0.45f, 1.0f), MAT_DEFAULT, [&] {  // seats, inward
        ring(c.b, C, T, R, A, B, 0.95f, 0.55f, H2 - m(2.0f), m(3.0f), n, 4000.0f, true);
    });
    c.run(citi ? Vec4(0.25f, 0.42f, 0.2f, 1.0f) : Vec4(0.2f, 0.35f, 0.55f, 1.0f), citi ? MAT_GRASS : MAT_DEFAULT, [&] {
        for (int k = 0; k < n; ++k) {  // field, a fan of triangles
            const float a0 = 2.0f * kPi * k / n, a1 = 2.0f * kPi * (k + 1) / n;
            const Vec3  d0 = T * (std::cos(a0) * A) + R * (std::sin(a0) * B),
                        d1 = T * (std::cos(a1) * A) + R * (std::sin(a1) * B);
            face(c.b, C + d0 * 0.56f + kUp * m(2.9f), C + d1 * 0.56f + kUp * m(2.9f), C + kUp * m(2.9f),
                 C + kUp * m(2.9f), kUp, c.look.grassTile);
        }
    });
    c.glow(nm::kMastNits, false, Vec4(1.0f, 0.97f, 0.9f, 1.0f), MAT_DEFAULT,
           [&] {  // lamp banks facing the field
               for (int k = 0; k < 6; ++k) {
                   const float a = 2.0f * kPi * (k + 0.5f) / 6.0f, r = citi ? 0.9f : 1.2f;
                   const Vec3  d = T * (std::cos(a) * A * r) + R * (std::sin(a) * B * r), e = glm::normalize(d);
                   const float top = citi ? m(nm::kCitiMast) : H2 + m(18.0f);
                   obox(c.b, C + d + kUp * top, e * m(0.4f), kUp * m(2.0f), glm::cross(kUp, e) * m(3.0f));
               }
           });
}

// #7 Kew Gardens: light-blue steel plate girders under every elevated ramp of the nearest junction.
void kew(const Ctx& c, int road) {
    const NetworkJunction* J = nearest_junction(c.net, road, c.lm.s[0]);
    if (!J) return;
    c.run(Vec4(0.36f, 0.55f, 0.78f, 1.0f), MAT_METAL, [&] {
        for (const NetRamp& rp : c.net.ramps()) {
            if (rp.junction != J->id || rp.closed) continue;
            for (size_t i = 0; i + 1 < rp.pts.size(); ++i) {
                const NetRampPoint &p = rp.pts[i], &q = rp.pts[i + 1];
                if (p.y - p.ground < m(nw::kPierMinH) || q.y - q.ground < m(nw::kPierMinH)) continue;
                const Vec3 a(static_cast<float>(p.x - c.seg.originX), p.y, static_cast<float>(p.z - c.seg.originZ));
                const Vec3 b(static_cast<float>(q.x - c.seg.originX), q.y, static_cast<float>(q.z - c.seg.originZ));
                const Vec3 mid = 0.5f * (a + b), side(-p.tz, 0.0f, p.tx);
                for (const float w : {-0.6f, 0.6f})
                    obox(c.b, mid + side * (w * rp.halfWidth) - kUp * (m(nw::kDeck) + 0.5f * m(nm::kKewGirder)),
                         0.5f * (b - a), kUp * (0.5f * m(nm::kKewGirder)), side * m(0.25f));
            }
        }
    });
}

// #8 Throgs Neck Bridge, Queens tower then Bronx tower: closed-box legs with struts, stiffened deck over the
// side spans, main and side cables, anchorages. A far segment (no shadow): the driver scales it like the skyline.
void throgs(const Ctx& c) {
    const Vec3  q = c.P(c.lm.s[0], c.lm.lat[0], c.lm.y), z = c.P(c.lm.s[1], c.lm.lat[1], c.lm.y);
    const Vec3  d    = Vec3(z.x - q.x, 0.0f, z.z - q.z);
    const float span = glm::length(d), sd = m(nm::kTnbSide), hw = m(nm::kTnbHalfW), leg = 0.5f * m(nm::kTnbLeg);
    if (span < m(100.0f)) return;
    const Vec3  u = d / span, v = glm::cross(kUp, u);
    const float H = m(nm::kTnbTower), dk = m(nm::kTnbDeck);
    const auto  P = [&](float t, float w, float y) { return q + u * t + v * w + kUp * y; };
    c.run(Vec4(0.32f, 0.37f, 0.38f, 1.0f), MAT_METAL, [&] {  // grey-green paint, dark against the haze
        for (const float t : {0.0f, span}) {
            for (const float w : {-hw, hw}) obox(c.b, P(t, w, 0.5f * H), u * leg, kUp * (0.5f * H), v * leg);
            for (const float y : {dk - m(3.0f), 0.62f * H, H - m(3.0f)})
                obox(c.b, P(t, 0.0f, y), u * (0.8f * leg), kUp * m(2.0f), v * hw);
        }
        obox(c.b, P(0.5f * span, 0.0f, dk), u * (0.5f * span + sd), kUp * m(2.5f), v * hw);  // deck + truss
        for (float t = -sd; t <= span + sd; t += m(60.0f))                                   // side-span piers
            if (t < -m(20.0f) || t > span + m(20.0f))
                obox(c.b, P(t, 0.0f, 0.5f * dk), u * m(2.0f), kUp * (0.5f * dk), v * m(3.0f));
        for (const float w : {-hw + m(1.0f), hw - m(1.0f)}) {  // main cable (parabola) and side cables
            const int n = 24;
            for (int k = 0; k < n; ++k) {
                const float t0 = span * k / n, t1 = span * (k + 1) / n;
                const auto  y = [&](float t) {
                    const float x = 2.0f * t / span - 1.0f;
                    return dk + m(3.0f) + (H - dk - m(3.0f)) * x * x;
                };
                cmesh::rod(c.b, P(t0, w, y(t0)), P(t1, w, y(t1)), m(0.6f));
            }
            cmesh::rod(c.b, P(0.0f, w, H), P(-sd, w, dk), m(0.6f));
            cmesh::rod(c.b, P(span, w, H), P(span + sd, w, dk), m(0.6f));
        }
    });
    c.run(c.look.concrete, MAT_CONCRETE, [&] {  // anchorages
        for (const float t : {-sd, span + sd})
            obox(c.b, P(t, 0.0f, 0.5f * dk), u * m(15.0f), kUp * (0.5f * dk + m(2.0f)), v * m(20.0f));
    });
    for (DrawCall& dc : c.seg.draws) dc.noShadow = true;
    c.seg.far = static_cast<uint32_t>(c.seg.draws.size());
}

}  // namespace

float landmark_lat(const CorridorNetwork& net, const NetworkLandmark& l) {
    const CorridorAlignment& al = net.road(l.road);
    const Kind               k  = kind_of(l);
    const float lat = k == Kind::Gas ? nm::kGasSide * (al.outer_edge(al.sample(l.s[0])) + m(nm::kGasOut)) : l.lat[0];
    const float r   = footprint(k);
    if (r <= 0.0f) return lat;
    for (int i = 0; i <= 60; ++i)  // nearest lateral clear of the network (the compaction moved roads under it)
        for (const float sg : {-1.0f, 1.0f})
            if (clear(net, world(al, l.s[0], lat + sg * m(10.0f) * static_cast<float>(i)), r))
                return lat + sg * m(10.0f) * static_cast<float>(i);
    return lat;
}

bool landmark_replaces(const CorridorNetwork& net, int r, double s) {
    return std::any_of(net.asset().landmarks.begin(), net.asset().landmarks.end(), [&](const NetworkLandmark& l) {
        return kind_of(l) == Kind::Jamaica && l.road == r && std::abs(l.s[0] - s) < m(nm::kYardReplace);
    });
}

std::vector<std::array<double, 3>> landmark_keepout(const CorridorNetwork& net) {
    std::vector<std::array<double, 3>> out;
    const auto dots = [&](const XZ& c, double tx, double tz, double a, double b) {  // ellipse a x b, cell 25 m
        const double st = m(25.0f);
        for (double x = -a; x <= a; x += st)
            for (double y = -b; y <= b; y += st)
                if ((x * x) / (a * a) + (y * y) / (b * b) <= 1.2)
                    out.push_back({c.x + tx * x - tz * y, c.z + tz * x + tx * y, m(20.0f)});
    };
    const auto line = [&](const XZ& a, const XZ& b, double r) {  // dots every 25 m from a to b
        const double L = std::hypot(b.x - a.x, b.z - a.z);
        for (double t = 0.0; t <= L; t += m(25.0f))
            out.push_back({a.x + (b.x - a.x) * t / L, a.z + (b.z - a.z) * t / L, r});
    };
    for (const NetworkLandmark& l : net.asset().landmarks) {
        const CorridorAlignment& al = net.road(l.road);
        const auto               sm = al.sample(std::clamp(l.s[0], 0.0, al.total_length()));
        const Kind               k  = kind_of(l);
        if (k == Kind::Citi || k == Kind::Ashe || k == Kind::Terminal) {  // sightline: no trees between road and it
            const float lat = landmark_lat(net, l), sg = lat < 0.0f ? -1.0f : 1.0f;
            for (int i = -3; i <= 3; ++i)  // a fan from the road within kSightRun of its station (the D6 wedge)
                line(world(al, l.s[0] + m(nm::kSightRun) * static_cast<float>(i) / 3.0f,
                           sg * (al.outer_edge(sm) + m(nm::kFenceOut))),
                     world(al, l.s[0], lat), m(30.0f));
        }
        if (k ==
            Kind::Throgs)  // no trees in the first kTnbSight from each work-zone wall within 8 km toward the towers
            for (const NetworkWall& wl : net.asset().walls) {
                const auto   p = net.road(wl.road).sample(wl.s);
                const XZ     b = world(al, l.s[0], l.lat[0]);
                const double d = std::hypot(b.x - p.x, b.z - p.z), ux = (b.x - p.x) / d, uz = (b.z - p.z) / d;
                if (d > m(8000.0f)) continue;
                for (const double o : {-m(60.0f), 0.0f, m(60.0f)})
                    line({p.x - uz * o, p.z + ux * o},
                         {p.x + ux * m(nm::kTnbSight) - uz * o, p.z + uz * m(nm::kTnbSight) + ux * o}, m(30.0f));
            }
        if (k == Kind::Fence)  // the airfield past the fence: open grass
            for (double q = std::min(l.s[0], l.s[1]); q <= std::max(l.s[0], l.s[1]); q += m(25.0f)) {
                const float sg = l.lat[0] < 0.0f ? -1.0f : 1.0f, e = al.outer_edge(al.sample(q)) + m(nm::kFenceOut);
                for (int i = 1; i <= 6; ++i) {
                    const XZ p = world(al, q, sg * (e + m(45.0f) * static_cast<float>(i)));
                    out.push_back({p.x, p.z, m(30.0f)});
                }
            }
        switch (k) {
            case Kind::Gas: {
                dots(world(al, l.s[0], landmark_lat(net, l) + nm::kGasSide * m(0.5f * nm::kKioskBack)), sm.tx, sm.tz,
                     m(30.0f), m(20.0f));
                break;
            }
            case Kind::Citi:
                dots(world(al, l.s[0], landmark_lat(net, l)), sm.tx, sm.tz, m(nm::kCitiA + 15.0f),
                     m(nm::kCitiB + 15.0f));
                break;
            case Kind::Ashe:
                dots(world(al, l.s[0], landmark_lat(net, l)), sm.tx, sm.tz, m(nm::kAsheR + 25.0f),
                     m(nm::kAsheR + 25.0f));
                break;
            case Kind::Terminal:
                dots(world(al, l.s[0], landmark_lat(net, l)), sm.tx, sm.tz, m(0.5f * nm::kTermL + 90.0f), m(80.0f));
                break;
            case Kind::Runway:
                for (int k = 0; k <= 40; ++k) {
                    const XZ a = world(al, l.s[0], l.lat[0]), b = world(al, l.s[1], l.lat[1]);
                    out.push_back({a.x + (b.x - a.x) * k / 40.0, a.z + (b.z - a.z) * k / 40.0, m(30.0f)});
                }
                break;
            default:
                break;
        }
    }
    return out;
}

std::vector<CorridorSegment> network_landmark_segments(const CorridorNetwork& net, const CorridorLook& look,
                                                       const StructureParams& sp) {
    std::vector<CorridorSegment> out;
    for (const NetworkLandmark& l : net.asset().landmarks) {
        const CorridorAlignment& al = net.road(l.road);
        CorridorSegment          seg;
        const float              lat = landmark_lat(net, l);
        const XZ                 o   = world(al, l.s[0], lat);
        seg.originX = o.x, seg.originZ = o.z;
        seg.startStation = seg.endStation = l.s[0];
        MeshBuilder b(seg.mesh, seg.draws);
        const Ctx   c{net, al, l, lat, look, sp, seg, b};
        switch (kind_of(l)) {
            case Kind::Gas:
                gas(c);
                break;
            case Kind::Arch:
                arch(c, l.road);
                break;
            case Kind::Astoria:
                astoria(c);
                break;
            case Kind::Jamaica:
                jamaica(c);
                break;
            case Kind::Fence:
                fence(c);
                break;
            case Kind::Runway:
                runway(c);
                break;
            case Kind::Terminal:
                terminal(c);
                break;
            case Kind::Citi:
                bowl(c, true);
                break;
            case Kind::Ashe:
                bowl(c, false);
                break;
            case Kind::Kew:
                kew(c, l.road);
                break;
            case Kind::Throgs:
                throgs(c);
                break;
        }
        if (!seg.mesh.empty()) out.push_back(std::move(seg));
    }
    return out;
}

}  // namespace swish
