#pragma once

// ── RigidBody.h ──────────────────────────────────────────────────────
// Purpose   : a generic 6-DOF rigid body (position, orientation quaternion, linear + angular velocity,
//             mass, body-frame inertia tensor) with force / torque accumulators and a semi-implicit
//             (symplectic) Euler step. First user: the stub rigid-body chassis
//             (scene/Entity/RigidChassis.h), which sits ALONGSIDE the bicycle model.
// Units     : consistent-unit, but the engine feeds it SI: metres, kilograms, seconds, newtons, radians.
//             Pure physics headers stay SI (CLAUDE.md "Units"); CarEntity converts to/from world units at
//             its boundary with utils/units/Units.h (kWuPerMetre). Nothing here types 1000 or pi.
// Complexity: O(1) per step, header-only, no allocation.
// Precision : float. The orientation update is the exact exponential map (rotate by |w| dt about w),
//             then the quaternion is renormalised, so |q| stays 1 over long runs.
// Example   : namespace up = swish::util::physics;
//             up::RigidBody b;  b.set_mass_properties(1670.f, up::RigidBodyParams{}.box_inertia());
//             b.add_force_at_point(f_world, p_world);  b.step(1.f / 240.f);
// See also  : scene/Entity/RigidChassis.h, docs/lessons-from-chrono/ (non-smooth contact, Lie-group rotations)
// Test      : tests/utils/test_physics_RigidBody.cpp
//
// ── Design note ──────────────────────────────────────────────────────
// State is held in the WORLD frame (position, v, w) with the inertia tensor in the BODY frame. Per step:
//   1. v  += F / m * h                        velocity first ...
//   2. L   = I_w w + tau h                    angular MOMENTUM carries the torque (this is the gyroscopic
//                                             term: I w' = tau - w x I w  <=>  dL/dt = tau)
//   3. x  += v * h;  q = exp(h w_mid) q       ... then position / orientation with the new velocities,
//                                             w_mid = I_w^-1 L at the half-step pose (midpoint rule)
//   4. w   = I_w^-1 L at the new pose
// Why not an explicit  w x (I w)  term: it GAINS energy for a spinning asymmetric body, and the implicit
// (Catto GDC 2015) form damps |L| (measured -4% over 10 s at h = 1/240 here). Carrying L makes a
// torque-free body conserve it exactly (to float rounding), with kinetic energy good to O(h^2).
// World inertia (R I R^T) is exposed for callers and energy checks.
// TODO(stub): no sleeping, no CCD, no constraints/joints; contacts live in the caller (impulse-based
//             non-smooth contact is the full model, see docs/lessons-from-chrono/).

#include "utils/types/Types.h"

#include <cmath>

namespace swish::util::physics {

// Body axes follow the car mesh: +X nose, +Y up, +Z right (= forward x up).
struct RigidBodyParams {
    // 911 Turbo S-ish defaults (992: 4.535 x 1.903 x 1.303 m, 1670 kg DIN). CarParams::mass and
    // TireParams::hCg own the real values; make_chassis_params() overrides these two from them.
    float mass_kg      = 1670.0f;
    float length_m     = 4.535f;  // along X (nose)
    float height_m     = 1.303f;  // along Y (up)
    float width_m      = 1.903f;  // along Z (right)
    float cog_height_m = 0.45f;   // centre of gravity above ground (low: rear-engine car, see TireParams::hCg)

    // Inertia about the CENTRE OF GRAVITY, body axes, from a solid-box approximation:
    //   Ixx (roll)  = m/12 (h^2 + w^2) + m d^2
    //   Iyy (yaw)   = m/12 (l^2 + w^2)
    //   Izz (pitch) = m/12 (l^2 + h^2) + m d^2
    // with d = h/2 - cog_height (the box centre sits above the CoG; parallel-axis term for the two
    // horizontal axes). Lands near the 911's published ~3000 kg m^2 yaw inertia (TireParams::iz = 3050).
    // TODO(stub): replace with measured / mass-point inertia (engine, fuel, occupants).
    mat3 box_inertia() const {
        const float k   = mass_kg / 12.0f;
        const float d   = 0.5f * height_m - cog_height_m;
        const float ixx = k * (height_m * height_m + width_m * width_m) + mass_kg * d * d;
        const float iyy = k * (length_m * length_m + width_m * width_m);
        const float izz = k * (length_m * length_m + height_m * height_m) + mass_kg * d * d;
        return mat3(vec3(ixx, 0.f, 0.f), vec3(0.f, iyy, 0.f), vec3(0.f, 0.f, izz));
    }
};

// Euler angles (radians) for the engine's model-matrix order  R = Ry(yaw) * Rx(pitch) * Rz(roll)
// (Entity::get_model_matrix). Returns (pitch.x, yaw.y, roll.z) so it drops straight into
// Entity::m_rotation (after glm::degrees). Gimbal lock at pitch = +-90 deg is irrelevant for a car.
inline vec3 euler_yxz_from_rotation(const mat3& r) {
    // r[col][row]; for Ry Rx Rz: r(1,2) = -sin(pitch), r(1,0)/r(1,1) = tan(roll), r(0,2)/r(2,2) = tan(yaw)
    const float sp    = std::fmax(-1.0f, std::fmin(1.0f, -r[2][1]));
    const float pitch = std::asin(sp);
    const float yaw   = std::atan2(r[2][0], r[2][2]);
    const float roll  = std::atan2(r[0][1], r[1][1]);
    return vec3(pitch, yaw, roll);
}

class RigidBody {
public:
    // ── Mass properties ───────────────────────────────────────────────
    void set_mass_properties(float mass_kg, const mat3& inertia_body) {
        m_mass        = mass_kg;
        m_inv_mass    = mass_kg > 0.f ? 1.0f / mass_kg : 0.f;
        m_inertia     = inertia_body;
        m_inv_inertia = glm::inverse(inertia_body);
    }
    float       mass() const { return m_mass; }
    const mat3& inertia_body() const { return m_inertia; }
    const mat3& inv_inertia_body() const { return m_inv_inertia; }

