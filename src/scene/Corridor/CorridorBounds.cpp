#include "CorridorDriver.h"
#include "scene/RoadGeometry/RoadGeometry.h"

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

// Keep the car this far inside the painted edge lines, so the wheels stay on
// pavement rather than straddling the shoulder line.
static constexpr float kLaneInset = 300.0f;  // WU

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
    const float lanes = m_al->sample(s).lanes;
    const float inner = m_al->paint_inner(lanes);  // median-side edge line
    // Run-off room: the bound is the OUTER edge of the shoulder, not the edge
    // line, so drifting wide puts two wheels on the shoulder instead of into an
    // invisible wall the width of a lane marking.
    const float outer = m_al->outer_edge(lanes);

    if (m_al->in_turnaround(s)) {
        // U-turn pad: the median barrier is omitted here and asphalt is laid
        // across it, so the whole width is open and the player can swing onto
        // the opposite carriageway. This is the ONLY lateral opening in the
        // corridor — everywhere else the barrier commits you to your side.
        minLat = -(outer - kLaneInset);
        maxLat = outer - kLaneInset;
        return;
    }
    // `side` is COMMITTED state, not the sign of the current lateral offset.
    // Deriving it from the instantaneous position is what let the car drive
    // straight through the median barrier: the median is only ~3.8 m wide in
    // lateral terms, while one frame at the delta-time cap (1/15 s) and top
    // speed (92,000 WU/s) can move the car 6.1 m sideways. Land on the far
    // side and a position-derived rule cheerfully answers "you are westbound
    // now, carry on". Committing the side makes the barrier impassable exactly
    // where the geometry says it is.
    if (side >= 0) {  // eastbound — right of the centreline
        minLat = inner + kLaneInset;
        maxLat = outer - kLaneInset;
    } else {  // westbound
        minLat = -(outer - kLaneInset);
        maxLat = -(inner + kLaneInset);
    }
}

}  // namespace swish
