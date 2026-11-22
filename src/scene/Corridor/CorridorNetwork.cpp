#include "CorridorNetwork.h"

#include "CorridorBridges.h"
#include "CorridorDiamonds.h"
#include "CorridorSurface.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace swish {

namespace {

namespace nw = road::network;
using util::units::ft;
using util::units::m;

constexpr float kEndMargin = 8000.0f;  // 8 m, as CorridorDriver: the car stops on road, not at the last vertex
constexpr float kLayerH    = 6500.0f;  // N1 ramp heights: DEM ground + layer x 6.5 m

// Box filter of radius k samples, window shrunk symmetric at the ends so the first and last values stay put.
template <class Get, class Set>
void box(size_t n, size_t k, Get get, Set set) {
    std::vector<double> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = get(i);
    for (size_t i = 0; i < n; ++i) {
        const size_t r   = std::min({k, i, n - 1 - i});
        double       sum = 0.0;
        for (size_t j = i - r; j <= i + r; ++j) sum += v[j];
        set(i, sum / static_cast<double>(2 * r + 1));
    }
}

// N3: 14 ft for a one-lane ramp, 12 ft per lane past one (AASHTO), plus the ramp shoulders.
float ramp_half_width(int lanes) {
    const float lw = lanes <= 1 ? nw::kRampLaneFt : 12.0f * static_cast<float>(lanes);
    return 0.5f * ft(lw + nw::kShoulderLFt + nw::kShoulderRFt);
}

// Lateral windows of road `al` at s on `side` (mainline only: the network ignores the LIE's diamonds).
bool span_holds(const CorridorAlignment& al, double s, int side, float lat, float inset) {
    for (const DriveSpan& sp : drive_spans(al, {}, s, side, inset))
        if (lat >= sp.lo && lat <= sp.hi) return true;
    return false;
}

}  // namespace

void CorridorNetwork::load(const std::string& networkPath, const std::string& assetDir, const CorridorAlignment* lie,
                           float underBlend, float hugEase, float maxGrade, float exitGore) {
    m_lie      = nullptr;
    m_hugEase  = hugEase;
    m_maxGrade = maxGrade, m_exitGore = exitGore;
    m_asset = load_corridor_network(networkPath);
    m_roads.assign(m_asset.roads.size(), CorridorAlignment{});
    for (const NetworkRoad& r : m_asset.roads)
        if (r.id != 0 && r.id < m_roads.size()) m_roads[r.id].load(assetDir + r.asset);
    m_lie = lie;
    m_structs.assign(m_roads.size(), {});
    for (size_t r = 1; r < m_roads.size(); ++r) {  // N3: viaducts and trenches draw their own walls and ground
        m_structs[r] = road_structures(m_roads[r]);
        std::vector<std::pair<double, double>> holes;
        for (const RoadStructure& st : m_structs[r]) holes.emplace_back(st.s0, st.s1);
        m_roads[r].set_verge_holes(std::move(holes));
    }
    // N4: a road under a LIE-over bridge runs at the D4 trench floor (yLow) under the deck, then climbs back to its own
    // profile over kLieClimb + underBlend; its junction's ramps under the bridge (the C/D roads) move with it. Before
    // the ramps pin to it.
    for (const NetworkJunction& j : m_asset.junctions)
        for (const CorridorBridge& br : lie->bridges()) {
            if (j.roadA != 0 || j.roadB >= m_roads.size() || !br.net || std::abs(br.s - j.sA) > br.half) continue;
            CorridorAlignment& al  = m_roads[j.roadB];
            const float        k   = std::tan(br.skew);
            const float        sec = std::sqrt(1.0f + k * k);  // metres along the under-road per unit of a
            const float  flat = lie_over_flat(*lie, br) * sec, len = m(road::bridges::kLieClimb) * sec + underBlend;
            const double sp = static_cast<double>(al.header().stationSpacing);
            std::vector<float> dy(al.header().stationCount, 0.0f);
            for (size_t i = 0; i < dy.size(); ++i) {
                const double s = static_cast<double>(i) * sp;
                const float  d = static_cast<float>(std::abs(s - j.sB));
                if (d > flat + len) continue;
                const float own = al.ground_y(s), u = std::clamp((d - flat) / len, 0.0f, 1.0f);
                // N5: a road already under the trench floor (the CIP, 9.8 m under the LIE) rises to it: the piers stand
                // on it
                const float y = br.yLow + (own - br.yLow) * u * u * (3.0f - 2.0f * u);
                dy[i]         = y - own;
                al.set_station_y(i, y - al.road_y());
            }
            const float half = br.halfA * std::cos(br.skew), fall = m(nw::kUnderFall);  // across the road
            for (const NetworkRamp& rp : m_asset.ramps) {
                if (rp.junction != j.id) continue;
                size_t h = 0;
                double s = 0.0;
                float  t = 0.0f;
                al.project_global(m_asset.rampPoints[rp.first].x, m_asset.rampPoints[rp.first].z, h, s, t);
                for (uint32_t q = rp.first; q < rp.first + rp.count; ++q) {
                    NetworkRampPoint& p = m_asset.rampPoints[q];
                    al.project(p.x, p.z, h, s, t);
                    const float w = 1.0f - std::clamp((std::abs(t) - half) / fall, 0.0f, 1.0f);
                    p.y += dy[std::min(dy.size() - 1, static_cast<size_t>(s / sp + 0.5))] * w * w * (3.0f - 2.0f * w);
                }
            }
        }
    // N2b: U-turn gap before each wall, paint only between the barrier faces
    for (const NetworkWall& w : m_asset.walls) {
        if (w.road == 0 || w.road >= m_roads.size()) continue;
        CorridorAlignment& al = m_roads[w.road];
        double             g0 = 0.0, g1 = 0.0;
        wz_gap(w.s, w.end, g0, g1);
        al.add_opening(g0, g1);
        const double f = wz_face(w.s, w.end);
        al.set_paint_range(w.end ? al.paint_from() : f, w.end ? f : al.paint_to());
    }
    build_ramps();
}

void CorridorNetwork::dress(float density, float far, float wallH, float graves) {
    m_ground.build(*this);
    using Spans = CorridorEdges::Spans;
    for (int r = 1; r < static_cast<int>(m_roads.size()); ++r) {
        CorridorAlignment& al = m_roads[static_cast<size_t>(r)];
        const double       sp = al.header().stationSpacing;
        // Where an attached ramp runs beside this road on its level (per side): rails and walls stop there (+ margin),
        // and its verge is cut where the ramp lies over the verge band (the ramp and the ground show, no seam).
        Spans busy[2], cut[2];
        for (const NetRamp& rp : m_ramps) {
            if (rp.from != r && rp.to != r) continue;
            size_t h = 0;
            double s = 0.0;
            float  t = 0.0f;
            al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
            for (const NetRampPoint& p : rp.pts) {
                al.project(p.x, p.z, h, s, t);
                const float in = std::abs(t) - rp.halfWidth, E = al.outer_edge(al.sample(s));
                if (s <= 0.0 || s >= al.total_length() || std::abs(p.y - al.surface_y(s, t)) > m(3.0f)) continue;
                if (in < E + m(8.0f)) busy[t > 0.0f].push_back({s - m(25.0f), s + m(25.0f)});
                if (in < E + al.header().grassExtent && std::abs(p.y - al.surface_y(s, t)) < m(1.5f))
                    cut[t > 0.0f].push_back({s - sp, s + sp});
            }
        }
        for (const int sd : {0, 1}) {
            std::sort(cut[sd].begin(), cut[sd].end());
            Spans merged;  // contiguous runs (cmesh::minus keeps holes as given)
            for (const auto& c : cut[sd])
                if (!merged.empty() && c.first <= merged.back().second)
                    merged.back().second = std::max(merged.back().second, c.second);
                else
                    merged.push_back(c);
            al.set_side_holes(sd ? 1 : -1, std::move(merged));
        }
        CorridorEdges e;
        e.dress = density, e.far = far, e.wallH = wallH, e.graves = graves;
        e.ground = &m_ground, e.netRoad = r;
        // Rails and walls from the LOOK rows, off structures (parapets / retaining walls there) and busy stretches.
        Spans       off  = al.verge_holes();
        const auto& rows = al.look_rows();
        for (size_t k = 0; k < rows.size(); ++k) {
            const auto kind = static_cast<CorridorEdgeKind>(rows[k].edge);
            if (kind == CorridorEdgeKind::None) continue;
            const double a = rows[k].start, z = k + 1 < rows.size() ? rows[k + 1].start : al.total_length();
            for (const int sd : {0, 1}) {
                Spans o = off;
                o.insert(o.end(), busy[sd].begin(), busy[sd].end());
                for (const auto& g : cmesh::minus(a, z, o))
                    if (g.second - g.first >= m(30.0f))
                        (kind == CorridorEdgeKind::Rail ? e.rail : e.wall)[sd].push_back(g);
            }
        }
        al.set_edges(std::move(e));
    }
}

