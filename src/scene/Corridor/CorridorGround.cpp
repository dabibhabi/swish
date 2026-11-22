#include "CorridorGround.h"

#include "CorridorDiamonds.h"
#include "CorridorNetwork.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swish {

namespace {
namespace nw = road::network;
using util::units::m;
constexpr int kFineTile = 32, kCoarseTile = 16;
const float   kNaN = std::numeric_limits<float>::quiet_NaN();
}  // namespace

void CorridorGround::build(const CorridorNetwork& net) {
    *this                        = {};
    m_pave.cell                  = m(50.0f);
    m_dem.cell                   = m(100.0f);
    const CorridorAlignment& lie = net.road(0);

    // Bbox of the network (roads 1..5 and every ramp), then the coarse margin.
    double     x0 = 1e300, z0 = 1e300, x1 = -1e300, z1 = -1e300;
    const auto grow = [&](double x, double z) {
        x0 = std::min(x0, x), z0 = std::min(z0, z), x1 = std::max(x1, x), z1 = std::max(z1, z);
    };
    for (int r = 1; r < static_cast<int>(net.road_count()); ++r)
        for (size_t i = 0; i < net.road(r).header().stationCount; i += 10) {
            const auto sm = net.road(r).sample(static_cast<double>(i) * net.road(r).header().stationSpacing);
            grow(sm.x, sm.z);
        }
    for (const NetRamp& rp : net.ramps())
        for (const NetRampPoint& p : rp.pts) grow(p.x, p.z);
    if (x1 < x0) return;
    const double mg = m(nw::kGroundMargin), cc = m(nw::kGroundCoarse), fc = m(nw::kGroundFine);
    m_x0 = std::floor((x0 - mg) / cc) * cc, m_z0 = std::floor((z0 - mg) / cc) * cc;
    m_coarseN[0]    = static_cast<int>(std::ceil((x1 + mg - m_x0) / cc)),
    m_coarseN[1]    = static_cast<int>(std::ceil((z1 + mg - m_z0) / cc));
    const int ratio = static_cast<int>(std::lround(cc / fc));
    m_fineN[0] = m_coarseN[0] * ratio, m_fineN[1] = m_coarseN[1] * ratio;
    const auto inside = [&](double x, double z) {
        return x > m_x0 - mg && x < m_x0 + m_coarseN[0] * cc + mg && z > m_z0 - mg && z < m_z0 + m_coarseN[1] * cc + mg;
    };

    // Pavement bands and DEM samples, every road station (DEM every third) and ramp point (DEM every fifth).
    m_fine.assign(static_cast<size_t>(m_fineN[0]) * static_cast<size_t>(m_fineN[1]), 0);
    const auto mark = [&](double x, double z) {  // fine cells within kGroundNear
        const double R  = m(nw::kGroundNear);
        const int    i0 = std::max(0, static_cast<int>((x - R - m_x0) / fc)),
                     i1 = std::min(m_fineN[0] - 1, static_cast<int>((x + R - m_x0) / fc));
        const int    j0 = std::max(0, static_cast<int>((z - R - m_z0) / fc)),
                     j1 = std::min(m_fineN[1] - 1, static_cast<int>((z + R - m_z0) / fc));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const double dx = m_x0 + (i + 0.5) * fc - x, dz = m_z0 + (j + 0.5) * fc - z;
                if (dx * dx + dz * dz < R * R) m_fine[static_cast<size_t>(j) * m_fineN[0] + i] = 1;
            }
    };
    double     demSum  = 0.0;
    size_t     demN    = 0;
    const auto add_dem = [&](double x, double z, float y) { m_dem.add({x, z, y}), demSum += y, ++demN; };
    for (int r = 0; r < static_cast<int>(net.road_count()); ++r) {
        const CorridorAlignment& al = net.road(r);
        const float              dg = al.header().grassY - al.header().roadY, ge = al.header().grassExtent;
        const double             sp = al.header().stationSpacing;
        for (size_t i = 0; i < al.header().stationCount; ++i) {
            const double s  = static_cast<double>(i) * sp;
            const auto   sm = al.sample(s);
            if (!inside(sm.x, sm.z) || (r == 0 && al.has_ends() && s < al.ends().portal)) continue;  // not the tube
            const float E = al.outer_edge(sm), lo = std::min(al.surface_y(s, -E), al.surface_y(s, E)) + dg;
            const auto  st     = std::find_if(net.structures(r).begin(), net.structures(r).end(),
                                              [&](const RoadStructure& x) { return s >= x.s0 && s <= x.s1; });
            const bool  via    = st != net.structures(r).end() && !st->trench,
                        trench = st != net.structures(r).end() && st->trench;
            if (r == 0) {  // the LIE: its dipped mainline, then its level verge (ground_y; its D5 far strip is its own)
                const float sk = m(nw::kGroundLieSink);
                m_pave.add({sm.x, sm.z, lo, E, sk, E, 0, true, false});
                m_pave.add({sm.x, sm.z, al.ground_y(s) + dg, E + ge, sk, -1.0f, 0, true, true});
            } else if (via) {  // a viaduct: no band (its deck is high), keep dressing out from under it
                m_pave.add({sm.x, sm.z, lo, E, 0.0f, E, r, false, false});
            } else {  // at grade over its verge; a trench only over its lanes (its walls and top ground are its own)
                m_pave.add({sm.x, sm.z, std::min(lo, al.ground_y(s) + dg), trench ? E : E + ge, m(nw::kGroundSink), E,
                            r, true, !trench});
            }
            if (i % 3 == 0) add_dem(sm.x, sm.z, al.terrain_y(s) + dg);
            if (r > 0 && i % 2 == 0) mark(sm.x, sm.z);
        }
    }
    for (const NetRamp& rp : net.ramps())
        for (size_t i = 0; i < rp.pts.size(); ++i) {
            const NetRampPoint& p  = rp.pts[i];
            const float         hw = rp.halfWidth + m(nw::kParapetW);
            const bool          up = p.y - p.ground > m(nw::kPierMinH);  // on piers: no band
            m_pave.add({p.x, p.z, p.y - std::abs(p.bank) * hw, hw, m(nw::kGroundSink), hw, -1, !up, !up});
            if (i % 5 == 0) add_dem(p.x, p.z, p.ground);
            if (i % 3 == 0) mark(p.x, p.z);
        }
    // D4 roads under the LIE (not network roads): their trench floor, easing to level ground over the approach.
    for (const CorridorBridge& b : lie.bridges()) {
        if (b.kind != CrossingKind::LieOver || b.net) continue;
        const auto  sm = lie.sample(b.s);
        const float hw = b.halfA * std::cos(b.skew), ap = m(road::dressing::kApproach), lv = lie.ground_y(b.s);
        const Vec2  ax(-sm.tz * std::cos(b.skew) - sm.tx * std::sin(b.skew),
                       sm.tx * std::cos(b.skew) - sm.tz * std::sin(b.skew));
        for (int sd = 0; sd < 2; ++sd)
            for (float t = 0.0f; t < b.reach[sd] + ap; t += m(10.0f)) {
                const float  k = std::clamp((t - b.reach[sd]) / ap, 0.0f, 1.0f), tt = sd ? t : -t;
                const double x = sm.x + ax.x * tt, z = sm.z + ax.y * tt;
                m_pave.add({x, z, b.yLow + (lv - b.yLow) * k, hw, m(nw::kGroundLieSink), hw, 0, true, false});
            }
    }
    for (const auto& k : landmark_keepout(net))  // N8: dressing off the landmark footprints (no ground effect)
        m_pave.add({k[0], k[1], 0.0f, 0.0f, 0.0f, static_cast<float>(k[2]), -1, false, false});
    for (const auto& [k, v] : m_pave.m)
        for (const Pave& p : v) m_maxHw = std::max(m_maxHw, p.hw);
    m_demMean = demN ? static_cast<float>(demSum / static_cast<double>(demN)) : 0.0f;

    // Vertex heights: the coarse grid everywhere, the fine one at vertices of drawn cells; rims drop to the coarse.
    const float dc = static_cast<float>(cc * std::sqrt(2.0)), df = static_cast<float>(fc * std::sqrt(2.0));
    m_coarseH.resize(static_cast<size_t>(m_coarseN[0] + 1) * static_cast<size_t>(m_coarseN[1] + 1));
    for (int j = 0; j <= m_coarseN[1]; ++j)
        for (int i = 0; i <= m_coarseN[0]; ++i)
            m_coarseH[static_cast<size_t>(j) * (m_coarseN[0] + 1) + i] =
                eval(m_x0 + i * cc, m_z0 + j * cc, dc) - m(nw::kGroundDrop);
    m_fineH.assign(static_cast<size_t>(m_fineN[0] + 1) * static_cast<size_t>(m_fineN[1] + 1), kNaN);
    for (int j = 0; j <= m_fineN[1]; ++j)
        for (int i = 0; i <= m_fineN[0]; ++i) {
            const int n = fine(i - 1, j - 1) + fine(i, j - 1) + fine(i - 1, j) + fine(i, j);
            if (n == 0) continue;
            m_fineH[static_cast<size_t>(j) * (m_fineN[0] + 1) + i] =
                eval(m_x0 + i * fc, m_z0 + j * fc, df) - (n < 4 ? m(nw::kGroundDrop) : 0.0f);
        }
}

