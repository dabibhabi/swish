#include "CarEntity.h"

#include "CarPhysics.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace swish {

void CarEntity::handle_input(GLFWwindow* window, float dt) {
    // Throttle / brake (arrow keys so WASD camera remains independent).
    // Set intent here; the longitudinal force model is integrated in update().
    m_throttle = 0.f;
    m_brake    = 0.f;
    m_reverse  = false;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        m_throttle = 1.f;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        if (m_forward_speed > kSpeedDeadZone)
            m_brake = 1.f;      // braking while moving forward
        else
            m_reverse = true;   // creep backward once ~stopped
    }

    // Steering
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        m_steering_angle -= kSteerRate * dt;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        m_steering_angle += kSteerRate * dt;
    } else {
        // Return to center
        float ret = kSteerReturn * dt;
        if (std::abs(m_steering_angle) <= ret)
            m_steering_angle = 0.f;
        else
            m_steering_angle -= std::copysign(ret, m_steering_angle);
    }

    m_steering_angle = std::clamp(m_steering_angle, -kMaxSteer, kMaxSteer);
}

void CarEntity::update(float dt) {
    // ── Longitudinal force balance (SI), P0 #5 ────────────────────────
    // m·v̇ = F_drive − ½ρC_dAv² − C_rr·mg, with F_drive power-limited and
    // traction-capped (CarPhysics.h). Top speed emerges from the balance —
    // no hard clamp — so it self-limits near ~205 mph.
    const CarParams params;
    float           v_mps = m_forward_speed / kWorldUnitsPerMeter;

    if (m_reverse) {
        v_mps -= (kReverseAccel / kWorldUnitsPerMeter) * dt;  // bounded low-speed reverse
    } else {
        const float prev = v_mps;
        v_mps += longitudinal_accel(v_mps, m_throttle, m_brake, params) * dt;
        // Braking must not drag the car backwards — stop at zero on a sign flip.
        if (m_brake > 0.f && prev > 0.f && v_mps < 0.f)
            v_mps = 0.f;
    }

    m_forward_speed = v_mps * kWorldUnitsPerMeter;

    // Finite/NaN guard; cap reverse only (forward top speed stays emergent).
    if (!std::isfinite(m_forward_speed))
        m_forward_speed = 0.f;
    if (m_forward_speed < -kMaxReverseSpeed)
        m_forward_speed = -kMaxReverseSpeed;

    // Dead-stop below threshold while coasting (no throttle / reverse input).
    if (std::abs(m_forward_speed) < kSpeedDeadZone && m_throttle == 0.f && !m_reverse)
        m_forward_speed = 0.f;

    if (m_forward_speed == 0.f)
        return;

    // ── Lateral / yaw dynamics (dynamic bicycle + saturating tires, P0 #4) ──
    // Below ~5 m/s the kinematic model is used (the dynamic model's 1/vx is
    // singular); above it, lateral velocity + yaw rate are integrated with tire
    // forces that saturate at μ·Fz, so the car can understeer and slide instead
    // of following a rigid geometric arc. This replaces the old kSteerRefSpeed
    // authority taper (a band-aid for the missing grip limit).
    const float wheelbase_m = kWheelbase / kWorldUnitsPerMeter;
    const float delta       = glm::radians(std::clamp(m_steering_angle, -kMaxSteer, kMaxSteer));
    const float vx          = m_forward_speed / kWorldUnitsPerMeter;  // m/s
    const float r_kin       = (vx / wheelbase_m) * std::tan(delta);   // kinematic yaw rate (right-pos)

    constexpr float kBlendSpeed = 5.0f;  // m/s
    if (std::abs(vx) < kBlendSpeed) {
        m_yaw_rate         = r_kin;  // keep dynamic state consistent for a smooth handoff
        m_lateral_velocity = 0.f;
    } else {
        const TireParams tire;
        BicycleDeriv     d = dynamic_bicycle_deriv(vx, m_lateral_velocity, m_yaw_rate, delta, params, tire);
        m_lateral_velocity += d.vlDot * dt;
        m_yaw_rate         += d.rDot * dt;
        // Smooth 5→8 m/s handoff from kinematic to full dynamic response.
        const float blend = glm::clamp((std::abs(vx) - kBlendSpeed) / 3.0f, 0.f, 1.f);
        m_yaw_rate         = glm::mix(r_kin, m_yaw_rate, blend);
        // Friction circle: cap lateral accel at μ·g (understeer at the limit;
        // also arrests sideslip growth under a sustained over-drive input).
        const float rMax   = max_yaw_rate(vx, params);
        m_yaw_rate         = std::clamp(m_yaw_rate, -rMax, rMax);
        m_lateral_velocity = std::clamp(m_lateral_velocity, -std::abs(vx), std::abs(vx));
    }

    // Apply heading (right-positive yaw decreases rotation.y) and wrap to ±180°.
    m_rotation.y -= glm::degrees(m_yaw_rate) * dt;
    m_rotation.y = std::fmod(m_rotation.y + 180.f, 360.f) - 180.f;

    // Advance position: forward motion + lateral drift. forward = mesh nose;
    // right = forward × up is the body's rightward axis (matches m_lateral_velocity).
    const float yaw_rad = glm::radians(m_rotation.y);
    const Vec3  forward(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
    const Vec3  right = glm::normalize(glm::cross(forward, Vec3(0.f, 1.f, 0.f)));
    m_position += (forward * m_forward_speed + right * (m_lateral_velocity * kWorldUnitsPerMeter)) * dt;

    // Clamp X to road bounds
    m_position.x = std::clamp(m_position.x, m_min_x, m_max_x);
}

std::vector<DrawCall> CarEntity::get_windshield_draw_calls() const {
    Mat4                  model = get_model_matrix();
    std::vector<DrawCall> result;

    for (const auto& s : get_glass_submeshes()) {
        if (!s.is_windshield)
            continue;
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.color       = s.color;
        dc.model       = model;
        result.push_back(dc);
    }
    return result;
}

std::vector<DrawCall> CarEntity::get_draw_calls() const {
    std::vector<DrawCall> result;
    result.reserve(get_submeshes().size());

    Mat4  car_model = get_model_matrix();
    float sw_angle  = m_steering_angle * kSteerRatio;

    // Cabin wash DISABLED (0.0): the interior now stays crisp and detailed in rain
    // via the `dry` wettable mask (whole car excluded from the wet-road BRDF, see
    // SceneGeometry + lighting.frag) plus depth-resolved fog that leaves the near
    // cabin fog-free. Any positive wash re-introduced the over-exposure reported.
    // The color.a sentinel path is kept for easy re-enable. a = 1 → wash 0.
    const float washAmount = 0.0f;

    for (const auto& s : get_submeshes()) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.dry         = true;  // whole car excluded from wet-road effects (lighting.frag wettable mask)
        dc.color       = s.is_interior ? Vec4(s.color.r, s.color.g, s.color.b, 1.0f + washAmount) : s.color;

        if (s.is_steering_wheel) {
            Mat4 R   = glm::rotate(Mat4(1.f), glm::radians(-sw_angle), Vec3(0.f, 0.f, 1.f));
            dc.model = car_model * s.sw_pivot_frame * R * glm::inverse(s.sw_pivot_frame);
        } else {
            dc.model = car_model;
        }
        result.push_back(dc);
    }
    return result;
}

}  // namespace swish