std::vector<WorkZone> CorridorNetwork::work_zones(const WorkZoneParams& p) const {
    std::vector<WorkZone> out;
    for (const NetworkWall& w : m_asset.walls) {
        if (w.road == 0 || w.road >= m_roads.size()) continue;
        double lo = 0.0, hi = 0.0;
        drivable(w.road, lo, hi);
        const double far   = w.end ? lo : hi;
        double       limit = far;  // the transition fits between the wall and the nearest junction on this road
        for (const NetworkJunction& j : m_asset.junctions)
            for (const auto& [r, s] : {std::pair{j.roadA, j.sA}, std::pair{j.roadB, j.sB}})
                if (r == w.road && (w.end ? (s < w.s && s > limit) : (s > w.s && s < limit))) limit = s;
        out.push_back(work_zone(m_roads[w.road], w.road, w.s, w.end, limit, far, p));
    }
    return out;
}

void CorridorNetwork::drivable(int r, double& lo, double& hi) const {
    lo = kEndMargin;
    hi = std::max(lo, road(r).total_length() - kEndMargin);
    for (const NetworkWall& w : m_asset.walls)
        if (w.road == r) (w.end ? hi : lo) = w.s;
}

void CorridorNetwork::build_ramps() {
    m_ramps.clear();
    const float step = m(nw::kStep);
    const auto  nR   = static_cast<int>(m_roads.size());

    // ── 1. End targets: an attached end lands inside its road's carriageway, at its surface ──
    struct End {
        double x = 0.0, z = 0.0;
        float  y   = 0.0f;
        double s   = 0.0;
        bool   set = false, road = false;
    };
    const size_t        n = m_asset.ramps.size();
    std::vector<End>    ends(2 * n);
    std::vector<double> rawX(2 * n), rawZ(2 * n);  // welds are found on the OSM points, before any snap
    for (size_t i = 0; i < n; ++i) {
        const NetworkRamp& r  = m_asset.ramps[i];
        const float        hw = ramp_half_width(r.lanes);
        for (int e = 0; e < 2; ++e) {
            const NetworkRampPoint& p            = m_asset.rampPoints[r.first + (e ? r.count - 1 : 0)];
            End&                    end          = ends[2 * i + static_cast<size_t>(e)];
            end                                  = {p.x, p.z, p.y, 0.0, false, false};
            rawX[2 * i + static_cast<size_t>(e)] = p.x;
            rawZ[2 * i + static_cast<size_t>(e)] = p.z;
            const int rid                        = e ? r.to : r.from;
            if (rid >= nR) continue;
            const CorridorAlignment& al = road(rid);
            size_t                   h  = 0;
            double                   s  = 0.0;
            float                    t  = 0.0f;
            al.project_global(p.x, p.z, h, s, t);
            // N3: an end just past the road's data end (the BQE's west leg into the GCP) joins its last drivable metres
            s = std::clamp(s, static_cast<double>(kEndMargin), al.total_length() - kEndMargin);
            // N3: the carriageway whose travel agrees with the ramp's (a lateral near the median is ambiguous)
            const NetworkRampPoint& q  = m_asset.rampPoints[e ? r.first + r.count - 2 : r.first + 1];
            const auto              s0 = al.sample(s);
            const double            dx = e ? p.x - q.x : q.x - p.x, dz = e ? p.z - q.z : q.z - p.z;
            const int  side  = dx * static_cast<double>(s0.tx) + dz * static_cast<double>(s0.tz) >= 0.0 ? +1 : -1;
            const auto spans = drive_spans(al, {}, s, side, 0.0f);
            if (spans.empty()) continue;  // past the pavement's end: leave the end unattached
            const DriveSpan sp = spans.front();
            const float     lo = sp.lo + hw, hi = sp.hi - hw;
            t             = lo <= hi ? std::clamp(t, lo, hi) : 0.5f * (sp.lo + sp.hi);
            const auto sm = al.sample(s);
            end           = {sm.x - static_cast<double>(t * sm.tz),
                             sm.z + static_cast<double>(t * sm.tx),
                             al.surface_y(s, t) + m(nw::kLift),
                             s,
                             true,
                             true};
        }
    }
    // ── 2. Weld: ends within kWeld share one point; a road-attached member wins, else the mean ──
    std::vector<size_t> root(ends.size());
    std::iota(root.begin(), root.end(), size_t{0});
    const auto find = [&](size_t a) {
        while (root[a] != a) a = root[a] = root[root[a]];
        return a;
    };
    const double weld = static_cast<double>(m(nw::kWeld));
    for (size_t a = 0; a < ends.size(); ++a)
        for (size_t b = a + 1; b < ends.size(); ++b)
            if (std::hypot(rawX[a] - rawX[b], rawZ[a] - rawZ[b]) < weld) root[find(a)] = find(b);
    struct Acc {
        double x = 0.0, z = 0.0, y = 0.0;
        int    k    = 0;
        int    road = -1;  // first road-attached member
    };
    std::vector<Acc> acc(ends.size());
    for (size_t a = 0; a < ends.size(); ++a) {
        Acc& c = acc[find(a)];
        c.x += ends[a].x, c.z += ends[a].z, c.y += static_cast<double>(ends[a].y), ++c.k;
        if (ends[a].road && c.road < 0) c.road = static_cast<int>(a);
    }
    for (size_t a = 0; a < ends.size(); ++a) {
        const Acc& c = acc[find(a)];
        if (c.k < 2 || ends[a].road) continue;
        if (c.road >= 0) {
            const End& t = ends[static_cast<size_t>(c.road)];
            ends[a]      = {t.x, t.z, t.y, 0.0, true, false};
        } else {
            ends[a] = {c.x / c.k, c.z / c.k, static_cast<float>(c.y / c.k), 0.0, true, false};
        }
    }

    // ── 3. Per ramp: shift onto the targets, resample, smooth, pin over the road, blend heights to the ends ──
    for (size_t i = 0; i < n; ++i) {
        const NetworkRamp& r  = m_asset.ramps[i];
        const float        hw = ramp_half_width(r.lanes);
        NetRamp            rp;
        rp.lanes      = std::max<uint8_t>(1, r.lanes);
        const End &e0 = ends[2 * i], &e1 = ends[2 * i + 1];
        // from / to = the road an end really joins (an end past its road's pavement joins nothing)
        rp.from = e0.road ? r.from : 255, rp.to = e1.road ? r.to : 255, rp.junction = r.junction, rp.halfWidth = hw;
        // raw polyline with arc fraction
        std::vector<double> rx, rz, ry, rg, ra{0.0};
        for (uint32_t k = 0; k < r.count; ++k) {
            const NetworkRampPoint& p = m_asset.rampPoints[r.first + k];
            rx.push_back(p.x), rz.push_back(p.z), ry.push_back(p.y);
            rg.push_back(static_cast<double>(p.y) - static_cast<double>(p.layer) * kLayerH);
            if (k) ra.push_back(ra.back() + std::hypot(rx[k] - rx[k - 1], rz[k] - rz[k - 1]));
        }
        const double L = ra.back();
        if (L < step) continue;
        // xz shift eased linearly along the ramp, so the ends land exactly on their targets
        const double d0x = e0.set ? e0.x - rx.front() : 0.0, d0z = e0.set ? e0.z - rz.front() : 0.0;
        const double d1x = e1.set ? e1.x - rx.back() : 0.0, d1z = e1.set ? e1.z - rz.back() : 0.0;
        for (size_t k = 0; k < rx.size(); ++k) {
            const double f = ra[k] / L;
            rx[k] += d0x + (d1x - d0x) * f;
            rz[k] += d0z + (d1z - d0z) * f;
        }
        // linear resample at kStep
        const size_t cnt = std::max<size_t>(2, static_cast<size_t>(std::ceil(L / step)) + 1);
        rp.pts.resize(cnt);
        for (size_t k = 0, seg = 0; k < cnt; ++k) {
            const double u = L * static_cast<double>(k) / static_cast<double>(cnt - 1);
            while (seg + 2 < ra.size() && ra[seg + 1] < u) ++seg;
            const double f =
                ra[seg + 1] > ra[seg] ? std::clamp((u - ra[seg]) / (ra[seg + 1] - ra[seg]), 0.0, 1.0) : 0.0;
            NetRampPoint& q = rp.pts[k];
            q.x             = rx[seg] + (rx[seg + 1] - rx[seg]) * f;
            q.z             = rz[seg] + (rz[seg + 1] - rz[seg]) * f;
            q.y             = static_cast<float>(ry[seg] + (ry[seg + 1] - ry[seg]) * f);
            q.ground        = static_cast<float>(rg[seg] + (rg[seg + 1] - rg[seg]) * f);
        }
        auto& P = rp.pts;
        // OSM node kinks out of the plan; layer steps out of the profile (two passes ~ a triangle filter)
        const size_t kxz = static_cast<size_t>(std::lround(m(nw::kSmoothXZ) / step));
        const size_t ky  = static_cast<size_t>(std::lround(m(nw::kSmoothY) / step));
        for (int pass = 0; pass < 2; ++pass) {
            box(cnt, kxz, [&](size_t k) { return P[k].x; }, [&](size_t k, double v) { P[k].x = v; });
            box(cnt, kxz, [&](size_t k) { return P[k].z; }, [&](size_t k, double v) { P[k].z = v; });
            box(cnt, ky, [&](size_t k) { return double(P[k].y); }, [&](size_t k, double v) { P[k].y = float(v); });
        }
        // arc length + tangents
        for (size_t k = 1; k < cnt; ++k)
            P[k].u = P[k - 1].u + static_cast<float>(std::hypot(P[k].x - P[k - 1].x, P[k].z - P[k - 1].z));
        for (size_t k = 0; k < cnt; ++k) {
            const size_t a = k ? k - 1 : 0, b = std::min(k + 1, cnt - 1);
            const double dx = P[b].x - P[a].x, dz = P[b].z - P[a].z, d = std::max(1e-9, std::hypot(dx, dz));
            P[k].tx = static_cast<float>(dx / d), P[k].tz = static_cast<float>(dz / d);
        }
        // pin: from an attached end inward, while the band lies over its road's pavement, ride its surface
        std::vector<float> ys(cnt);  // the smoothed profile, kept for the anchors below
        for (size_t k = 0; k < cnt; ++k) ys[k] = P[k].y;
        std::vector<char> pinned(cnt, 0);
        size_t            lastPin[2] = {0, cnt - 1};
        for (int e = 0; e < 2; ++e) {
            const End& end = e ? e1 : e0;
            const int  rid = e ? r.to : r.from;
            if (!end.road) continue;
            rp.endS[e]                     = end.s;
            const CorridorAlignment& al    = road(rid);
            size_t                   h     = al.index_at(end.s);
            int                      side0 = 0;
            for (size_t j = 0; j + 2 < cnt; ++j) {  // N5: past half way (a C/D riding its road's lane for 200 m)
                const size_t k = e ? cnt - 1 - j : j;
                if (e && k <= lastPin[0] + 1) break;
                double s = 0.0;
                float  t = 0.0f;
                al.project(P[k].x, P[k].z, h, s, t);
                const int side = t >= 0.0f ? +1 : -1;
                if (j == 0) side0 = side;
                const auto sm = al.sample(s);
                if (side != side0 || std::abs(t) - hw > al.outer_edge(sm)) break;
                if (2 * j >= cnt && std::abs(ys[k] - al.surface_y(s, t)) > m(nw::kPinPastDy))
                    break;  // past half way only while level with it (not a flyover above its lanes)
                // on the road's plane: its height at the ramp centre, its bank turned into the ramp's lateral
                P[k].y     = al.surface_y(s, t) + m(nw::kLift);
                P[k].bank  = sm.bank * (sm.tx * P[k].tx + sm.tz * P[k].tz);
                pinned[k]  = 1;
                lastPin[e] = k;
                rp.pin[e]  = e ? P.back().u - P[k].u : P[k].u;
            }
        }
        // heights between the pins: the smoothed profile plus a correction eased linearly between the two anchors
        const size_t a = lastPin[0], b = lastPin[1];
        if (b > a) {
            // offset at each anchor: pinned road height, or the welded / snapped end height, minus the profile
            const float oa = pinned[a] ? P[a].y - ys[a] : (e0.set ? e0.y - ys[a] : 0.0f);
            const float ob = pinned[b] ? P[b].y - ys[b] : (e1.set ? e1.y - ys[b] : 0.0f);
            for (size_t k = a; k <= b; ++k) {
                if (pinned[k]) continue;
                const float f = (P[k].u - P[a].u) / std::max(1.0f, P[b].u - P[a].u);
                P[k].y        = ys[k] + oa + (ob - oa) * f;
            }
        }
        // bank eases out over kBankEase past each pin (unpinned ramp is level across, as C1's paths)
        for (size_t k = 0; k < cnt; ++k) {
            if (pinned[k]) continue;
            const float da = pinned[a] ? P[k].u - P[a].u : 1e30f, db = pinned[b] ? P[b].u - P[k].u : 1e30f;
            const float wa = std::max(0.0f, 1.0f - da / m(nw::kBankEase)),
                        wb = std::max(0.0f, 1.0f - db / m(nw::kBankEase));
            P[k].bank      = (pinned[a] ? wa * P[a].bank : 0.0f) + (pinned[b] ? wb * P[b].bank : 0.0f);
        }
        rp.minX = rp.minZ = 1e300;
        rp.maxX = rp.maxZ = -1e300;
        for (const NetRampPoint& q : P) {
            rp.minX = std::min(rp.minX, q.x), rp.maxX = std::max(rp.maxX, q.x);
            rp.minZ = std::min(rp.minZ, q.z), rp.maxZ = std::max(rp.maxZ, q.z);
        }
        const double pad = static_cast<double>(hw) + static_cast<double>(m(10.0f));
        rp.minX -= pad, rp.minZ -= pad, rp.maxX += pad, rp.maxZ += pad;
        m_ramps.push_back(std::move(rp));
    }
    clear_ramps();
    grade_ramps();
    // N3: siblings share a start (a fork) or an end (a merge)
    m_siblings.assign(m_ramps.size(), {});
    const auto near = [&](const NetRampPoint& a, const NetRampPoint& b) {
        return std::hypot(a.x - b.x, a.z - b.z) < static_cast<double>(m(1.0f));
    };
    for (size_t a = 0; a < m_ramps.size(); ++a)
        for (size_t b = 0; b < m_ramps.size(); ++b)
            if (a != b && (near(m_ramps[a].pts.front(), m_ramps[b].pts.front()) ||
                           near(m_ramps[a].pts.back(), m_ramps[b].pts.back())))
                m_siblings[a].push_back(static_cast<int>(b));
    // N6: a ramp is dead when no road is reachable past its end; a dead ramp leaving a network road (the LIE keeps its
    // exits), or a dead branch off a live ramp, is a closed local exit: held at its barricades (exit_closure)
    const size_t      R = m_ramps.size();
    std::vector<char> live(R, 0);   // reaches a road: fixpoint over the welds (cloverleaf chains loop)
    for (size_t a = 0; a < R; ++a)  // on a road, or welded onto a ramp's end that is (a merge onto it)
        for (size_t b = 0; !live[a] && b < R; ++b)
            live[a] = m_ramps[b].to != 255 && (b == a || near(m_ramps[b].pts.back(), m_ramps[a].pts.back()));
    for (bool grew = true; grew;) {
        grew = false;
        for (size_t a = 0; a < R; ++a)
            for (size_t b = 0; !live[a] && b < R; ++b)
                if (live[b] && b != a && near(m_ramps[b].pts.front(), m_ramps[a].pts.back())) live[a] = 1, grew = true;
    }
    for (size_t a = 0; a < R; ++a) m_ramps[a].dead = !live[a];
    for (size_t a = 0; a < R; ++a) {
        NetRamp& rp = m_ramps[a];
        if (live[a] || rp.from == 0) continue;
        bool ok = rp.from != 255;  // off a network road
        for (size_t b = 0; !ok && b < R; ++b) ok = b != a && near(m_ramps[b].pts.back(), rp.pts.front()) && live[b];
        if (!ok) continue;
        // the closure stands kExitGore past the gore; a fork branch's 30 m past where its band clears its siblings'
        float u0 = rp.pin[0] + m_exitGore;
        if (rp.from == 255)
            for (const NetRampPoint& q : rp.pts) {
                bool clear = true;
                for (size_t c = 0; clear && c < R; ++c) {
                    if (c == a || !near(m_ramps[c].pts.front(), rp.pts.front())) continue;
                    double u   = 0.0;
                    float  lat = 0.0f;
                    ramp_project(m_ramps[c], q.x, q.z, u, lat);
                    clear = std::abs(lat) > rp.halfWidth + m_ramps[c].halfWidth;
                }
                u0 = q.u + m(30.0f);
                if (clear) break;
            }
        rp.closed = true;
        rp.stop   = std::max(0.0f, std::min(u0, rp.length() - m(5.0f)) - m(road::workzone::kExitHold));
    }
}

