#pragma once

// 6-DOF rigid-body chassis on four spring-damper wheel contacts (STUB). SI units (m, kg, s, N); CarEntity
// converts world units at its boundary. Header-only, no renderer / GLFW dependency, so it is unit-tested
// without a GPU. Part of the CarPhysics family but NOT included by CarPhysics.h: the bicycle model stays
// the default and this is the alternative the debug UI can switch to (PhysicsModel below).
//
// ── Design note ───────────────────────────────────────────────────────
//   * One utils::physics::RigidBody at the centre of gravity (body axes: +X nose, +Y up, +Z right).
//   * Four contacts at the loader's WheelFrame hub positions. Each is a spring-damper along world +Y:
//       pen = ground_y - (hub_y - r) + preload,   Fn = max(0, k pen - c v_y)   (cannot pull the road up)
//     k and c come from the Phase-B design inputs (SuspensionParams fn / zeta via spring_from_ride_freq),
//     so a mass retarget re-derives them. `preload` = static_load / k: the spring's free length is longer
//     than the as-modelled ride height by exactly the static sag, so the design pose IS the equilibrium
//     and the mesh does not sink on switch-on. With preload = 0 the sag is the textbook mg/(4k).
//   * Tire force at the contact patch (hub - r up): lateral Fy = -C_alpha * v_s / (|v_f| + v_ref) clamped
//     to mu Fn, then longitudinal Fx clamped to the REMAINING friction circle (Tire.h
//     friction_circle_fx_max), so cornering and braking/driving share one budget per wheel.
//     The per-axle longitudinal force comes from the existing longitudinal model (longitudinal_accel's
//     `split`), shared left/right in proportion to each wheel's Fn.
//   * Ground is the flat plane y = ground_y; the caller passes it per advance().
//   * Fixed substep h = 1/240 s (same as Phase-B), clamped count per frame. Stability: the stiffest mode
//     here is the rear corner, omega_n h = 2 pi 2.0 / 240 = 0.052, far below the bound.
//
// TODO(stub): road-height query per wheel (RoadGeometry::road_surface_y at each patch, slope + crown).
// TODO(stub): unsprung mass per wheel (wheel-hop mode), bump stops (SuspensionParams::travelMaxM).
// TODO(stub): anti-roll bars (arb_target_shift_m) and aero downforce / pitch moment.
// TODO(stub): wheel spin DOF + slip ratio + a real tire curve (Pacejka / Fiala); today Fx is an
//             axle-level request clamped by the friction circle and Fy is a saturating linear slip model.
// TODO(stub): contact patch is taken straight below the hub; a rolled wheel's patch shifts by r sin(roll).
// TODO(stub): camber thrust, self-aligning torque, combined-slip direction coupling.

#include "CarParams.h"
#include "Tire.h"
#include "WheelKinematics.h"
#include "utils/physics/RigidBody.h"
#include "utils/types/Types.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace swish {

// Which model drives the car. Bicycle is the shipped default; RigidBody is debug-only (CarEntity).
// `Count` makes it usable with util::meta::next_enum / an ImGui combo.
enum class PhysicsModel : int { Bicycle = 0, RigidBody, Count };

// Corner order matches CarEntity: 0 FL, 1 FR, 2 BL, 3 BR.
struct ChassisCorner {
    vec3  hub_body_m{0.f};  // wheel centre relative to the CoG, body frame
    float radius_m   = 0.34f;
    float k_n_per_m  = 0.f;
    float c_ns_per_m = 0.f;
    float preload_m  = 0.f;
    bool  front      = false;
};

struct RigidChassisParams {
    util::physics::RigidBodyParams body{};
    std::array<ChassisCorner, 4>   corner{};
    vec3                           cog_mesh_m{0.f};  // CoG in the car-mesh frame (entity origin = ground under the car)
    float                          c_alpha_front_n_per_rad = 0.f;   // PER WHEEL
    float                          c_alpha_rear_n_per_rad  = 0.f;   // PER WHEEL
    float                          slip_ref_speed_mps      = 1.5f;  // regularises slip angle at standstill
    float                          gravity                 = 9.81f;
};

