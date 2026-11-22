#include "CorridorDriver.h"
#include "CorridorEnds.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorDriver — the parts that depend on nothing but the alignment.
//
// Split out of CorridorDriver.cpp deliberately: update()/place_at_start()
// need Renderer (Vulkan) and CarEntity (GLFW input), which the unit-test
// harness has no business linking. The drivable-bounds rule is the piece most
// worth testing directly — it is what replaced the retired treadmill's
// world-X clamp — so it lives here, where a test can reach it.
// ══════════════════════════════════════════════════════════════════════

void CorridorDriver::set_alignment(const CorridorAlignment* alignment) {
    m_al = alignment;
    if (m_al && m_al->valid()) {
        // Physics and wheel visuals read the surface height through
        // RoadGeometry::road_surface_y(x), which derives height from world X
        // via the crown formula — meaningful only on an axis-aligned road. On a
        // curving alignment world X is not a lateral offset at all, and this
        // stretch of the LIE is flat anyway (spec §2), so point that query at
        // the corridor's single surface height. Geometry and physics then agree
        // exactly, which matters far more here than a 2% crown does.
        RoadGeometry::set_flat_surface(m_al->road_y());
    }
}

void CorridorDriver::set_segments(std::vector<CorridorPlacement> placements) { m_segments = std::move(placements); }

void CorridorDriver::carriageway_bounds(double s, int side, float& minLat, float& maxLat) const {
    if (!m_al || !m_al->valid()) {
        minLat = -1e9f;
        maxLat = 1e9f;
        return;
    }
    const DriveSpan sp = drive_spans(*m_al, {}, s, side, kLaneInset).front();
    minLat             = sp.lo;
    maxLat             = sp.hi;
}

