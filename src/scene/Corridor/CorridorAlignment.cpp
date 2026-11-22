#include "CorridorAlignment.h"

#include "scene/RoadScene/RoadParams.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

void CorridorAlignment::load(const std::string& path) { m_asset = load_corridor_asset(path); }

const CorridorExit* CorridorAlignment::next_exit(double s, bool westbound) const {
    const CorridorExit* best = nullptr;
    // Linear scan: about 100 exits, called once a frame at most (HUD).
    for (const CorridorExit& e : m_asset.exits) {
        if ((e.westbound != 0) != westbound) continue;
        if (!westbound && e.station >= s) return &e;  // sorted ascending: first one ahead
        if (westbound && e.station <= s) best = &e;   // last one at or below s
    }
    return best;
}

const CorridorDistrict* CorridorAlignment::district_at(double s) const {
    const CorridorDistrict* out = nullptr;
    for (const CorridorDistrict& d : m_asset.districts)
        if (d.start <= s || out == nullptr) out = &d;
    return out;
}

Pavement CorridorAlignment::surface_at(double s) const {
    Pavement out = Pavement::Legacy;
    for (const CorridorPavement& p : m_asset.pavement)  // 8 rows; sorted, so the last start <= s wins
        if (p.start <= s || out == Pavement::Legacy) out = static_cast<Pavement>(p.kind);
    return out;
}

float CorridorAlignment::dip(double s) const {
    if (m_cross.empty() || m_dipHalf <= 0.0f) return 0.0f;
    const auto it  = std::lower_bound(m_cross.begin(), m_cross.end(), s,
                                      [](const CorridorCrossroad& c, double v) { return c.s < v; });
    float      out = 0.0f;
    // Spacing >= 900 m and dipHalf <= 800 m: only the two bracketing crossroads can reach s.
    for (auto c = (it == m_cross.begin() ? it : it - 1); c != m_cross.end() && c <= it; ++c) {
        const double u = std::abs(s - c->s) / static_cast<double>(m_dipHalf);
        if (u < 1.0) out = std::max(out, c->depth * static_cast<float>(1.0 - u * u * (3.0 - 2.0 * u)));
    }
    return out;
}

size_t CorridorAlignment::index_at(double s) const {
    const double sp = static_cast<double>(m_asset.header.stationSpacing);
    const double i  = std::floor(s / sp);
    if (i <= 0.0) return 0;
    const size_t last = m_asset.stations.size() - 1;
    return std::min(static_cast<size_t>(i), last);
}

CorridorAlignment::Sample CorridorAlignment::sample(double s) const {
    Sample out;
    if (!valid()) return out;

    const double sp   = static_cast<double>(m_asset.header.stationSpacing);
    const size_t last = m_asset.stations.size() - 1;
    const size_t i    = index_at(s);
    const size_t j    = std::min(i + 1, last);
    // Fractional position inside the station interval. Clamped, so querying
    // past either end pins to the terminal station instead of extrapolating.
    const double u = (i == j) ? 0.0 : std::clamp((s - static_cast<double>(i) * sp) / sp, 0.0, 1.0);

    const CorridorStation& a = m_asset.stations[i];
    const CorridorStation& b = m_asset.stations[j];

    out.x     = a.x + (b.x - a.x) * u;
    out.z     = a.z + (b.z - a.z) * u;
    out.lanes = a.lanes + (b.lanes - a.lanes) * static_cast<float>(u);
    out.y     = a.y + (b.y - a.y) * static_cast<float>(u);
    out.bank  = a.bank + (b.bank - a.bank) * static_cast<float>(u);
    out.med   = m_asset.header.medianHalfWidth + median_extra(s);

    float tx = a.tx + (b.tx - a.tx) * static_cast<float>(u);
    float tz = a.tz + (b.tz - a.tz) * static_cast<float>(u);
    // Lerping unit tangents shortens them; renormalize so downstream
    // heading/lateral math stays unit-scaled.
    const float len = std::sqrt(tx * tx + tz * tz);
    if (len > 1e-6f) {
        out.tx = tx / len;
        out.tz = tz / len;
    }
    // C4: plaza widening and the east lane drop live in the lane count, so mesh, verge, paint and bounds follow
    if (has_ends()) {
        const auto ease = [](double a, double b, double v) {
            const double u = std::clamp((v - a) / (b - a), 0.0, 1.0);
            return static_cast<float>(u * u * (3.0 - 2.0 * u));
        };
        const CorridorEnds& e = m_ends;
        if (s > e.plaza0 && s < e.plaza3)
            out.lanes +=
                e.plazaLanes * (s < e.plaza2 ? ease(e.plaza0, e.plaza1, s) : 1.0f - ease(e.plaza2, e.plaza3, s));
        if (s > e.taper0) {
            const float t = ease(e.taper0, e.taper1, s);
            out.lanes -= t;        // one lane drops
            out.bank *= 1.0f - t;  // superelevation runs out: the roundabout is level across
        }
    }
    return out;
}