void CorridorNetwork::clear_ramps() {
    // N3: where a ramp crosses a road or another ramp of its junction closer than kClear, it is raised over it (or
    // dropped under it when it was below): each point's deficit spreads as a smooth bump of half width kClearSpread
    // and eases to 0 over kClearEase from both ends, so pins and welds keep their heights.
    const float clear = m(nw::kClear), Wc = m(nw::kClearSpread), ease = m(nw::kClearEase);
    const auto  apply = [&](NetRamp& rp, const std::vector<float>& need, float W, float E) {
        auto& P = rp.pts;
        // N5: a welded end moves too (the welds meet again below), only a road pin holds
        const float        a = rp.from != 255 ? rp.pin[0] : -E, b = rp.length() - (rp.to != 255 ? rp.pin[1] : -E);
        std::vector<float> d(P.size(), 0.0f);
        for (size_t k = 0; k < P.size(); ++k) {
            if (need[k] == 0.0f) continue;
            for (size_t i = 0; i < P.size(); ++i) {
                const float f = std::max(0.0f, 1.0f - std::abs(P[i].u - P[k].u) / W), w = f * f * (3.0f - 2.0f * f);
                if (std::abs(need[k] * w) > std::abs(d[i])) d[i] = need[k] * w;
            }
        }
        for (size_t i = 0; i < P.size(); ++i) {
            const float e = std::clamp(std::min(P[i].u - a, b - P[i].u) / E, 0.0f, 1.0f);
            P[i].y += d[i] * e * e * (3.0f - 2.0f * e);
        }
    };
    const auto lift =
        [&](NetRamp& rp) {  // N4: on top of a junction road's grass verge (it sank in: paint over the grass)
            const NetworkJunction& j = m_asset.junctions[rp.junction];
            std::vector<float>     need(rp.pts.size(), 0.0f);
            for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
                const CorridorAlignment& al = road(r);
                size_t                   h  = 0;
                double                   s  = 0.0;
                float                    t  = 0.0f;
                al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
                for (size_t k = 0; k < rp.pts.size(); ++k) {
                    const NetRampPoint& q = rp.pts[k];
                    al.project(q.x, q.z, h, s, t);
                    if ((rp.from == r && q.u <= rp.pin[0] + m(1.0f)) ||
                        (rp.to == r && rp.length() - q.u <= rp.pin[1] + m(1.0f)))
                        continue;  // riding on it
                    const float e = al.outer_edge(al.sample(s)), in = std::abs(t) - rp.halfWidth;
                    if (std::abs(t) < e || in > e + al.header().grassExtent || s <= 0.0 || s >= al.total_length())
                        continue;
                    // N5: a ramp hugging the edge (its band over the shoulder, its centreline off it) rides the
                    // shoulder
                    const int   sd  = t >= 0.0f ? +1 : -1;
                    const float ref = in < e ? al.surface_y(s, static_cast<float>(sd) * std::max(in, 0.0f))
                                             : corridor_verge_y(al, {}, sd, s, in);
                    const float v   = ref + m(nw::kLift) - q.y;
                    if (v < m(nw::kVergeSink)) need[k] = std::max(need[k], v);  // deeper: it passes under (a trench)
                }
            }
            // a short ease: it may not lift an underpass; N5: a short end ease too, so the gore stretch is lifted
            apply(rp, need, m(nw::kClearEase) * 0.5f, m_hugEase);
        };
    for (int it = 0; it < nw::kClearPasses; ++it) {  // N5: the welds meet again (moving crossings): clear again
        for (NetRamp& rp : m_ramps) lift(rp);        // N5: each round: a crossing's bump may sink a shoulder run
        for (NetRamp& rp : m_ramps) {                // roads of its junction it does not ride on
            const NetworkJunction& j = m_asset.junctions[rp.junction];
            std::vector<float>     need(rp.pts.size(), 0.0f);
            for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
                const CorridorAlignment& al = road(r);
                size_t                   h  = 0;
                double                   s  = 0.0;
                float                    t  = 0.0f;
                al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
                std::vector<float> gap(rp.pts.size(), 1e30f);  // ramp over the road's surface, where over its pavement
                for (size_t k = 0; k < rp.pts.size(); ++k) {
                    const NetRampPoint& q = rp.pts[k];
                    al.project(q.x, q.z, h, s, t);
                    if (q.u <= rp.pin[0] + m(1.0f) && rp.from == r) continue;  // riding on it
                    if (rp.length() - q.u <= rp.pin[1] + m(1.0f) && rp.to == r) continue;
                    const auto sm = al.sample(s);
                    if (std::abs(t) > al.outer_edge(sm) + m(1.0f) || s <= 0.0 || s >= al.total_length())
                        continue;  // its centreline over the pavement
                    if (std::abs(q.tx * sm.tx + q.tz * sm.tz) > 0.94f)
                        continue;  // N4: beside it (a C/D road), not across
                    gap[k] = q.y - al.surface_y(s, t);
                }
                // N4: one crossing (a run of points over the pavement) goes wholly over or wholly under, by its mean
                // gap: a per-point choice flipped mid-run and tore the ramp (exit 22's C/D under the LIE)
                for (size_t a = 0, b = 0; a < gap.size(); a = b) {
                    for (b = a + 1; b < gap.size() && (gap[a] < 1e29f) == (gap[b] < 1e29f);) ++b;
                    if (gap[a] >= 1e29f) continue;
                    float mean = 0.0f;
                    for (size_t k = a; k < b; ++k) mean += gap[k] / static_cast<float>(b - a);
                    for (size_t k = a; k < b; ++k) {  // over: raise to clear; under: drop to clear below
                        const float n =
                            mean >= -m(0.5f) ? std::max(0.0f, clear - gap[k]) : std::min(0.0f, -clear - gap[k]);
                        if (std::abs(n) > std::abs(need[k])) need[k] = n;
                    }
                }
            }
            apply(rp, need, Wc, ease);
        }
        // N5: room under a junction road over a ramp point (a 4-level stack): the upper ramp climbs at most this much,
        // the lower one drops by the rest
        const auto headroom = [&](const NetRamp& rp) {
            const NetworkJunction& j = m_asset.junctions[rp.junction];
            std::vector<float>     room(rp.pts.size(), 1e30f);
            for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
                const CorridorAlignment& al = road(r);
                size_t                   h  = 0;
                double                   s  = 0.0;
                float                    t  = 0.0f;
                al.project_global(rp.pts.front().x, rp.pts.front().z, h, s, t);
                for (size_t k = 0; k < rp.pts.size(); ++k) {
                    al.project(rp.pts[k].x, rp.pts[k].z, h, s, t);
                    if (s <= 0.0 || s >= al.total_length() || std::abs(t) > al.outer_edge(al.sample(s)) + rp.halfWidth)
                        continue;
                    const float above = al.surface_y(s, t) - clear - rp.pts[k].y;
                    if (above > -clear) room[k] = std::min(room[k], std::max(0.0f, above));  // a road over it
                }
            }
            return room;
        };
        std::vector<std::vector<float>> drop(m_ramps.size());
        for (size_t i = 0; i < m_ramps.size(); ++i) drop[i].assign(m_ramps[i].pts.size(), 0.0f);
        for (size_t ia = 0; ia < m_ramps.size(); ++ia) {  // other ramps: the upper one of each crossing climbs
            NetRamp&                 A = m_ramps[ia];
            std::vector<float>       need(A.pts.size(), 0.0f);
            const std::vector<float> room = headroom(A);
            for (size_t ib = 0; ib < m_ramps.size(); ++ib) {
                const NetRamp& B = m_ramps[ib];
                if (ib == ia || B.junction != A.junction || A.maxX < B.minX || A.minX > B.maxX || A.maxZ < B.minZ ||
                    A.minZ > B.maxZ)
                    continue;
                for (size_t k = 0; k < A.pts.size(); ++k) {
                    const NetRampPoint& q = A.pts[k];
                    if (q.u < m(30.0f) || A.length() - q.u < m(30.0f)) continue;  // near its welds and gores
                    double u   = 0.0;
                    float  lat = 0.0f;
                    ramp_project(B, q.x, q.z, u, lat);
                    if (u < m(30.0) || u > B.length() - m(30.0f) || std::abs(lat) > B.halfWidth + m(1.0f)) continue;
                    const size_t i = std::min(B.pts.size() - 1, static_cast<size_t>(u / m(nw::kStep)));
                    if (std::abs(q.tx * B.pts[i].tx + q.tz * B.pts[i].tz) > 0.94f)
                        continue;  // side by side, not a crossing
                    const float g = q.y - ramp_y(B, u);
                    if (g < 0.0f || g >= clear) continue;
                    const float up  = std::min(clear - g, room[k]);
                    need[k]         = std::max(need[k], up);
                    const size_t bi = std::min(B.pts.size() - 1, static_cast<size_t>(u / m(nw::kStep) + 0.5));
                    drop[ib][bi]    = std::min(drop[ib][bi], up - (clear - g));
                }
            }
            apply(A, need, Wc, ease);
        }
        for (size_t i = 0; i < m_ramps.size(); ++i) apply(m_ramps[i], drop[i], Wc, ease);
        // N5: welds meet again at their mean height, each member eased back over kClearEase from its end
        struct End {
            size_t r;
            int    e;
        };
        std::vector<End> ends;
        for (size_t i = 0; i < m_ramps.size(); ++i)
            for (const int e : {0, 1})
                if ((e ? m_ramps[i].to : m_ramps[i].from) == 255) ends.push_back({i, e});
        const auto at = [&](const End& q) -> NetRampPoint& {
            return q.e ? m_ramps[q.r].pts.back() : m_ramps[q.r].pts.front();
        };
        std::vector<char> done(ends.size(), 0);
        for (size_t g = 0; g < ends.size(); ++g) {
            if (done[g]) continue;
            std::vector<size_t> grp;
            for (size_t h = g; h < ends.size(); ++h)
                if (!done[h] && std::hypot(at(ends[h]).x - at(ends[g]).x, at(ends[h]).z - at(ends[g]).z) < m(1.0f))
                    grp.push_back(h), done[h] = 1;
            float mean = 0.0f;
            for (const size_t h : grp) mean += at(ends[h]).y / static_cast<float>(grp.size());
            for (const size_t h : grp) {
                NetRamp&    rp = m_ramps[ends[h].r];
                const float dy = mean - at(ends[h]).y;
                for (NetRampPoint& q : rp.pts) {
                    const float d = ends[h].e ? rp.length() - q.u : q.u, w = std::clamp(1.0f - d / ease, 0.0f, 1.0f);
                    q.y += dy * w * w * (3.0f - 2.0f * w);
                }
            }
        }
    }
}

