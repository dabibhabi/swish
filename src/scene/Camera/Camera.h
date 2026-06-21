#pragma once

#include "../../utils/Types.h"

struct GLFWwindow;

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Camera — FPS-style POV camera with WASD + mouse look.
//
// Yaw/pitch angles define the view direction. No target point —
// the forward vector is computed from the angles.
//
// Coordinate system:
//   +X = right, +Y = up, -Z = forward (into screen)
//   Yaw 0 = looking along -Z, Yaw 90 = looking along -X
// ══════════════════════════════════════════════════════════════════════

class Camera {
public:
    Camera()  = default;
    ~Camera() = default;

    // ── Position ──────────────────────────────────────────────────
    void        set_position(const Vec3& pos);
    const Vec3& get_position() const;

    // ── Orientation (yaw/pitch in degrees) ────────────────────────
    void  set_yaw(float degrees);
    void  set_pitch(float degrees);
    float get_yaw() const;
    float get_pitch() const;

    // ── Movement speed (world units per second) ───────────────────
    void  set_move_speed(float speed);
    float get_move_speed() const;

    // ── Mouse sensitivity ─────────────────────────────────────────
    void  set_mouse_sensitivity(float sens);
    float get_mouse_sensitivity() const;

    // ── Legacy target-based API (kept for compatibility) ──────────
    void        set_target(const Vec3& target);
    const Vec3& get_target() const;
    void        set_up(const Vec3& up);
    const Vec3& get_up() const;

    // ── Perspective parameters ────────────────────────────────────
    void  set_perspective(float fov_deg, float aspect, float near_plane, float far_plane);
    float get_fov() const;
    float get_aspect() const;
    float get_near() const;
    float get_far() const;

    // ── Input processing ──────────────────────────────────────────
    void process_keyboard(GLFWwindow* window, float delta_time);
    void process_mouse(float x_offset, float y_offset);

    // ── Computed matrices ─────────────────────────────────────────
    Mat4 get_view_matrix() const;
    Mat4 get_projection_matrix() const;

    // ── Computed direction vectors ────────────────────────────────
    Vec3 get_forward() const;
    Vec3 get_right() const;

private:
    // Position
    Vec3 m_position = {0.0f, 0.0f, 0.0f};

    // Orientation (Euler angles in degrees)
    float m_yaw   = -90.0f;  // -90 = looking along -Z (default forward)
    float m_pitch = -5.0f;   // slight downward look (driving perspective)

    // Legacy target (updated from yaw/pitch)
    Vec3 m_target = {0.0f, 0.0f, -1.0f};
    Vec3 m_up     = {0.0f, 1.0f, 0.0f};

    // Perspective
    float m_fov    = 65.0f;
    float m_aspect = 16.0f / 9.0f;
    float m_near   = 10.0f;
    float m_far    = 2000000.0f;

    // Control tuning
    float m_move_speed        = 26800.0f;  // ~60 mph in world units/sec
    float m_mouse_sensitivity = 0.1f;

    // Collision bounds
    bool  m_collision_enabled = false;
    float m_bounds_min_x      = 0.0f;
    float m_bounds_max_x      = 0.0f;
    float m_bounds_min_y      = 0.0f;
    float m_bounds_max_y      = 0.0f;

    // Internal: update direction vectors from yaw/pitch
    void update_vectors();

public:
    void set_collision_bounds(float min_x, float max_x, float min_y, float max_y);
    void set_collision_enabled(bool enabled);
};

}  // namespace swish
