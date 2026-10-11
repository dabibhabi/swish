#pragma once

#include "CarLights.h"   // CarLightAnchor / car_spot_lights (car lights)
#include "CarPhysics.h"  // GearState (Wave 2 step 3b torque-curve/gearbox)
#include "Entity.h"
#include "RigidChassis.h"     // PhysicsModel + the 6-DOF chassis (debug-only alternative to the bicycle)
#include "SlipVisuals.h"
#include "WheelKinematics.h"  // CornerSpring (Phase-B suspension state)
#include "utils/input/InputFrame.h"
#include "utils/units/Units.h"

#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct GLFWwindow;

namespace swish {

class CarEntity : public ModelEntity {
public:
    // Process keyboard input each frame before calling update().
    void handle_input(GLFWwindow* window, float dt);

    // Device-agnostic input seam (keyboard, gamepad, replay, bench). handle_input() builds a
    // SteerMode::Rate frame from the arrow keys and calls this, so the keyboard path is
    // bit-identical to before. Sets throttle/brake/reverse intent and moves the steering angle.
    void apply_input_frame(const InputFrame& frame, float dt);

    // Advance physics: drag → steering yaw rate → position integration.
    void update(float dt) override;

    // Clamp position to road bounds (X axis).
    void set_road_bounds(float min_x, float max_x) {
        m_min_x = min_x;
        m_max_x = max_x;
    }

    float get_speed() const { return m_forward_speed; }
    // Speed in SI. Callers outside this class shouldn't have to know WORLD_SCALE —
    // kWorldUnitsPerMeter is private, and duplicating the literal at call sites is
    // how unit bugs get in (see the Phase-B draw-offset double-conversion).
    float get_speed_mps() const { return m_forward_speed / kWorldUnitsPerMeter; }

    // Steering angle (degrees) get/set — the debug UI overrides this to pose the
    // wheel. set clamps to the physical lock. kSteerRatio exposed for the gizmo.
    float get_steering_angle() const { return m_steering_angle; }
    void  set_steering_angle(float deg) {
        m_steering_angle = deg < -kMaxSteer ? -kMaxSteer : (deg > kMaxSteer ? kMaxSteer : deg);
    }
    static constexpr float steer_max() { return kMaxSteer; }
    static constexpr float steer_ratio() { return kSteerRatio; }

    // World-space pivot frame of the steering-wheel submesh (model * sw_pivot_frame),
    // or the car model if there's no wheel submesh. Positions the steering gizmo; its
    // local Z is the wheel's spin axis.
    Mat4 get_steering_wheel_pivot_world() const;

    // Debug calibration: a full REST-orientation correction applied to the steering
    // wheel about its pivot (yaw/pitch/roll/quaternion), visible at any steer angle,
    // so the debug UI can straighten a tilted/misaligned wheel. The left-right spin
    // still happens about local +Z. Identity (default) → unchanged.
    void set_steer_axis_correction(const glm::quat& q) { m_steer_axis_correction = q; }

    // Rain intensity [0,1] — drives the interior cabin "wash toward light gray"
    // tint applied in get_draw_calls (via the gbuffer.frag color.a sentinel).
    void set_rain_intensity(float intensity) { m_rain_intensity = intensity; }

    // Road wetness [0,1] — RainSystem::get_wetness()'s INTEGRATED value
    // (asymmetric ramp: 0.08/s wet, 0.012/s dry), NOT raw rain intensity, so
    // the wet-grip model (CarPhysics.h::effective_mu) loses grip gradually
    // during rain and recovers gradually after, matching the visual sheen.
    // App.cpp pushes this once per frame, before update().
    void set_wetness(float wetness) { m_wetness = wetness; }

    // Effective tire-road friction this frame (CarPhysics.h::effective_mu,
    // Wave 2 step 4), recomputed once per update() from wetness + speed +
    // standing-water depth at the car's position. Exposed for debug/telemetry.
    float get_effective_mu() const { return m_effective_mu; }