    // ── State ─────────────────────────────────────────────────────────
    const vec3& position() const { return m_pos; }  // centre of gravity, world
    const quat& orientation() const { return m_q; }
    const vec3& linear_velocity() const { return m_v; }
    const vec3& angular_velocity() const { return m_w; }  // world frame, rad/s

    void set_position(const vec3& p) { m_pos = p; }
    void set_orientation(const quat& q) {
        m_q = glm::normalize(q);
        m_w = inv_inertia_world() * m_l;  // L is the conserved state; w follows the pose
    }
    void set_linear_velocity(const vec3& v) { m_v = v; }
    void set_angular_velocity(const vec3& w) {
        m_w = w;
        m_l = inertia_world() * w;
    }

    mat3 rotation() const { return glm::mat3_cast(m_q); }
    mat3 inertia_world() const {
        const mat3 r = rotation();
        return r * m_inertia * glm::transpose(r);
    }
    mat3 inv_inertia_world() const {
        const mat3 r = rotation();
        return r * m_inv_inertia * glm::transpose(r);
    }

    // Velocity of the material point currently at world position p.
    vec3 point_velocity(const vec3& p_world) const { return m_v + glm::cross(m_w, p_world - m_pos); }

    // ── Accumulators (cleared by step) ────────────────────────────────
    void add_force(const vec3& f_world) { m_force += f_world; }
    void add_torque(const vec3& tau_world) { m_torque += tau_world; }
    // Force applied at a world point: also adds the moment (p - CoG) x F.
    void add_force_at_point(const vec3& f_world, const vec3& p_world) {
        m_force += f_world;
        m_torque += glm::cross(p_world - m_pos, f_world);
    }
    const vec3& force_accumulator() const { return m_force; }
    const vec3& torque_accumulator() const { return m_torque; }
    void        clear_accumulators() {
        m_force  = vec3(0.f);
        m_torque = vec3(0.f);
    }

    // ── Diagnostics ───────────────────────────────────────────────────
    vec3  angular_momentum_world() const { return m_l; }  // L = I_w w, carried as the state
    float kinetic_energy() const {
        const vec3 wb = glm::transpose(rotation()) * m_w;
        return 0.5f * m_mass * glm::dot(m_v, m_v) + 0.5f * glm::dot(wb, m_inertia * wb);
    }

    // One semi-implicit Euler step of h seconds, then the accumulators are cleared.
    void step(float h) {
        // Linear: velocity first, then position with the new velocity.
        m_v += m_force * (m_inv_mass * h);
        m_pos += m_v * h;

        // Angular: L is the state that torque changes; w = I_w^-1 L follows the orientation. Advancing L by
        // tau h and re-deriving w IS the gyroscopic term (I w' = tau - w x I w  <=>  dL/dt = tau).
        m_l += m_torque * h;
        const vec3 l = m_l;
        // Midpoint orientation: w at the half-step pose, then a full exponential step with it (2nd order,
        // no secular drift in |L| or kinetic energy for a torque-free body).
        const quat q_half = rotate_by(m_q, inv_inertia_world() * l, 0.5f * h);
        m_w               = inv_inertia_world(q_half) * l;
        m_q               = rotate_by(m_q, m_w, h);
        m_w               = inv_inertia_world() * l;  // consistent with the new pose

        clear_accumulators();
    }

private:
    // q advanced by rotating at angular velocity w (world) for time t: exact exponential map, renormalised.
    static quat rotate_by(const quat& q, const vec3& w, float t) {
        const float len = glm::length(w);
        if (len * t < 1e-12f)
            return glm::normalize(q);
        return glm::normalize(glm::angleAxis(len * t, w / len) * q);
    }
    mat3 inv_inertia_world(const quat& q) const {
        const mat3 r = glm::mat3_cast(q);
        return r * m_inv_inertia * glm::transpose(r);
    }

    float m_mass     = 1.0f;
    float m_inv_mass = 1.0f;
    mat3  m_inertia{1.0f};
    mat3  m_inv_inertia{1.0f};

    vec3 m_pos{0.f};
    quat m_q{1.f, 0.f, 0.f, 0.f};
    vec3 m_v{0.f};
    vec3 m_w{0.f};  // derived: I_w^-1 L
    vec3 m_l{0.f};  // angular momentum about the CoG, world frame: the state torque acts on

    vec3 m_force{0.f};
    vec3 m_torque{0.f};
};

}  // namespace swish::util::physics
