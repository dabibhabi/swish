#include "CorridorBridges.h"

#include "scene/RoadScene/MeshBuilder.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace swish {

using util::units::ft;
using util::units::m;
namespace rb = road::bridges;

namespace {

using cmesh::face;
using cmesh::hexa;
using cmesh::pole;
using cmesh::rod;

constexpr Vec3 kUp(0.0f, 1.0f, 0.0f);

float smooth(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

float inch(float v) { return util::units::ft(v / 12.0f); }

// One draw for everything fn pushes; metalness tag when the draw exists.
template <class Fn>
void batch(MeshBuilder& b, std::vector<DrawCall>& draws, const Vec4& c, MaterialId mat, float metal, Fn&& fn) {
    const uint32_t mk = b.batch_mark();
    fn();
    if (b.close_batch(mk, c, mat) && metal >= 0.0f) draws.back().metalness = metal;
}

// Box in a deck frame: a in [a0, a1], t in [t0, t1], y in [y0, y1].
void box(MeshBuilder& b, const DeckFrame& F, float a0, float a1, float t0, float t1, float y0, float y1,
         float tile = 4000.0f) {
    std::array<Vec3, 8> q;
    for (int i = 0; i < 8; ++i) q[i] = F.P(i & 1 ? a1 : a0, i & 2 ? t1 : t0, i & 4 ? y1 : y0);
    hexa(b, q, tile);
}

// Quad with the normal taken from its own corners, flipped toward `hint`; plan-space UVs keep textures continuous.
void quad(MeshBuilder& b, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const Vec3& hint,
          float tile) {
    const auto same = [](const Vec3& x, const Vec3& y) { return glm::length(x - y) < 1.0f; };
    // face() normalises p1 - p0 and p2 - p0: a collapsed edge (a slope toe at zero height) would give NaN UVs
    if (same(p0, p2) && same(p1, p3)) return;
    const std::array<Vec3, 4> q = same(p0, p2)   ? std::array<Vec3, 4>{p0, p1, p3, p3}
                                  : same(p1, p3) ? std::array<Vec3, 4>{p0, p1, p2, p2}
                                                 : std::array<Vec3, 4>{p0, p1, p2, p3};
    Vec3                      n = glm::cross(q[1] - q[0], q[2] - q[0]);
    if (glm::length(n) < 1e-6f || same(q[0], q[1])) return;
    n = glm::normalize(n);
    if (glm::dot(n, hint) < 0.0f) n = -n;
    face(b, q[0], q[1], q[2], q[3], n, tile);
}

// Convex polygon a-b-c-d (in order round the edge) as one strip-order quad.
void cap4(MeshBuilder& b, const Vec3& pa, const Vec3& pb, const Vec3& pc, const Vec3& pd, const Vec3& n) {
    face(b, pa, pb, pd, pc, n);
}

// F-shape traffic face (NJ runs, break at 10 in) with a vertical back: (inward from the outer face, height), WU.
std::array<Vec2, 6> f_profile() {
    const float lower = (rb::kFBreakIn - road::edges::kNjRevealIn) / std::tan(55.0f * util::units::kRadPerDeg);
    const float upper = (road::edges::kNjHeightIn - rb::kFBreakIn) / std::tan(84.0f * util::units::kRadPerDeg);
    const float top = 8.0f, base = top + upper + lower;
    return {{{0.0f, 0.0f},
             {0.0f, inch(road::edges::kNjHeightIn)},
             {inch(top), inch(road::edges::kNjHeightIn)},
             {inch(top + upper), inch(rb::kFBreakIn)},
             {inch(base), inch(road::edges::kNjRevealIn)},
             {inch(base), 0.0f}}};
}

// Parapet profile extruded along t on deck edge a = sgn * A, standing on y0.
void parapet(MeshBuilder& b, const DeckFrame& F, float sgn, float A, float t0, float t1, float y0) {
    const auto pr = f_profile();
    const auto p  = [&](const Vec2& q, float t) { return F.P(sgn * (A - q.x), t, y0 + q.y); };
    const Vec3 mid =
        0.5f * (p(Vec2(inch(7.0f), inch(16.0f)), t0) + p(Vec2(inch(7.0f), inch(16.0f)), t1));  // inside the profile
    for (size_t i = 0; i + 1 < pr.size(); ++i) {
        const Vec3 a0 = p(pr[i], t0), a1 = p(pr[i + 1], t0), b0 = p(pr[i], t1), b1 = p(pr[i + 1], t1);
        quad(b, a0, a1, b0, b1, 0.5f * (a0 + b1) - mid, 4000.0f);
    }
    for (const float t : {t0, t1}) {
        const Vec3 n = glm::normalize(F.et) * (t == t0 ? -1.0f : 1.0f);
        cap4(b, p(pr[0], t), p(pr[1], t), p(pr[2], t), p(pr[5], t), n);
        cap4(b, p(pr[2], t), p(pr[3], t), p(pr[4], t), p(pr[5], t), n);
    }
}

// Girder centres across the deck, fascia girders kOverhang in from the edges.
std::vector<float> girders(float A) {
    const float        w = 2.0f * (A - m(rb::kOverhang));
    const int          n = std::max(2, static_cast<int>(std::lround(w / m(rb::kSpacing))) + 1);
    std::vector<float> out;
    for (int i = 0; i < n; ++i) out.push_back(-0.5f * w + w * static_cast<float>(i) / static_cast<float>(n - 1));
    return out;
}

// Height of a C3a-style crest over the last kStubFade of a road ending at `end` (|t|), 0 before it.
float crest(float d, float end) {
    const float d0 = end - m(diamond::kStubFade), d1 = end - m(diamond::kStubCrest), H = m(diamond::kCrestH);
    if (d <= d0) return 0.0f;
    if (d <= d1) return H * smooth((d - d0) / (d1 - d0));
    return H - (H + m(diamond::kStubDrop)) * smooth((d - d1) / (end - d1));
}

// Knots in [lo, hi] every `step`, plus extra cut points.
std::vector<float> knots(float lo, float hi, float step, std::vector<float> extra = {}) {
    std::vector<float> k;
    for (float d = lo; d < hi; d += step) k.push_back(d);
    k.push_back(hi);
    for (const float e : extra)
        if (e > lo && e < hi) k.push_back(e);
    std::sort(k.begin(), k.end());
    k.erase(std::unique(k.begin(), k.end(), [](float x, float y) { return std::abs(x - y) < 1.0f; }), k.end());
    return k;
}

BridgeStyle district_style(const CorridorAlignment& al, double s) {
    const CorridorDistrict* d = al.district_at(s);
    return d && std::strncmp(d->name, "Suffolk", 8) == 0 ? BridgeStyle::Concrete : BridgeStyle::Steel;
}

bool in_district(const CorridorAlignment& al, double s, const char* name) {
    const CorridorDistrict* d = al.district_at(s);
    return d && std::strncmp(d->name, name, sizeof(d->name)) == 0;
}

// |t| of the service road's outer curb on `side` at s; 0 when none.
float sr_outer(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s, int side) {
    for (const CorridorPath& p : paths)
        if (p.kind == CorridorPath::Kind::Service && p.side == side && s >= p.s0 && s <= p.s1)
            return std::abs(corridor_path_at(al, p, s).lat) + p.halfWidth + m(diamond::kCurbW);
    return 0.0f;
}

// Deck depth from slab top to girder soffit for a girder style.
float girder_depth() { return m(rb::kSlab + rb::kHaunch + rb::kGirderD); }

const Vec4 kSteelQueens(0.42f, 0.56f, 0.46f, 1.0f);  // weathered green paint (photo exit-17, BQE)
const Vec4 kSteelNassau(0.62f, 0.64f, 0.63f, 1.0f);  // grey paint
const Vec4 kSteelLirr(0.48f, 0.52f, 0.48f, 1.0f);    // dark green-black railroad paint
const Vec4 kBearing(0.15f, 0.15f, 0.16f, 1.0f);      // elastomer and sole plates
const Vec4 kStone(1.0f, 0.95f, 0.88f, 1.0f);         // warm grey granite ashlar
const Vec4 kBallast(0.36f, 0.36f, 0.37f, 1.0f);      // crushed trap rock
const Vec4 kRail(0.42f, 0.36f, 0.31f, 1.0f);         // rusted rail sides

// Supports: pier caps, piers (median wall at t = 0, bents or walls elsewhere), abutment seats and backwalls.
void supports(MeshBuilder& b, const DeckSpec& d, float slabBot, const std::function<float(float)>& base, bool stone) {
    const DeckFrame& F    = d.F;
    const float      capT = d.soffit - m(rb::kBearingH), capB = capT - m(rb::kCapH), hw = 0.5f * m(rb::kCapW);
    for (const float p : d.piers) {
        box(b, F, -d.A + m(0.4f), d.A - m(0.4f), p - hw, p + hw, capB, capT);
        if (stone) continue;  // stone-clad shafts go in the stone batch
        if (p == 0.0f)
            box(b, F, -d.A + m(1.0f), d.A - m(1.0f), -d.medianHalf, d.medianHalf, base(p), capB);
        else if (d.style == BridgeStyle::Steel) {
            const float w = 2.0f * (d.A - m(1.5f));
            const int   n = std::max(2, static_cast<int>(w / m(rb::kColStep)) + 1);
            for (int i = 0; i < n; ++i) {
                const float a = -0.5f * w + w * static_cast<float>(i) / static_cast<float>(n - 1);
                pole(b, F.P(a, p, base(p)), F.P(a, p, capB), m(rb::kColR));
            }
        } else
            box(b, F, -d.A + m(1.2f), d.A - m(1.2f), p - 0.5f * m(rb::kWallPier), p + 0.5f * m(rb::kWallPier), base(p),
                capB);
    }
    if (!d.seats) return;
    for (const float sg : {-1.0f, 1.0f}) {
        const float te = sg > 0.0f ? d.t1 : d.t0;
        box(b, F, -d.A, d.A, te - hw, te + hw, capB, capT);
        const float b0 = te + sg * (hw - m(0.3f)), b1 = te + sg * hw;
        box(b, F, -d.A, d.A, std::min(b0, b1), std::max(b0, b1), capT, slabBot);  // backwall under the slab end
    }
}

void stone_shafts(MeshBuilder& b, const DeckSpec& d, const std::function<float(float)>& base) {
    const float capB = d.soffit - m(rb::kBearingH + rb::kCapH), st = m(rb::kStoneTile);
    for (const float p : d.piers) {
        const float w = p == 0.0f ? d.medianHalf : 0.5f * m(rb::kStonePier);
        box(b, d.F, -d.A + m(0.6f), d.A - m(0.6f), p - w, p + w, base(p), capB, st);
    }
}

// Girder span ends along t: abutment ends sit on the seat, pier ends leave a 6 cm joint.
std::vector<std::pair<float, float>> spans(const DeckSpec& d) {
    const float        hw = 0.5f * m(rb::kCapW) - m(0.32f);
    std::vector<float> sup{d.t0 - hw};
    for (const float p : d.piers) sup.push_back(p);
    sup.push_back(d.t1 + hw);
    std::sort(sup.begin() + 1, sup.end() - 1);
    std::vector<std::pair<float, float>> out;
    for (size_t i = 0; i + 1 < sup.size(); ++i)
        out.push_back({sup[i] + (i ? m(0.03f) : 0.0f), sup[i + 1] - (i + 2 < sup.size() ? m(0.03f) : 0.0f)});
    return out;
}

}  // namespace

float arch_rise(float span) { return std::min(rb::kArchRise * span, m(rb::kArchRiseMax)); }

void emit_deck(MeshBuilder& b, std::vector<DrawCall>& draws, const DeckSpec& d, const std::function<float(float)>& base,
               const CorridorLook& look) {
    const DeckFrame& F       = d.F;
    const bool       stone   = d.style == BridgeStyle::Stone;
    const bool       steel   = d.style == BridgeStyle::Steel;
    const float      slabBot = d.top - m(rb::kSlab), gTop = slabBot - m(rb::kHaunch), hw = 0.5f * m(rb::kCapW);
    const float      e0 = d.t0 - hw, e1 = d.t1 + hw;  // slab ends over the backwalls
    const auto       ga = girders(d.A);
    const auto       sp = spans(d);
    const float      D  = gTop - d.soffit;

    batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
        box(b, F, -d.A, d.A, e0, e1, slabBot, d.top);
        if (!stone)
            for (const float sg : {-1.0f, 1.0f}) {  // drip notch under the overhang: water leaves before the fascia
                const float a = sg * (d.A - m(0.08f));
                box(b, F, a - 0.5f * m(rb::kDrip), a + 0.5f * m(rb::kDrip), e0, e1, slabBot - m(rb::kDrip), slabBot);
                parapet(b, F, sg, d.A, e0, e1, d.top);
            }
        for (const float a : ga) {
            const float hf = 0.5f * (steel ? m(rb::kFlangeW) : m(rb::kBeamTop));
            box(b, F, a - hf, a + hf, e0 + hw - m(0.32f), e1 - hw + m(0.32f), gTop, slabBot);  // haunch
            if (steel) continue;
            for (const auto& [t0, t1] : sp) {  // precast I-beam: bottom flange, web, top flange
                const float fl = m(rb::kBeamFl);
                box(b, F, a - 0.5f * m(rb::kBeamBot), a + 0.5f * m(rb::kBeamBot), t0, t1, d.soffit, d.soffit + fl);
                box(b, F, a - 0.5f * m(rb::kBeamWeb), a + 0.5f * m(rb::kBeamWeb), t0, t1, d.soffit + fl, gTop - fl);
                box(b, F, a - hf, a + hf, t0, t1, gTop - fl, gTop);
            }
        }
        if (!steel)  // end diaphragms over every support
            for (const auto& [t0, t1] : sp)
                for (const float t : {t0, t1}) {
                    const float c = t == t0 ? t0 + m(0.25f) : t1 - m(0.25f);
                    box(b, F, ga.front(), ga.back(), c - m(0.2f), c + m(0.2f), d.soffit + m(0.2f), gTop);
                }
        supports(b, d, slabBot, base, stone);
    });

    if (steel || d.fence)
        batch(b, draws, d.nassau ? kSteelNassau : kSteelQueens, MAT_METAL, 0.0f, [&] {
            const float fw = m(rb::kFlangeW), ft_ = m(rb::kFlangeT), wt = m(rb::kWebT);
            if (steel)
                for (size_t i = 0; i < ga.size(); ++i) {
                    const float a = ga[i];
                    for (const auto& [t0, t1] : sp) {
                        box(b, F, a - 0.5f * fw, a + 0.5f * fw, t0, t1, d.soffit, d.soffit + ft_);
                        box(b, F, a - 0.5f * wt, a + 0.5f * wt, t0, t1, d.soffit + ft_, gTop - ft_);
                        box(b, F, a - 0.5f * fw, a + 0.5f * fw, t0, t1, gTop - ft_, gTop);
                        if (i == 0 || i + 1 == ga.size()) {  // fascia stiffeners face the road below
                            const float so = i == 0 ? -1.0f : 1.0f, x0 = a + so * 0.5f * wt, x1 = a + so * 0.45f * fw;
                            for (float t = t0 + m(0.5f * rb::kStiffStep); t < t1; t += m(rb::kStiffStep))
                                box(b, F, std::min(x0, x1), std::max(x0, x1), t - m(0.008f), t + m(0.008f),
                                    d.soffit + ft_, gTop - ft_);
                        }
                        if (i + 1 < ga.size())  // X cross-frames at the ends and every 7.5 m
                            for (float t = t0 + m(0.4f); t < t1; t += std::max(m(7.5f), (t1 - t0 - m(0.8f)) / 3.0f)) {
                                const float a1 = ga[i + 1], lo = d.soffit + 0.15f * D, hi = gTop - 0.15f * D;
                                rod(b, F.P(a, t, lo), F.P(a1, t, hi), m(0.04f));
                                rod(b, F.P(a, t, hi), F.P(a1, t, lo), m(0.04f));
                                rod(b, F.P(a, t, lo), F.P(a1, t, lo), m(0.04f));
                            }
                    }
                }
            if (d.fence)  // 8 ft chain-link frame on the parapet: posts and three rails (mesh needs alpha, omitted)
                for (const float sg : {-1.0f, 1.0f}) {
                    const float a = sg * (d.A - m(0.1f)), y0 = d.top + inch(road::edges::kNjHeightIn);
                    for (float t = e0; t <= e1 + 1.0f; t += m(rb::kFencePost))
                        rod(b, F.P(a, t, y0), F.P(a, t, y0 + m(rb::kFenceH)), m(0.03f));
                    for (const float h : {0.05f, 0.5f, 1.0f})
                        rod(b, F.P(a, e0, y0 + h * m(rb::kFenceH)), F.P(a, e1, y0 + h * m(rb::kFenceH)), m(0.02f));
                }
        });

    batch(b, draws, kBearing, MAT_METAL, 0.0f, [&] {
        for (const auto& [t0, t1] : sp)
            for (const float t : {t0 + m(0.25f), t1 - m(0.25f)})
                for (const float a : ga)
                    box(b, F, a - 0.5f * m(rb::kBearingW), a + 0.5f * m(rb::kBearingW), t - 0.5f * m(rb::kBearingL),
                        t + 0.5f * m(rb::kBearingL), d.soffit - m(rb::kBearingH), d.soffit);
    });

    if (stone)
        batch(b, draws, kStone, MAT_STONE, -1.0f, [&] {
            const float st = m(rb::kStoneTile), P = m(rb::kStoneParapet);
            for (const float sg : {-1.0f, 1.0f}) {
                const float a0 = sg * d.A, a1 = sg * (d.A + m(0.08f));  // ashlar skin over the fascia
                box(b, F, std::min(a0, a1), std::max(a0, a1), e0, e1, d.soffit - m(0.1f), d.top, st);
                const float p0 = sg * (d.A - m(0.45f));  // solid stone parapet
                box(b, F, std::min(p0, a1), std::max(p0, a1), e0, e1, d.top, d.top + P, st);
            }
            stone_shafts(b, d, base);
        });
}