    // ── Telemetry accessors (debug-UI HUD; issue 5's readout half) ────
    // The cockpit gauge cluster is BAKED PIXELS in the GLB texture atlas (no
    // gauge/needle/dial nodes exist to drive), so it can't serve as a speed
    // readout — these feed the ImGui telemetry panel instead. Trivial,
    // side-effect-free, header-only inline: compiles into both debug and
    // release, but only the (debug-only) panel ever calls them, so release
    // behavior is unaffected either way.

    // Driver-intent / wheel readouts for telemetry + replay (read-only).
    float get_throttle() const { return m_throttle; }
    float get_brake() const { return m_brake; }
    bool  get_reverse() const { return m_reverse; }
    float get_lateral_velocity() const { return m_lateral_velocity; }  // m/s, body rightward
    float get_yaw_rate() const { return m_yaw_rate; }                  // rad/s, right-positive
    // Wheel spin angle in [0, 2pi), corner order 0 FL, 1 FR, 2 BL, 3 BR (c in [0,4)).
    float get_wheel_spin(int c) const { return m_wheel_spin[static_cast<size_t>(c)]; }
    // Phase-B quarter-car state for a corner (x_m travel in m, v_mps), same corner order.
    const CornerSpring& get_corner_suspension(int c) const { return m_corner_susp[static_cast<size_t>(c)]; }

    // Current gear, 0-indexed (0 = 1st ... 6 = 7th) — GearState has no other
    // public accessor.
    int get_gear() const { return m_gear_state.gear; }

    // Engine rpm implied by this frame's speed + gear, via the SAME no-slip
    // driveline mapping CarEntity::update() already uses to drive the
    // auto-shift machine (CarPhysics.h::engine_rpm) — not new state, just the
    // existing model evaluated for display. GearboxParams{} matches update()'s
    // local `const GearboxParams gearbox;` (also default-constructed).
    float get_engine_rpm() const {
        return engine_rpm(m_forward_speed / kWorldUnitsPerMeter, m_gear_state.gear, GearboxParams{},
                          rear_wheel_radius());
    }

    // Phase-C slip/skid/spray-boost summary (SlipVisuals.h), evaluated from the
    // CURRENT state on demand; const, touches no physics state.
    // TODO(stub): see SlipVisuals.h for what the full version needs.
    CarSlipVisuals get_wheel_slip_visuals(const SlipVisualParams& sp = SlipVisualParams{}) const {
        SlipInputs in;
        in.vx    = m_forward_speed / kWorldUnitsPerMeter;
        in.vl    = m_lateral_velocity;
        in.r     = m_yaw_rate;
        in.delta = glm::radians(std::clamp(m_steering_angle, -kMaxSteer, kMaxSteer));
        in.fx    = m_long_force;
        in.mu    = m_effective_mu;
        return compute_slip_visuals(in, CarParams{}, TireParams{}, sp);
    }

    // World-space tyre contact point of wheel c (0 FL · 1 FR · 2 BL · 3 BR), WU: the loader-recovered pivot centre
    // dropped by the rolling radius. Falls back to the car origin until wheel frames are loaded. Const, read-only
    // (debug per-wheel spray feed; nothing in the sim or draw path calls it).
    Vec3 get_wheel_contact_world(int c) const {
        if (!m_wheels_valid || c < 0 || c > 3)
            return get_position();
        Vec3 p = Vec3((get_model_matrix() * m_wheel_frames[static_cast<std::size_t>(c)].frame)[3]);
        p.y -= m_wheel_frames[static_cast<std::size_t>(c)].radius * kWorldUnitsPerMeter;
        return p;
    }

    // ── Road-wheel articulation (kinematic spin + steer) ──────────────
    // Loader-recovered per-corner pivot frames (0 FL · 1 FR · 2 BL · 3 BR).
    // Until this is called, wheel submeshes render rigid (pre-feature look).
    void set_wheel_frames(const std::array<WheelFrame, 4>& frames) {
        m_wheel_frames = frames;
        m_wheels_valid = true;
    }
    bool wheels_valid() const { return m_wheels_valid; }