bool CorridorGround::fine(int i, int j) const {
    return i >= 0 && j >= 0 && i < m_fineN[0] && j < m_fineN[1] && m_fine[static_cast<size_t>(j) * m_fineN[0] + i];
}

float CorridorGround::dem(double x, double z) const {
    const double soft = static_cast<double>(m(nw::kGroundSoft)) * m(nw::kGroundSoft);
    for (double R = m(nw::kGroundIdw); R < m(1e4f); R *= 3.0) {  // far from every road: widen the search
        double w = 0.0, wy = 0.0;
        m_dem.near(x, z, R, [&](const Dem& d) {
            const double q = (d.x - x) * (d.x - x) + (d.z - z) * (d.z - z);
            if (q > R * R) return;
            const double k = 1.0 / (q + soft);
            w += k, wy += k * d.y;
        });
        if (w > 0.0) return static_cast<float>(wy / w);
    }
    return m_demMean;
}

float CorridorGround::eval(double x, double z, float diag) const {
    float       lo = -std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::max();
    const float reach = m(nw::kGroundReach);
    m_pave.near(x, z, m_maxHw + diag + reach, [&](const Pave& p) {
        if (!p.bound) return;
        const float d = static_cast<float>(std::hypot(p.x - x, p.z - z)) - p.hw, e = std::max(0.0f, d - diag);
        if (e > reach) return;
        hi = std::min(hi, p.y - p.sink + nw::kGroundCut * e);
        if (p.fill) lo = std::max(lo, p.y - p.sink - nw::kGroundFill * e);
    });
    return std::min(std::max(dem(x, z) - m(nw::kGroundDem), lo), hi);  // cut wins: never over pavement
}

