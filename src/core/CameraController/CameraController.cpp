#include "CameraController.h"

#include "scene/Camera/Camera.h"
#include "scene/Entity/CarEntity.h"
#include "utils/math/Noise.h"
#include "utils/math/Smoothing.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace swish {

namespace um = util::math;  // exp_smooth*, quasi_noise (utils/math/)

namespace {

// ── Cockpit seat (car-mesh space: metres, nose +X, ground y = 0, driver side -Z) ──
// The entity scale (1000) converts it to world units through the model matrix. The
// steering-wheel centre sits at (0.18, 0.76, -0.34); the eye goes behind and above it
// (roof tops out at 1.29).
const Vec3 kSeatEye(-0.32f, 1.05f, -0.34f);

// Heading yaw ease: only the HEADING-derived term is smoothed (F3) — a light head/neck
// inertia cue against fast steering. Mouse look is added on top RAW (never smoothed).
constexpr float kHeadingYawSmoothRate = 18.0f;  // 1/s, tau ~56ms: a cushion, not lag

// ── F1: speed/acceleration FOV kick ──────────────────────────────────
constexpr float kFovSpeedMaxDeg     = 10.0f;    // deg, at top speed
constexpr float kFovKickMaxDeg      = 4.0f;     // deg, extra transient
constexpr float kFovSpeedSmoothRate = 2.5f;     // 1/s (~400ms) — the cruising term
constexpr float kFovKickAttackRate  = 10.0f;    // 1/s (~100ms) — snaps in fast
constexpr float kFovKickReleaseRate = 3.0f;     // 1/s (~330ms) — fades out slower
constexpr float kAccelRefForKick    = 8000.0f;  // WU/s^2 (~8 m/s^2) accel that maxes the kick

// ── F1: speed-scaled camera shake ────────────────────────────────────
constexpr float kShakePosVerticalWU = 3.0f;  // ~3mm max vertical jitter
constexpr float kShakePosLateralWU  = 1.5f;  // ~1.5mm max lateral jitter
constexpr float kShakeYawDeg        = 0.05f;
constexpr float kShakePitchDeg      = 0.08f;

constexpr float kHeliZoom      = 35000.0f;   // WU/s orbit-radius change (Q/E)
constexpr float kHeliRadiusMax = 250000.0f;  // WU
constexpr float kHeliRadiusMin = 20000.0f;   // WU

constexpr float kMouseLookSens = 0.1f;

// Place the camera at `eye` looking toward `target`, then add mouse look offsets.
void look_at_with_offset(Camera& cam, const Vec3& eye, const Vec3& target, float look_yaw_deg, float look_pitch_deg) {
    cam.set_position(eye);
    Vec3  to     = target - eye;
    float len_sq = glm::dot(to, to);
    if (len_sq < 1e-8f) return;
    Vec3  dir   = to * (1.0f / std::sqrt(len_sq));
    float yaw   = glm::degrees(std::atan2(dir.z, dir.x));
    float pitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    cam.set_yaw(yaw + look_yaw_deg);
    cam.set_pitch(pitch + look_pitch_deg);
}

// Car-mesh-local point → world (nose +X, ground y≈0, driver −Z; scale via model).
Vec3 car_local_to_world(const CarEntity& car, const Vec3& local_m) {
    return Vec3(car.get_model_matrix() * Vec4(local_m, 1.0f));
}

}  // namespace

void CameraController::set_mode(CameraMode mode) {
    m_mode       = mode;
    m_look_yaw   = 0.f;
    m_look_pitch = 0.f;
    // Force the yaw-follow to re-initialize from the current heading next
    // cockpit frame (no stale baseline / no long spin-up), and drop the FOV
    // kick so re-entering cockpit doesn't inherit a leftover transient.
    m_cam_yaw_initialized = false;
    m_fov_kick            = 0.f;
}

bool CameraController::mouse_look(float x_offset, float y_offset) {
    if (!camera_mode_car_attached(m_mode)) return false;
    // Mouse look relative to the car-attached framing. Helicopter: orbit
    // around the car (yaw = azimuth, pitch = elevation bias) — never free-look
    // away so the car stays framed while driving.
    const bool heli = m_mode == CameraMode::Helicopter;
    m_look_yaw      = std::clamp(m_look_yaw + x_offset * kMouseLookSens, heli ? -180.f : -150.f, heli ? 180.f : 150.f);
    m_look_pitch    = std::clamp(m_look_pitch + y_offset * kMouseLookSens, heli ? -40.f : -60.f, heli ? 40.f : 60.f);
    return true;
}

