#include "CorridorWorkZone.h"

#include "CorridorNetwork.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

namespace {

namespace wz = road::workzone;
using util::units::ft;
using util::units::m;

// Item at (s, lat) on a road facing `faceDir` x tangent; past the data ends it runs on along the end tangent.
WzItem on_road(const CorridorAlignment& al, WzKind k, double s, float lat, int faceDir) {
    const double sc = std::clamp(s, 0.0, al.total_length()), ds = s - sc;
    const auto   sm = al.sample(sc);
    WzItem       it;
    it.kind = k, it.s = s, it.lat = lat;
    it.x = sm.x + static_cast<double>(sm.tx) * ds - static_cast<double>(lat * sm.tz);
    it.z = sm.z + static_cast<double>(sm.tz) * ds + static_cast<double>(lat * sm.tx);
    // on the verge the graded ground, else the carriageway plane
    it.y  = std::abs(lat) > al.outer_edge(sm) ? corridor_verge_y(al, {}, lat >= 0.0f ? 1 : -1, sc, std::abs(lat))
                                              : al.surface_y(sc, lat);
    it.fx = static_cast<float>(faceDir) * sm.tx, it.fz = static_cast<float>(faceDir) * sm.tz;
    return it;
}

}  // namespace

float wz_buffer_ft(float mph) {
    const float f = std::clamp((mph - 45.0f) / 5.0f, 0.0f, 5.0f);
    const int   i = std::min(4, static_cast<int>(f));
    return wz::kBufferFt[i] + (wz::kBufferFt[i + 1] - wz::kBufferFt[i]) * (f - static_cast<float>(i));
}

void wz_gap(double wall, int end, double& g0, double& g1) {
    const double dir = end ? 1.0 : -1.0, a = wall - dir * m(wz::kGapClear),
                 b = wall - dir * m(wz::kGapClear + wz::kGapLen);
    g0 = std::min(a, b), g1 = std::max(a, b);
}

double wz_face(double wall, int end) { return wall + (end ? 1.0 : -1.0) * m(wz::kFaceBack); }

