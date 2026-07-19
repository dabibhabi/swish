#include "Treadmill.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../Entity/CarEntity.h"
#include "../RoadGeometry/RoadGeometry.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace swish {

Treadmill::Treadmill()  = default;
Treadmill::~Treadmill() = default;

void Treadmill::set_intro(float introLen, std::vector<LightDesc> sceneLights) {
    m_introLen    = introLen;
    m_sceneLights = std::move(sceneLights);
}

void Treadmill::set_ribbons(std::vector<Ribbon> ribbons) {
    m_ixRibbons = std::move(ribbons);
}

void Treadmill::update(CarEntity& car, Renderer& renderer, GLFWwindow* window, float deltaTime,
                       bool debugEdit) {
    using RG = RoadGeometry;

    Vec3 pos = car.get_position();

    // ── Endless-road treadmill ───────────────────────────────────────
    // 1) Origin rebase: keep the car's render-frame Z small (float32 precision)
    //    by shifting the world back toward 0 in whole chunk lengths — the periodic
    //    tile looks identical afterwards, so the rebase is invisible. The camera is
    //    welded from the car, so it follows automatically.
    if (std::abs(pos.z) > RG::kRebaseThreshold) {
        float shift = std::round(pos.z / RG::kChunkLen) * RG::kChunkLen;
        pos.z -= shift;
        car.set_position(pos);
        m_originShift -= static_cast<double>(shift);
        // Shift the intro lamp point-lights into the new render frame so they stay
        // attached to their posts (lighting works in render frame).
        for (LightDesc& l : m_sceneLights)
            l.position.z -= shift;
        renderer.set_scene_lights(m_sceneLights);
    }

    // 2) Active chunk window around the car (render-frame Z offsets for the
    //    canonical chunk). renderZ = trueZ + m_originShift; chunk slot k spans
    //    trueZ [-introLen - k·L, -introLen - (k+1)·L].
    const double carTrueZ = static_cast<double>(pos.z) - m_originShift;

    // Guided drivable bounds (Layer 3): feed the car the lateral bounds for its
    // current surface — opens across the median inside a crossover window so it can
    // cross to the opposite carriageway; else the EB/WB roadway. The car's existing
    // X-clamp (CarEntity::update) enforces them next frame.
    float bMinX = 0.0f, bMaxX = 0.0f;
    RG::drivable_bounds(pos.x, carTrueZ, m_introLen, bMinX, bMaxX);
    car.set_road_bounds(bMinX, bMaxX);

    const int kCar =
        static_cast<int>(std::floor((-static_cast<double>(m_introLen) - carTrueZ) / RG::kChunkLen));
    const int          kStart = std::max(0, kCar - RG::kChunksBehind);
    const int          kEnd   = kCar + RG::kChunksAhead;
    std::vector<float> slotOffsets;
    for (int k = kStart; k <= kEnd; ++k) {
        double slotTrueStart = -static_cast<double>(m_introLen) - static_cast<double>(k) * RG::kChunkLen;
        slotOffsets.push_back(static_cast<float>(slotTrueStart + m_originShift));
    }
    renderer.set_road_chunks(static_cast<float>(m_originShift), slotOffsets);

    // Sparse elevated interchanges (~1.5 mi apart) around the car; interchange j is
    // centred at trueZ = −introLen − (j+1)·spacing. Culling drops the far ones.
    const float  kInterchangeSpacing = 8.0f * RG::kChunkLen;
    const double nIx =
        (-static_cast<double>(m_introLen) - carTrueZ) / static_cast<double>(kInterchangeSpacing);
    const int          jNear = static_cast<int>(std::floor(nIx));
    std::vector<float> ixOffsets;
    for (int j = std::max(0, jNear - 2); j <= jNear + 1; ++j) {
        double ixTrueZ = -static_cast<double>(m_introLen) - static_cast<double>(j + 1) * kInterchangeSpacing;
        ixOffsets.push_back(static_cast<float>(ixTrueZ + m_originShift));
    }
    renderer.set_interchanges(ixOffsets);

    // ── Interchange ribbon-follower (drivable ramps / deck) ───────────
    // Off the interchange the car drives freely (above); on it, the car tracks a
    // graded ribbon — arc-length advanced by its speed, lateral by steering, with
    // Y/heading/pitch taken from the curve — so it climbs the ramp, crosses the
    // deck, and descends onto the opposite mainline or the frontage road. Guided,
    // not on-rails: you still steer within the ribbon and pick branches at junctions.
    if (!m_ixRibbons.empty() && !debugEdit) {
        const float spd   = car.get_speed();
        const int   steer = (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ? -1 : 0) +
                          (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ? 1 : 0);
        if (m_carRibbon >= 0) {
            const float   ixZ = static_cast<float>(m_ixTrueZ + m_originShift);  // render-frame Z of the instance
            const Ribbon* r   = &m_ixRibbons[m_carRibbon];
            m_ribbonS += spd * deltaTime;
            const float latLim = std::max(0.f, r->halfWidth - 1500.f);
            m_ribbonT = std::clamp(m_ribbonT + static_cast<float>(steer) * 9000.f * deltaTime, -latLim, latLim);

            const float len = RG::ribbon_length(*r);
            if (m_ribbonS >= len) {  // reached the end → junction / exit
                const int   nxt      = (r->branch >= 0 && steer > 0) ? r->branch : r->next;
                const float overflow = m_ribbonS - len;
                if (nxt < 0) {  // leave the interchange onto free road
                    Vec3 p, tn;
                    RG::ribbon_sample(*r, len, p, tn);
                    car.set_position(p + Vec3(0.f, 0.f, ixZ));
                    Vec3 th = glm::normalize(Vec3(tn.x, 0.f, tn.z));
                    car.set_rotation(Vec3(0.f, glm::degrees(std::atan2(-th.z, th.x)), 0.f));
                    m_carRibbon = -1;
                } else {
                    m_carRibbon = nxt;
                    m_ribbonS   = overflow;
                    m_ribbonT   = 0.f;
                }
            }
            if (m_carRibbon >= 0) {
                r = &m_ixRibbons[m_carRibbon];
                Vec3 p, tn;
                RG::ribbon_sample(*r, m_ribbonS, p, tn);
                Vec3 rightH = glm::cross(tn, Vec3(0.f, 1.f, 0.f));
                if (glm::length(rightH) > 1e-4f)
                    rightH = glm::normalize(rightH);
                car.set_position(p + Vec3(0.f, 0.f, ixZ) + rightH * m_ribbonT);
                Vec3        th    = glm::normalize(Vec3(tn.x, 0.f, tn.z));
                const float yaw   = glm::degrees(std::atan2(-th.z, th.x));
                const float pitch = glm::degrees(std::asin(std::clamp(tn.y, -1.f, 1.f)));
                car.set_rotation(Vec3(0.f, yaw, pitch));  // .z = pitch (car nose is +X)
                car.set_road_bounds(-1e9f, 1e9f);         // guided → no lateral clamp
            }
        } else {  // free: attach to an on-ramp when the car reaches its entry
            const Vec3 entry = m_ixRibbons[0].pts.front();
            const Vec3 cp    = car.get_position();
            for (float ixOff : ixOffsets) {
                if (std::abs((cp.z - ixOff) - entry.z) < 7000.f && std::abs(cp.x - entry.x) < 5000.f &&
                    spd > 500.f) {
                    m_carRibbon = 0;
                    m_ixTrueZ   = static_cast<double>(ixOff) - m_originShift;
                    m_ribbonS   = 0.f;
                    m_ribbonT   = std::clamp(cp.x - entry.x, -(m_ixRibbons[0].halfWidth - 1500.f),
                                             m_ixRibbons[0].halfWidth - 1500.f);
                    break;
                }
            }
        }
    }
}

}  // namespace swish