namespace {

struct Piece {
    DrivePiece kind;
    DriveSpan  span;  // lateral coverage, no inset
    float      y, dyds;
};

// Mainline window on `side`, painted median-side edge line to the OUTER edge of the shoulder: drifting wide puts
// two wheels on the shoulder instead of into an invisible wall. In a turn-around pad the median is paved across.
//
// `side` is COMMITTED state, not the sign of the current lateral offset. Deriving it from the instantaneous position
// is what let the car drive straight through the median barrier: the median is only ~3.8 m wide in lateral terms,
// while one frame at the delta-time cap (1/15 s) and top speed (92,000 WU/s) can move the car 6.1 m sideways.
DriveSpan mainline_span(const CorridorAlignment& al, double s, int side) {
    const auto  sm = al.sample(s);
    const float c = util::units::m(road::ends::kCurbClear), inner = al.paint_inner(sm);
    float       outer = al.outer_edge(sm);
    if (al.in_tunnel(s)) {  // C4: lanes only; the walk curbs are walls
        const float lo = inner + c, hi = al.paint_outer(sm) - c;
        return side >= 0 ? DriveSpan{lo, hi} : DriveSpan{-hi, -lo};
    }
    if (al.in_turnaround(s)) return {-outer, outer};
    // D2: the car's side stops at the face it can see: NJ barrier toe, W-beam face
    const float lo = al.edges().jersey && sm.med <= al.header().medianHalfWidth && s < al.ends().barrierEnd
                         ? util::units::ft(0.5f * road::edges::kNjBaseIn / 12.0f) + c
                         : inner;
    if (const float r = al.rail_face(s, side); r > 0.0f) outer = std::min(outer, r - c);
    return side >= 0 ? DriveSpan{lo, outer} : DriveSpan{-outer, -lo};
}

// Every piece on `side` whose station range holds s. `lat` only sets the mainline height (bank).
std::vector<Piece> pieces_at(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s, int side,
                             float lat) {
    using util::units::m;
    const double       h = 0.5 * static_cast<double>(al.header().stationSpacing);
    std::vector<Piece> out;
    const float        grade = (al.surface_y(s + h, lat) - al.surface_y(s - h, lat)) / static_cast<float>(2.0 * h);
    // C4: past the mainline's end an empty piece (lo > hi) still carries the fallback height; the ring takes over
    out.push_back({DrivePiece::Mainline, s < al.main_end() ? mainline_span(al, s, side) : DriveSpan{1.0f, 0.0f},
                   al.surface_y(s, lat), grade});
    for (const DriveSpan& r : ring_spans(al, s)) out.push_back({DrivePiece::Mainline, r, al.surface_y(s, lat), grade});
    for (const CorridorPath& p : paths) {
        if (p.side != side || s < p.s0 || s > p.s1) continue;
        const PathPoint pt = corridor_path_at(al, p, s);
        const double    a = std::max(p.s0, s - h), b = std::min(p.s1, s + h);
        const float     g =
            b > a ? (corridor_path_at(al, p, b).y - corridor_path_at(al, p, a).y) / static_cast<float>(b - a) : 0.0f;
        out.push_back({p.kind == CorridorPath::Kind::Ramp ? DrivePiece::Ramp : DrivePiece::Service,
                       {pt.lat - p.halfWidth, pt.lat + p.halfWidth},
                       pt.y,
                       g});
    }
    // The service road's band carried across the crossroad deck; turning onto the crossroad is C3a.
    const double hc = static_cast<double>(corridor_cross_half(al)), tuck = hc - static_cast<double>(m(0.5f));
    const double near = static_cast<double>(m(1.0f));
    for (const CorridorCrossroad& c : al.crossroads()) {
        if (std::abs(s - c.s) > hc) continue;
        const float gr = (al.ground_y(c.s + h) - al.ground_y(c.s - h)) / static_cast<float>(2.0 * h);
        const float y  = al.ground_y(c.s) + 2.0f + gr * static_cast<float>(s - c.s);  // deck height, as emit_crossroad
        for (const CorridorPath& p : paths) {
            if (p.kind != CorridorPath::Kind::Service || p.side != side) continue;
            if (std::abs(p.s1 - (c.s - tuck)) > near && std::abs(p.s0 - (c.s + tuck)) > near) continue;
            const float l = corridor_path_at(al, p, c.s).lat;
            out.push_back({DrivePiece::Crossroad, {l - p.halfWidth, l + p.halfWidth}, y, gr});
        }
    }
    return out;
}

}  // namespace

DriveSurface drive_surface(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s, float lat) {
    DriveSurface best;
    bool         hit = false;
    for (const Piece& p : pieces_at(al, paths, s, lat >= 0.0f ? +1 : -1, lat)) {
        if (p.kind == DrivePiece::Mainline && !hit) best = {DrivePiece::None, p.y, p.dyds};  // fallback height
        if (lat < p.span.lo || lat > p.span.hi || (hit && p.y <= best.y)) continue;
        best = {p.kind, p.y, p.dyds};  // highest wins: seams are coplanar to a few WU, so wheels never sink
        hit  = true;
    }
    return best;
}

std::vector<DriveSpan> drive_spans(const CorridorAlignment& al, const std::vector<CorridorPath>& paths, double s,
                                   int side, float inset) {
    std::vector<DriveSpan> v;
    for (const Piece& p : pieces_at(al, paths, s, side, 0.0f))
        if (p.span.hi - p.span.lo >= 2.0f * inset) v.push_back({p.span.lo + inset, p.span.hi - inset});
    std::sort(v.begin(), v.end(), [](const DriveSpan& a, const DriveSpan& b) { return a.lo < b.lo; });
    std::vector<DriveSpan> out;
    for (const DriveSpan& sp : v) {
        if (!out.empty() && sp.lo <= out.back().hi)
            out.back().hi = std::max(out.back().hi, sp.hi);  // overlap: a gore or a ramp landing joins the pieces
        else
            out.push_back(sp);
    }
    return out;
}

}  // namespace swish