WorkZone work_zone(const CorridorAlignment& al, int road, double wall, int end, double limit, double far,
                   const WorkZoneParams& p) {
    WorkZone z;
    z.road = road, z.end = end, z.dir = end ? 1 : -1, z.wall = wall, z.face = wz_face(wall, end);
    const int   dir          = z.dir;
    const float sg           = static_cast<float>(dir);
    const auto  S            = [&](double u) { return wall - static_cast<double>(dir) * u; };
    const auto  sm           = al.sample(S(m(150.0)));
    const int   N            = std::clamp(static_cast<int>(std::lround(sm.lanes)), 1, 4);
    z.lanes                  = N;
    const CorridorHeader& h  = al.header();
    const float           lw = h.laneWidth, pi = al.paint_inner(sm), po = al.paint_outer(sm), med = h.medianHalfWidth;
    const float           outer = al.outer_edge(sm), Sm = wz::kSpeedMph;

    // ── Transition: L = W S per closed lane, 2L tangents, then the buffer, squeezed only if the link is short ──
    const double L0 = ft(lw / ft(1.0f) * Sm), B0 = ft(wz_buffer_ft(Sm));
    const double gapFar = m(wz::kGapClear + wz::kGapLen), gapNear = m(wz::kGapClear);
    const int    nT   = (N - 1) + 2 * std::max(0, N - 2);  // in units of L: tapers plus 2L tangents
    const double need = B0 + nT * L0, avail = std::abs(wall - limit) - m(wz::kRampClear);
    z.k = gapFar + need > avail ? std::max<float>(wz::kTaperMinK, static_cast<float>((avail - gapFar) / need)) : 1.0f;
    z.L = static_cast<float>(L0) * z.k;
    z.buffer          = static_cast<float>(B0) * z.k;
    const double uEnd = gapFar + z.buffer, T = uEnd + nT * static_cast<double>(z.L);
    z.taper0 = S(T);
    wz_gap(wall, end, z.gap0, z.gap1);

    // Channelizers on a straight run in (u, lat), at most `spacingFt` / drums apart, end point excluded.
    const auto line = [&](WzKind k, double u0, float l0, double u1, float l1, float spacingFt) {
        const double sp = ft(spacingFt) / std::max(0.1f, p.drums);
        const int    n  = std::max(1, static_cast<int>(std::ceil(std::abs(u0 - u1) / sp - 1e-9)));
        for (int i = 0; i < n; ++i) {
            const double f = static_cast<double>(i) / n;
            z.items.push_back(on_road(al, k, S(u0 + (u1 - u0) * f), l0 + (l1 - l0) * static_cast<float>(f), -dir));
        }
    };
    const auto   edge = [&](int lanesOpen) { return sg * (pi + static_cast<float>(lanesOpen) * lw); };
    const double L    = z.L;
    for (int k = 0; k < N - 1; ++k) {  // taper k closes lane N - k (right lanes first, toward the median gap)
        const double uk = T - 3.0 * k * L;
        line(WzKind::Drum, uk, edge(N - k), uk - L, edge(N - k - 1), Sm);  // MUTCD 6K.01: S ft in a taper
        if (k < N - 2) line(WzKind::Drum, uk - L, edge(N - k - 1), uk - 3.0 * L, edge(N - k - 1), 2.0f * Sm);
        WzItem a = on_road(al, WzKind::Arrow, S(uk),
                           k == 0 ? sg * (po + 0.5f * h.outerShoulder) : sg * (pi + (N - k + 0.5f) * lw), -dir);
        z.items.push_back(a);  // MUTCD 6L.06: shoulder at the first taper, then in the closed lane
    }
    line(WzKind::Drum, uEnd, edge(1), gapFar, edge(1), wz::kNySpaceFt);               // buffer, NYSDOT 40 ft
    line(WzKind::Drum, gapFar, edge(1), gapNear, sg * (med + m(0.4f)), wz::kDownFt);  // into the crossover
    z.items.push_back(on_road(al, WzKind::Drum, S(gapNear), sg * (med + m(0.4f)), -dir));

    // ── Advance signs at the freeway A / B / C spacing before the taper (dropped past the other drivable end) ──
    const double room = std::abs(far - wall) - m(10.0);
    const double uA = T + p.spacing * ft(wz::kSignAFt), uB = uA + p.spacing * ft(wz::kSignBFt),
                 uC   = uB + p.spacing * ft(wz::kSignCFt);
    const float  sl   = sg * (outer + m(2.0f));
    const auto   sign = [&](double u, WzFace f, float lat, int faceDir) {
        if (u > room) return;
        WzItem it = on_road(al, WzKind::Sign, S(u), lat, faceDir);
        it.face   = f;
        z.items.push_back(it);
    };
    sign(uC, WzFace::RoadWork, sl, -dir);
    sign(0.5 * (uB + uC), WzFace::Speeding, sl, -dir);  // NYSDOT 619: NYR9-11 at the start of the zone
    if (N >= 2) {
        sign(uB, static_cast<WzFace>(static_cast<int>(WzFace::LaneClosed1) + std::min(N - 2, 2)), sl, -dir);
        sign(uA, WzFace::LaneEnds, sl, -dir);
    }
    sign(T, WzFace::EndRoadWork, -sl, dir);  // G20-2 for the way back, on the other carriageway

    // ── Closure: barricades with R11-2 past the hold, cushion, full-width barrier wall ──
    const float cw = outer - med - m(0.6f);
    const int   nb = std::max(1, static_cast<int>(cw / (wz::kBarricadeW + m(0.15f))));
    for (int i = 0; i < nb; ++i) {
        WzItem it = on_road(al, WzKind::Barricade, S(-m(3.0)),
                            sg * (med + m(0.3f) + (static_cast<float>(i) + 0.5f) * cw / static_cast<float>(nb)), -dir);
        it.len    = wz::kBarricadeW;
        it.face   = i == nb / 2 ? WzFace::RoadClosed : WzFace::None;  // MUTCD 6G.04: R11-2 at the centre
        z.items.push_back(it);
    }
    const float cl = sg * (med + 0.5f * (outer - med));
    for (int r = 0; r < 3; ++r)  // sand barrels: 1, 2, 3 toward the wall
        for (int j = 0; j <= r; ++j)
            z.items.push_back(on_road(al, WzKind::Barrel, S(-m(wz::kFaceBack - 0.55f - static_cast<float>(2 - r))),
                                      cl + sg * m(static_cast<float>(j) - 0.5f * static_cast<float>(r)), -dir));
    {
        // segments of at most 20 ft spanning shoulder edge to shoulder edge (NYSDOT 619-01 allows 8-20 ft)
        const double half = 0.5 * ft(wz::kTcbBaseIn / 12.0f), w = 2.0 * (outer + m(0.3f));
        const double sw  = S(-m(wz::kFaceBack) - half);
        const int    n   = static_cast<int>(std::ceil(w / ft(wz::kTcbSegFt)));
        const double seg = w / n;
        for (int i = 0; i < n; ++i) {
            const float lc       = static_cast<float>(-0.5 * w + (i + 0.5) * seg);
            WzItem      it       = on_road(al, WzKind::Wall, sw, lc, -dir);
            it.len               = static_cast<float>(seg) - m(0.025f);  // pinned joints
            const float  hl      = 0.5f * it.len * sg;                   // axis = dir x right normal
            const double sc      = std::clamp(sw, 0.0, al.total_length());
            it.dy                = al.surface_y(sc, lc + hl) - al.surface_y(sc, lc - hl);
            it.y                 = al.surface_y(sc, lc);
            const uint8_t lowBit = dir > 0 ? 1 : 2;  // tapered 6 in ends at the road edges (NYSDOT 619-01)
            it.taper             = static_cast<uint8_t>((i == 0 ? lowBit : 0) | (i == n - 1 ? 3 - lowBit : 0));
            z.items.push_back(it);
        }
    }
    // the other carriageway, gap to wall: closed with cones
    for (float l = med + m(0.4f); l <= outer - m(0.3f); l += m(1.5f))
        z.items.push_back(on_road(al, WzKind::Cone, S(gapNear - m(3.0)), -sg * l, -dir));

    // ── Past the wall: a few knocked drums, a cone, spare barrier segments, a millings pile ──
    const auto debris = [&](WzKind k, float u, float lat, float spin) {
        WzItem it = on_road(al, k, S(-m(wz::kFaceBack + u)), lat, -dir);
        it.spin   = spin;
        z.items.push_back(it);
    };
    debris(WzKind::DrumDown, 6.0f, edge(1) + sg * 0.2f * lw, 0.7f);
    debris(WzKind::DrumDown, 15.0f, -sg * (pi + 0.6f * lw), 2.1f);
    debris(WzKind::ConeDown, 9.0f, edge(2) + sg * 0.3f * lw, 1.3f);
    for (int i = 0; i < 4; ++i)
        debris(WzKind::Drum, 12.0f + 0.7f * static_cast<float>(i % 2), sg * (po + m(0.6f + 0.7f * (i / 2))), 0.0f);
    for (const float u : {22.0f, 29.0f}) {
        WzItem     it  = on_road(al, WzKind::Wall, S(-m(wz::kFaceBack + u)), sg * (po + 0.5f * h.outerShoulder), -dir);
        const auto sm2 = al.sample(std::clamp(it.s, 0.0, al.total_length()));
        it.fx = -sm2.tz, it.fz = sm2.tx;  // facing across the road: the segment lies along it
        it.len = static_cast<float>(ft(wz::kTcbSegFt));
        z.items.push_back(it);
    }
    debris(WzKind::Pile, 40.0f, edge(1) + sg * 0.5f * lw, 0.4f);

    // ── Horizon tail: the asset's data past the face, else extruded to tailM ──
    const double beyond = end ? al.total_length() - z.face : z.face;
    if (beyond < m(p.tailM)) {
        z.tailFrom = end ? al.total_length() : 0.0;
        z.tailLen  = static_cast<float>(m(p.tailM) - std::max(0.0, beyond));
    }
    return z;
}