    // Phase-B quarter-car suspension (archive/docs/wheels/phase-b-spec.md). A LATE
    // per-frame update: call from App.cpp AFTER CorridorDriver::update() (so the
    // body Y-snap and any origin rebase have settled) and before draw
    // calls are generated. Touches ONLY the per-corner suspension state; the
    // vertical offset it produces is composed into get_draw_calls' local
    // wheel/steer matrices (§7) — the entity transform itself is untouched (§8).
    void update_wheel_visuals(float dt);

    // Debug-UI tuning (App pushes these under SWISH_DEBUG_UI each frame).
    // The defaults ARE the shipped behavior, so release — which never calls
    // this — behaves exactly like an untouched panel.
    void set_wheel_tuning(bool spin_on, bool steer_on, float spin_mul) {
        m_wheel_spin_on  = spin_on;
        m_wheel_steer_on = steer_on;
        m_wheel_spin_mul = spin_mul;
    }

#ifdef SWISH_DEBUG_UI
    // Physics-model selector (stub; the debug-UI "Physics model" toggle binds to these).
    // Bicycle (default) is the shipped model; RigidBody drives the entity pose, including pitch/roll
    // and heave, from a 6-DOF body on four spring-damper wheel contacts (RigidChassis.h).
    // Switching either way hands the speed / yaw state over so the car does not jump.
    void         set_physics_model(PhysicsModel model);
    PhysicsModel get_physics_model() const { return m_physics_model; }
#endif

    // World-space unit vector pointing toward the car's nose (+X at yaw=0).
    Vec3 get_forward() const {
        float yaw = glm::radians(m_rotation.y);
        return Vec3(std::cos(yaw), 0.f, -std::sin(yaw));
    }

    // ── Car lights (tasks/todo.md "Car lights") ──────────────────────
    // Lamp anchors in car space, read by ModelManager::load_car from the GLB's
    // `<Anchor>_Lamp` nodes (CarLights.h). Empty for assets without them (the
    // original porsche.glb), which then get no car lights.
    void set_light_anchors(std::vector<CarLightAnchor> anchors) { m_light_anchors = std::move(anchors); }
    const std::vector<CarLightAnchor>& get_light_anchors() const { return m_light_anchors; }
    // World-space spot lights for the current pose; empty when everything is off.
    std::vector<SpotLightDesc> get_spot_lights(const CarLightState& state, const CarLightParams& params) const {
        return car_spot_lights(m_light_anchors, get_model_matrix(), state, params);
    }

    std::vector<DrawCall> get_draw_calls() const override;

#ifdef SWISH_DEBUG_UI
    // Per-object velocity buffer support (tasks/todo.md "Per-object velocity buffer";
    // debug-only, VelocityPass). get_draw_calls() stashes each submesh's freshly-computed
    // model matrix as its LAST step every call; this accessor returns the matrices that
    // were current BEFORE that call (i.e. last frame's pose) in the SAME order, so a caller
    // reading this right after get_draw_calls() gets an exact index-for-index pairing of
    // (this frame, last frame) transforms for car body + wheels — the rigid-motion diff
    // VelocityPass needs. Falls back to "no motion" (equal to this frame's own matrices)
    // whenever the submesh count changes between calls (e.g. wheel frames just finished
    // loading), so a topology change can never mis-pair indices.
    const std::vector<Mat4>& get_prev_draw_models() const { return m_lastExposedPrevModels; }
#endif

    // Glass (BLEND) draw calls — same VBO/IBO as the opaque mesh but rendered
    // in the forward transparent pass (GlassPass).
    std::vector<DrawCall> get_windshield_draw_calls() const;

private:
    float     m_forward_speed  = 0.f;                       // world units / second, positive = forward
    float     m_steering_angle = 0.f;                       // degrees, negative = left, positive = right
    float     m_rain_intensity = 0.f;                       // [0,1] — drives the interior cabin wash tint
    glm::quat m_steer_axis_correction{1.f, 0.f, 0.f, 0.f};  // debug spin-axis fix (identity = none)

