#include "Camera.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Position
// ══════════════════════════════════════════════════════════════════════

void Camera::set_position(const Vec3& pos) { m_position = pos; }
const Vec3& Camera::get_position() const { return m_position; }

// ══════════════════════════════════════════════════════════════════════
// Orientation
// ══════════════════════════════════════════════════════════════════════

void Camera::set_yaw(float degrees) {
    m_yaw = degrees;
    update_vectors();
}
void Camera::set_pitch(float degrees) {
    m_pitch = std::clamp(degrees, -89.0f, 89.0f);
    update_vectors();
}
float Camera::get_yaw() const {
    return m_yaw;
}
float Camera::get_pitch() const {
    return m_pitch;
}

// ══════════════════════════════════════════════════════════════════════
// Speed / Sensitivity
// ══════════════════════════════════════════════════════════════════════

void Camera::set_move_speed(float speed) {
    m_move_speed = speed;
}
float Camera::get_move_speed() const {
    return m_move_speed;
}
void Camera::set_mouse_sensitivity(float sens) {
    m_mouse_sensitivity = sens;
}
float Camera::get_mouse_sensitivity() const {
    return m_mouse_sensitivity;
}

// ══════════════════════════════════════════════════════════════════════
// Legacy target-based API
// ══════════════════════════════════════════════════════════════════════

void Camera::set_target(const Vec3& target) {
    m_target = target;
    // Compute yaw/pitch from position→target direction
    Vec3 dir = glm::normalize(target - m_position);
    m_yaw    = glm::degrees(std::atan2(dir.z, dir.x));
    m_pitch  = glm::degrees(std::asin(std::clamp(dir.y, -1.f, 1.f)));
}

const Vec3& Camera::get_target() const {
    return m_target;
}
void Camera::set_up(const Vec3& up) {
    m_up = up;
}
const Vec3& Camera::get_up() const {
    return m_up;
}

// ══════════════════════════════════════════════════════════════════════
// Perspective
// ══════════════════════════════════════════════════════════════════════

void Camera::set_perspective(float fov_deg, float aspect, float near_plane, float far_plane) {
    m_fov    = fov_deg;
    m_aspect = aspect;
    m_near   = near_plane;
    m_far    = far_plane;
}

float Camera::get_fov() const {
    return m_fov;
}
float Camera::get_aspect() const {
    return m_aspect;
}
float Camera::get_near() const {
    return m_near;
}
float Camera::get_far() const {
    return m_far;
}

// ══════════════════════════════════════════════════════════════════════
// Direction vectors from yaw/pitch
// ══════════════════════════════════════════════════════════════════════

Vec3 Camera::get_forward() const {
    float yaw_rad   = glm::radians(m_yaw);
    float pitch_rad = glm::radians(m_pitch);
    return glm::normalize(
        Vec3(std::cos(yaw_rad) * std::cos(pitch_rad), std::sin(pitch_rad), std::sin(yaw_rad) * std::cos(pitch_rad)));
}

Vec3 Camera::get_right() const {
    return glm::normalize(glm::cross(get_forward(), Vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::update_vectors() {
    Vec3 forward = get_forward();
    m_target     = m_position + forward;
}

// ══════════════════════════════════════════════════════════════════════
// Input processing
// ══════════════════════════════════════════════════════════════════════

void Camera::process_keyboard(GLFWwindow* window, float delta_time) {
    Vec3 forward = get_forward();
    Vec3 right   = get_right();

    // Flatten forward to XZ plane for movement (don't fly up/down with pitch)
    Vec3 move_forward = glm::normalize(Vec3(forward.x, 0.0f, forward.z));

    float velocity = m_move_speed * delta_time;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        velocity *= 2.f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_position += move_forward * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_position -= move_forward * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_position -= right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_position += right * velocity;

    // Vertical movement (Q/E for up/down)
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        m_position.y += velocity;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        m_position.y -= velocity;

    // Clamp position to collision bounds (barrier/guardrail)
    if (m_collision_enabled) {
        m_position.x = std::clamp(m_position.x, m_bounds.min_x, m_bounds.max_x);
        m_position.y = std::clamp(m_position.y, m_bounds.min_y, m_bounds.max_y);
    }

    update_vectors();
}

void Camera::set_collision_bounds(float min_x, float max_x, float min_y, float max_y) {
    m_bounds.min_x = min_x;
    m_bounds.max_x = max_x;
    m_bounds.min_y = min_y;
    m_bounds.max_y = max_y;
}

void Camera::set_collision_enabled(bool enabled) {
    m_collision_enabled = enabled;
}

void Camera::process_mouse(float x_offset, float y_offset) {
    m_yaw += x_offset * m_mouse_sensitivity;
    m_pitch += y_offset * m_mouse_sensitivity;
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
    update_vectors();
}

// ══════════════════════════════════════════════════════════════════════
// Computed matrices
// ══════════════════════════════════════════════════════════════════════

Mat4 Camera::get_view_matrix() const {
    return glm::lookAt(m_position, m_position + get_forward(), m_up);
}

Mat4 Camera::get_projection_matrix() const {
    Mat4 proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
    proj[1][1] *= -1.0f;  // Vulkan clip space Y is inverted vs OpenGL
    return proj;
}

}  // namespace swish
