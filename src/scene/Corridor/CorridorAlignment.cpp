#include "CorridorAlignment.h"

#include <algorithm>
#include <cmath>

namespace swish {

void CorridorAlignment::load(const std::string& path) { m_asset = load_corridor_asset(path); }

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

    float tx = a.tx + (b.tx - a.tx) * static_cast<float>(u);
    float tz = a.tz + (b.tz - a.tz) * static_cast<float>(u);
    // Lerping unit tangents shortens them; renormalize so downstream
    // heading/lateral math stays unit-scaled.
    const float len = std::sqrt(tx * tx + tz * tz);
    if (len > 1e-6f) {
        out.tx = tx / len;
        out.tz = tz / len;
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

float CorridorAlignment::paint_outer(float lanes) const {
    const CorridorHeader& h = m_asset.header;
    return h.medianHalfWidth + h.innerShoulder + lanes * h.laneWidth;
}

bool CorridorAlignment::in_turnaround(double s) const {
    const double pad = static_cast<double>(m_asset.header.turnaroundLength);
    return s < pad || s > m_asset.header.totalLength - pad;
}

}  // namespace swish