void CorridorNetwork::grade_ramps() {
    // N6: clear_ramps leaves 50-90 % over 12 m next to pins and welds. Every profile is re-solved as the target clamped
    // into a band [lo, hi] that is g-Lipschitz along the ramp graph (points joined along each ramp and at every weld,
    // so a weld is one shared height and a climb spreads along the whole chain): road pins fixed; clearance over /
    // under roads and the shoulder as one-sided bounds (never worse than now, never more than needed); a ramp crossing
    // another is bounded only once the solve took its clearance (the lower one capped, the upper one floored).
    const float clear = m(nw::kClear), ease = m(nw::kClearEase);
    const float clearPk  = ft(nw::kParkwayClearFt) + m(nw::kDeck);  // N7: cars only above or below
    namespace rb         = road::bridges;
    const float lieDepth = m(rb::kSlab + rb::kHaunch + rb::kGirderD + rb::kLieCover);
    const auto  parkway  = [](int r) {
        return std::find(std::begin(nw::kParkways), std::end(nw::kParkways), r) != std::end(nw::kParkways);
    };
    const size_t       R = m_ramps.size();
    std::vector<float> g(R, m_maxGrade);  // per ramp: raised only where the band is empty (tight compact junctions)
    using Field = std::vector<std::vector<float>>;
    Field y0(R), lo0(R), hi0(R);
    for (size_t i = 0; i < R; ++i) {
        const size_t n = m_ramps[i].pts.size();
        y0[i].resize(n), lo0[i].assign(n, -1e30f), hi0[i].assign(n, 1e30f);
        for (size_t k = 0; k < n; ++k) y0[i][k] = m_ramps[i].pts[k].y;
    }
    // weld groups: free ends (no road) within 1 m, as clear_ramps
    std::vector<std::vector<std::pair<size_t, size_t>>> welds;  // (ramp, point index)
    {
        std::vector<std::pair<size_t, size_t>> ends;
        for (size_t i = 0; i < R; ++i)
            for (const int e : {0, 1})
                if ((e ? m_ramps[i].to : m_ramps[i].from) == 255) ends.emplace_back(i, e ? y0[i].size() - 1 : 0);
        std::vector<char> done(ends.size(), 0);
        for (size_t a = 0; a < ends.size(); ++a) {
            if (done[a]) continue;
            const NetRampPoint& pa = m_ramps[ends[a].first].pts[ends[a].second];
            welds.emplace_back();
            for (size_t b = a; b < ends.size(); ++b) {
                const NetRampPoint& pb = m_ramps[ends[b].first].pts[ends[b].second];
                if (!done[b] && std::hypot(pa.x - pb.x, pa.z - pb.z) < m(1.0f))
                    welds.back().push_back(ends[b]), done[b] = 1;
            }
        }
    }
    const auto floor_at = [&](size_t i, size_t k, float v) { lo0[i][k] = std::max(lo0[i][k], std::min(y0[i][k], v)); };
    const auto ceil_at  = [&](size_t i, size_t k, float v) { hi0[i][k] = std::min(hi0[i][k], std::max(y0[i][k], v)); };
    struct Cross {
        size_t a, k, b, q;  // ramp a's point k stands at least c over ramp b's point q
        float  c;
    };
    std::vector<Cross> cross;
    struct Edge {
        size_t i, k;
        float  ref;    // its road's pavement edge height under the band
        bool   hug;    // band over the shoulder (or its grass verge), centreline off the pavement
        bool   verge;  // over the grass verge (N4's lift as a floor), not its pavement
    };
    std::vector<Edge>                      edges;
    std::vector<std::pair<size_t, size_t>> runs;  // edges of one (ramp, road)
    for (size_t i = 0; i < R; ++i) {
        const NetRamp& rp = m_ramps[i];
        const auto&    P  = rp.pts;
        const size_t   n  = P.size();
        for (size_t k = 0; k < n; ++k)  // riding its road (the attached end itself included)
            if ((rp.from != 255 && P[k].u <= rp.pin[0]) || (rp.to != 255 && rp.length() - P[k].u <= rp.pin[1]))
                lo0[i][k] = hi0[i][k] = y0[i][k];
        const NetworkJunction& j = m_asset.junctions[rp.junction];
        for (const int r : {static_cast<int>(j.roadA), static_cast<int>(j.roadB)}) {
            const CorridorAlignment& al = road(r);
            size_t                   h  = 0;
            double                   s  = 0.0;
            float                    t  = 0.0f;
            al.project_global(P.front().x, P.front().z, h, s, t);
            const size_t first = edges.size();
            for (size_t k = 0; k < n; ++k) {
                al.project(P[k].x, P[k].z, h, s, t);
                if (s <= 0.0 || s >= al.total_length()) continue;
                const auto  sm = al.sample(s);
                const float e = al.outer_edge(sm), in = std::abs(t) - rp.halfWidth;
                const int   sd = t >= 0.0f ? +1 : -1;
                const bool  vg = in >= e;  // over the grass verge (none in a structure's hole: it drops off there)
                if (vg && (in > e + al.header().grassExtent ||
                           std::any_of(al.verge_holes().begin(), al.verge_holes().end(),
                                       [&](const auto& hl) { return s > hl.first && s < hl.second; })))
                    continue;
                const float ref     = vg ? corridor_verge_y(al, {}, sd, s, in)
                                         : al.surface_y(s, static_cast<float>(sd) * std::clamp(in, 0.0f, std::abs(t)));
                const auto  merging = [&](float d) {  // within d of where it rides this road
                    return (rp.from == r && P[k].u <= rp.pin[0] + d) ||
                           (rp.to == r && rp.length() - P[k].u <= rp.pin[1] + d);
                };
                const float ry = al.surface_y(s, t);
                if (!vg && std::abs(t) <= e && std::abs(P[k].tx * sm.tx + P[k].tz * sm.tz) <= 0.94f && !merging(ease)) {
                    if (y0[i][k] >= ry - m(0.5f))
                        floor_at(i, k, ry + (parkway(r) ? clearPk : clear));  // over its pavement: keep the clearance
                    else
                        ceil_at(i, k,
                                ry - (!parkway(rp.from) && !parkway(rp.to) ? clear
                                      : r == 0 ? ft(nw::kParkwayClearFt) + lieDepth  // the LIE's D4 girders
                                               : clearPk));
                }
                // N4's verge lift starts 1 m past the pin; the shoulder lift kHugEase past it
                edges.push_back({i, k, ref, std::abs(t) > e && !merging(vg ? m(1.0f) : m_hugEase), vg});
            }
            if (edges.size() > first) runs.emplace_back(first, edges.size());
        }
        for (size_t ib = 0; ib < R; ++ib) {  // ramps of its junction it passes over (clear_ramps' crossing rule)
            const NetRamp& B = m_ramps[ib];
            if (ib == i || B.junction != rp.junction || rp.maxX < B.minX || rp.minX > B.maxX || rp.maxZ < B.minZ ||
                rp.minZ > B.maxZ)
                continue;
            for (size_t k = 0; k < n; ++k) {
                if (P[k].u < m(30.0f) || rp.length() - P[k].u < m(30.0f)) continue;
                double u   = 0.0;
                float  lat = 0.0f;
                ramp_project(B, P[k].x, P[k].z, u, lat);
                if (u < m(30.0) || u > B.length() - m(30.0f) || std::abs(lat) > B.halfWidth + m(1.0f)) continue;
                const auto   it = std::lower_bound(B.pts.begin(), B.pts.end(), static_cast<float>(u),
                                                   [](const NetRampPoint& p, float v) { return p.u < v; });
                const size_t q  = std::min(B.pts.size() - 1, static_cast<size_t>(it - B.pts.begin()));
                if (std::abs(P[k].tx * B.pts[q].tx + P[k].tz * B.pts[q].tz) > 0.94f) continue;
                const float gap = y0[i][k] - y0[ib][q];
                const float cl  = parkway(B.from) || parkway(B.to) ? clearPk : clear;   // N7: cars only below
                if (gap >= m(1.0f)) cross.push_back({i, k, ib, q, std::min(cl, gap)});  // level: a merge, not over
            }
        }
    }
    // closure along the graph: up = least g-Lipschitz majorant (v >= v_nb - g d), else greatest minorant
    const auto close = [&](Field& v, bool up) {
        const auto pull = [&](float& a, float b, float d) {
            const float c  = up ? std::max(a, b - d) : std::min(a, b + d);
            const bool  ch = std::abs(c - a) > 0.5f;  // 0.5 mm
            a              = c;
            return ch;
        };
        for (int it = 0, ch = 1; ch && it < 400; ++it) {
            ch = 0;
            for (size_t i = 0; i < R; ++i) {
                const auto& P = m_ramps[i].pts;
                for (size_t k = 1; k < P.size(); ++k) ch |= pull(v[i][k], v[i][k - 1], g[i] * (P[k].u - P[k - 1].u));
                for (size_t k = P.size() - 1; k-- > 0;) ch |= pull(v[i][k], v[i][k + 1], g[i] * (P[k + 1].u - P[k].u));
            }
            for (const Cross& c : cross)
                ch |= up ? pull(v[c.a][c.k], v[c.b][c.q] + c.c, 0.0f) : pull(v[c.b][c.q], v[c.a][c.k] - c.c, 0.0f);
            for (const auto& w : welds) {
                float e = v[w[0].first][w[0].second];
                for (const auto& [i, k] : w) e = up ? std::max(e, v[i][k]) : std::min(e, v[i][k]);
                for (const auto& [i, k] : w) ch |= pull(v[i][k], e, 0.0f);
            }
        }
    };
    Field      y, lo, hi;
    const auto solve = [&] {
        for (int it = 0; it < 200; ++it) {  // a ramp whose band is empty somewhere gets 2 % more grade, until none is
            lo = lo0, hi = hi0;
            close(lo, true), close(hi, false);
            bool any = false;
            for (size_t i = 0; i < R; ++i)
                for (size_t k = 0; k < lo[i].size(); ++k)
                    if (lo[i][k] > hi[i][k] + 1.0f) {
                        g[i] *= 1.02f, any = true;
                        break;
                    }
            if (!any) break;
        }
        // target clamped into the band; mean of its minorant and majorant (both stay in the band, both g-Lipschitz)
        const auto fit = [&] {
            for (size_t i = 0; i < R; ++i)
                for (size_t k = 0; k < y[i].size(); ++k)
                    y[i][k] = std::clamp(y[i][k], lo[i][k], std::max(lo[i][k], hi[i][k]));
            Field a = y, b = y;
            close(a, false), close(b, true);
            for (size_t i = 0; i < R; ++i)
                for (size_t k = 0; k < y[i].size(); ++k) y[i][k] = 0.5f * (a[i][k] + b[i][k]);
        };
        y = y0;
        fit();
        const size_t ks = static_cast<size_t>(std::lround(m(nw::kGradeSmooth) / m(nw::kStep)));
        for (size_t i = 0; i < R; ++i)  // round the grade breaks (vertical curves); ends stay put
            box(
                y[i].size(), ks, [&](size_t k) { return double(y[i][k]); },
                [&](size_t k, double v) { y[i][k] = float(v); });
        fit();
    };
    // a ramp beside a road rides on its shoulder, unless it dives 2 m or more under it nearby (an underpass approach);
    // the dive is judged on the solved profile, so solve again while new floors appear
    const auto shoulders = [&](const Field& yr) {
        bool added = false;
        for (const auto& [a, b] : runs) {
            std::vector<float> deep;
            for (size_t q = a; q < b; ++q)  // under its pavement: an underpass
                if (!edges[q].verge && edges[q].ref - yr[edges[q].i][edges[q].k] >= m(nw::kVergeSink))
                    deep.push_back(m_ramps[edges[q].i].pts[edges[q].k].u);
            for (size_t q = a; q < b; ++q) {
                const Edge& e = edges[q];
                const float u = m_ramps[e.i].pts[e.k].u, f = e.ref + m(nw::kLift);
                const bool  deepNear = std::any_of(deep.begin(), deep.end(),
                                                   [&](float d) { return std::abs(d - u) < m(nw::kClearSpread); });
                if (!e.hug || deepNear) continue;
                if (e.verge) {  // N4: a shallow sink into the grass verge is lifted, as a target (grade wins)
                    if (e.ref - y0[e.i][e.k] < m(nw::kVergeSink)) y0[e.i][e.k] = std::max(y0[e.i][e.k], f);
                    continue;
                }
                if (f <= lo0[e.i][e.k] + m(0.01f)) continue;
                lo0[e.i][e.k] = f, added = true;
            }
        }
        return added;
    };
    shoulders(y0);
    solve();
    for (int it = 0; it < 4 && shoulders(y); ++it) solve();
    for (size_t i = 0; i < R; ++i)
        for (size_t k = 0; k < y[i].size(); ++k) m_ramps[i].pts[k].y = y[i][k];
}

