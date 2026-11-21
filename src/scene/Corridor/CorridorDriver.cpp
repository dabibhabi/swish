#include "CorridorDriver.h"

#include "CorridorDressing.h"
#include "CorridorEnds.h"
#include "debug/DebugParams.h"
#include "renderer/Renderer/Renderer.h"
#include "scene/Camera/Camera.h"
#include "scene/Entity/CarEntity.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "utils/units/Units.h"

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
    if (!m_al || !m_al->valid()) return;

    // Baked, not hardcoded: station 0 is the tunnel portal's tight loop ramp,
    // so the baker picks the first station past it where the road straightens.
    m_station                          = static_cast<double>(m_al->header().startStation);
    const CorridorAlignment::Sample sm = m_al->sample(m_station);
    m_lanes                            = sm.lanes;
    // Right-hand lane of the eastbound carriageway.
    m_lateral   = m_al->paint_outer(m_lanes) - 0.5f * m_al->header().laneWidth;
    m_hint      = m_al->index_at(m_station);
    m_side      = +1;  // committed eastbound
    m_seeded    = true;
    m_prevS     = m_station;
    m_prevLat   = m_lateral;
    m_prevPiece = DrivePiece::Mainline;

    // right-of-travel = cross(tangent, up) — the same convention the mesh
    // generator's SweepFrames use, so "lateral" means one thing everywhere.
    const Vec3 right(-sm.tz, 0.0f, sm.tx);
    RoadGeometry::set_flat_surface(m_al->surface_y(m_station, m_lateral));
    car.set_position(Vec3(static_cast<float>(sm.x) + right.x * m_lateral, m_al->surface_y(m_station, m_lateral),
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
    if (!m_al || !m_al->valid()) return;

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
#ifdef SWISH_DEBUG_UI
    const bool driveOff = renderer.debug_params().corridorDriveOff;
#else
    constexpr bool driveOff = true;  // release has no paths: mainline only either way
#endif
    static const std::vector<CorridorPath> kNone;
    const std::vector<CorridorPath>&       paths = driveOff ? m_paths : kNone;
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
        if (m_al->in_turnaround(sClamped)) m_side = (m_lateral >= 0.0f) ? +1 : -1;

        // C3: the union of the pieces under the car (mainline, ramps, service roads, crossroad run) at this station.
        const std::vector<DriveSpan> spans      = drive_spans(*m_al, paths, sClamped, m_side, kLaneInset);
        float                        latClamped = m_lateral, snap = 1e30f;
        for (const DriveSpan& sp : spans) {
            const float c = std::clamp(m_lateral, sp.lo, sp.hi);
            if (std::abs(c - m_lateral) < snap) {
                snap       = std::abs(c - m_lateral);
                latClamped = c;
            }
        }
        double sNew = sClamped;
        // A side road ended under the car: hold it at last frame's spot instead of jumping it sideways across grass.
        // C4: no window at all (the roundabout's far rim) holds the car too
        const bool deadEnd =
            spans.empty() || (snap > util::units::m(road::diamond::kDeadEnd) && m_prevPiece != DrivePiece::Mainline &&
                              m_prevPiece != DrivePiece::None &&
                              std::abs(sClamped - m_prevS) < static_cast<double>(util::units::m(20.0f)));
        if (deadEnd) {
            sNew       = m_prevS;
            latClamped = m_prevLat;
        }

        if (sNew != m_station || latClamped != m_lateral) {
            const CorridorAlignment::Sample sm = m_al->sample(sNew);
            const Vec3                      right(-sm.tz, 0.0f, sm.tx);
            pos.x = static_cast<float>(sm.x + right.x * latClamped);
            pos.z = static_cast<float>(sm.z + right.z * latClamped + m_originShift);
            car.set_position(pos);
#ifdef SWISH_DEBUG_UI
            if (deadEnd)
                car.wall_contact(Vec3(sm.tx, 0.0f, sm.tz) * (sNew > m_station ? 1.0f : -1.0f));
            else if (latClamped != m_lateral)
                car.wall_contact(right * (latClamped > m_lateral ? 1.0f : -1.0f));
#endif
            m_station = sNew;
            m_lateral = latClamped;
        }
    }
    m_lanes = m_al->sample(m_station).lanes;
    // The height query is a plane re-aimed each frame at the surface under the car. The bicycle
    // model reads only its height; the 6-DOF chassis reads the slope per wheel.
    const DriveSurface surf = drive_surface(*m_al, paths, m_station, m_lateral);
    {
        float dydx = 0.0f, dydz = 0.0f;
        if (surf.piece == DrivePiece::Mainline || surf.piece == DrivePiece::None) {
            m_al->surface_slope(m_station, dydx, dydz);
        } else {  // off the mainline every piece is level across: grade along the tangent only
            const CorridorAlignment::Sample sm = m_al->sample(m_station);
            dydx                               = surf.dyds * sm.tx;
            dydz                               = surf.dyds * sm.tz;
        }
        const Vec3 at = car.get_position();
        RoadGeometry::set_plane_surface(surf.y, at.x, at.z, dydx, dydz);
    }
    m_prevS     = m_station;
    m_prevLat   = m_lateral;
    m_prevPiece = surf.piece;

    // ── 4. Visible segment window ────────────────────────────────────
    // Coarse reject on the segment's bounding sphere against the camera far
    // plane; every surviving segment still gets full per-draw frustum +
    // distance culling inside SceneGeometry::record_draws.
    const Vec3                    camPos  = renderer.get_camera() ? renderer.get_camera()->get_position() : pos;
    const double                  reachSq = static_cast<double>(RG::kCameraFar) * static_cast<double>(RG::kCameraFar);
    std::vector<CorridorInstance> visible;
    visible.reserve(16);
    for (size_t i = 0; i < m_segments.size(); ++i) {
#ifdef SWISH_DEBUG_UI
        if (i == m_skyline) continue;  // D5: placed below
#endif
        const CorridorPlacement& p = m_segments[i];
        // Segment centre in the render frame (computed in double, then narrowed
        // — this is the one place the big numbers get turned into small ones).
        const double cx = p.originX + static_cast<double>(p.boundsCenter.x);
        const double cz = p.originZ + static_cast<double>(p.boundsCenter.z) + m_originShift;
        const double dx = cx - static_cast<double>(camPos.x);
        const double dz = cz - static_cast<double>(camPos.z);
        const double r  = static_cast<double>(p.boundsRadius);
        const double d  = std::sqrt(dx * dx + dz * dz) - r;
        if (d > 0.0 && d * d > reachSq) continue;
        CorridorInstance inst;
        inst.segment = static_cast<uint32_t>(i);
        inst.offset  = Vec3(static_cast<float>(p.originX), 0.0f, static_cast<float>(p.originZ + m_originShift));
        visible.push_back(inst);
    }
#ifdef SWISH_DEBUG_UI
    if (m_skyline < m_segments.size() && renderer.debug_params().skylineShow) {  // D5: Midtown card, per frame
        const CorridorPlacement& p = m_segments[m_skyline];
        CorridorInstance         inst;
        inst.segment = static_cast<uint32_t>(m_skyline);
        inst.offset  = Vec3(static_cast<float>(p.originX), 0.0f, static_cast<float>(p.originZ + m_originShift));
        Mat4 model;
        if (skyline_model(camPos, inst.offset, model)) {
            renderer.set_corridor_draw_model(m_skyline, 0, model);
            visible.push_back(inst);
        }
    }
#endif
    renderer.set_corridor_instances(visible);

#ifdef SWISH_DEBUG_UI
    // Lighting L6: the street lamps of the visible segments, moved into the render frame. The renderer keeps the
    // nearest ones. Off (an empty list) unless the time of day switches lamps on.
    {
        DebugParams&               dp = renderer.debug_params();
        std::vector<SpotLightDesc> lamps;
        if (dp.corridorLamps && dp.lampsOn) {
            const float gain = dp.lampLumens / 16000.0f;  // the panel's lamp output, relative to the default
            for (const CorridorInstance& inst : visible) {
                for (SpotLightDesc l : m_segments[inst.segment].lamps) {
                    l.position += inst.offset;
                    l.intensity *= gain;
                    lamps.push_back(l);
                }
            }
        }
        // C4: tunnel luminaires burn day and night. Inside, only the car's own tube: spots are unshadowed and would
        // light the other tube through the dividing wall. The roof boxes hide the sky from the tube interior.
        const double portal = m_al->ends().portal;
        if (portal > 0.0 && m_station < portal + static_cast<double>(util::units::m(150.0f))) {
            const double reach = static_cast<double>(util::units::m(110.0f));
            for (const int side : {+1, -1}) {
                if (m_station < portal && side != m_side) continue;
                for (SpotLightDesc l :
                     corridor_tunnel_lights(*m_al, side, m_station - reach, m_station + reach, dp.corridorTunnelLux)) {
                    l.position.z += static_cast<float>(m_originShift);
                    lamps.push_back(l);
                }
            }
        }
        // The eye adapts inside the tube: the preset's EV floor is a daylight range, so lower it while in there.
        const bool tube = portal > 0.0 && m_station < portal;
        if (tube) {
            if (dp.evMin != road::ends::kTunnelEvMin) m_evMinOutside = dp.evMin;  // a preset or the panel moved it
            dp.evMin = road::ends::kTunnelEvMin;
        } else if (m_evMinOutside >= 0.0f) {
            dp.evMin       = m_evMinOutside;
            m_evMinOutside = -1.0f;
        }
        const auto roofs = corridor_tunnel_roofs(*m_al, m_station, m_originShift);
        dp.labRoofCount  = static_cast<int>(std::min(roofs.size(), dp.labRoofs.size()));
        for (int i = 0; i < dp.labRoofCount; ++i) dp.labRoofs[static_cast<size_t>(i)] = roofs[static_cast<size_t>(i)];
        renderer.set_world_spot_lights(lamps);
    }
#endif

#ifdef SWISH_DEBUG_UI
    // Corridor telemetry — the readout that makes an 18 mi finite road
    // verifiable at all: without a station number there is no way to tell
    // "drove the whole corridor" from "stuck against the lateral clamp".
    // Throttled to ~1 Hz. Debug-gated, so release prints nothing.
    m_logTimer += deltaTime;
    if (m_logTimer >= 1.0f) {
        m_logTimer                  = 0.0f;
        static const char* kPiece[] = {"none", "main", "ramp", "service", "cross"};
        std::printf(
            "[corridor] s=%7.3f/%.3f mi  lat=%+8.0f %s %s y=%.0f lanes=%.2f  shift=%+11.0f  renderZ=%+11.0f  "
            "segs=%zu\n",
            m_station / 1609344.0, m_al->total_length() / 1609344.0, static_cast<double>(m_lateral),
            (m_side >= 0 ? "EB" : "WB"), kPiece[static_cast<int>(m_prevPiece)],
            static_cast<double>(car.get_position().y), m_lanes, m_originShift, static_cast<double>(pos.z),
            visible.size());
        const CorridorExit*     ex = m_al->next_exit(m_station, m_side < 0);
        const CorridorDistrict* di = m_al->district_at(m_station);
        if (ex != nullptr)
            std::printf("[corridor]   %s, next exit %s in %.2f mi: %s %s\n", di != nullptr ? di->name : "?", ex->ref,
                        std::abs(ex->station - m_station) / 1609344.0, ex->destRef, ex->dest);
        std::fflush(stdout);
    }
#endif
}

}  // namespace swish
