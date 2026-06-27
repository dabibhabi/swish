#pragma once

#include "Entity.h"

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

    std::vector<DrawCall> get_draw_calls() const override;

private:
    float m_forward_speed  = 0.f;   // world units / second, positive = forward
    float m_steering_angle = 0.f;   // degrees, negative = left, positive = right

    float m_min_x = -1e9f;
    float m_max_x =  1e9f;

    // Physics constants (tuned for the ~1000 WU/m scale)
    static constexpr float kMaxForwardSpeed = 30'000.f;   // ~108 km/h
    static constexpr float kMaxReverseSpeed =  8'000.f;
    static constexpr float kAccel           = 18'000.f;   // WU/s²
    static constexpr float kBrakeAccel      = 24'000.f;
    static constexpr float kDragCoeff       =      2.5f;  // per-second linear drag
    static constexpr float kSpeedDeadZone  =      0.5f;
    static constexpr float kWheelLockToDeg =    450.f;   // full lock-to-lock steering range
    static constexpr float kMaxSteer        =     35.f;   // degrees
    static constexpr float kSteerRatio      = kWheelLockToDeg / kMaxSteer;
    static constexpr float kSteerRate       =     90.f;   // degrees/s
    static constexpr float kSteerReturn     =    120.f;   // return-to-center rate
    static constexpr float kWheelbase       =  2'800.f;   // ~2.8m wheelbase in WU
};

}  // namespace swish