void CorridorNetwork::ramp_project(const NetRamp& rp, double x, double z, double& u, float& lat) const {
    const auto&  P    = rp.pts;
    double       best = 1e300;
    const size_t last = P.size() - 2;
    for (size_t i = 0; i <= last; ++i) {
        const double dx = P[i + 1].x - P[i].x, dz = P[i + 1].z - P[i].z, l2 = std::max(1e-9, dx * dx + dz * dz);
        double       f = ((x - P[i].x) * dx + (z - P[i].z) * dz) / l2;
        if (i > 0) f = std::max(f, 0.0);  // the end segments extend along their tangents
        if (i < last) f = std::min(f, 1.0);
        const double px = P[i].x + f * dx, pz = P[i].z + f * dz, d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
        if (d2 >= best) continue;
        best           = d2;
        const double l = std::sqrt(l2);
        u              = static_cast<double>(P[i].u) + f * l;
        lat            = static_cast<float>(((x - px) * -dz + (z - pz) * dx) / l);  // right of travel = (-tz, tx)
    }
}

float CorridorNetwork::ramp_y(const NetRamp& rp, double u) const {
    const auto&  P  = rp.pts;
    const float  uf = std::clamp(static_cast<float>(u), 0.0f, rp.length());
    const auto   it = std::upper_bound(P.begin(), P.end(), uf, [](float v, const NetRampPoint& q) { return v < q.u; });
    const size_t i  = std::clamp<size_t>(static_cast<size_t>(it - P.begin()), 1, P.size() - 1);
    const float  f  = (uf - P[i - 1].u) / std::max(1e-3f, P[i].u - P[i - 1].u);
    return P[i - 1].y + (P[i].y - P[i - 1].y) * f;
}

