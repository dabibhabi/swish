#pragma once

#include "Entity.h"

#include <cmath>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CarEntity — driveable vehicle using a simplified bicycle-model.
//
// Arrow keys: UP = throttle, DOWN = brake/reverse, LEFT/RIGHT = steer.
// WASD remains free for the spectator camera.
//
// Coordinate convention: the loader normalizes the mesh so the nose
// points +X at yaw=0; heading and velocity share one definition:
//   forward = R_y(yaw)·(+X) = (cos(yaw_rad), 0, -sin(yaw_rad))
// so yaw=+90° faces -Z (down the road) and the body always lines up
// with the direction of travel.
// ══════════════════════════════════════════════════════════════════════

class CarEntity : public ModelEntity {
public:
    // Process keyboard input each frame before calling update().
    void handle_input(GLFWwindow* window, float dt);

    // Advance physics: drag → steering yaw rate → position integration.
    void update(float dt) override;

    // Clamp position to road bounds (X axis).
    void set_road_bounds(float min_x, float max_x) {
        m_min_x = min_x;
        m_max_x = max_x;
    }

    float get_speed() const { return m_forward_speed; }

    // Rain intensity [0,1] — drives the interior cabin "wash toward light gray"
    // tint applied in get_draw_calls (via the gbuffer.frag color.a sentinel).
    void set_rain_intensity(float intensity) { m_rain_intensity = intensity; }

    // World-space unit vector pointing toward the car's nose (+X at yaw=0).
    Vec3 get_forward() const {
        float yaw = glm::radians(m_rotation.y);
        return Vec3(std::cos(yaw), 0.f, -std::sin(yaw));
    }

    std::vector<DrawCall> get_draw_calls() const override;

    // Glass (BLEND) draw calls — same VBO/IBO as the opaque mesh but rendered
    // in the forward transparent pass (GlassPass).
    std::vector<DrawCall> get_windshield_draw_calls() const;

private:
    float m_forward_speed  = 0.f;  // world units / second, positive = forward
    float m_steering_angle = 0.f;  // degrees, negative = left, positive = right
    float m_rain_intensity = 0.f;  // [0,1] — drives the interior cabin wash tint

    // Per-frame longitudinal controls, set by handle_input(), applied in update().
    float m_throttle = 0.f;        // [0,1]
    float m_brake    = 0.f;        // [0,1]
    bool  m_reverse  = false;      // DOWN held while ~stopped → creep backward

    float m_min_x = -1e9f;
    float m_max_x = 1e9f;

    // Kinematic / control constants (~1000 WU/m scale, 1 mph ≈ 447 WU/s).
    // The longitudinal + tire DYNAMICS constants live in CarParams (CarPhysics.h);
    // this header keeps only the steering and speed-cap knobs.
    static constexpr float kWorldUnitsPerMeter = 1'000.f;  // 1 m = 1000 WU
    static constexpr float kMaxForwardSpeed = 92'000.f;  // ~205 mph (only for kMaxSpeed normalization)
    static constexpr float kMaxReverseSpeed = 12'000.f;  // ~27 mph in reverse
    static constexpr float kReverseAccel    = 6'000.f;   // WU/s²; gentle low-speed reverse
    static constexpr float kSpeedDeadZone   = 0.5f;
    static constexpr float kWheelLockToDeg  = 450.f;  // full lock-to-lock steering range
    static constexpr float kMaxSteer        = 35.f;   // degrees
    static constexpr float kSteerRatio      = kWheelLockToDeg / kMaxSteer;
    static constexpr float kSteerRate       = 90.f;     // degrees/s
    static constexpr float kSteerReturn     = 120.f;    // return-to-center rate
    // Real 992 Turbo S wheelbase ≈ 2.45 m (was 2.8 m).
    static constexpr float kWheelbase       = 2'450.f;  // 2.45 m in WU

    // Speed-dependent steering authority (variable ratio): the effective lock
    // is scaled by kSteerRefSpeed / (kSteerRefSpeed + |v|), so steering is
    // sharp when parking and gentle at speed. This caps the bicycle-model yaw
    // rate (∝ v·tan(δ)) and prevents spin-out near top speed.
    static constexpr float kSteerRefSpeed   = 12'000.f;  // WU/s; authority halves here

public:
    static constexpr float kMaxSpeed = kMaxForwardSpeed;  // exposed for normalization
};

}  // namespace swish