// Per-step driver inputs.
struct ChassisInput {
    float steer_rad  = 0.f;   // road-wheel angle, right-positive (CarEntity::m_steering_angle convention)
    float fx_front_n = 0.f;   // signed axle tire force along the heading (+ = forward), both front wheels
    float fx_rear_n  = 0.f;   // same, rear axle
    vec3  body_force_n{0.f};  // extra world force at the CoG (aero drag)
    float mu = CarParams{}.tireMu;
};

struct ChassisCornerState {
    float pen_m = 0.f;  // spring compression incl. preload (0 = airborne)
    float fn_n  = 0.f;  // normal force
    float fx_n  = 0.f;  // longitudinal tire force actually applied
    float fy_n  = 0.f;  // lateral tire force actually applied
};

// Build chassis parameters from the loader's wheel pivot positions (mesh space, metres; origin on the
// ground under the car, so hub y == rolling radius) and the existing car/tire/suspension params.
// The CoG is placed from the static axle split: x so the front axle carries b/L of the weight, y = hCg.
inline RigidChassisParams make_chassis_params(const std::array<vec3, 4>&  hub_mesh_m,
                                              const std::array<float, 4>& radius_m, const CarParams& cp,
                                              const TireParams& tp, const SuspensionParams& sp) {
    RigidChassisParams out;
    out.body.mass_kg      = cp.mass;
    out.body.cog_height_m = tp.hCg;
    out.gravity           = cp.gravity;

    const float L      = tp.a + tp.b;
    const float xFront = 0.5f * (hub_mesh_m[0].x + hub_mesh_m[1].x);
    const float xRear  = 0.5f * (hub_mesh_m[2].x + hub_mesh_m[3].x);
    const float zMid   = 0.25f * (hub_mesh_m[0].z + hub_mesh_m[1].z + hub_mesh_m[2].z + hub_mesh_m[3].z);
    const vec3  cogMesh(xFront - (tp.a / L) * (xFront - xRear), tp.hCg, zMid);
    out.cog_mesh_m = cogMesh;

    // Same corner masses CarEntity::update_wheel_visuals() derives (sprung mass per corner).
    const float sprungTotal = cp.mass - 4.f * sp.unsprungMassKg;
    const float massFront   = sprungTotal * (tp.b / L) * 0.5f;
    const float massRear    = sprungTotal * (tp.a / L) * 0.5f;
    const float loadFront   = cp.mass * cp.gravity * (tp.b / L) * 0.5f;  // static N per front wheel
    const float loadRear    = cp.mass * cp.gravity * (tp.a / L) * 0.5f;

    for (int c = 0; c < 4; ++c) {
        ChassisCorner& k = out.corner[c];
        k.front          = c < 2;
        k.hub_body_m     = hub_mesh_m[c] - cogMesh;
        k.radius_m       = radius_m[c];
        spring_from_ride_freq(k.front ? massFront : massRear, k.front ? sp.fnFrontHz : sp.fnRearHz,
                              k.front ? sp.zetaFront : sp.zetaRear, k.k_n_per_m, k.c_ns_per_m);
        k.preload_m = (k.front ? loadFront : loadRear) / k.k_n_per_m;
    }
    out.c_alpha_front_n_per_rad = 0.5f * tp.cAlphaFront;
    out.c_alpha_rear_n_per_rad  = 0.5f * tp.cAlphaRear;
    return out;
}

class RigidChassis {
public:
    static constexpr float kStepS       = 1.0f / 240.0f;  // fixed substep (matches Phase-B suspension)
    static constexpr int   kMaxSubsteps = 16;             // 16 / 240 s = 1/15 s, the engine's dt cap

    explicit RigidChassis(const RigidChassisParams& p = RigidChassisParams{}) { set_params(p); }

    void set_params(const RigidChassisParams& p) {
        m_p = p;
        m_body.set_mass_properties(p.body.mass_kg, p.body.box_inertia());
    }
    const RigidChassisParams&                params() const { return m_p; }
    util::physics::RigidBody&                body() { return m_body; }
    const util::physics::RigidBody&          body() const { return m_body; }
    const std::array<ChassisCornerState, 4>& corner_state() const { return m_state; }

    // Advance by a frame's dt: ceil(dt / h) equal substeps of at most kStepS (count clamped to kMaxSubsteps).
    void advance(float dt, const ChassisInput& in, float ground_y_m) {
        dt            = std::min(dt, kMaxSubsteps * kStepS);
        const int   n = std::max(1, static_cast<int>(std::ceil(dt / kStepS)));
        const float h = dt / static_cast<float>(n);
        for (int i = 0; i < n; ++i) step(h, in, ground_y_m);
    }