void emit_arch(MeshBuilder& b, std::vector<DrawCall>& draws, const DeckFrame& F, float A, const std::vector<float>& sup,
               float spring, float top, float p0, float p1, const CorridorLook& look) {
    const float st       = m(rb::kStoneTile);
    const auto  intrados = [&](float t, float lo, float hi) {
        const float h = 0.5f * (hi - lo), R = arch_rise(hi - lo), rho = (h * h + R * R) / (2.0f * R);
        const float x = t - 0.5f * (lo + hi);
        return spring + std::sqrt(std::max(0.0f, rho * rho - x * x)) - (rho - R);
    };
    const Vec3 side = glm::normalize(glm::cross(F.et, kUp));
    batch(b, draws, kStone, MAT_STONE, -1.0f, [&] {
        for (size_t j = 0; j + 1 < sup.size(); ++j)
            for (int i = 0; i < 12; ++i) {
                const float ta = sup[j] + (sup[j + 1] - sup[j]) * static_cast<float>(i) / 12.0f;
                const float tb = sup[j] + (sup[j + 1] - sup[j]) * static_cast<float>(i + 1) / 12.0f;
                const float ya = intrados(ta, sup[j], sup[j + 1]), yb = intrados(tb, sup[j], sup[j + 1]);
                for (const float sg : {-1.0f, 1.0f}) {
                    const Vec3 n = side * (glm::dot(side, F.ea) * sg > 0.0f ? 1.0f : -1.0f);
                    quad(b, F.P(sg * A, ta, ya), F.P(sg * A, tb, yb), F.P(sg * A, ta, top), F.P(sg * A, tb, top), n,
                         st);
                }
            }
        for (const float sg : {-1.0f, 1.0f}) {
            const float a0 = sg * (A - m(0.45f)), a1 = sg * A;
            box(b, F, std::min(a0, a1), std::max(a0, a1), p0, p1, top, top + m(rb::kStoneParapet), st);
        }
    });
    batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
        for (size_t j = 0; j + 1 < sup.size(); ++j)
            for (int i = 0; i < 12; ++i) {
                const float ta = sup[j] + (sup[j + 1] - sup[j]) * static_cast<float>(i) / 12.0f;
                const float tb = sup[j] + (sup[j + 1] - sup[j]) * static_cast<float>(i + 1) / 12.0f;
                const float ya = intrados(ta, sup[j], sup[j + 1]), yb = intrados(tb, sup[j], sup[j + 1]);
                quad(b, F.P(-A, ta, ya), F.P(A, ta, ya), F.P(-A, tb, yb), F.P(A, tb, yb), -kUp, 4000.0f);  // vault
            }
        for (const float sg : {-1.0f, 1.0f}) {  // coping on the parapets
            const float a0 = sg * (A - m(0.5f)), a1 = sg * (A + m(0.05f)), y = top + m(rb::kStoneParapet);
            box(b, F, std::min(a0, a1), std::max(a0, a1), p0, p1, y, y + m(0.1f));
        }
    });
}

