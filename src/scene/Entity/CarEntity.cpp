#include "CarEntity.h"

#include "CarPhysics.h"
#include "WheelKinematics.h"
#include "scene/RoadGeometry/RoadGeometry.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace swish {

void CarEntity::apply_profile(const CarProfile& profile) {
    m_profile = profile;
    // WetGripParams::muDry must equal CarParams::tireMu: that identity keeps the dry path exact.
    m_wet_grip.muDry  = profile.car.tireMu;
    m_effective_mu    = profile.car.tireMu;
    m_steering_angle  = std::clamp(m_steering_angle, -profile.steerLockDeg, profile.steerLockDeg);
    m_gear_state.gear = std::clamp(m_gear_state.gear, 0, profile.gearbox.gearCount - 1);
#ifdef SWISH_DEBUG_UI
    m_rigid_ready = false;  // re-seed the 6-DOF body with the new mass/geometry on its next step
#endif
}

void CarEntity::handle_input(GLFWwindow* window, float dt) {
    // Keyboard -> InputFrame (arrow keys so WASD camera remains independent), then the
    // shared apply path. UP wins over DOWN; LEFT wins over RIGHT (unchanged precedence).
    InputFrame f;
    f.steer_mode = SteerMode::Rate;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        f.throttle = 1.f;
    else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        f.brake = 1.f;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        f.steer = -1.f;
    else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        f.steer = 1.f;
    apply_input_frame(f, dt);
}

void CarEntity::apply_input_frame(const InputFrame& f, float dt) {
    // Longitudinal intent; the force model is integrated in update(). Throttle wins over
    // brake (keyboard precedence). TODO(stub): analog trail-braking (throttle + brake together).
    m_throttle = 0.f;
    m_brake    = 0.f;
    m_reverse  = false;
    if (f.throttle > 0.f) {
        m_throttle = std::clamp(f.throttle, 0.f, 1.f);
    } else if (f.brake > 0.f) {
        if (m_forward_speed > kSpeedDeadZone)
            m_brake = std::clamp(f.brake, 0.f, 1.f);  // braking while moving forward
        else
            m_reverse = true;  // creep backward once ~stopped
    }

    if (f.steer_mode == SteerMode::Rate) {
        if (f.steer < 0.f) {
            m_steering_angle -= kSteerRate * dt;
        } else if (f.steer > 0.f) {
            m_steering_angle += kSteerRate * dt;
        } else {
            // Return to center
            float ret = kSteerReturn * dt;
            if (std::abs(m_steering_angle) <= ret)
                m_steering_angle = 0.f;
            else
                m_steering_angle -= std::copysign(ret, m_steering_angle);
        }
    } else {
        // Analog: approach the commanded angle at kSteerRate without overshoot.
        const float target = std::clamp(f.steer, -1.f, 1.f) * m_profile.steerLockDeg;
        const float step   = kSteerRate * dt;
        const float diff   = target - m_steering_angle;
        if (std::abs(diff) <= step)
            m_steering_angle = target;
        else
            m_steering_angle += std::copysign(step, diff);
    }

    m_steering_angle = std::clamp(m_steering_angle, -m_profile.steerLockDeg, m_profile.steerLockDeg);
}