// Point-to-segment projection over the station index window [lo, hi].
// `lateral` is the signed offset along right-of-travel = (-tz, tx), which is
// the same convention MeshBuilder uses (cross(tangent, up)).
static void project_window(const std::vector<CorridorStation>& st, double sp, size_t lo, size_t hi, double px,
                           double pz, size_t& bestI, double& bestS, float& bestLat) {
    double bestD2 = 1e300;
    bestI         = lo;
    bestS         = static_cast<double>(lo) * sp;
    bestLat       = 0.0f;

    for (size_t i = lo; i < hi; ++i) {
        const double ax = st[i].x, az = st[i].z;
        const double bx = st[i + 1].x, bz = st[i + 1].z;
        const double ex = bx - ax, ez = bz - az;
        const double e2 = ex * ex + ez * ez;
        if (e2 < 1e-9) continue;
        const double u  = std::clamp(((px - ax) * ex + (pz - az) * ez) / e2, 0.0, 1.0);
        const double qx = ax + ex * u, qz = az + ez * u;
        const double d2 = (px - qx) * (px - qx) + (pz - qz) * (pz - qz);
        if (d2 < bestD2) {
            bestD2 = d2;
            bestI  = i;
            bestS  = (static_cast<double>(i) + u) * sp;
            // Signed side, from the segment's own direction.
            const double il = 1.0 / std::sqrt(e2);
            const double tx = ex * il, tz = ez * il;
            bestLat = static_cast<float>((px - ax) * -tz + (pz - az) * tx);
        }
    }
}

void CorridorAlignment::project(double x, double z, size_t& hint, double& s, float& lateral) const {
    if (!valid()) {
        s       = 0.0;
        lateral = 0.0f;
        return;
    }
    const double sp   = static_cast<double>(m_asset.header.stationSpacing);
    const size_t last = m_asset.stations.size() - 1;
    // A window of +/-64 stations is +/-640 m at the shipped 10 m spacing —
    // far more than a frame's travel even at 200 mph (~90 m/s), so the hint
    // can never fall out of the window between frames.
    constexpr size_t kWindow = 64;
    const size_t     lo      = (hint > kWindow) ? hint - kWindow : 0;
    const size_t     hi      = std::min(hint + kWindow, last);
    project_window(m_asset.stations, sp, lo, hi, x, z, hint, s, lateral);
}

void CorridorAlignment::project_global(double x, double z, size_t& hint, double& s, float& lateral) const {
    if (!valid()) {
        s       = 0.0;
        lateral = 0.0f;
        return;
    }
    project_window(m_asset.stations, static_cast<double>(m_asset.header.stationSpacing), 0, m_asset.stations.size() - 1,
                   x, z, hint, s, lateral);
}

float CorridorAlignment::inner_edge(float) const { return m_asset.header.medianHalfWidth; }

float CorridorAlignment::outer_edge(float lanes) const {
    const CorridorHeader& h = m_asset.header;
    return h.medianHalfWidth + h.innerShoulder + lanes * h.laneWidth + h.outerShoulder;
}

float CorridorAlignment::paint_inner(float) const {
    return m_asset.header.medianHalfWidth + m_asset.header.innerShoulder;
}

float CorridorAlignment::outer_edge(const Sample& sm) const {
    const CorridorHeader& h = m_asset.header;
    return sm.med + h.innerShoulder + sm.lanes * h.laneWidth + h.outerShoulder;
}

float CorridorAlignment::paint_inner(const Sample& sm) const { return sm.med + m_asset.header.innerShoulder; }

float CorridorAlignment::paint_outer(const Sample& sm) const {
    const CorridorHeader& h = m_asset.header;
    return sm.med + h.innerShoulder + sm.lanes * h.laneWidth;
}

float CorridorAlignment::median_extra(double s) const {
    const CorridorEdges& e = m_edges;
    if (e.wideExtra <= 0.0f || s <= e.wide0 - e.ease || s >= e.wide1 + e.ease) return 0.0f;
    const double u = s < e.wide0   ? (s - e.wide0 + e.ease) / e.ease
                     : s > e.wide1 ? (e.wide1 + e.ease - s) / e.ease
                                   : 1.0;
    return e.wideExtra * static_cast<float>(u * u * (3.0 - 2.0 * u));
}

float CorridorAlignment::rail_face(double s, int side) const {
    for (const auto& [a, b] : m_edges.rail[side > 0 ? 1 : 0])
        if (s >= a && s <= b) return outer_edge(sample(s)) + util::units::m(road::edges::kRailOffset);
    return 0.0f;
}

float CorridorAlignment::paint_outer(float lanes) const {
    const CorridorHeader& h = m_asset.header;
    return h.medianHalfWidth + h.innerShoulder + lanes * h.laneWidth;
}

bool CorridorAlignment::in_turnaround(double s) const {
    // C4: the plaza opening and the roundabout replace the end pads
    if (has_ends()) return (s >= m_ends.padW0 && s <= m_ends.padW1) || s >= m_ends.ringC - m_ends.ringRo;
    const double pad = static_cast<double>(m_asset.header.turnaroundLength);
    return s < pad || s > m_asset.header.totalLength - pad;
}

}  // namespace swish