namespace {

// Road on a profile y(t) along t (knots ks), a in [-A, A]: asphalt, curbs and walks (or one walk deck), paint.
struct RoadStrip {
    float A = 0.0f, rw = 0.0f;  // half-extent; asphalt half-width (0 = footway: concrete deck only)
    float wingEnd[2] = {0.0f, 0.0f}, parapetIn = 0.0f;  // walks stop short of the parapet up to here
    int   lanes = 0;
};

void emit_road(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const DeckFrame& F,
               const RoadStrip& R, const std::vector<float>& ks, const std::function<float(float)>& y,
               const CorridorLook& look) {
    const CorridorHeader& h  = al.header();
    const float           my = h.markingLift, lw = m(diamond::kLine), cw = m(diamond::kCurbW), ch = m(diamond::kCurbH);
    const auto            yt = [&](float t) { return y(t) + diamond::kLiftWu; };  // over the slab top
    const Vec4            tan(glm::normalize(F.et), 1.0f);
    const auto            plan = [&](float a0, float a1, float lift, float tile) {
        for (size_t k = 0; k + 1 < ks.size(); ++k) {
            const float t0 = ks[k], t1 = ks[k + 1];
            Vertex      v[4] = {{F.P(a0, t0, yt(t0) + lift), kUp, Vec2(a0, t0) / tile, tan},
                                {F.P(a1, t0, yt(t0) + lift), kUp, Vec2(a1, t0) / tile, tan},
                                {F.P(a0, t1, yt(t1) + lift), kUp, Vec2(a0, t1) / tile, tan},
                                {F.P(a1, t1, yt(t1) + lift), kUp, Vec2(a1, t1) / tile, tan}};
            Vec3        n    = glm::normalize(glm::cross(v[1].position - v[0].position, v[2].position - v[0].position));
            if (n.y < 0.0f) {  // keep the face up: swap to the other winding
                std::swap(v[0], v[1]);
                std::swap(v[2], v[3]);
                n = -n;
            }
            for (Vertex& q : v) q.normal = n;
            b.push_quad(v[0], v[1], v[2], v[3], QuadWinding::Fan);
        }
    };
    // a slab on the profile, top lift `ty`, bottom `by` below the road surface
    const auto prism = [&](float a0, float a1, float t0, float t1, float by, float ty) {
        for (size_t k = 0; k + 1 < ks.size(); ++k) {
            const float u0 = std::max(t0, ks[k]), u1 = std::min(t1, ks[k + 1]);
            if (u1 - u0 < 1.0f) continue;
            std::array<Vec3, 8> q;
            for (int i = 0; i < 8; ++i) {
                const float t = i & 2 ? u1 : u0;
                q[i]          = F.P(i & 1 ? a1 : a0, t, yt(t) + (i & 4 ? ty : by));
            }
            hexa(b, q);
        }
    };
    const float lo = ks.front(), hi = ks.back();
    if (R.rw > 0.0f)
        batch(b, draws, look.asphalt, MAT_ASPHALT, -1.0f, [&] { plan(-R.rw, R.rw, 0.0f, look.asphaltTile); });
    batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
        if (R.rw <= 0.0f) {  // footway deck: one concrete walk
            plan(-R.A + R.parapetIn, R.A - R.parapetIn, ch, look.concreteTile);
            return;
        }
        for (const float sg : {-1.0f, 1.0f}) {
            const float c0 = sg * R.rw, c1 = sg * (R.rw + cw);
            prism(std::min(c0, c1), std::max(c0, c1), lo, hi, -m(0.3f), ch);  // curb
            if (R.A - R.rw - cw < m(0.5f)) continue;
            // walk: short of the parapet on the deck and wings, out to the slope top beyond
            const float in = sg * (R.rw + cw), mid0 = -R.wingEnd[0], mid1 = R.wingEnd[1];
            for (const auto& [t0, t1, out] :
                 {std::tuple{lo, mid0, R.A}, std::tuple{mid0, mid1, R.A - R.parapetIn}, std::tuple{mid1, hi, R.A}}) {
                const float o = sg * out;
                if (t1 > t0) prism(std::min(in, o), std::max(in, o), t0, t1, -m(0.3f), ch);
            }
        }
    });
    if (R.rw <= 0.0f || R.lanes <= 0) return;
    const float edge = R.rw - m(diamond::kGutter);
    batch(b, draws, look.white, MAT_DEFAULT, -1.0f, [&] {
        for (const float sg : {-1.0f, 1.0f}) {
            const float e0 = sg * (edge - lw), e1 = sg * edge;
            plan(std::min(e0, e1), std::max(e0, e1), my, 4000.0f);
        }
        const int perDir = std::max(1, R.lanes / 2);
        for (int j = 1; j < perDir; ++j)  // lane lines: dashes cut to the knots
            for (const float sg : {-1.0f, 1.0f}) {
                const float c = sg * static_cast<float>(j) * h.laneWidth;
                if (std::abs(c) > edge - h.laneWidth * 0.5f) continue;
                for (float t = lo; t < hi; t += h.dashLength + h.dashGap) {
                    const float t1 = std::min(hi, t + h.dashLength);
                    const Vec3  p0 = F.P(c - 0.5f * lw, t, yt(t) + my), p1 = F.P(c + 0.5f * lw, t, yt(t) + my);
                    const Vec3  p2 = F.P(c - 0.5f * lw, t1, yt(t1) + my), p3 = F.P(c + 0.5f * lw, t1, yt(t1) + my);
                    quad(b, p0, p1, p2, p3, kUp, 4000.0f);
                }
            }
    });
    if (R.lanes >= 2)
        batch(b, draws, look.yellow, MAT_DEFAULT, -1.0f, [&] {
            plan(0.5f * lw, 1.5f * lw, my, 4000.0f);
            plan(-1.5f * lw, -0.5f * lw, my, 4000.0f);
        });
    for (size_t i = draws.size() - (R.lanes >= 2 ? 2 : 1); i < draws.size(); ++i) draws[i].retro = 1.0f;
}