    // Wet-road grip model (Wave 2 step 4, CarPhysics.h::effective_mu).
    // m_wetness is pushed by App each frame (set_wetness()); m_effective_mu is
    // this frame's result, recomputed at the top of update() and then written
    // into every CarParams::tireMu construction that frame. Defaults match
    // CarParams' own dry tireMu so nothing reads a wrong value pre-update().
    float         m_wetness      = 0.f;    // [0,1] RainSystem::get_wetness()
    float         m_effective_mu = 1.05f;  // matches CarParams::tireMu default (Turbo S, PS4S)
    WetGripParams m_wet_grip{};            // debug-UI sliders can attach here later

    // ── Hydroplane spin-out (Phase 2, archive/docs/phase-2-hydroplane-spinout.md) ──
    // m_plane_frac: how completely the tire is planing this frame, in [0,1]
    // (CarPhysics.h::hydroplane_fraction). EXACTLY zero on dry roads, on
    // merely-wet roads, and on standing water below vPlane — and every Phase-2
    // term is weighted by it, so those cases are bit-identical to Phase 1.
    // m_long_force: last step's SIGNED per-axle longitudinal tire force (N),
    // reported by longitudinal_accel. Feeds the lateral half of the friction
    // circle one step later; the lag is what breaks the circular dependency
    // between the two axes (see the comment at its use in update()).
    float            m_plane_frac = 0.f;
    AxleLongitudinal m_long_force{};

    // Road-wheel state: per-corner spin angle θ, wrapped to [0, 2π) every
    // step (WheelKinematics.h) so float precision holds over km drives.
    std::array<WheelFrame, 4> m_wheel_frames{};
    std::array<float, 4>      m_wheel_spin{0.f, 0.f, 0.f, 0.f};
    bool                      m_wheels_valid = false;
    // Phase-B per-corner suspension state, advanced by update_wheel_visuals()
    // (WheelKinematics.h). All four start at rest {0,0} — identical to
    // today's rigid pose until the first call moves them (§7).
    std::array<CornerSpring, 4> m_corner_susp{};
    // Debug tuning (defaults = shipped behavior; see set_wheel_tuning).
    bool  m_wheel_spin_on  = true;
    bool  m_wheel_steer_on = true;
    float m_wheel_spin_mul = 1.f;

#ifdef SWISH_DEBUG_UI
    // Per-object velocity buffer bookkeeping (see get_prev_draw_models() above). Both are
    // mutable: get_draw_calls() stays logically const (a pure "compute this frame's draw
    // list" query) while still rolling this rolling-previous-frame cache forward.
    // m_rollingPrevModels: after a call, holds THIS call's per-submesh models — becomes
    // "previous" for the NEXT call.
    mutable std::vector<Mat4> m_rollingPrevModels;
    // m_lastExposedPrevModels: snapshot of m_rollingPrevModels as it was BEFORE the most
    // recent get_draw_calls() call — what get_prev_draw_models() returns.
    mutable std::vector<Mat4> m_lastExposedPrevModels;
#endif

    std::vector<CarLightAnchor> m_light_anchors;  // car-space lamps (see set_light_anchors)
#ifdef SWISH_DEBUG_UI
    // ── Rigid-body chassis (stub; RigidChassis.h) ─────────────────────
    // Only live while m_physics_model == RigidBody. The body is authoritative for pose: each step it
    // follows any external shift of m_position (origin rebase, teleport) via m_rigid_last_pos, integrates,
    // then writes position (WU), Euler rotation (deg) and the telemetry scalars back to the entity.
    PhysicsModel m_physics_model = PhysicsModel::Bicycle;
    RigidChassis m_rigid;
    bool         m_rigid_ready = false;  // body seeded from the entity pose
    Vec3         m_rigid_last_pos{0.f};  // m_position as this model last wrote it (WU)
    void         init_rigid_body();
    void         update_rigid_body(float dt);
#endif