void CameraController::poll_input(GLFWwindow* window, Camera* camera, float dt, bool cursor_captured, bool debug_edit) {
    // Free-fly WASD only.
    if (camera && cursor_captured && m_mode == CameraMode::FreeFly && !debug_edit) {
        camera->process_keyboard(window, dt);
    }

    // Helicopter height: Q/E while attached (does not steal free-fly Q/E).
    if (m_mode == CameraMode::Helicopter && cursor_captured && !debug_edit) {
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            m_heli_height_wu = std::min(m_heli_height_wu + kHeliZoom * dt, kHeliRadiusMax);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            m_heli_height_wu = std::max(m_heli_height_wu - kHeliZoom * dt, kHeliRadiusMin);
    }
}

// ── Camera modes (C cycles) ───────────────────────────────────────────
// Cockpit keeps the seat-rigid + FOV-kick + shake path. Other car-attached modes place
// the eye from car pose / mesh locals and look at a framing target; mouse look is
// additive. Free-fly is WASD-only (poll_input) and pins FOV to base.
void CameraController::update(Camera* camera, const CarEntity* car, float dt, bool debug_edit) {
    if (camera && car && m_mode == CameraMode::Cockpit) {
        update_cockpit(*camera, *car, dt, debug_edit);
    } else if (camera && car && camera_mode_car_attached(m_mode)) {
        update_exterior(*camera, *car);
    } else if (camera && m_mode == CameraMode::FreeFly) {
        update_free_fly(*camera);
    }
}

void CameraController::update_cockpit(Camera& camera, const CarEntity& car, float dt, bool debug_edit) {
    // Seat position is a RIGID attach to the car model matrix — never
    // smoothed. The dashboard/steering wheel are meshes baked into
    // that SAME model matrix, so easing the eye toward it separately
    // would desync the head from a dash that doesn't itself move —
    // the classic chase-cam "swimming" artifact. Any felt motion here
    // comes only from the tiny shake offset added below, which is
    // small enough (single-digit mm) not to read as desync.
    Vec3 eye = car_local_to_world(car, kSeatEye);

    // Camera forward = (cos yaw, ·, sin yaw); car forward =
    // (cos heading, 0, -sin heading) -> camera yaw = -heading.
    // Mouse look (m_look_yaw/m_look_pitch) is added on top RAW so it never
    // loses responsiveness (todo.md F1/F3 — do not smooth mouse deltas into mush).
    float target_heading_yaw = -car.get_rotation().y;
    if (!m_cam_yaw_initialized) {
        m_cam_yaw_smoothed    = target_heading_yaw;  // snap on first frame / mode re-entry
        m_cam_yaw_initialized = true;
    }
    if (!debug_edit)
        m_cam_yaw_smoothed =
            um::exp_smooth_angle_deg(m_cam_yaw_smoothed, target_heading_yaw, kHeadingYawSmoothRate, dt);
    float yaw   = m_cam_yaw_smoothed + m_look_yaw;
    float pitch = m_look_pitch;

    if (!debug_edit) {
        const float speed_ratio = update_cockpit_fov(camera, car, dt);
        apply_cockpit_shake(car, dt, speed_ratio, eye, yaw, pitch);
    }

    camera.set_position(eye);
    camera.set_yaw(yaw);
    camera.set_pitch(pitch);
}

float CameraController::update_cockpit_fov(Camera& camera, const CarEntity& car, float dt) {
    float speed_ratio = std::clamp(car.get_speed() / CarEntity::kMaxSpeed, 0.0f, 1.0f);
    float speed_ease  = speed_ratio * speed_ratio * (3.0f - 2.0f * speed_ratio);  // smoothstep
    m_fov_speed_term  = um::exp_smooth(m_fov_speed_term, kFovSpeedMaxDeg * speed_ease, kFovSpeedSmoothRate, dt);

    float accel       = (car.get_speed() - m_prev_car_speed) / std::max(dt, 1e-4f);
    float accel_norm  = std::clamp(accel / kAccelRefForKick, 0.0f, 1.0f);  // widen on accel only
    float kick_target = kFovKickMaxDeg * accel_norm;
    float kick_rate   = kick_target > m_fov_kick ? kFovKickAttackRate : kFovKickReleaseRate;
    m_fov_kick        = um::exp_smooth(m_fov_kick, kick_target, kick_rate, dt);
    m_prev_car_speed  = car.get_speed();

    camera.set_fov(m_base_fov + m_fov_speed_term + m_fov_kick);
    return speed_ratio;
}