// Footprint and heights for one crossing (shared by corridor_bridges and emit_bridge).
CorridorBridge make_bridge(const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
                           const CorridorCrossing& x, float clearFt) {
    CorridorBridge b;
    b.s           = x.station;
    b.skew        = x.skew;
    b.kind        = static_cast<CrossingKind>(x.kind);
    b.style       = static_cast<BridgeStyle>(x.style);
    b.railUnder   = x.railUnder != 0;
    b.lanes       = x.lanes;
    b.halfA       = 0.5f * x.width / std::cos(b.skew);
    const float k = std::tan(b.skew), E = al.outer_edge(al.sample(b.s)), g = al.ground_y(b.s);
    if (b.kind == CrossingKind::LieOver) {
        float lo = 1e30f;
        for (const float a : {-b.halfA, b.halfA})
            for (const float t : {-E, 0.0f, E}) lo = std::min(lo, al.surface_y(b.s + a + t * k, t));
        const float depth = b.style == BridgeStyle::Stone ? m(rb::kSpandrel) + arch_rise(2.0f * b.halfA)
                                                          : girder_depth() + m(rb::kLieCover);
        b.yLow            = lo - depth - ft(rb::kUnderClearFt);
        const float end   = E + m(0.5f + rb::kLieFlat) + (b.railUnder ? m(40.0f) : m(rb::kLieClimb));
        b.reach[0] = b.reach[1] = end;
    } else {
        float hi = g;
        for (const float a : {-b.halfA, 0.0f, b.halfA})
            for (const float t : {-E, 0.0f, E}) hi = std::max(hi, al.surface_y(b.s + a + t * k, t));
        b.yLow           = hi + ft(clearFt);
        const float seat = b.yLow - m(rb::kBearingH + rb::kCapH) - g;
        for (const int i : {0, 1}) {
            const float o   = sr_outer(al, paths, b.s, i ? +1 : -1);
            const float toe = o > 0.0f ? o + m(rb::kToeClear) : E + m(rb::kClearZone);
            b.reach[i] =
                b.style == BridgeStyle::Stone ? toe : toe + 0.5f * m(rb::kCapW) + rb::kSpill * std::max(0.0f, seat);
        }
    }
    b.half = b.halfA + std::max(b.reach[0], b.reach[1]) * std::abs(k) + m(1.0f);
    return b;
}

}  // namespace