float CorridorGround::tri(const std::vector<float>& h, int nx, int i, int j, float u, float v) {
    const auto  at = [&](int a, int b) { return h[static_cast<size_t>(b) * (nx + 1) + a]; };
    const float a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
    return u + v <= 1.0f ? a + u * (b - a) + v * (c - a) : d + (1.0f - u) * (c - d) + (1.0f - v) * (b - d);
}

float CorridorGround::height(double x, double z) const {
    if (!valid()) return 0.0f;
    const double fc = m(nw::kGroundFine), cc = m(nw::kGroundCoarse);
    const double fx = (x - m_x0) / fc, fz = (z - m_z0) / fc;
    const int    i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fz));
    if (fine(i, j)) return tri(m_fineH, m_fineN[0], i, j, static_cast<float>(fx - i), static_cast<float>(fz - j));
    const double cx = std::clamp((x - m_x0) / cc, 0.0, m_coarseN[0] - 1e-6),
                 cz = std::clamp((z - m_z0) / cc, 0.0, m_coarseN[1] - 1e-6);
    const int    ci = static_cast<int>(cx), cj = static_cast<int>(cz);
    return tri(m_coarseH, m_coarseN[0], ci, cj, static_cast<float>(cx - ci), static_cast<float>(cz - cj));
}

float CorridorGround::clearance(double x, double z, int road) const {
    float best = 1e9f;
    m_pave.near(x, z, m_maxHw + m(nw::kGroundReach), [&](const Pave& p) {
        if (p.kw < 0.0f || (road >= 0 && p.road == road)) return;
        best = std::min(best, static_cast<float>(std::hypot(p.x - x, p.z - z)) - p.kw);
    });
    return best;
}