void CameraController::apply_cockpit_shake(const CarEntity& car, float dt, float speed_ratio, Vec3& eye, float& yaw,
                                           float& pitch) {
    m_shake_time += dt;
    float shake_envelope = speed_ratio * speed_ratio;

    Vec3 carFwd   = car.get_forward();
    Vec3 carRight = glm::normalize(glm::cross(carFwd, Vec3(0.f, 1.f, 0.f)));

    float vertical_shake =
        um::quasi_noise(m_shake_time, 13.0f, 7.0f, 19.0f, 0.0f) * kShakePosVerticalWU * shake_envelope;
    float lateral_shake = um::quasi_noise(m_shake_time, 11.0f, 5.0f, 17.0f, 1.7f) * kShakePosLateralWU * shake_envelope;
    float yaw_shake     = um::quasi_noise(m_shake_time, 9.0f, 4.0f, 15.0f, 3.4f) * kShakeYawDeg * shake_envelope;
    float pitch_shake   = um::quasi_noise(m_shake_time, 14.5f, 6.5f, 21.0f, 5.1f) * kShakePitchDeg * shake_envelope;

    eye += Vec3(0.f, 1.f, 0.f) * vertical_shake + carRight * lateral_shake;
    yaw += yaw_shake;
    pitch += pitch_shake;
}

void CameraController::update_exterior(Camera& camera, const CarEntity& car) {
    // Exterior / debug cameras — FOV pinned (no cockpit kick).
    camera.set_fov(m_mode == CameraMode::Helicopter ? std::max(m_base_fov, 70.0f) : m_base_fov);
    m_fov_speed_term = 0.0f;
    m_fov_kick       = 0.0f;

    const Vec3 carPos = car.get_position();
    const Vec3 fwd    = car.get_forward();
    const Vec3 up(0.f, 1.f, 0.f);

    Vec3 eye;
    Vec3 target;

    switch (m_mode) {
        case CameraMode::Chase: {
            // Tight third-person: ~6.5 m back, 2.2 m up; look past the nose.
            eye    = carPos - fwd * 6500.0f + up * 2200.0f;
            target = carPos + fwd * 4000.0f + up * 1000.0f;
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
        }
        case CameraMode::Hood: {
            eye    = car_local_to_world(car, Vec3(0.85f, 1.08f, 0.0f));
            target = eye + fwd * 40000.0f + up * (-800.0f);
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
        }
        case CameraMode::Bumper: {
            eye    = car_local_to_world(car, Vec3(2.35f, 0.55f, 0.0f));
            target = eye + fwd * 40000.0f;
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
        }
        case CameraMode::Roof: {
            eye    = car_local_to_world(car, Vec3(0.0f, 1.42f, 0.0f));
            target = eye + fwd * 50000.0f + up * (-2500.0f);
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
        }
        case CameraMode::Side: {
            // Driver-side exterior (~4 m out), looking at cabin.
            eye    = car_local_to_world(car, Vec3(0.0f, 1.15f, -4.0f));
            target = car_local_to_world(car, Vec3(0.2f, 1.0f, -0.3f));
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
        }
        case CameraMode::Helicopter:
            update_helicopter(camera, car);
            break;
        default:
            eye    = carPos + up * 3000.0f;
            target = carPos + fwd * 10000.0f;
            look_at_with_offset(camera, eye, target, m_look_yaw, m_look_pitch);
            break;
    }
}

void CameraController::update_helicopter(Camera& camera, const CarEntity& car) {
    const Vec3 carPos = car.get_position();
    const Vec3 fwd    = car.get_forward();
    const Vec3 up(0.f, 1.f, 0.f);

    // True follow-orbit: mouse yaw spins around the car, mouse pitch
    // changes elevation; eye always tracks carPos and look-at is the
    // car (no free-look that loses the vehicle while driving).
    Vec3  right = glm::cross(fwd, up);
    float rlen  = glm::length(right);
    if (rlen > 1e-6f)
        right *= (1.0f / rlen);
    else
        right = Vec3(1.f, 0.f, 0.f);

    const float elev_deg = std::clamp(52.0f - m_look_pitch, 18.0f, 85.0f);
    const float elev     = glm::radians(elev_deg);
    const float azim     = glm::radians(m_look_yaw);
    const Vec3  flatDir  = glm::normalize((-fwd) * std::cos(azim) + right * std::sin(azim));
    const float radius   = m_heli_height_wu;
    const Vec3  eye      = carPos + flatDir * (radius * std::cos(elev)) + up * (radius * std::sin(elev));
    const Vec3  target   = carPos + up * 800.0f;
    look_at_with_offset(camera, eye, target, 0.0f, 0.0f);
}

void CameraController::update_free_fly(Camera& camera) {
    // Free-fly never modulates FOV — always pinned to base, and the
    // eased terms reset so re-entering cockpit doesn't inherit a
    // stale widened value.
    camera.set_fov(m_base_fov);
    m_fov_speed_term = 0.0f;
    m_fov_kick       = 0.0f;
}

}  // namespace swish
