#include "CarEntity.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace swish {

void CarEntity::handle_input(GLFWwindow* window, float dt) {
    // Throttle / brake (arrow keys so WASD camera remains independent)
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        m_forward_speed += kAccel * dt;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        if (m_forward_speed > 0.f)
            m_forward_speed -= kBrakeAccel * dt;  // braking
        else
            m_forward_speed -= kAccel * dt;  // reversing
    }

    m_forward_speed = std::clamp(m_forward_speed, -kMaxReverseSpeed, kMaxForwardSpeed);

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
    // Drag (true exponential decay)
    m_forward_speed *= std::exp(-kDragCoeff * dt);

    // Dead-stop below threshold
    if (std::abs(m_forward_speed) < kSpeedDeadZone)
        m_forward_speed = 0.f;

    if (m_forward_speed == 0.f)
        return;

    // Bicycle model yaw rate: ω = (v / L) * tan(δ)
    // Increasing yaw turns the body LEFT (R_y is counterclockwise from
    // above), so positive steering (= right) decreases yaw.
    //
    // Variable steering ratio: reduce the EFFECTIVE lock as speed rises so the
    // yaw rate stays controllable at high speed (full lock at v=92000 WU/s
    // would yaw >1300 deg/s — instant spin). The scale falls off smoothly from
    // 1.0 at standstill toward 0 as |v| grows; the visual steering wheel still
    // tracks the raw m_steering_angle (via kSteerRatio in get_draw_calls).
    float steer_scale = kSteerRefSpeed / (kSteerRefSpeed + std::abs(m_forward_speed));
    float safe_steer = std::clamp(m_steering_angle, -kMaxSteer, kMaxSteer) * steer_scale;
    float yaw_rate   = (m_forward_speed / kWheelbase) * std::tan(glm::radians(safe_steer));  // rad/s
    m_rotation.y -= glm::degrees(yaw_rate) * dt;
    // Wrap heading to ±180° so cockpit camera yaw composition stays sane.
    m_rotation.y = std::fmod(m_rotation.y + 180.f, 360.f) - 180.f;

    // Advance position along heading. forward = R_y(yaw)·(+X), the same
    // direction the mesh nose points, so body and velocity stay aligned.
    float yaw_rad = glm::radians(m_rotation.y);
    Vec3  forward(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
    m_position += forward * m_forward_speed * dt;

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

    // Cabin wash: interior submeshes tint toward light gray as rain rises. Encoded
    // in color.a as the gbuffer.frag sentinel (a = 1 + wash; max ≈0.5 at full rain,
    // so even heavy rain reads as a clear LIGHT GRAY, not white). Light rain (0.35)
    // → wash ≈0.18, a slight shift. Non-interior submeshes keep a = 1 → wash 0.
    const float washAmount = m_rain_intensity * 0.5f;

    for (const auto& s : get_submeshes()) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
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
