#include "CorridorDriver.h"

#include "renderer/Renderer/Renderer.h"
#include "scene/Camera/Camera.h"
#include "scene/Entity/CarEntity.h"
#include "scene/RoadGeometry/RoadGeometry.h"

#include <algorithm>
#include <cmath>
#ifdef SWISH_DEBUG_UI
#include <cstdio>
#endif

namespace swish {

namespace {
// The car is held this far short of each terminus, so it stops on road rather
// than at the exact last vertex.
constexpr float kEndMargin = 8000.0f;  // 8 m
// Rebase steps are rounded to this, purely so the shift is a tidy number in
// logs/debug readouts; correctness does not depend on it (unlike the retired
// treadmill, which needed whole chunk lengths).
constexpr double kRebaseQuantum = 1000.0;
}  // namespace

// set_alignment / set_segments / carriageway_bounds live in CorridorBounds.cpp
// so the unit tests can link them without dragging in Vulkan and GLFW.

void CorridorDriver::place_at_start(CarEntity& car) {
    if (!m_al || !m_al->valid())
        return;

    // Baked, not hardcoded: station 0 is the tunnel portal's tight loop ramp,
    // so the baker picks the first station past it where the road straightens.
    m_station                          = static_cast<double>(m_al->header().startStation);
    const CorridorAlignment::Sample sm = m_al->sample(m_station);
    m_lanes                            = sm.lanes;
    // Right-hand lane of the eastbound carriageway.
    m_lateral = m_al->paint_outer(m_lanes) - 0.5f * m_al->header().laneWidth;
    m_hint    = m_al->index_at(m_station);
    m_side    = +1;  // committed eastbound
    m_seeded  = true;

    // right-of-travel = cross(tangent, up) — the same convention the mesh
    // generator's SweepFrames use, so "lateral" means one thing everywhere.
    const Vec3 right(-sm.tz, 0.0f, sm.tx);
    car.set_position(Vec3(static_cast<float>(sm.x) + right.x * m_lateral, m_al->road_y(),
                          static_cast<float>(sm.z + m_originShift) + right.z * m_lateral));
    car.set_rotation(Vec3(0.0f, glm::degrees(std::atan2(-sm.tz, sm.tx)), 0.0f));
    // The lateral clamp lives here, in corridor space — CarEntity's world-X
    // clamp cannot express "stay in your lane" on a curving road.
    car.set_road_bounds(-1e9f, 1e9f);
}

void CorridorDriver::update(CarEntity& car, Renderer& renderer, float deltaTime, bool debugEdit) {
#ifndef SWISH_DEBUG_UI
    (void)deltaTime;
#endif
    if (!m_al || !m_al->valid())
        return;

    using RG = RoadGeometry;
    Vec3 pos = car.get_position();

    // ── 1. Origin rebase ─────────────────────────────────────────────
    // Shift the world back toward Z = 0 so the float32 coordinates the GPU
    // sees stay small. Every segment offset below is recomputed from doubles,
    // so an arbitrary shift moves all geometry together and is invisible.
    if (std::abs(pos.z) > RG::kRebaseThreshold) {
        const double shift = std::round(static_cast<double>(pos.z) / kRebaseQuantum) * kRebaseQuantum;
        pos.z -= static_cast<float>(shift);
        car.set_position(pos);
        m_originShift -= shift;
    }

    // ── 2. Track the car in corridor space ───────────────────────────
    const double trueX = static_cast<double>(pos.x);
    const double trueZ = static_cast<double>(pos.z) - m_originShift;
    if (!m_seeded) {
        m_al->project_global(trueX, trueZ, m_hint, m_station, m_lateral);
        m_seeded = true;
    } else {
        m_al->project(trueX, trueZ, m_hint, m_station, m_lateral);
    }

    // ── 3. Confine the car: lateral clamp + terminus stops ───────────
    // The car keeps its own free bicycle model; this only pulls it back onto
    // the drivable surface, the same role the retired X-clamp had.
    if (!debugEdit) {
        const double sMin     = kEndMargin;
        const double sMax     = std::max(sMin, m_al->total_length() - kEndMargin);
        const double sClamped = std::clamp(m_station, sMin, sMax);

        // The committed carriageway may only change inside a turn-around pad —
        // that is the U-turn (spec decision #7), and the only place the median
        // barrier isn't built. Elsewhere the commitment is what stops a
        // long-frame lateral jump from teleporting through the barrier.
        if (m_al->in_turnaround(sClamped))
            m_side = (m_lateral >= 0.0f) ? +1 : -1;

        float minLat = 0.0f, maxLat = 0.0f;
        carriageway_bounds(sClamped, m_side, minLat, maxLat);
        const float latClamped = std::clamp(m_lateral, minLat, maxLat);

        if (sClamped != m_station || latClamped != m_lateral) {
            const CorridorAlignment::Sample sm = m_al->sample(sClamped);
            const Vec3                      right(-sm.tz, 0.0f, sm.tx);
            pos.x = static_cast<float>(sm.x + right.x * latClamped);
            pos.z = static_cast<float>(sm.z + right.z * latClamped + m_originShift);
            car.set_position(pos);
            m_station = sClamped;
            m_lateral = latClamped;
        }
    }
    m_lanes = m_al->sample(m_station).lanes;

    // ── 4. Visible segment window ────────────────────────────────────
    // Coarse reject on the segment's bounding sphere against the camera far
    // plane; every surviving segment still gets full per-draw frustum +
    // distance culling inside SceneGeometry::record_draws.
    const Vec3                    camPos  = renderer.get_camera() ? renderer.get_camera()->get_position() : pos;
    const double                  reachSq = static_cast<double>(RG::kCameraFar) * static_cast<double>(RG::kCameraFar);
    std::vector<CorridorInstance> visible;
    visible.reserve(16);
    for (size_t i = 0; i < m_segments.size(); ++i) {
        const CorridorPlacement& p = m_segments[i];
        // Segment centre in the render frame (computed in double, then narrowed
        // — this is the one place the big numbers get turned into small ones).
        const double cx = p.originX + static_cast<double>(p.boundsCenter.x);
        const double cz = p.originZ + static_cast<double>(p.boundsCenter.z) + m_originShift;
        const double dx = cx - static_cast<double>(camPos.x);
        const double dz = cz - static_cast<double>(camPos.z);
        const double r  = static_cast<double>(p.boundsRadius);
        const double d  = std::sqrt(dx * dx + dz * dz) - r;
        if (d > 0.0 && d * d > reachSq)
            continue;
        CorridorInstance inst;
        inst.segment = static_cast<uint32_t>(i);
        inst.offset  = Vec3(static_cast<float>(p.originX), 0.0f, static_cast<float>(p.originZ + m_originShift));
        visible.push_back(inst);
    }
    renderer.set_corridor_instances(visible);

#ifdef SWISH_DEBUG_UI
    // Corridor telemetry — the readout that makes an 18 mi finite road
    // verifiable at all: without a station number there is no way to tell
    // "drove the whole corridor" from "stuck against the lateral clamp".
    // Throttled to ~1 Hz. Debug-gated, so release prints nothing.
    m_logTimer += deltaTime;
    if (m_logTimer >= 1.0f) {
        m_logTimer = 0.0f;
        std::printf("[corridor] s=%7.3f/%.3f mi  lat=%+8.0f %s lanes=%.2f  shift=%+11.0f  renderZ=%+11.0f  segs=%zu\n",
                    m_station / 1609344.0, m_al->total_length() / 1609344.0, static_cast<double>(m_lateral),
                    (m_side >= 0 ? "EB" : "WB"), m_lanes, m_originShift, static_cast<double>(pos.z), visible.size());
        std::fflush(stdout);
    }
#endif
}

}  // namespace swish
