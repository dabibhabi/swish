#pragma once

#include "scene/CameraMode.h"
#include "utils/types/Types.h"

struct GLFWwindow;

namespace swish {

class Camera;
class CarEntity;

// ══════════════════════════════════════════════════════════════════════
// CameraController — per-frame camera placement for every CameraMode, split
// out of App::run() (docs: src/core/README.md).
//
// Owns all camera-feel state that used to be bare App members: the active mode,
// mouse-look offsets, the helicopter orbit radius, the cockpit heading-yaw
// smoothing, the base FOV and its speed/kick widening terms, and the shake clock.
// It does NOT own the Camera or the car — both are passed in each call — so it has
// no Vulkan knowledge and App keeps a single unique_ptr-owned subsystem list.
//
// Per-frame contract (called by App in this order):
//   poll_input(...)   free-fly WASD + helicopter Q/E   (was: input block in run())
//   update(...)       pose/FOV for the active mode      (was: camera block in run())
// ══════════════════════════════════════════════════════════════════════
class CameraController {
public:
    CameraMode mode() const { return m_mode; }

    // Switch preset. Zeroes the mouse-look offsets and forces the cockpit yaw-follow
    // to re-initialise from the current heading, and drops the FOV kick so re-entering
    // cockpit never inherits a stale baseline. (Log output stays in App::set_camera_mode.)
    void set_mode(CameraMode mode);

    // Base FOV read back from the Camera once after set_perspective(): the single source
    // of truth the speed/kick terms widen away from and return to at rest.
    void set_base_fov(float fov_deg) { m_base_fov = fov_deg; }

    // Mouse look for car-attached modes (relative to the car-attached framing; helicopter
    // orbits the car). Returns true when consumed; false in free-fly, where the caller
    // forwards the delta to Camera::process_mouse.
    bool mouse_look(float x_offset, float y_offset);

    // Keyboard: free-fly WASD (Camera::process_keyboard) and helicopter Q/E orbit-radius.
    void poll_input(GLFWwindow* window, Camera* camera, float dt, bool cursor_captured, bool debug_edit);

    // Place the eye / set the FOV for the active mode. `camera`/`car` may be null (no-op
    // for the modes that need them). `debug_edit` freezes the cockpit smoothing/kick/shake.
    void update(Camera* camera, const CarEntity* car, float dt, bool debug_edit);

private:
    void update_cockpit(Camera& camera, const CarEntity& car, float dt, bool debug_edit);
    // Speed/acceleration FOV kick; returns the speed ratio (0..1) shared with the shake.
    float update_cockpit_fov(Camera& camera, const CarEntity& car, float dt);
    void  apply_cockpit_shake(const CarEntity& car, float dt, float speed_ratio, Vec3& eye, float& yaw, float& pitch);
    void  update_exterior(Camera& camera, const CarEntity& car);
    void  update_helicopter(Camera& camera, const CarEntity& car);
    void  update_free_fly(Camera& camera);

    CameraMode m_mode = CameraMode::Cockpit;

    float m_look_yaw   = 0.0f;  // mouse-look offsets (car-attached modes)
    float m_look_pitch = 0.0f;

    // Helicopter orbit radius (WU). Q/E nudge; mouse orbits around the car.
    float m_heli_height_wu = 55000.0f;  // ~55 m — high enough to see exits, low enough to read gantries

    // Cockpit feel (tasks/todo.md F1/F3). The seat position rides the car model matrix
    // rigidly (never smoothed); only the heading-derived yaw term is eased.
    float m_cam_yaw_smoothed    = 0.0f;
    bool  m_cam_yaw_initialized = false;

    float m_base_fov = 65.0f;
    // Speed-linked FOV widening (slow, ~400ms ease) and a fast-attack/slow-release "kick"
    // on hard acceleration. Both chase their targets through utils/math/Smoothing.h.
    float m_fov_speed_term = 0.0f;
    float m_fov_kick       = 0.0f;
    float m_prev_car_speed = 0.0f;  // WU/s, for a finite-difference accel estimate

    // Deterministic shake phase — advances by wall-clock dt (never frame count).
    float m_shake_time = 0.0f;
};

}  // namespace swish