std::vector<CorridorBridge> corridor_bridges(const CorridorAlignment& al, const std::vector<CorridorPath>& paths,
                                             std::vector<CorridorCrossroad>& cross, float clearFt) {
    std::vector<CorridorBridge> out;
    if (!al.has_ends()) return out;
    for (CorridorCrossroad& c : cross) c.style = district_style(al, c.s);
    const double        join = static_cast<double>(m(diamond::kJoin)), pad = static_cast<double>(m(rb::kMergePad));
    const double        lo = al.ends().open0 + static_cast<double>(m(rb::kApproachLen));
    const double        hi = al.ends().taper0 - static_cast<double>(m(rb::kApproachLen));
    std::vector<double> best(cross.size(), 1e30);
    std::vector<CorridorBridge> cand;
    std::vector<float>          width;
    for (const CorridorCrossing& x : al.crossings()) {
        if (std::any_of(std::begin(road::network::kOwnedCrossings), std::end(road::network::kOwnedCrossings),
                        [&](const char* p) { return std::string_view(x.name).starts_with(p); }))
            continue;  // N3: a network road draws this one (the BQE viaduct over the LIE)
        const bool       net = std::any_of(std::begin(road::network::kNetUnder), std::end(road::network::kNetUnder),
                                           [&](const char* p) { return std::string_view(x.name).starts_with(p); });
        CorridorCrossing xc  = x;  // N4: a girder bridge spanning the network road and its C/D roads
        if (net) {
            xc.width = std::max(x.width, 2.0f * m(road::network::kUnderHalf) * std::cos(x.skew));
            xc.style = static_cast<uint8_t>(BridgeStyle::Concrete);
        }
        CorridorBridge b = make_bridge(al, paths, xc, clearFt);
        b.net            = net;
        if (net) b.yLow -= m(road::network::kUnderDig);
        // side slopes of the approach fill reach about 2H past the deck edge, outside the corridor
        const double fp = static_cast<double>(b.half) + (b.kind == CrossingKind::LieOver ? 0.0 : m(15.0f));
        if (b.s - fp < lo || b.s + fp > hi) continue;
        bool merged = false;
        for (size_t i = 0; i < cross.size(); ++i) {
            const CorridorCrossroad& c = cross[i];
            if (b.s + fp + pad < c.s - c.lenBefore - join || b.s - fp - pad > c.s + c.lenAfter + join) continue;
            merged = true;  // inside the diamond: the crossroad is this structure
            if (b.kind == CrossingKind::RoadOver && std::abs(b.s - c.s) < best[i]) {
                best[i]        = std::abs(b.s - c.s);
                cross[i].style = b.style;
            }
        }
        if (merged) continue;
        if (b.kind == CrossingKind::LieOver &&
            (al.dip(b.s - b.half) > 0.0f || al.dip(b.s + b.half) > 0.0f || al.median_extra(b.s - b.half) > 0.0f ||
             al.median_extra(b.s + b.half) > 0.0f))
            continue;  // trench under a cut or a grass median: not modelled
        cand.push_back(b);
        width.push_back(x.width);
    }
    // overlapping footprints: the wider structure stays
    std::vector<size_t> order(cand.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t c) { return width[a] > width[c]; });
    for (const size_t i : order) {
        const CorridorBridge& b  = cand[i];
        const double          fb = static_cast<double>(b.half) + (b.kind == CrossingKind::LieOver ? 0.0 : m(15.0f));
        if (std::none_of(out.begin(), out.end(), [&](const CorridorBridge& o) {
                const double fo = static_cast<double>(o.half) + (o.kind == CrossingKind::LieOver ? 0.0 : m(15.0f));
                return std::abs(o.s - b.s) < fb + fo + pad;
            }))
            out.push_back(b);
    }
    std::sort(out.begin(), out.end(), [](const CorridorBridge& a, const CorridorBridge& c) { return a.s < c.s; });
    return out;
}

float lie_over_flat(const CorridorAlignment& al, const CorridorBridge& br) {
    return al.outer_edge(al.sample(br.s)) + m(0.5f + rb::kLieFlat);
}

std::vector<std::pair<double, double>> bridge_verge_holes(const CorridorAlignment& al, int side) {
    std::vector<std::pair<double, double>> out;
    for (const CorridorBridge& b : al.bridges()) {
        if (b.kind != CrossingKind::LieOver) continue;
        const float E = al.outer_edge(al.sample(b.s)), T = E + al.header().grassExtent, k = std::tan(b.skew);
        const float w = b.halfA + m(rb::kTrenchWall), sg = static_cast<float>(side);
        const float e0 = sg * E * k, e1 = sg * T * k;
        out.push_back({b.s - w + std::min(e0, e1), b.s + w + std::max(e0, e1)});
    }
    return out;
}