float CorridorNetwork::ramp_surface(const NetRamp& rp, double u, float lat) const {
    const auto&  P = rp.pts;
    const size_t i = std::min(P.size() - 1, static_cast<size_t>(std::max(0.0, u) / m(nw::kStep) + 0.5));
    return ramp_y(rp, u) - P[i].bank * lat;
}

float CorridorNetwork::ramp_grade(const NetRamp& rp, double u) const {
    const double h = static_cast<double>(m(nw::kStep));
    return (ramp_y(rp, u + h) - ramp_y(rp, u - h)) / static_cast<float>(2.0 * h);
}

// N3: ramps are one-way: the travel tangent of o at arc length u, dotted with the car's heading (hx, hz).
float heading_dot(const NetRamp& o, double u, float hx, float hz) {
    const size_t i = std::min(o.pts.size() - 1, static_cast<size_t>(std::max(0.0, u) / m(nw::kStep)));
    return o.pts[i].tx * hx + o.pts[i].tz * hz;
}

int CorridorNetwork::enter_ramp(int road, double x, double z, float y, float inset, NetPos& out, float hx,
                                float hz) const {
    int   best = -1;
    float bdy  = m(nw::kHandoffDy);
    for (size_t k = 0; k < m_ramps.size(); ++k) {
        const NetRamp& rp = m_ramps[k];
        if ((rp.from != road && rp.to != road) || x < rp.minX || x > rp.maxX || z < rp.minZ || z > rp.maxZ) continue;
        // N2b: a ramp that joins this road past a work-zone wall is closed
        double lo = 0.0, hi = 0.0;
        drivable(road, lo, hi);
        const double js = rp.from == road ? rp.endS[0] : rp.endS[1];
        if (js < lo || js > hi) continue;
        double u   = 0.0;
        float  lat = 0.0f;
        ramp_project(rp, x, z, u, lat);
        const float dy = std::abs(ramp_surface(rp, u, lat) - y);
        if (u < 0.0 || u > rp.end() || std::abs(lat) > rp.halfWidth - inset || dy > bdy ||
            heading_dot(rp, u, hx, hz) < 0.0f)
            continue;
        bdy  = dy;
        best = static_cast<int>(k);
        out  = NetPos{road, best, u, lat, +1, 0};
    }
    return best;
}