    // One substep: gravity + contacts + tires into the accumulators, then RigidBody::step.
    void step(float h, const ChassisInput& in, float ground_y_m) {
        const vec3 up(0.f, 1.f, 0.f);
        const mat3 r   = m_body.rotation();
        const vec3 cog = m_body.position();

        m_body.add_force(vec3(0.f, -m_p.body.mass_kg * m_p.gravity, 0.f));
        m_body.add_force(in.body_force_n);

        // Pass 1: normal forces (the tire pass needs each axle's load split).
        std::array<vec3, 4> patch{};
        for (int c = 0; c < 4; ++c) {
            const ChassisCorner& k = m_p.corner[c];
            ChassisCornerState&  s = m_state[c];
            patch[c]               = cog + r * k.hub_body_m - up * k.radius_m;
            s                      = ChassisCornerState{};
            const float pen        = ground_y_m - patch[c].y + k.preload_m;
            if (pen <= 0.f) continue;
            s.pen_m        = pen;
            const float vy = m_body.point_velocity(patch[c]).y;
            s.fn_n         = std::max(0.f, k.k_n_per_m * pen - k.c_ns_per_m * vy);
            m_body.add_force_at_point(up * s.fn_n, patch[c]);
        }

        // Pass 2: tire forces at the patches.
        const float cs = std::cos(in.steer_rad), sn = std::sin(in.steer_rad);
        for (int c = 0; c < 4; ++c) {
            ChassisCornerState& s = m_state[c];
            if (s.fn_n <= 0.f) continue;
            const ChassisCorner& k = m_p.corner[c];

            // Wheel heading projected onto the (flat) ground plane; s = f x up is the wheel's right.
            const vec3 fBody = k.front ? vec3(cs, 0.f, sn) : vec3(1.f, 0.f, 0.f);
            vec3       f     = r * fBody;
            f.y              = 0.f;
            const float fl   = glm::length(f);
            if (fl < 1e-4f) continue;  // wheel axis (nearly) vertical: no ground-plane heading
            f /= fl;
            const vec3 sdir = glm::cross(f, up);

            const vec3  vc    = m_body.point_velocity(patch[c]);
            const float vf    = glm::dot(vc, f);
            const float vs    = glm::dot(vc, sdir);
            const float cap   = in.mu * s.fn_n;
            const float calph = k.front ? m_p.c_alpha_front_n_per_rad : m_p.c_alpha_rear_n_per_rad;
            const float fy    = std::clamp(-calph * vs / (std::fabs(vf) + m_p.slip_ref_speed_mps), -cap, cap);

            // Axle request shared across the pair by normal load.
            const int   mate   = (c % 2 == 0) ? c + 1 : c - 1;
            const float axleFn = s.fn_n + m_state[mate].fn_n;
            const float fxAxle = k.front ? in.fx_front_n : in.fx_rear_n;
            const float fxReq  = axleFn > 1e-3f ? fxAxle * (s.fn_n / axleFn) : 0.f;
            const float fxMax  = friction_circle_fx_max(in.mu, s.fn_n, fy);
            const float fx     = std::clamp(fxReq, -fxMax, fxMax);

            s.fx_n = fx;
            s.fy_n = fy;
            m_body.add_force_at_point(f * fx + sdir * fy, patch[c]);
        }

        m_body.step(h);
    }

    // Mechanical energy (J): kinetic + gravitational + spring. Diagnostic for the damping test.
    // Uses the pen / fn of the LAST step, so call it between steps.
    float mechanical_energy(float ground_y_m) const {
        float      e = m_body.kinetic_energy() + m_p.body.mass_kg * m_p.gravity * m_body.position().y;
        const mat3 r = m_body.rotation();
        for (int c = 0; c < 4; ++c) {
            const ChassisCorner& k   = m_p.corner[c];
            const float          pen = ground_y_m - (m_body.position() + r * k.hub_body_m).y + k.radius_m + k.preload_m;
            if (pen > 0.f) e += 0.5f * k.k_n_per_m * pen * pen;
        }
        return e;
    }

private:
    RigidChassisParams                m_p{};
    util::physics::RigidBody          m_body{};
    std::array<ChassisCornerState, 4> m_state{};
};

}  // namespace swish