WorkZone exit_closure(const CorridorNetwork& net, int ramp, const WorkZoneParams& p) {
    WorkZone       z;
    const NetRamp& rp = net.ramps()[static_cast<size_t>(ramp)];
    const double   u0 = rp.closed ? static_cast<double>(rp.stop + m(wz::kExitHold))  // N6: where the car is held
                                  : std::min(static_cast<double>(rp.pin[0] + m(wz::kExitGore)),
                                             static_cast<double>(rp.length() - m(5.0f)));
    z.wall = z.face = u0;
    // Item at arc length u, lateral lat on the ramp, facing approaching traffic.
    const auto at = [&](WzKind k, double u, float lat) {
        const auto& P = rp.pts;
        size_t      i = 1;
        while (i + 1 < P.size() && P[i].u < u) ++i;
        const double f = std::clamp((u - P[i - 1].u) / std::max(1e-3, double(P[i].u - P[i - 1].u)), 0.0, 1.0);
        WzItem       it;
        it.kind = k, it.s = u, it.lat = lat, it.fx = -P[i].tx, it.fz = -P[i].tz;
        it.x = P[i - 1].x + (P[i].x - P[i - 1].x) * f - static_cast<double>(lat * P[i].tz);
        it.z = P[i - 1].z + (P[i].z - P[i - 1].z) * f + static_cast<double>(lat * P[i].tx);
        it.y = net.ramp_surface(rp, u, lat);
        return it;
    };
    const float  hw = rp.halfWidth - m(0.5f);
    const double sp = ft(wz::kDownFt) / std::max(0.1f, p.drums);
    for (double u = std::max(0.0, u0 - m(30.0)); u < u0; u += sp)
        for (const float l : {-hw, hw}) z.items.push_back(at(WzKind::Drum, u, l));
    const int nb = std::max(1, static_cast<int>(2.0f * hw / (wz::kBarricadeW + m(0.15f))));
    for (int i = 0; i < nb; ++i) {
        WzItem it =
            at(WzKind::Barricade, u0, -hw + (static_cast<float>(i) + 0.5f) * 2.0f * hw / static_cast<float>(nb));
        it.len  = wz::kBarricadeW;
        it.face = i == nb / 2 ? WzFace::ExitClosed : WzFace::None;
        z.items.push_back(it);
    }
    for (float l = -hw; l <= hw; l += m(1.5f)) z.items.push_back(at(WzKind::Cone, u0 - m(6.0), l));
    return z;
}

}  // namespace swish