NetStep CorridorNetwork::step(const NetPos& cur, double x, double z, float y, float inset) const {
    NetStep r;
    r.pos = cur;
    r.x   = x;
    r.z   = z;
    // The surface of whichever piece r.pos ends on, at (r.x, r.z).
    const auto finish = [&] {
        if (r.pos.ramp >= 0) {
            const NetRamp& rp = m_ramps[static_cast<size_t>(r.pos.ramp)];
            const float    g  = ramp_grade(rp, r.pos.s);
            const size_t   i  = std::min(rp.pts.size() - 1, static_cast<size_t>(r.pos.s / m(nw::kStep)));
            const float    bk = rp.pts[i].bank;
            r.y               = ramp_surface(rp, r.pos.s, r.pos.lat);
            r.dydx            = g * rp.pts[i].tx + bk * rp.pts[i].tz;  // grade along, bank across (as surface_slope)
            r.dydz            = g * rp.pts[i].tz - bk * rp.pts[i].tx;
        } else {
            const CorridorAlignment& al = road(r.pos.road);
            r.y                         = al.surface_y(r.pos.s, r.pos.lat);
            al.surface_slope(r.pos.s, r.dydx, r.dydz);
        }
        return r;
    };

    // ── on a road ──
    if (cur.ramp < 0) {
        const CorridorAlignment& al = road(cur.road);
        NetPos&                  p  = r.pos;
        al.project(x, z, p.hint, p.s, p.lat);
        if (al.in_turnaround(p.s)) p.side = p.lat >= 0.0f ? +1 : -1;  // N2b: a U-turn gap re-commits the side
        double lo = 0.0, hi = 0.0;
        drivable(cur.road, lo, hi);
        if (p.s >= lo && p.s <= hi && span_holds(al, p.s, p.side, p.lat, inset)) return finish();
        NetPos     q;
        const auto sh = al.sample(p.s);  // heading: along the committed carriageway
        if (enter_ramp(cur.road, x, z, y, inset, q, sh.tx * static_cast<float>(p.side),
                       sh.tz * static_cast<float>(p.side)) >= 0) {
            r.pos = q;
            return finish();
        }
        // clamp: walls along, nearest window across
        const double sc = std::clamp(p.s, lo, hi);
        float        lc = p.lat, snap = 1e30f;
        for (const DriveSpan& sp : drive_spans(al, {}, sc, p.side, inset)) {
            const float c = std::clamp(p.lat, sp.lo, sp.hi);
            if (std::abs(c - p.lat) < snap) snap = std::abs(c - p.lat), lc = c;
        }
        r.held    = sc != p.s;
        r.clamped = lc != p.lat;
        p.s = sc, p.lat = lc;
        const auto sm = al.sample(sc);
        r.x           = sm.x - static_cast<double>(lc * sm.tz);
        r.z           = sm.z + static_cast<double>(lc * sm.tx);
        return finish();
    }

    // ── on a ramp ──
    const NetRamp& rp  = m_ramps[static_cast<size_t>(cur.ramp)];
    double         u   = 0.0;
    float          lat = 0.0f;
    ramp_project(rp, x, z, u, lat);
    const float lim = rp.halfWidth - inset;
    if (u >= 0.0 && u <= rp.end() && std::abs(lat) <= lim) {
        r.pos.s   = u;
        r.pos.lat = lat;
        // N3: at a fork (or merge) the branch whose centreline is nearer takes the car, while both are level
        for (const int k : m_siblings[static_cast<size_t>(cur.ramp)]) {
            const NetRamp& o  = m_ramps[static_cast<size_t>(k)];
            double         ou = 0.0;
            float          ol = 0.0f;
            ramp_project(o, x, z, ou, ol);
            if (ou < 0.0 || ou > o.end() || std::abs(ol) > o.halfWidth - inset ||
                std::abs(ol) > std::abs(r.pos.lat) - m(nw::kForkBias) ||
                std::abs(ramp_surface(o, ou, ol) - y) > m(nw::kHandoffDy) ||
                heading_dot(o, ou, rp.pts[std::min(rp.pts.size() - 1, static_cast<size_t>(u / m(nw::kStep)))].tx,
                            rp.pts[std::min(rp.pts.size() - 1, static_cast<size_t>(u / m(nw::kStep)))].tz) < 0.0f)
                continue;
            r.pos = NetPos{cur.road, k, ou, ol, +1, 0};
        }
        return finish();
    }
    // Hand off: a road at either end, or another ramp, holding the car within kHandoffDy of its height.
    const size_t ih = std::min(
        rp.pts.size() - 1, static_cast<size_t>(std::clamp(u, 0.0, static_cast<double>(rp.length())) / m(nw::kStep)));
    const float hx = rp.pts[ih].tx, hz = rp.pts[ih].tz;  // the car's heading: this ramp's travel direction
    float       bdy  = m(nw::kHandoffDy);
    bool        hit  = false;
    const bool  past = rp.closed && u > rp.end();  // N6: at a closed exit's barricades: held
    for (int e = 0; e < 2 && !past; ++e) {
        const int rid = e ? rp.to : rp.from;
        // N5: only the road at the end the car leaves by (a C/D beside its from-road welds on over its shoulder)
        if (rid >= static_cast<int>(m_roads.size()) || (u >= 0.5 * static_cast<double>(rp.length())) != (e == 1))
            continue;
        const CorridorAlignment& al = road(rid);
        NetPos                   q{rid, -1, 0.0, 0.0f, +1, al.index_at(rp.endS[e])};
        al.project(x, z, q.hint, q.s, q.lat);
        q.side    = q.lat >= 0.0f ? +1 : -1;
        double lo = 0.0, hi = 0.0;
        drivable(rid, lo, hi);
        const float dy = std::abs(al.surface_y(q.s, q.lat) - y);
        if (q.s < lo || q.s > hi || dy > bdy || !span_holds(al, q.s, q.side, q.lat, inset)) continue;
        bdy = dy, hit = true, r.pos = q;
    }
    for (size_t k = 0; !hit && k < m_ramps.size(); ++k) {  // a road that holds the car wins (a merge lands on it)
        const NetRamp& o = m_ramps[k];
        if (static_cast<int>(k) == cur.ramp || x < o.minX || x > o.maxX || z < o.minZ || z > o.maxZ) continue;
        if (past && o.dead) continue;  // N6: not on through a closed exit (a live fork sibling may take the car back)
        double ou = 0.0;
        float  ol = 0.0f;
        ramp_project(o, x, z, ou, ol);
        const float dy = std::abs(ramp_surface(o, ou, ol) - y);
        if (ou < 0.0 || ou > o.end() || std::abs(ol) > o.halfWidth - inset || dy > bdy ||
            heading_dot(o, ou, hx, hz) < 0.0f)
            continue;
        bdy = dy, hit = true, r.pos = NetPos{cur.road, static_cast<int>(k), ou, ol, +1, 0};
    }
    if (hit) return finish();
    // clamp to this ramp: across to its lane window, along to its ends (a dead end holds the car)
    const double uc = std::clamp(u, 0.0, static_cast<double>(rp.end()));
    const float  lc = std::clamp(lat, -lim, lim);
    r.held          = uc != u;
    r.clamped       = lc != lat;
    r.pos.s = uc, r.pos.lat = lc;
    const auto&  P  = rp.pts;
    const auto   it = std::upper_bound(P.begin(), P.end(), static_cast<float>(uc),
                                       [](float v, const NetRampPoint& q) { return v < q.u; });
    const size_t i  = std::clamp<size_t>(static_cast<size_t>(it - P.begin()), 1, P.size() - 1);
    const double sl = std::max(1e-3, static_cast<double>(P[i].u - P[i - 1].u));
    const double f  = (uc - static_cast<double>(P[i - 1].u)) / sl;
    const double dx = (P[i].x - P[i - 1].x) / sl, dz = (P[i].z - P[i - 1].z) / sl;
    r.x = P[i - 1].x + (P[i].x - P[i - 1].x) * f - static_cast<double>(lc) * dz;
    r.z = P[i - 1].z + (P[i].z - P[i - 1].z) * f + static_cast<double>(lc) * dx;
    return finish();
}