void CarEntity::update(float dt) {
#ifdef SWISH_DEBUG_UI
    if (m_physics_model == PhysicsModel::RigidBody) {
        update_rigid_body(dt);
        return;
    }
#endif
    // ── Longitudinal force balance (SI), P0 #5 + Wave 2 3b + Phase 1 ──
    // m·v̇ = F_drive − ½ρC_dAv² − C_rr·mg, with F_drive = engine_torque(rpm)
    // fed through the PDK-style auto gearbox (CarPhysics.h, gear_drive_force),
    // split front/rear by the AWD PTU and then limited by each axle's own
    // friction circle under load transfer (awd_deliverable_force). Top speed
    // emerges from the balance — no hard clamp — so it self-limits at
    // ~205 mph, and the launch emerges from the tire model at ~1.05 g
    // (992 Turbo S; archive/docs/phase-1-physics-speed-accuracy.md §4.1-4.2).
    // `tire` supplies the a/b/hCg axle geometry the load transfer needs; it's
    // declared here (not just below, §Lateral) so both uses share one instance.
    // All of it comes from the active CarProfile (default: 992 Turbo S).
    const TireParams&       tire       = m_profile.tire;
    const GearboxParams&    gearbox    = m_profile.gearbox;
    const DrivetrainParams& drivetrain = m_profile.drivetrain;
    const float             rWheelRear = rear_wheel_radius();
    float                   v_mps      = m_forward_speed / kWorldUnitsPerMeter;

    // ── Wet-road grip model (Wave 2 step 4) ────────────────────────────
    // m_wetness is RainSystem's INTEGRATED wetness (asymmetric wet/dry ramp,
    // NOT raw rain intensity — see set_wetness()). filmOverTread queries the
    // renderer's own puddle mask at the car's CURRENT (pre-this-frame)
    // position, so the car can actually hydroplane on standing water, not
    // just fade grip with rain. See
    // docs/lessons-from-chrono/07-wet-roads-and-rain.md §4.
    const float wetness       = std::clamp(m_wetness, 0.0f, 1.0f);
    const float filmOverTread = film_over_tread(m_position.x, m_position.z, wetness);
    // Body-speed MAGNITUDE, not the forward component: hydroplaning depends on
    // how fast the tire is travelling over the water, and once the car is
    // sideways those differ enormously. Keying off v_mps alone would make a
    // spinning car "stop hydroplaning" merely because its nose had rotated
    // (Phase 2, archive/docs/phase-2-hydroplane-spinout.md as-built §5). Identical to
    // v_mps whenever sideslip is zero, and irrelevant when wetness == 0
    // (effective_mu short-circuits to muDry), so dry stays bit-identical.
    const float v_body = std::sqrt(v_mps * v_mps + m_lateral_velocity * m_lateral_velocity);
    m_effective_mu     = effective_mu(wetness, v_body, filmOverTread, m_wet_grip);
    // How completely the tire is planing, in [0,1] — the SAME smoothstep
    // effective_mu uses. Zero on dry roads, on merely-wet roads, and on
    // standing water below vPlane, which is what makes every Phase-2 change
    // below provably inert outside a genuine hydroplane (spec decision 2).
    m_plane_frac = hydroplane_fraction(v_body, filmOverTread, m_wet_grip);

    CarParams params = m_profile.car;
    params.tireMu    = m_effective_mu;  // feeds longitudinal_accel, dynamic_bicycle_deriv, max_yaw_rate

    // ── Combined slip: how much circle is cornering already using? ─────
    // Phase 1 §4.2 — F_x² + F_y² ≤ (μ·F_z)². The lateral forces are evaluated
    // from the state at the START of this step (m_lateral_velocity/m_yaw_rate
    // still hold last step's integrated values, and the steer angle is this
    // frame's input), which is the same state the lateral block below will
    // integrate from — so this is a consistent same-step reading, not a stale
    // one. Below kDynamicBlendSpeed the yaw model is kinematic and no tire
    // forces exist, so the circle sees zero lateral demand (correct: at
    // walking pace the tires are nowhere near saturated).
    //
    // Phase 2 closes the circle the OTHER way too: m_long_force carries LAST
    // step's per-axle longitudinal tire force, which shrinks this step's
    // lateral capacity (weighted by m_plane_frac). The one-step lag is what
    // breaks the otherwise-circular dependency (lateral → longitudinal →
    // lateral); it is the same explicit-Euler staleness the block above
    // already relies on for m_lateral_velocity/m_yaw_rate.
    // v_body, not |v_mps|: the threshold asks "is the dynamic tire model
    // well-conditioned?", and that depends on how fast the CONTACT PATCHES are
    // moving, not on the forward component alone. They are the same number in
    // all non-sliding cases (the kinematic branch below pins m_lateral_velocity
    // to 0, so v_body == |v_mps| whenever the car has been below the threshold),
    // but in a spin they differ enormously: a car sliding at 39 m/s with only
    // 4.7 m/s of that pointing forward would otherwise be handed to the
    // kinematic model, which zeroes m_lateral_velocity and teleports 37 m/s of
    // real lateral momentum out of existence mid-slide.
    AxleLateral latUse{};
    if (v_body >= kDynamicBlendSpeed) {
        const float deltaNow =
            glm::radians(std::clamp(m_steering_angle, -m_profile.steerLockDeg, m_profile.steerLockDeg));
        latUse = axle_lateral_use(axle_lateral_forces(v_mps, m_lateral_velocity, m_yaw_rate, deltaNow, params, tire,
                                                      m_long_force, m_plane_frac));
    }

    if (m_reverse) {
        v_mps -= (kReverseAccel / kWorldUnitsPerMeter) * dt;  // bounded low-speed reverse
        // Reverse is a bounded low-speed creep, so its cap belongs HERE, on the
        // driver-commanded branch. It used to be applied unconditionally further
        // down, which would have silently destroyed momentum during a Phase-2
        // spin: a spun car's forward speed legitimately passes through zero and
        // goes well past −kMaxReverseSpeed while it slides backwards.
        v_mps        = std::max(v_mps, -kMaxReverseSpeed / kWorldUnitsPerMeter);
        m_long_force = AxleLongitudinal{};
    } else {
        // Advance the auto-shift state machine BEFORE computing this frame's
        // force, exactly like Chrono's ChAutomaticTransmissionShafts (see
        // docs/lessons-from-chrono/06-driveline-powertrain-drivers.md §3):
        // evaluate the shift off the current rpm, then drive through
        // whichever gear results this step.
        m_gear_state     = gearbox_shift_step(m_gear_state, v_mps, gearbox, rWheelRear, dt);
        const float prev = v_mps;
        v_mps += longitudinal_accel(v_mps, m_throttle, m_brake, m_gear_state.gear, params, tire, gearbox, rWheelRear,
                                    drivetrain, latUse, &m_long_force, m_profile.torque) *
                 dt;
        // Phase 2: the body-frame transport term v̇x += v_l·r that the model was
        // missing. Zero unless the car is BOTH sideslipping and yawing, and
        // gated by m_plane_frac, so dry/merely-wet handling is untouched. It is
        // what makes a spin conserve momentum instead of the nose dragging the
        // velocity vector around with it (CarPhysics.h::body_transport_accel).
        v_mps += body_transport_accel(m_lateral_velocity, m_yaw_rate, m_plane_frac) * dt;
        // Braking must not drag the car backwards — stop at zero on a sign flip.
        // Suppressed while planing: mid-spin the forward component legitimately
        // crosses zero under the transport term, and pinning it at 0 there would
        // teleport the car's momentum away.
        if (m_brake > 0.f && prev > 0.f && v_mps < 0.f && m_plane_frac <= 0.f)
            v_mps = 0.f;
    }

    m_forward_speed = v_mps * kWorldUnitsPerMeter;

    // Finite/NaN guard. Forward top speed stays emergent; the reverse cap now
    // lives on the m_reverse branch above (a spin's negative speed is real
    // motion, not a reverse-gear command).
    if (!std::isfinite(m_forward_speed))
        m_forward_speed = 0.f;

    // Dead-stop below threshold while coasting (no throttle / reverse input).
    // Also resets the gearbox to 1st, matching a real automatic's behavior at
    // a full stop and guaranteeing the next launch starts short (traction-
    // capped) rather than wherever an abrupt stop left the gear state.
    //
    // Phase 2 adds the lateral-state conditions: a spinning car's FORWARD
    // component passes through zero while it is still sliding sideways at speed
    // and rotating hard. Snapping the speed to exactly 0 there would trip the
    // early return below and freeze the spin mid-rotation, so the dead stop now
    // requires the car to be genuinely at rest, not merely crossing vx = 0.
    if (std::abs(m_forward_speed) < kSpeedDeadZone && m_throttle == 0.f && !m_reverse &&
        std::abs(m_lateral_velocity) < kRestLateralSpeed && std::abs(m_yaw_rate) < kRestYawRate) {
        m_forward_speed    = 0.f;
        m_lateral_velocity = 0.f;
        m_yaw_rate         = 0.f;
        m_gear_state       = GearState{};
    }

    // ── Road-wheel spin (kinematic rolling, per wheel) ────────────────
    // ω_i = v_i / R_i with v_i each hub's own rolling speed (outer wheels faster in a
    // turn, fronts along their steered heading); see advance_wheel_spin. Signed v runs
    // the wheels backward in reverse; the per-corner spin SIGN (which axle direction the
    // baked frame points) is applied at draw-matrix time, so θ itself is side-agnostic.
    advance_wheel_spin(dt);

    // Truly at rest? Nothing left to integrate. The lateral terms matter for
    // Phase 2: a spin's forward component crosses zero while the car is still
    // sliding and rotating, and returning here would freeze it mid-spin.
    if (m_forward_speed == 0.f && m_lateral_velocity == 0.f && m_yaw_rate == 0.f)
        return;

    // ── Lateral / yaw dynamics (dynamic bicycle + saturating tires, P0 #4) ──
    // Below ~5 m/s the kinematic model is used (the dynamic model's 1/vx is
    // singular); above it, lateral velocity + yaw rate are integrated with tire
    // forces that saturate at μ·Fz, so the car can understeer and slide instead
    // of following a rigid geometric arc. This replaces the old kSteerRefSpeed
    // authority taper (a band-aid for the missing grip limit).
    const float wheelbase_m = m_profile.car.wheelbase;
    const float delta = glm::radians(std::clamp(m_steering_angle, -m_profile.steerLockDeg, m_profile.steerLockDeg));
    const float vx    = m_forward_speed / kWorldUnitsPerMeter;  // m/s
    const float r_kin = (vx / wheelbase_m) * std::tan(delta);   // kinematic yaw rate (right-pos)

    // Same threshold the combined-slip read above uses (CarEntity.h) — one
    // source of truth for "is the dynamic tire model live this frame" — and,
    // like it, measured on the BODY SPEED rather than the forward component
    // (see the comment there for why; identical for a car that isn't sliding).
    constexpr float kBlendSpeed = kDynamicBlendSpeed;  // m/s
    const float     v_body_lat  = std::sqrt(vx * vx + m_lateral_velocity * m_lateral_velocity);
    if (v_body_lat < kBlendSpeed) {
        m_yaw_rate         = r_kin;  // keep dynamic state consistent for a smooth handoff
        m_lateral_velocity = 0.f;
    } else {
        // Reuses the `tire` declared above (§Longitudinal) — same axle geometry
        // feeds both the RWD traction cap and the bicycle-model slip angles.
        // m_long_force / m_plane_frac close the friction circle in the
        // longitudinal→lateral direction (Phase 2; both inert when not planing).
        // Pre-integration magnitudes, for the rate limit on the clamps below.
        const float vlPrev = std::abs(m_lateral_velocity);
        const float rPrev  = std::abs(m_yaw_rate);

        BicycleDeriv d =
            dynamic_bicycle_deriv(vx, m_lateral_velocity, m_yaw_rate, delta, params, tire, m_long_force, m_plane_frac);
        m_lateral_velocity += d.vlDot * dt;
        m_yaw_rate += d.rDot * dt;
        // Smooth 5→8 m/s handoff from kinematic to full dynamic response (on the
        // same body-speed measure as the branch above, so the two agree).
        const float blend = glm::clamp((v_body_lat - kBlendSpeed) / 3.0f, 0.f, 1.f);
        m_yaw_rate        = glm::mix(r_kin, m_yaw_rate, blend);
        // Friction circle: cap lateral accel at μ·g (understeer at the limit;
        // also arrests sideslip growth under a sustained over-drive input).
        // Phase 2 releases both caps in proportion to m_plane_frac — they encode
        // a QUASI-STEADY (v̇l = 0) assumption that a hydroplaning car violates,
        // and enforcing them anyway is what made a spin impossible. At
        // m_plane_frac == 0 these are bit-identical to the pre-Phase-2 lines.
        //
        // Both are RATE-LIMITED (CarPhysics.h::rate_limited_bound): when a
        // hydroplane ends, m_plane_frac falls to 0 within a frame or two and the
        // bounds snap back to their quasi-steady values. Applying that snap
        // directly deletes real momentum — a live run measured the body speed
        // dropping 31.8 → 12.5 m/s in one 0.5 s sample, ~4 g that no tire
        // produced. A clamp may stop a state growing; it must not brake harder
        // than friction can. No-op whenever the bound was not already exceeded,
        // which is every non-planing case (dry sideslip runs at ~3°, nowhere
        // near the 45° bound).
        const float vBody = std::sqrt(vx * vx + m_lateral_velocity * m_lateral_velocity);
        const float rMax  = rate_limited_bound(yaw_rate_limit(vx, vBody, params, tire, m_plane_frac), rPrev,
                                               max_yaw_decel(params, tire), dt);
        const float vlMax =
            rate_limited_bound(sideslip_limit(vx, vBody, m_plane_frac), vlPrev, params.tireMu * params.gravity, dt);
        m_yaw_rate         = std::clamp(m_yaw_rate, -rMax, rMax);
        m_lateral_velocity = std::clamp(m_lateral_velocity, -vlMax, vlMax);
    }

    // Apply heading (right-positive yaw decreases rotation.y) and wrap to ±180°.
    m_rotation.y -= glm::degrees(m_yaw_rate) * dt;
    m_rotation.y = std::fmod(m_rotation.y + 180.f, 360.f) - 180.f;

    // Advance position: forward motion + lateral drift. forward = mesh nose;
    // right = forward × up is the body's rightward axis (matches m_lateral_velocity).
    const float yaw_rad = glm::radians(m_rotation.y);
    const Vec3  forward(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
    const Vec3  right = glm::normalize(glm::cross(forward, Vec3(0.f, 1.f, 0.f)));
    m_position += (forward * m_forward_speed + right * (m_lateral_velocity * kWorldUnitsPerMeter)) * dt;

    // Clamp X to road bounds
    m_position.x = std::clamp(m_position.x, m_min_x, m_max_x);
}

#ifdef SWISH_DEBUG_UI
// ── Rigid-body chassis (stub) ─────────────────────────────────────────
// Alternative to the bicycle model above; see RigidChassis.h for the design note and TODO(stub) list.
void CarEntity::set_physics_model(PhysicsModel model) {
    if (model == m_physics_model)
        return;
    m_physics_model = model;
    m_rigid_ready   = false;  // re-seed from the entity pose on the next RigidBody step
    if (model == PhysicsModel::Bicycle) {
        m_rotation.x = 0.f;  // the bicycle model owns yaw only; drop pitch/roll left by the body
        m_rotation.z = 0.f;
    }
}

void CarEntity::init_rigid_body() {
    // Hubs from the loader's wheel frames (mesh space, m); before the loader finishes, a plausible box.
    std::array<vec3, 4>  hub;
    std::array<float, 4> radius;
    if (m_wheels_valid) {
        for (int c = 0; c < 4; c++) {
            hub[c]    = vec3(m_wheel_frames[c].frame[3]);
            radius[c] = m_wheel_frames[c].radius;
        }
    } else {
        const float halfWb = 0.5f * m_profile.car.wheelbase;
        const float halfTr = 0.8f;  // TODO(stub): track from CarParams once it has one
        const float r      = m_profile.wheelRadiusM;
        radius.fill(r);
        hub = {vec3(halfWb, r, -halfTr), vec3(halfWb, r, halfTr), vec3(-halfWb, r, -halfTr), vec3(-halfWb, r, halfTr)};
    }
    const CarParams&       carParams  = m_profile.car;
    const TireParams&      tireParams = m_profile.tire;
    const SuspensionParams susp;
    m_rigid.set_params(make_chassis_params(hub, radius, carParams, tireParams, susp));

    // Seed from the entity pose + the bicycle's speed state (R = Ry(yaw) Rx(pitch) Rz(roll), Entity.cpp).
    const quat                q  = glm::angleAxis(glm::radians(m_rotation.y), vec3(0.f, 1.f, 0.f)) *
                                   glm::angleAxis(glm::radians(m_rotation.x), vec3(1.f, 0.f, 0.f)) *
                                   glm::angleAxis(glm::radians(m_rotation.z), vec3(0.f, 0.f, 1.f));
    const mat3                r  = glm::mat3_cast(q);
    const vec3                fw = r * vec3(1.f, 0.f, 0.f);
    const vec3                rt = r * vec3(0.f, 0.f, 1.f);
    util::physics::RigidBody& b  = m_rigid.body();
    b.set_orientation(q);
    b.set_position(m_position / kWorldUnitsPerMeter + r * m_rigid.params().cog_mesh_m);
    b.set_linear_velocity(fw * (m_forward_speed / kWorldUnitsPerMeter) + rt * m_lateral_velocity);
    b.set_angular_velocity(vec3(0.f, -m_yaw_rate, 0.f));  // yaw rate is right-positive; +Y rotation is left
    b.clear_accumulators();
    m_rigid_last_pos = m_position;
    m_rigid_ready    = true;
}

void CarEntity::update_rigid_body(float dt) {
    if (!m_rigid_ready)
        init_rigid_body();
    util::physics::RigidBody& b = m_rigid.body();

    // The entity may have been moved under us (corridor origin rebase, teleport): follow it.
    const Vec3 shift = m_position - m_rigid_last_pos;
    if (shift != Vec3(0.f))
        b.set_position(b.position() + shift / kWorldUnitsPerMeter);

    const vec3 up(0.f, 1.f, 0.f);
    vec3       fwd   = b.rotation() * vec3(1.f, 0.f, 0.f);
    fwd.y            = 0.f;
    fwd              = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : vec3(1.f, 0.f, 0.f);
    const vec3  v    = b.linear_velocity();
    const float vFwd = glm::dot(v, fwd);
    const vec3  vHoriz(v.x, 0.f, v.z);
    const float vBody = glm::length(vHoriz);

    // Wet-road grip: same model as the bicycle path (see update()).
    const float wetness = std::clamp(m_wetness, 0.0f, 1.0f);
    const float film    = film_over_tread(m_position.x, m_position.z, wetness);
    m_effective_mu      = effective_mu(wetness, vBody, film, m_wet_grip);
    m_plane_frac        = hydroplane_fraction(vBody, film, m_wet_grip);

    // Longitudinal request: the existing force model gives per-axle TIRE forces (aero excluded) along the
    // heading; the chassis clamps them per wheel by the friction circle. Aero acts at the CoG.
    CarParams params                   = m_profile.car;
    params.tireMu                      = m_effective_mu;
    const TireParams&       tire       = m_profile.tire;
    const GearboxParams&    gearbox    = m_profile.gearbox;
    const DrivetrainParams& drivetrain = m_profile.drivetrain;
    const float             rWheelRear = rear_wheel_radius();

    ChassisInput in;
    in.mu           = m_effective_mu;
    in.steer_rad    = glm::radians(std::clamp(m_steering_angle, -m_profile.steerLockDeg, m_profile.steerLockDeg));
    in.body_force_n = -0.5f * params.airDensity * params.dragCoeff * params.frontalArea * vBody * vHoriz;
    if (m_reverse) {
        // Bounded low-speed creep, like the bicycle path: constant push until the reverse cap.
        const float push =
            vFwd > -kMaxReverseSpeed / kWorldUnitsPerMeter ? -params.mass * kReverseAccel / kWorldUnitsPerMeter : 0.f;
        in.fx_front_n = 0.5f * push;
        in.fx_rear_n  = 0.5f * push;
    } else {
        m_gear_state = gearbox_shift_step(m_gear_state, vFwd, gearbox, rWheelRear, dt);
        AxleLongitudinal split{};
        longitudinal_accel(vFwd, m_throttle, m_brake, m_gear_state.gear, params, tire, gearbox, rWheelRear, drivetrain,
                           AxleLateral{}, &split, m_profile.torque);
        // Brake / rolling resistance flip sign with v: ramp them out below 0.5 m/s so the standstill does
        // not chatter (TODO(stub): proper static-friction / stick model).
        const float ramp = m_throttle > 0.f ? 1.f : std::min(1.f, std::fabs(vFwd) / 0.5f);
        in.fx_front_n    = split.front * ramp;
        in.fx_rear_n     = split.rear * ramp;
    }

    // TODO(stub): road height query per wheel; flat plane at the road surface under the entity for now.
    m_rigid.advance(dt, in, RoadGeometry::road_surface_y(m_position.x) / kWorldUnitsPerMeter);

    // ── Write back: pose (WU + Euler degrees) and the scalars the rest of the game reads ──
    const mat3 rn = b.rotation();
    const vec3 e  = util::physics::euler_yxz_from_rotation(rn);  // (pitch, yaw, roll) rad
    m_rotation    = Vec3(glm::degrees(e.x), glm::degrees(e.y), glm::degrees(e.z));
    m_position    = (b.position() - rn * m_rigid.params().cog_mesh_m) * kWorldUnitsPerMeter;

    // Road X bounds: clamp the entity and drag the body with it (outward velocity removed).
    const float clampedX = std::clamp(m_position.x, m_min_x, m_max_x);
    if (clampedX != m_position.x) {
        b.set_position(b.position() + vec3((clampedX - m_position.x) / kWorldUnitsPerMeter, 0.f, 0.f));
        vec3 vel = b.linear_velocity();
        vel.x    = 0.f;
        b.set_linear_velocity(vel);
        m_position.x = clampedX;
    }
    m_rigid_last_pos = m_position;

    vec3 fwdN          = rn * vec3(1.f, 0.f, 0.f);
    fwdN.y             = 0.f;
    fwdN               = glm::length(fwdN) > 1e-4f ? glm::normalize(fwdN) : vec3(1.f, 0.f, 0.f);
    const vec3 vN      = b.linear_velocity();
    m_forward_speed    = glm::dot(vN, fwdN) * kWorldUnitsPerMeter;
    m_lateral_velocity = glm::dot(vN, glm::cross(fwdN, up));
    m_yaw_rate         = -b.angular_velocity().y;
    if (!std::isfinite(m_forward_speed))
        m_forward_speed = 0.f;

    // Wheel spin (kinematic rolling, per wheel), same as the bicycle path.
    advance_wheel_spin(dt);
}
#endif  // SWISH_DEBUG_UI

// ── Road-wheel articulation: Ackermann steer + per-wheel spin (S1a) ───
// Draw-only: the bicycle model keeps the single steering angle δ. The fronts are drawn
// at the Ackermann angle about δ (WheelKinematics.h::ackermann_wheel_angle, centreline
// convention: cot δ_w = cot δ − z_w / L), with L and the front track t from the
// loader-measured rig geometry (CarRig.h), z_w = ±t/2 by the hub's side.
float CarEntity::get_wheel_steer_rad(int c) const {
    if (!m_wheels_valid || !m_wheel_steer_on || c < 0 || c > 1)
        return 0.f;  // 0 FL, 1 FR steer; the rears never do
    const float delta = glm::radians(std::clamp(m_steering_angle, -m_profile.steerLockDeg, m_profile.steerLockDeg));
    const CarRigGeometry g = m_rig_geometry ? *m_rig_geometry : measure_car_rig_geometry(m_wheel_frames, false);
    if (g.wheelbase_m <= 0.f)
        return delta;
    const float halfTrack = 0.5f * g.track_front_m;
    const float zw        = m_wheel_frames[static_cast<size_t>(c)].frame[3].z >= 0.f ? halfTrack : -halfTrack;
    return ackermann_wheel_angle(delta, zw, g.wheelbase_m, m_ackermann);
}

// Kinematic per-wheel ω into the m_wheel_omega hook, then integrate the visible spin.
// The lever arms are car-space hub positions relative to the entity origin, the point
// update() moves at (v_x, v_l) and rotates about (WheelKinematics.h::wheel_rolling_speed).
void CarEntity::advance_wheel_spin(float dt) {
    if (!m_wheels_valid)
        return;
    const float vx = m_forward_speed / kWorldUnitsPerMeter;
    for (int c = 0; c < 4; c++) {
        const WheelFrame& wf = m_wheel_frames[static_cast<size_t>(c)];
        const float       v  = wheel_rolling_speed(vx, m_lateral_velocity, m_yaw_rate, wf.frame[3].x, wf.frame[3].z,
                                                   get_wheel_steer_rad(c));
        m_wheel_omega[static_cast<size_t>(c)] = v / wf.radius;
    }
    if (!m_wheel_spin_on)
        return;
    for (size_t c = 0; c < 4; c++)
        m_wheel_spin[c] = wheel_spin_advance(m_wheel_spin[c], m_wheel_omega[c] * m_wheel_spin_mul, dt);
}

// ── Phase-B quarter-car suspension (kinematic travel only) ────────────
// archive/docs/wheels/phase-b-spec.md. LATE update: App.cpp calls this after
// CorridorDriver::update() so the body's Y-snap (and any origin rebase)
// have already settled. Touches ONLY the per-corner suspension state —
// the entity transform is never written here (§8); the resulting offset is
// consumed solely as a local draw-matrix translation in get_draw_calls (§7).
void CarEntity::update_wheel_visuals(float dt) {
    if (!m_wheels_valid)
        return;

    // Chassis/tyre numbers from the active CarProfile, like update(); the
    // suspension/ARB params are local defaults. Debug sliders (spec
    // §11) are a later step; these ARE the shipped defaults until then.
    // tireMu is overridden with this frame's wet-grip result (computed in
    // update(), which always runs first) for consistency with the other
    // CarParams construction site — unused by the suspension math today,
    // but keeps every CarParams in the file reading the same effective mu.
    CarParams carParams               = m_profile.car;
    carParams.tireMu                  = m_effective_mu;
    const TireParams&      tireParams = m_profile.tire;
    const SuspensionParams susp;
    const AntiRollParams   arb;

    // Corner sprung mass, reusing the same axle split CarPhysics.h already
    // uses for tyre loads (FzF/FzR, CarPhysics.h:97-98) — derived from
    // CarParams::mass at runtime, NOT hardcoded, so a future mass retarget
    // (Wave 2 step 3) re-derives k/c automatically (spec §0/§2).
    const float L           = tireParams.a + tireParams.b;
    const float sprungTotal = carParams.mass - 4.f * susp.unsprungMassKg;
    const float massFront   = sprungTotal * (tireParams.b / L) * 0.5f;
    const float massRear    = sprungTotal * (tireParams.a / L) * 0.5f;

    float kFront, cFront, kRear, cRear;
    spring_from_ride_freq(massFront, susp.fnFrontHz, susp.zetaFront, kFront, cFront);
    spring_from_ride_freq(massRear, susp.fnRearHz, susp.zetaRear, kRear, cRear);

    constexpr float kHTarget = 1.f / 240.f;

    // Road input (§6): the per-corner residual relative to the body's own
    // already-snapped Y, so this does not fight the App.cpp Y-snap. On flat
    // road all four targets are 0 and the rest pose is unchanged.
    const Mat4  car_model = get_model_matrix();
    const float bodyRoadY = RoadGeometry::road_surface_y(get_position().x);

    // Snapshot BEFORE any corner advances: the anti-roll-bar coupling below
    // reads the opposite corner's travel, and reading it from the in-progress
    // array would make corner 0 see last frame's partner while corner 1 sees
    // this frame's — an order-dependent asymmetry between two corners that are
    // supposed to be mirror images (there is a symmetry test for exactly this).
    const std::array<CornerSpring, 4> prev = m_corner_susp;

    for (int c = 0; c < 4; c++) {
        const bool  front      = c < 2;
        const float cornerMass = front ? massFront : massRear;
        const float k          = front ? kFront : kRear;
        const float damp       = front ? cFront : cRear;

        // Corner world X from the loader-recovered pivot frame (car_model * wf.frame).
        const Mat4  cornerWorld = car_model * m_wheel_frames[c].frame;
        const float cornerRoadY = RoadGeometry::road_surface_y(cornerWorld[3].x);
        float       x_target_m  = (cornerRoadY - bodyRoadY) / kWorldUnitsPerMeter;

        // Anti-roll bar (Phase 1 §4.3): a torsion spring between the two
        // corners of one axle, folded into the spring target rather than
        // integrated separately (WheelKinematics.h::arb_target_shift_m).
        // Corner order is 0 FL · 1 FR · 2 BL · 3 BR, so the partner is the
        // sibling within each pair: 0↔1 and 2↔3.
        const int partner = (c % 2 == 0) ? c + 1 : c - 1;
        x_target_m += arb_target_shift_m(prev[c].x_m, prev[partner].x_m, front ? arb.frontRatio : arb.rearRatio);

        m_corner_susp[c] =
            suspension_advance(m_corner_susp[c], x_target_m, k, damp, cornerMass, dt, kHTarget, susp.travelMaxM);
    }
}

void CarEntity::update_gauges(float dt) {
    float rpm = get_engine_rpm();
    float kmh = std::fabs(get_speed_mps()) * util::units::kKmhPerMps;
#ifdef SWISH_DEBUG_UI
    if (m_gauge_test) {
        rpm = m_gauge_test_rpm;
        kmh = m_gauge_test_kmh;
    }
#endif
    m_gauge_state = gauges_step(m_gauge_state, m_gauge_params, rpm, kmh, dt);
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

Mat4 CarEntity::get_steering_wheel_pivot_world() const {
    const Mat4 car_model = get_model_matrix();
    for (const auto& s : get_submeshes()) {
        if (s.is_steering_wheel)
            return car_model * s.sw_pivot_frame;
    }
    return car_model;
}

std::vector<DrawCall> CarEntity::get_draw_calls() const {
    std::vector<DrawCall> result;
    result.reserve(get_submeshes().size());

    Mat4 car_model = get_model_matrix();
    // Steering wheel = the SAME rate-limited steer angle the fronts are drawn about, times the
    // profile ratio (993: 16.5, ±29.5° lock -> 973.5° = 2.70 turns lock to lock).
    float sw_angle = m_steering_angle * m_profile.steerRatio;

    // ── Road-wheel matrices ────────────────────────────────────────────
    // One matrix per corner for spinning pieces (tire/rim/disc) and one for
    // steer-only pieces (calipers; only the fronts actually turn). δ_c is the
    // corner's own Ackermann angle (get_wheel_steer_rad), so a caliper turns
    // exactly with its wheel and never spins. In car
    // space:  T(P) · R_y(−δ_c) · F · R_x(s·θ) · F⁻¹  — steer sits LEFT of
    // spin so the axle turns with the upright, and spin is conjugated
    // through the corner's baked frame F (camber tilt + 180° side flip),
    // i.e. about the true axle: spinning about an untilted axis instead
    // wobbles the cambered rim by ± r·sin(2°) ≈ 13 mm at the rear.
    Mat4 wheelM[4];
    Mat4 steerM[4];
    if (m_wheels_valid) {
        const SuspensionGeometry geom;  // Phase-1 camber/toe gain curves (defaults = shipped)
        for (int c = 0; c < 4; c++) {
            const WheelFrame& wf        = m_wheel_frames[c];
            const bool        front     = c < 2;                    // 0 FL, 1 FR steer; rears don't
            const float       steer_rad = -get_wheel_steer_rad(c);  // R_y(−δ_c): + δ turns the nose to +Z (right)
            const Mat4 spin = wf.frame * glm::rotate(Mat4(1.f), wf.spin_sign * m_wheel_spin[c], Vec3(1.f, 0.f, 0.f)) *
                              glm::inverse(wf.frame);
            Mat4       steer(1.f);
            if (front && steer_rad != 0.f) {
                const Vec3 P(wf.frame[3]);
                steer = glm::translate(Mat4(1.f), P) * glm::rotate(Mat4(1.f), steer_rad, Vec3(0.f, 1.f, 0.f)) *
                        glm::translate(Mat4(1.f), -P);
            }
            // Phase-B suspension travel (archive/docs/wheels/phase-b-spec.md §7): a pure
            // vertical translation in car-local space, inserted LEFT of steer so
            // the steer pivot P is unaffected by travel (the upright carries the
            // pivot). At rest (x_m == 0) this is identity — bit-identical to
            // pre-Phase-B behavior.
            // x_m is in METERS and `travel` is applied AFTER car_model, which already
            // carries the entity scale (1000) — the same space kSeatEye is authored in
            // (App.cpp). Multiplying by util::units::kWuPerMetre (utils/units/Units.h) here would scale by 1000
            // twice and throw the wheel ~14 m into the air.
            const Mat4 travel =
                glm::translate(Mat4(1.f), Vec3(0.f, suspension_draw_offset_m(m_corner_susp[c].x_m), 0.f));

            // ── Camber / toe from suspension travel (Phase 1 §4.3) ──────
            // A pure function of this corner's travel — no state. The DELTA
            // from the rest pose is used, because the asset's WheelFrame
            // already bakes in static camber; at x_m == 0 both angles are
            // exactly 0 and `align` is the identity, so the rest pose stays
            // bit-identical to pre-Phase-1.
            //
            // SIGNS. Car space: nose +X, up +Y, right +Z (forward × up).
            // Camber is a rotation about +X, which tilts a wheel's top toward
            // +Z; toe is a rotation about +Y, which turns a nose toward −Z. In
            // both cases the mirror between the two sides is captured by
            // sign(z) of the corner's own pivot — read off the loader-recovered
            // frame rather than hardcoded per index, so it cannot disagree with
            // however the asset happens to be laid out:
            //   camber γ (negative = top leans IN) ⇒ θ_x = sign(z)·γ
            //   toe    τ (positive = toe-IN)       ⇒ θ_y = sign(z)·τ
            const CamberToe ct       = camber_toe_delta_from_travel(m_corner_susp[c].x_m, front, geom);
            const float     sideSign = (wf.frame[3].z >= 0.f) ? 1.f : -1.f;  // +1 = right side (+Z)
            Mat4            align(1.f);
            if (ct.camberDeg != 0.f || ct.toeDeg != 0.f) {
                const Vec3 P(wf.frame[3]);
                align = glm::translate(Mat4(1.f), P) *
                        glm::rotate(Mat4(1.f), glm::radians(sideSign * ct.camberDeg), Vec3(1.f, 0.f, 0.f)) *
                        glm::rotate(Mat4(1.f), glm::radians(sideSign * ct.toeDeg), Vec3(0.f, 1.f, 0.f)) *
                        glm::translate(Mat4(1.f), -P);
            }

            // `align` sits RIGHT of steer (the alignment change is measured in
            // the body frame, then the whole upright is steered) and LEFT of
            // spin (the upright carries the axle, so tilting it tilts the spin
            // axis — the same reasoning that puts `spin` inside wf.frame).
            wheelM[c] = car_model * travel * steer * align * spin;
            steerM[c] = car_model * travel * steer * align;
        }
    }

    // Cabin wash DISABLED (0.0): the interior now stays crisp and detailed in rain
    // via the `dry` wettable mask (whole car excluded from the wet-road BRDF, see
    // SceneGeometry + lighting.frag) plus depth-resolved fog that leaves the near
    // cabin fog-free. Any positive wash re-introduced the over-exposure reported.
    // The color.a sentinel path is kept for easy re-enable. a = 1 → wash 0.
    const float washAmount = 0.0f;

    for (const auto& s : get_submeshes()) {
        DrawCall dc{};
        dc.indexOffset = s.indexOffset;
        dc.indexCount  = s.indexCount;
        dc.material    = s.material;
        dc.dry         = true;  // whole car excluded from wet-road effects (lighting.frag wettable mask)
        dc.color       = s.is_interior ? Vec4(s.color.r, s.color.g, s.color.b, 1.0f + washAmount) : s.color;

        if (s.is_steering_wheel) {
            // The wheel spins left-right about local +Z (R). The debug correction C
            // is a full REST orientation of the wheel about the pivot (yaw/pitch/roll
            // /quaternion) — visible at any steer angle — so the wheel can be dialed
            // straight if the model's pivot is tilted. Identity C = unchanged.
            Mat4 C   = glm::mat4_cast(m_steer_axis_correction);
            Mat4 R   = glm::rotate(Mat4(1.f), glm::radians(-sw_angle), Vec3(0.f, 0.f, 1.f));
            dc.model = car_model * s.sw_pivot_frame * C * R * glm::inverse(s.sw_pivot_frame);
        } else if (s.rig_part == RigPart::WiperDriver || s.rig_part == RigPart::WiperPassenger) {
            dc.model = car_model * rig_part_sweep(s.rig_pivot_frame, s.rig_part, m_wiper_angle);  // S1b wipers
        } else if (m_wheels_valid && s.wheel_corner >= 0) {
            // Road wheels: tire/rim/disc pieces spin (+steer at the front);
            // caliper pieces only steer with the front uprights.
            dc.model = s.wheel_spins ? wheelM[s.wheel_corner] : steerM[s.wheel_corner];
        } else if (s.rig_part == RigPart::NeedleRpm) {  // dial needles (GaugeNeedles.h)
            dc.model = car_model *
                       needle_pose(s.rig_pivot_frame, s.rig_part, m_gauge_state.rpm.pos, m_gauge_params.rpmRestDeg);
        } else if (s.rig_part == RigPart::NeedleSpeed) {
            dc.model = car_model *
                       needle_pose(s.rig_pivot_frame, s.rig_part, m_gauge_state.speed.pos, m_gauge_params.speedRestDeg);
        } else {
            dc.model = car_model;
        }
        result.push_back(dc);
    }

#ifdef SWISH_DEBUG_UI
    // Per-object velocity buffer bookkeeping (see get_prev_draw_models(), CarEntity.h).
    // m_rollingPrevModels currently holds whatever the PREVIOUS call left behind — that's
    // exactly what this frame's draws should be paired against, so expose it before
    // overwriting it with this frame's own matrices below.
    const bool topologyChanged = m_rollingPrevModels.size() != result.size();
    if (topologyChanged) {
        // Wheel frames just loaded / submesh count changed — fall back to "no motion this
        // frame" for every draw rather than mis-pairing indices against the old count.
        m_lastExposedPrevModels.clear();
        m_lastExposedPrevModels.reserve(result.size());
        for (const auto& dc : result)
            m_lastExposedPrevModels.push_back(dc.model);
    } else {
        m_lastExposedPrevModels = m_rollingPrevModels;
    }
    m_rollingPrevModels.resize(result.size());
    for (size_t i = 0; i < result.size(); ++i)
        m_rollingPrevModels[i] = result[i].model;
#endif

    return result;
}

}  // namespace swish