void emit_bridge(MeshBuilder& b, std::vector<DrawCall>& draws, const CorridorAlignment& al, const CorridorBridge& br,
                 const std::vector<CorridorPath>& paths, double ox, double oz, const CorridorLook& look) {
    const CorridorHeader& h  = al.header();
    const auto            sm = al.sample(br.s);
    const Vec3            O(static_cast<float>(sm.x - ox), 0.0f, static_cast<float>(sm.z - oz));
    const Vec3            f(sm.tx, 0.0f, sm.tz), r(-sm.tz, 0.0f, sm.tx);
    const float           k = std::tan(br.skew), E = al.outer_edge(sm), g = al.ground_y(br.s);
    const float           T = E + h.grassExtent, A = br.halfA, hw = 0.5f * m(rb::kCapW);
    const bool            stone = br.style == BridgeStyle::Stone;
    const double          sp    = static_cast<double>(h.stationSpacing);
    const float           gt    = (al.ground_y(br.s + sp) - al.ground_y(br.s - sp)) / static_cast<float>(2.0 * sp);

    // grass apron under a road leaving the corridor, 5 WU under the verge it tucks below
    const auto apron = [&](const DeckFrame& F, float a0, float a1, float t0, float t1) {
        quad(b, F.P(a0, t0, g - 5.0f), F.P(a1, t0, g - 5.0f), F.P(a0, t1, g - 5.0f), F.P(a1, t1, g - 5.0f), kUp,
             look.grassTile);
    };

    if (br.kind == CrossingKind::LieOver) {
        // ── LIE over a road: the mainline deck spans the under-road between U-walls ──
        const DeckFrame F{O, r + f * k, f, -sm.bank, gt};  // a across the LIE (along the under-road), t along it
        const float     top0 = al.surface_y(br.s, 0.0f) - m(rb::kLieCover), Ad = E + m(0.45f);
        const float     wt = m(rb::kTrenchWall), dFlat = E + m(0.5f + rb::kLieFlat);
        const float     dEnd = br.reach[0] + (br.railUnder ? 0.0f : m(20.0f) + m(diamond::kStubFade));
        const auto      yu   = [&](float d) {
            if (br.railUnder) return br.yLow;
            return br.yLow + (g - br.yLow) * smooth((d - dFlat) / m(rb::kLieClimb)) + crest(d, dEnd);
        };
        // trench walls stop here; N4: a network road's walls hold the LIE's verge only, down to its trench floor
        const float roadEnd = br.railUnder ? br.reach[0] : br.net ? T : dFlat + m(rb::kLieClimb);
        DeckSpec    dk{F,
                       stone ? BridgeStyle::Concrete : br.style,
                       Ad,
                       top0,
                       top0 - girder_depth(),
                       -(A + 0.5f * wt),
                       A + 0.5f * wt,
                       {},
                       true,
                       false,
                       in_district(al, br.s, "Nassau"),
                       0.0f};
        // service-road decks over the trench
        struct Sr {
            float c, half, top;
        };
        std::vector<Sr> srs;
        for (const CorridorPath& p : paths)
            if (p.kind == CorridorPath::Kind::Service && br.s >= p.s0 && br.s <= p.s1) {
                const PathPoint q = corridor_path_at(al, p, br.s);
                srs.push_back({q.lat, p.halfWidth + m(diamond::kCurbW + 0.45f), q.y - m(rb::kLieCover)});
            }
        const auto sr_frame = [&](const Sr& s) {
            DeckSpec d = dk;
            d.F        = {O + F.ea * s.c, F.ea, F.et, 0.0f, gt};
            d.A        = s.half;
            d.top      = s.top;
            d.soffit   = s.top - girder_depth();
            return d;
        };
        const auto none = [](float) { return 0.0f; };
        if (stone) {
            const float spring = br.yLow + ft(rb::kUnderClearFt);
            emit_arch(b, draws, F, Ad, {-A, A}, spring, top0, -A - wt, A + wt, look);
            for (const Sr& s : srs) {
                const DeckSpec d = sr_frame(s);
                emit_arch(b, draws, d.F, d.A, {-A, A}, spring, d.top, -A - wt, A + wt, look);
            }
        } else {
            if (br.net) {  // N4: piers at the network road's median and past its carriageways, on the trench floor
                const float po = m(road::network::kUnderPier);
                dk.piers       = {-po, 0.0f, po};
                dk.medianHalf  = m(0.6f);
                emit_deck(b, draws, dk, [&](float) { return br.yLow - m(0.5f); }, look);
            } else {
                emit_deck(b, draws, dk, none, look);
            }
            for (const Sr& s : srs) emit_deck(b, draws, sr_frame(s), none, look);
        }

        const DeckFrame Fu{O, F.ea, F.et, 0.0f, 0.0f};  // flat frame: absolute heights
        // U-walls: up to the deck seats under a deck, a 0.3 m coping over the verge elsewhere
        const auto deck_at = [&](float a) -> float {  // seat bottom over lateral a, or -inf
            if (std::abs(a) <= Ad) return stone ? -1e30f : dk.soffit - m(rb::kBearingH + rb::kCapH) - sm.bank * a;
            for (const Sr& s : srs)
                if (std::abs(a - s.c) <= s.half)
                    return stone ? -1e30f : s.top - girder_depth() - m(rb::kBearingH + rb::kCapH);
            return -1e30f;
        };
        std::vector<float> extra{-Ad, Ad};
        for (const Sr& s : srs) extra.insert(extra.end(), {s.c - s.half, s.c + s.half});
        const auto ka     = knots(-roadEnd, roadEnd, m(2.5f), extra);
        const auto top_at = [&](float a, float tw) {
            const float d = deck_at(a);
            if (d > -1e29f) return d;
            if (stone && (std::abs(a) <= Ad || std::any_of(srs.begin(), srs.end(),
                                                           [&](const Sr& s) { return std::abs(a - s.c) <= s.half; })))
                return yu(std::abs(a)) + ft(rb::kUnderClearFt);  // stone: the wall carries the arch springs
            const int   side = a > 0.0f ? 1 : -1;
            const float v    = std::abs(a) <= T ? corridor_verge_y(al, paths, side, br.s + a * k + tw, std::abs(a)) : g;
            return v + m(0.3f);
        };
        batch(b, draws, stone ? kStone : look.concrete, stone ? MAT_STONE : MAT_CONCRETE, -1.0f, [&] {
            for (const float sg : {-1.0f, 1.0f}) {
                const float t0 = sg * A, t1 = sg * (A + wt);
                for (size_t i = 0; i + 1 < ka.size(); ++i) {
                    const float         a0 = ka[i], a1 = ka[i + 1], am = 0.5f * (a0 + a1);
                    const float         d = deck_at(am) > -1e29f ? gt * sg * A : 0.0f;  // seat follows the deck's grade
                    const float         y0 = std::max(top_at(am, sg * A) + d, yu(std::abs(am)) + m(0.3f));
                    std::array<Vec3, 8> q;
                    for (int c = 0; c < 8; ++c) {
                        const float a = c & 1 ? a1 : a0;
                        q[c] = Fu.P(a, c & 2 ? t1 : t0, c & 4 ? y0 : (br.net ? br.yLow : yu(std::abs(a))) - m(0.6f));
                    }
                    hexa(b, q, stone ? m(rb::kStoneTile) : 4000.0f);
                }
            }
            if (br.railUnder)  // end walls close the cut
                for (const float sg : {-1.0f, 1.0f}) {
                    const float a0 = sg * roadEnd, a1 = sg * (roadEnd + wt);
                    box(b, Fu, std::min(a0, a1), std::max(a0, a1), -A - wt, A + wt, br.yLow - m(0.6f), g + m(0.3f));
                }
        });

        // under-road (or track bed)
        const auto ku = knots(-dEnd, dEnd, m(2.5f));
        if (br.railUnder) {
            batch(b, draws, kBallast, MAT_DIRT, -1.0f,
                  [&] { box(b, Fu, -dEnd, dEnd, -A + m(0.3f), A - m(0.3f), br.yLow - m(0.3f), br.yLow, 3000.0f); });
        } else if (!br.net) {  // N4: a network road draws its own
            RoadStrip rs;
            rs.A             = A;
            rs.lanes         = std::min<int>(br.lanes, static_cast<int>(2.0f * A / h.laneWidth));
            const bool walks = br.lanes > 0 && br.lanes <= 5;
            rs.rw            = br.lanes == 0 ? 0.0f : A - (walks ? m(rb::kWalk) + m(diamond::kCurbW) : m(0.6f));
            // road strip runs along a in this frame: swap axes
            const DeckFrame Fs{O, F.et, F.ea, 0.0f, 0.0f};
            emit_road(b, draws, al, Fs, rs, ku, [&](float t) { return yu(std::abs(t)); }, look);
        }
        // verge wedges between the hole's straight cut and the skewed walls, and aprons past the corridor
        batch(b, draws, look.grass, MAT_GRASS, -1.0f, [&] {
            const auto kt = knots(E, T, m(2.5f));
            for (const int side : {-1, +1}) {
                const float  sg = static_cast<float>(side), e0 = sg * E * k, e1 = sg * T * k;
                const double lo = br.s - A - wt + std::min(e0, e1), hi = br.s + A + wt + std::max(e0, e1);
                for (size_t i = 0; i + 1 < kt.size(); ++i) {
                    const float d0 = kt[i], d1 = kt[i + 1];
                    const auto  V = [&](double s, float d) {
                        return cmesh::at(al, s, sg * d, corridor_verge_y(al, paths, side, s, d), ox, oz);
                    };
                    for (const float w : {-1.0f, 1.0f}) {
                        const double c0 = br.s + sg * d0 * k + w * A,
                                     c1 = br.s + sg * d1 * k + w * A;  // wall inner face
                        const double x0 = w < 0.0f ? lo : hi;
                        if (std::abs(x0 - c0) < 1.0 && std::abs(x0 - c1) < 1.0) continue;
                        quad(b, V(x0, d0), V(c0, d0), V(x0, d1), V(c1, d1), kUp, look.grassTile);
                    }
                }
            }
            for (const float w : {-1.0f, 1.0f})  // aprons either side of the trench, past the verge (N4: its own verge)
                for (const float sg : {-1.0f, 1.0f})
                    if (!br.net)
                        apron(Fu, sg * (T - m(3.0f)), sg * (dEnd + m(10.0f)), w * (A + wt),
                              w * (A + m(diamond::kApronHalf)));
        });
        return;
    }

    // ── Road or rail over the LIE ──
    const DeckFrame          F{O, f, r + f * k, 0.0f, 0.0f};
    const bool               rail = br.kind == CrossingKind::RailOver;
    const float              po   = E + m(road::edges::kPierOffset);
    const std::vector<float> piers{-po, 0.0f, po};
    const float              R0 = br.reach[0], R1 = br.reach[1];
    float                    rise = 0.0f;
    if (stone)
        for (const float sp2 : {R0 - po, po}) rise = std::max(rise, arch_rise(sp2));
    const float top  = rail    ? br.yLow + m(rb::kFloorD)
                       : stone ? br.yLow + rise + m(rb::kSpandrel)
                               : br.yLow + girder_depth();
    const float mh   = ft(road::edges::kNjBaseIn / 24.0f) + m(0.01f);  // median pier: NJ toe + 1 cm (D2)
    const auto  base = [&](float t) {
        if (t == 0.0f) return al.surface_y(br.s, 0.0f) - m(1.0f);
        return corridor_verge_y(al, paths, t > 0.0f ? 1 : -1, br.s + t * k, std::abs(t)) - m(0.5f);
    };
    const bool fence = (br.lanes == 0 || !in_district(al, br.s, "Suffolk")) && !rail;
    DeckSpec   dk{F, br.style, A, top, br.yLow, -R0, R1, piers, true, fence, in_district(al, br.s, "Nassau"), mh};

    if (stone) {
        emit_arch(b, draws, F, A, {-R0, -po, 0.0f, po, R1}, br.yLow, top, -R0 - m(rb::kWing), R1 + m(rb::kWing), look);
        batch(b, draws, kStone, MAT_STONE, -1.0f, [&] {
            const float st = m(rb::kStoneTile);
            for (const float p : piers) {  // stone piers up to the springs, full width
                const float w = p == 0.0f ? mh : 0.5f * m(rb::kStonePier);
                box(b, F, -A, A, p - w, p + w, base(p), br.yLow, st);
            }
            for (const float sg : {-1.0f, 1.0f}) {  // full-height abutments and wing walls
                const float R = sg > 0.0f ? R1 : R0, t0 = sg * R, t1 = sg * (R + m(1.0f));
                box(b, F, -A, A, std::min(t0, t1), std::max(t0, t1), g - m(0.5f), top, st);
                for (const float w : {-1.0f, 1.0f}) {
                    const float a0 = w * (A - m(0.4f)), a1 = w * A, u1 = sg * (R + m(rb::kWing));
                    box(b, F, std::min(a0, a1), std::max(a0, a1), std::min(t0, u1), std::max(t0, u1), g - m(0.5f), top,
                        st);
                }
            }
        });
    } else if (rail) {
        // LIRR through-girder: floor between two deep plate girders, bearings and supports as a girder bridge
        DeckSpec d2 = dk;
        d2.A        = A + m(0.6f);
        batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
            box(b, F, -d2.A, d2.A, -R0 - hw, R1 + hw, br.yLow, top);
            supports(b, d2, top, base, false);
        });
        batch(b, draws, kSteelLirr, MAT_METAL, 0.0f, [&] {
            const float H = m(rb::kThroughH);
            for (const float sg : {-1.0f, 1.0f}) {
                const float a = sg * (A + m(0.3f)), w = m(0.025f), fl = m(0.3f);
                box(b, F, a - w, a + w, -R0 - hw, R1 + hw, br.yLow, top + H);
                box(b, F, a - fl, a + fl, -R0 - hw, R1 + hw, top + H - m(0.05f), top + H);
                box(b, F, a - fl, a + fl, -R0 - hw, R1 + hw, br.yLow - m(0.05f), br.yLow);
                const float x0 = a + sg * w, x1 = a + sg * (w + m(0.15f));
                for (float t = -R0; t <= R1; t += m(rb::kThroughStiff))
                    box(b, F, std::min(x0, x1), std::max(x0, x1), t - m(0.012f), t + m(0.012f), br.yLow, top + H);
            }
        });
        batch(b, draws, kBearing, MAT_METAL, 0.0f, [&] {
            for (const float sg : {-1.0f, 1.0f})
                for (const auto& [t0, t1] : spans(d2))
                    for (const float t : {t0 + m(0.25f), t1 - m(0.25f)}) {
                        const float a = sg * (A + m(0.3f));
                        box(b, F, a - m(0.3f), a + m(0.3f), t - m(0.25f), t + m(0.25f), br.yLow - m(0.17f),
                            br.yLow - m(0.05f));
                    }
        });
    } else
        emit_deck(b, draws, dk, base, look);

    // ── Approaches: spill slopes under the deck ends, fill with 1V:2H sides, road or track bed on top ──
    float       dEnd[2], dA[2];
    const float seatB = br.yLow - m(rb::kBearingH + rb::kCapH);
    for (const int i : {0, 1}) {
        const float R = i ? R1 : R0;
        dA[i]         = stone ? R + m(1.0f) : R + hw;
        dEnd[i]       = rail ? dA[i] + m(rb::kRailApproach) + rb::kSpill * (top - g)
                             : dA[i] + m(rb::kApproachFlat + rb::kApproachLen + 20.0f) + m(diamond::kStubFade);
    }
    const auto yr = [&](float d, int i) {  // surface height at |t| = d on side i
        if (rail) {
            const float d1 = dA[i] + m(rb::kRailApproach);
            return d <= d1 ? top : std::max(g, top - (d - d1) / rb::kSpill);
        }
        const float u = (d - dA[i] - m(rb::kApproachFlat)) / m(rb::kApproachLen);
        return top + (g - top) * smooth(u) + crest(d, dEnd[i]);
    };
    batch(b, draws, look.grass, MAT_GRASS, -1.0f, [&] {
        for (const int i : {0, 1}) {
            const float sg = i ? 1.0f : -1.0f, R = i ? R1 : R0;
            const float toe = stone ? R : R - hw - rb::kSpill * std::max(0.0f, seatB - g);
            const auto  hgt = [&](float d) {  // fill height over grade at d
                if (d <= R - hw)
                    return stone ? 0.0f : std::max(0.0f, seatB - g) * (d - toe) / std::max(1.0f, R - hw - toe);
                return std::max(0.0f, yr(d, i) - g);
            };
            const float wingEnd = dA[i] + m(rb::kWing);
            const auto  ks = knots(toe, dEnd[i], m(5.0f), {R - hw, dA[i], wingEnd, dEnd[i] - m(diamond::kStubFade)});
            for (size_t j = 0; j + 1 < ks.size(); ++j) {
                const float d0 = ks[j], d1 = ks[j + 1];
                if (stone && d1 <= wingEnd) continue;  // stone wing walls retain the fill
                for (const float w : {-1.0f, 1.0f}) {
                    const float h0 = hgt(d0), h1 = hgt(d1);
                    const auto  P = [&](float a, float d, float y) { return F.P(a, sg * d, y); };
                    quad(b, P(w * A, d0, g + h0), P(w * A, d1, g + h1), P(w * (A + rb::kSpill * h0), d0, g),
                         P(w * (A + rb::kSpill * h1), d1, g), kUp + F.ea * (0.1f * w), look.grassTile);
                }
                if (rail && d0 >= dA[i] + m(rb::kRailApproach) - 1.0f)  // track bed's end slope
                    quad(b, F.P(-A, sg * d0, g + hgt(d0)), F.P(A, sg * d0, g + hgt(d0)), F.P(-A, sg * d1, g + hgt(d1)),
                         F.P(A, sg * d1, g + hgt(d1)), kUp, look.grassTile);
            }
        }
        for (const int i : {0, 1}) {
            const float sg = i ? 1.0f : -1.0f;
            apron(F, -A - m(diamond::kApronHalf), A + m(diamond::kApronHalf), sg * (T - m(3.0f)),
                  sg * (dEnd[i] + m(10.0f)));
        }
    });
    if (!stone)  // concrete slope paving under the deck ends, wing walls along the fill
        batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
            for (const int i : {0, 1}) {
                const float sg = i ? 1.0f : -1.0f, R = i ? R1 : R0;
                const float toe = R - hw - rb::kSpill * std::max(0.0f, seatB - g);
                quad(b, F.P(-A, sg * toe, g + 5.0f), F.P(A, sg * toe, g + 5.0f), F.P(-A, sg * (R - hw), seatB),
                     F.P(A, sg * (R - hw), seatB), kUp, look.concreteTile);
                for (const float w : {-1.0f, 1.0f}) {
                    const float a0 = w * (A - m(0.3f)), a1 = w * A, t0 = sg * (R - hw),
                                t1 = sg * (dA[i] + m(rb::kWing));
                    box(b, F, std::min(a0, a1), std::max(a0, a1), std::min(t0, t1), std::max(t0, t1), g - m(0.3f),
                        yr(dA[i], i) - m(0.3f));
                }
            }
        });

    // ── Surface over deck and approaches ──
    std::vector<float> ks;
    for (const int i : {0, 1}) {
        const float sg = i ? 1.0f : -1.0f;
        const auto k1 = knots(dA[i], dEnd[i], m(5.0f), {dA[i] + m(rb::kApproachFlat), dEnd[i] - m(diamond::kStubFade)});
        const auto k2 = knots(dEnd[i] - m(diamond::kStubFade), dEnd[i], m(2.5f));
        for (const float d : k1) ks.push_back(sg * d);
        for (const float d : k2) ks.push_back(sg * d);
    }
    std::sort(ks.begin(), ks.end());
    ks.erase(std::unique(ks.begin(), ks.end(), [](float x, float y) { return std::abs(x - y) < 1.0f; }), ks.end());
    if (rail) {
        // ballast, ties and running rails per track, square to the rails
        const Vec3  along = glm::normalize(F.et), across = glm::normalize(glm::cross(kUp, along));
        const float wPerp = 2.0f * A * std::cos(br.skew);
        const int   n     = std::max(1, static_cast<int>(std::lround(wPerp / m(rb::kTrack))));
        const auto  yb    = [&](float t) { return yr(std::abs(t), t > 0.0f ? 1 : 0); };
        const float t0 = -(dA[0] + m(rb::kRailApproach)), t1 = dA[1] + m(rb::kRailApproach);
        batch(b, draws, kBallast, MAT_DIRT, -1.0f,
              [&] { box(b, F, -A + m(0.2f), A - m(0.2f), t0, t1, top - m(0.1f), top + m(0.3f), 3000.0f); });
        batch(b, draws, look.concrete, MAT_CONCRETE, -1.0f, [&] {
            for (int j = 0; j < n; ++j) {
                const float a = -A + (2.0f * A) * (static_cast<float>(j) + 0.5f) / static_cast<float>(n);
                for (float t = t0 + m(0.3f); t < t1; t += m(rb::kTieStep))
                    cmesh::obox(b, F.P(a, t, yb(t) + m(0.33f)), across * (0.5f * m(rb::kTieL)), kUp * m(0.09f),
                                along * m(0.115f));
            }
        });
        batch(b, draws, kRail, MAT_METAL, 0.6f, [&] {
            for (int j = 0; j < n; ++j) {
                const float a = -A + (2.0f * A) * (static_cast<float>(j) + 0.5f) / static_cast<float>(n);
                for (const float sg : {-1.0f, 1.0f}) {
                    const Vec3 off = across * (sg * 0.5f * m(rb::kGauge)), lift = kUp * m(0.5f);
                    rod(b, F.P(a, t0, top) + off + lift, F.P(a, t1, top) + off + lift, m(0.04f));
                }
            }
        });
    } else {
        RoadStrip rs;
        rs.A             = A;
        rs.lanes         = br.lanes;
        const bool walks = br.lanes > 0 && br.lanes <= 5;
        rs.rw            = br.lanes == 0 ? 0.0f : A - (walks ? m(rb::kWalk) + m(diamond::kCurbW) : m(0.6f));
        rs.parapetIn     = stone ? m(0.45f) : inch(15.2f);
        rs.wingEnd[0]    = dA[0] + m(rb::kWing);
        rs.wingEnd[1]    = dA[1] + m(rb::kWing);
        emit_road(b, draws, al, F, rs, ks, [&](float t) { return yr(std::abs(t), t > 0.0f ? 1 : 0); }, look);
    }
}

}  // namespace swish