std::vector<CorridorSegment> CorridorGround::segments(const CorridorLook& look) const {
    std::vector<CorridorSegment> out;
    if (!valid()) return out;
    // One tile of grid (cell c, heights h, nx cells per row) over cells [i0, i0 + T) x [j0, j0 + T) where `draw`.
    const auto tile = [&](const std::vector<float>& h, int nx, int nz, double c, int i0, int j0, int T, auto draw) {
        CorridorSegment seg;
        seg.originX = m_x0 + i0 * c, seg.originZ = m_z0 + j0 * c;
        const auto H = [&](int i, int j) {
            return h[static_cast<size_t>(std::clamp(j, 0, nz)) * (nx + 1) + std::clamp(i, 0, nx)];
        };
        const auto            ok = [&](int i, int j) { return !std::isnan(H(i, j)); };
        MeshData&             md = seg.mesh;
        const int             W = std::min(T, nx - i0), D = std::min(T, nz - j0);
        std::vector<uint32_t> idx(static_cast<size_t>(W + 1) * (D + 1), UINT32_MAX);
        const uint32_t        mk = md.getIndexCount();
        for (int j = 0; j < D; ++j)
            for (int i = 0; i < W; ++i) {
                if (!draw(i0 + i, j0 + j)) continue;
                uint32_t q[4];
                for (int k = 0; k < 4; ++k) {
                    const int a = i + (k & 1), b = j + (k >> 1), ga = i0 + a, gb = j0 + b;
                    uint32_t& v = idx[static_cast<size_t>(b) * (W + 1) + a];
                    if (v == UINT32_MAX) {  // normal from the neighbours that exist (central where both do)
                        const float hx0 = ok(ga - 1, gb) ? H(ga - 1, gb) : H(ga, gb),
                                    hx1 = ok(ga + 1, gb) ? H(ga + 1, gb) : H(ga, gb);
                        const float hz0 = ok(ga, gb - 1) ? H(ga, gb - 1) : H(ga, gb),
                                    hz1 = ok(ga, gb + 1) ? H(ga, gb + 1) : H(ga, gb);
                        const float dx =
                            static_cast<float>(c) * ((ok(ga - 1, gb) ? 1.0f : 0.0f) + (ok(ga + 1, gb) ? 1.0f : 0.0f));
                        const float dz =
                            static_cast<float>(c) * ((ok(ga, gb - 1) ? 1.0f : 0.0f) + (ok(ga, gb + 1) ? 1.0f : 0.0f));
                        const Vec3 n = glm::normalize(
                            Vec3(-(hx1 - hx0) / std::max(dx, 1.0f), 1.0f, -(hz1 - hz0) / std::max(dz, 1.0f)));
                        const float lx = static_cast<float>(a * c), lz = static_cast<float>(b * c);
                        const float ux = static_cast<float>((ga * c) / look.grassTile),
                                    uz = static_cast<float>((gb * c) / look.grassTile);
                        v              = md.getVertexCount();
                        md.addVertex({Vec3(lx, H(ga, gb), lz), n, Vec2(ux, uz), Vec4(1.0f, 0.0f, 0.0f, 1.0f)});
                    }
                    q[k] = v;
                }
                for (const int k : {0, 2, 1, 1, 2, 3}) md.addIndex(q[k]);  // (a c b) (b c d): faces up
            }
        MeshBuilder b(seg.mesh, seg.draws);
        if (!b.close_batch(mk, look.grass, MAT_GRASS)) return;
        Vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
        for (const Vertex& v : md.getVertices()) mn = glm::min(mn, v.position), mx = glm::max(mx, v.position);
        seg.boundsCenter = 0.5f * (mn + mx);
        seg.boundsRadius = 0.5f * glm::length(mx - mn);
        out.push_back(std::move(seg));
    };
    const double fc = m(nw::kGroundFine), cc = m(nw::kGroundCoarse);
    const int    ratio = static_cast<int>(std::lround(cc / fc));
    for (int j = 0; j < m_fineN[1]; j += kFineTile)
        for (int i = 0; i < m_fineN[0]; i += kFineTile)
            tile(m_fineH, m_fineN[0], m_fineN[1], fc, i, j, kFineTile, [&](int a, int b) { return fine(a, b); });
    // coarse cells wholly under drawn fine cells are skipped
    const auto covered = [&](int a, int b) {
        for (int q = 0; q < ratio; ++q)
            for (int p = 0; p < ratio; ++p)
                if (!fine(a * ratio + p, b * ratio + q)) return false;
        return true;
    };
    for (int j = 0; j < m_coarseN[1]; j += kCoarseTile)
        for (int i = 0; i < m_coarseN[0]; i += kCoarseTile)
            tile(m_coarseH, m_coarseN[0], m_coarseN[1], cc, i, j, kCoarseTile,
                 [&](int a, int b) { return !covered(a, b); });
    return out;
}

}  // namespace swish