    // Per-frame longitudinal controls, set by handle_input(), applied in update().
    float m_throttle = 0.f;    // [0,1]
    float m_brake    = 0.f;    // [0,1]
    bool  m_reverse  = false;  // DOWN held while ~stopped → creep backward

    // 7-speed PDK-style auto-shift state (Wave 2 step 3b, CarPhysics.h). Pure
    // state struct advanced each frame by gearbox_shift_step(); starts in 1st.
    GearState m_gear_state{};

    // Rear (driven-axle) rolling radius for the gearbox's rpm/force math —
    // the loader-measured value once wheel frames are recovered (§ Road-wheel
    // articulation below), else this fallback (matches the task's ~0.363 m
    // measured rear radius) so the drivetrain isn't zero-force/undefined
    // before ModelManager::load_car finishes.
    static constexpr float kFallbackRearRadiusM = 0.363f;
    float                  rear_wheel_radius() const {
        return m_wheels_valid ? m_wheel_frames[2].radius : kFallbackRearRadiusM;  // 2 = BL (rear)
    }

    // Lateral/yaw dynamic state (dynamic bicycle model, P0 #4). Right-positive.
    float m_lateral_velocity = 0.f;  // m/s (body rightward)
    float m_yaw_rate         = 0.f;  // rad/s (right-positive; heading -= deg(r)·dt)

    float m_min_x = -1e9f;
    float m_max_x = 1e9f;

    // Kinematic / control constants (~1000 WU/m scale, 1 mph ≈ 447 WU/s).
    // The longitudinal + tire DYNAMICS constants live in CarParams (CarPhysics.h);
    // this header keeps only the steering and speed-cap knobs.
    static constexpr float kWorldUnitsPerMeter = util::units::kWuPerMetre;  // 1 m = 1000 WU
    static constexpr float kMaxForwardSpeed    = 92'000.f;  // ~205 mph (only for kMaxSpeed normalization)
    static constexpr float kMaxReverseSpeed    = 12'000.f;  // ~27 mph in reverse
    static constexpr float kReverseAccel       = 6'000.f;   // WU/s²; gentle low-speed reverse
    static constexpr float kSpeedDeadZone      = 0.5f;
    static constexpr float kWheelLockToDeg     = 450.f;  // full lock-to-lock steering range
    static constexpr float kMaxSteer           = 35.f;   // degrees
    static constexpr float kSteerRatio         = kWheelLockToDeg / kMaxSteer;
    static constexpr float kSteerRate          = 90.f;   // degrees/s
    static constexpr float kSteerReturn        = 120.f;  // return-to-center rate
    // Real 992 Turbo S wheelbase ≈ 2.45 m (was 2.8 m).
    static constexpr float kWheelbase = 2'450.f;  // 2.45 m in WU
    // Below this speed CarEntity::update() uses the KINEMATIC yaw model (the
    // dynamic bicycle's 1/vx is singular there), so no lateral tire forces
    // exist to feed the friction circle. Named here because both the lateral
    // block and the longitudinal block need the same threshold.
    static constexpr float kDynamicBlendSpeed = 5.0f;  // m/s
    // "Genuinely at rest" thresholds for the lateral state, gating the dead-stop
    // snap and the early return (Phase 2). A spinning car crosses vx = 0 while
    // still sliding and rotating; without these it would freeze mid-spin.
    // 0.05 m/s is a tenth of kSpeedDeadZone in SI (0.5 WU/s = 0.0005 m/s is far
    // too tight to ever latch), and 0.02 rad/s ≈ 1.1 deg/s is below anything
    // visible in a frame.
    static constexpr float kRestLateralSpeed = 0.05f;  // m/s
    static constexpr float kRestYawRate      = 0.02f;  // rad/s
    // (The old kSteerRefSpeed authority taper is gone — the dynamic tire model's
    //  μ·Fz saturation now provides the real high-speed grip limit.)

public:
    static constexpr float kMaxSpeed = kMaxForwardSpeed;  // exposed for normalization
};

}  // namespace swish