NetPos CorridorNetwork::place(int r, double s) const {
    const CorridorAlignment& al = road(r);
    double                   lo = 0.0, hi = 0.0;
    drivable(r, lo, hi);
    NetPos p;
    p.road = r;
    p.s    = std::clamp(s, lo, hi);
    p.lat  = al.paint_outer(al.sample(p.s)) - 0.5f * al.header().laneWidth;
    p.hint = al.index_at(p.s);
    return p;
}

ExitSite net_exit_site(const CorridorNetwork& net, int r, double s, int side) {
    const CorridorAlignment& al = net.road(r);
    ExitSite                 x;
    double                   bd = 1e300;
    for (const NetRamp& rp : net.ramps()) {
        const double d = (rp.endS[0] - s) * side;  // downstream of the record (its node is the link's start)
        if (rp.from != r || d < -m(200.0f) || d > m(900.0f) || std::abs(d) >= bd) continue;
        double ps = 0.0;
        float  pt = 0.0f;
        size_t h  = al.index_at(rp.endS[0]);
        al.project(rp.pts.front().x, rp.pts.front().z, h, ps, pt);
        if ((pt >= 0.0f ? 1 : -1) != side) continue;
        // N6: a left exit runs toward the median 40 m past its pin
        const auto& q = rp.pts[std::min(rp.pts.size() - 1, static_cast<size_t>((rp.pin[0] + m(40.0f)) / m(nw::kStep)))];
        double      qs = 0.0;
        float       qt = 0.0f;
        al.project(q.x, q.z, h, qs, qt);
        bd = std::abs(d), x.gore = rp.endS[0] + static_cast<double>(side) * static_cast<double>(rp.pin[0]);
        x.closed = rp.closed, x.left = std::abs(qt) < std::abs(pt) - m(1.0f);
    }
    return x;
}

CorridorSigns network_signs(const CorridorNetwork& net, int r) {
    // N3 guide signs: gores where the network's exit ramps leave this road, junction anchors at their stations
    std::vector<std::pair<std::string, double>> jn;
    for (const NetworkJunction& j : net.asset().junctions)
        if (j.roadA == r || j.roadB == r) jn.emplace_back(j.name, j.roadA == r ? j.sA : j.sB);
    const CorridorAlignment& al     = net.road(r);
    const auto               exitAt = [&](double s, int side) { return net_exit_site(net, r, s, side); };
    const auto               goreAt = [&](double s, int side) {
        const ExitSite x = exitAt(s, side);
        return x.gore >= 0.0 ? x.gore : s + static_cast<double>(side) * m(nw::kGoreAfter);
    };
    const auto clearOf = [&](double s, int side) {  // N6: outer extent of this road's ramps beside station s
        const auto sm = al.sample(s);
        float      c  = 0.0f;
        for (const NetRamp& rp : net.ramps()) {
            if ((rp.from != r && rp.to != r) || sm.x < rp.minX - m(50.0) || sm.x > rp.maxX + m(50.0) ||
                sm.z < rp.minZ - m(50.0) || sm.z > rp.maxZ + m(50.0))
                continue;
            size_t h = al.index_at(s);
            for (const NetRampPoint& q : rp.pts) {
                double ps = 0.0;
                float  pt = 0.0f;
                al.project(q.x, q.z, h, ps, pt);
                if (std::abs(ps - s) < m(8.0) && pt * static_cast<float>(side) > 0.0f &&
                    std::abs(pt) - rp.halfWidth < al.outer_edge(sm) + m(road::signs::kPostOut + 1.0f))  // alongside
                    c = std::max(c, std::abs(pt) + rp.halfWidth);
            }
        }
        return c;
    };
    CorridorSigns signs;
    add_guide_signs(signs, al, r, goreAt, jn);
    double lo = 0.0, hi = 0.0;
    net.drivable(r, lo, hi);
    add_exit_signs(signs, al, r, lo, hi, exitAt, clearOf);  // N6
    return signs;
}

}  // namespace swish
