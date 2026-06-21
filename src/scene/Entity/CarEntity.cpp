#include "CarEntity.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace swish {

void CarEntity::handle_input(GLFWwindow* window, float dt) {
    // Throttle / brake (arrow keys so WASD camera remains independent)
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        m_forward_speed += kAccel * dt;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        if (m_forward_speed > 0.f)
            m_forward_speed -= kBrakeAccel * dt;  // braking
        else
            m_forward_speed -= kAccel * dt;        // reversing
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
    // Drag (exponential decay approximation)
    float drag = 1.f - kDragCoeff * dt;
    if (drag < 0.f) drag = 0.f;
    m_forward_speed *= drag;

    // Dead-stop below threshold
    if (std::abs(m_forward_speed) < 0.5f)
        m_forward_speed = 0.f;

    if (m_forward_speed == 0.f)
        return;

    // Bicycle model yaw rate: ω = (v / L) * tan(δ)
    // Increasing yaw turns the body LEFT (R_y is counterclockwise from
    // above), so positive steering (= right) decreases yaw.
    float steer_rad = glm::radians(m_steering_angle);
    float yaw_rate  = (m_forward_speed / kWheelbase) * std::tan(steer_rad);  // rad/s
    m_rotation.y -= glm::degrees(yaw_rate) * dt;

    // Advance position along heading. forward = R_y(yaw)·(+X), the same
    // direction the mesh nose points, so body and velocity stay aligned.
    float yaw_rad = glm::radians(m_rotation.y);
    Vec3  forward(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
    m_position += forward * m_forward_speed * dt;

    // Clamp X to road bounds
    m_position.x = std::clamp(m_position.x, m_min_x, m_max_x);
}

std::vector<DrawCall> CarEntity::get_draw_calls() const {
    Mat4                  car_model = get_model_matrix();
    std::vector<DrawCall> result;
    result.reserve(get_submeshes().size());

    // Porsche 911 GT3: ~2.5 turns lock-to-lock = ±450° wheel for ±35° tire
    constexpr float kSteerRatio = 450.f / kMaxSteer;
    float           sw_deg      = -m_steering_angle * kSteerRatio;

    // Pivot-local Z matches the Blender steer empty on this asset.
    constexpr Vec3 kSteerLocalAxis(0.f, 0.f, 1.f);
    Mat4           sw_steer_xform(1.f);
    bool           sw_steer_ready = false;

    for (const auto& s : get_submeshes()) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.color       = s.color;

        if (s.is_steering_wheel) {
            if (!sw_steer_ready) {
                Mat4 R_loc = glm::rotate(Mat4(1.f), glm::radians(sw_deg), kSteerLocalAxis);
                sw_steer_xform =
                    s.sw_pivot_frame * R_loc * glm::inverse(s.sw_pivot_frame);
                sw_steer_ready = true;
            }
            dc.model = car_model * sw_steer_xform;
        } else {
            dc.model = car_model;
        }
        result.push_back(dc);
    }
    return result;
}

}  // namespace swish
