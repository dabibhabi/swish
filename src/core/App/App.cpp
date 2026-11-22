#include "App.h"

#include "core/Window/Window.h"
#ifdef SWISH_DEBUG_UI
#include "core/Input/GlfwGamepadSource.h"
#include "debug/TelemetryRecorder.h"
#endif
#include "renderer/Renderer/Renderer.h"
#include "renderer/SpraySystem/WheelSpray.h"
#include "renderer/TextureManager/TextureManager.h"
#include "scene/Camera/Camera.h"
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/CarLooks.h"
#include "scene/ModelManager/ModelManager.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "scene/SceneManager/SceneManager.h"
#include "utils/units/Units.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <thread>

namespace swish {

App::App() = default;

// Defined here (not defaulted in the header) so the unique_ptr members see the
// complete subsystem types. Members destruct in reverse declaration order.
App::~App() = default;

// ══════════════════════════════════════════════════════════════════════
// Accessors (archive/src/core/App/REFACTOR.md §3b) — each setter is the single place that keeps
// the field in sync with the renderer/car side effect it implies.
// ══════════════════════════════════════════════════════════════════════

void App::set_rain_level(int level) {
    static constexpr float kRainLevels[] = {0.0f, 0.35f, 1.0f};
    m_rain_level                         = ((level % 3) + 3) % 3;
    const float rain                     = kRainLevels[m_rain_level];
    m_renderer->set_rain_intensity(rain);
    // Drive the interior cabin wash from the same rain level so the cockpit
    // reads light gray as rain rises (off → light → heavy).
    if (m_car) m_car->set_rain_intensity(rain);
#ifdef SWISH_DEBUG_UI
    // apply_debug_params copies the panel's rain slider into the renderer every
    // frame; keep the slider in step so the R key is not overwritten next frame.
    m_renderer->debug_params().rainIntensity = rain;
#endif
    // Rain and a clear-day sky are mutually exclusive — an azure sunny sky with
    // rain falling reads wrong — so rain switches a clear-day preset to Cloudy.
    // Going through set_weather_preset keeps m_weather_preset (the G-key cycle)
    // in step with what the renderer shows.
    if (const WeatherPreset p = preset_for_rain(m_weather_preset, rain); p != m_weather_preset) set_weather_preset(p);
}

void App::set_weather_preset(WeatherPreset preset) {
    m_weather_preset = preset;
    m_renderer->set_weather_preset(preset);
    std::cout << "[weather] " << weather_preset_name(preset) << std::endl;
}

void App::set_wiper_mode(WiperMode mode) {
    m_wiper = wiper_set_mode(m_wiper, mode);  // the blade keeps its place; Off finishes the wipe and parks
    std::cout << "[wiper] " << wiper_mode_name(mode) << std::endl;
}

void App::set_camera_mode(CameraMode mode) {
    m_camera_ctl.set_mode(mode);  // resets look offsets + cockpit smoothing / FOV kick
    std::cout << "[camera] " << camera_mode_name(mode) << std::endl;
}

void App::set_cursor_captured(bool captured) {
    m_cursor_captured = captured;
    glfwSetInputMode(m_window->getHandle(), GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    m_first_mouse = true;
}

// ══════════════════════════════════════════════════════════════════════
// Main application loop
// ══════════════════════════════════════════════════════════════════════

int App::run() {
    init();

    float last_frame_time = static_cast<float>(glfwGetTime());
    // E4: fps in the title bar (both builds), refreshed twice a second from wall-clock time. frame_delta is
    // untouched (it is capped at 1/15 s for the physics, so it would under-report slow frames).
    double fps_window_start = glfwGetTime();
    int    fps_frames       = 0;
    // P2: cap at 60 fps; FIFO alone runs at 120 on ProMotion and doubles the GPU work.
    using Clock                            = std::chrono::steady_clock;
    constexpr Clock::duration kFramePeriod = std::chrono::nanoseconds(1'000'000'000 / 60);
    Clock::time_point         next_frame   = Clock::now();
    GLFWwindow* const         win          = m_window->getHandle();
    while (!m_window->shouldClose()) {
        // P2: a minimized or hidden window renders nothing visible, so idle instead of drawing.
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED) || !glfwGetWindowAttrib(win, GLFW_VISIBLE)) {
            glfwWaitEventsTimeout(0.1);
            last_frame_time = static_cast<float>(glfwGetTime());  // no dt spike on restore
            next_frame      = Clock::now();
            continue;
        }
        const float dt = frame_delta(last_frame_time);
        ++fps_frames;
        if (const double now = glfwGetTime(); now - fps_window_start >= 0.5) {
            char title[64];
            std::snprintf(title, sizeof(title), "swish  |  %.0f fps", fps_frames / (now - fps_window_start));
            glfwSetWindowTitle(m_window->getHandle(), title);
            fps_window_start = now;
            fps_frames       = 0;
        }
#ifdef SWISH_DEBUG_UI
        sync_debug_edit_mode_from_panel();  // the panel may have toggled it last frame
#endif
        // In debug edit-mode the scene freezes so you can tune a static frame.
        const bool debug_edit = debug_edit_mode();

        poll_events();
        process_key_bindings();
        m_camera_ctl.poll_input(m_window->getHandle(), m_renderer->get_camera(), dt, m_cursor_captured, debug_edit);
        update_car(dt, debug_edit);
        update_camera(dt, debug_edit);

        m_renderer->drawFrame(dt);

        // Fixed deadlines keep the average at 60; after a long frame, resync instead of bursting to catch up.
        next_frame += kFramePeriod;
        if (const Clock::time_point now = Clock::now(); next_frame < now)
            next_frame = now;
        else
            std::this_thread::sleep_until(next_frame);
    }

    shutdown();
    return 0;
}

float App::frame_delta(float& last_frame_time) const {
    float current_time = static_cast<float>(glfwGetTime());
    float delta_time   = current_time - last_frame_time;
    last_frame_time    = current_time;
    return std::min(delta_time, 1.0f / 15.0f);  // cap at ~67ms to prevent physics explosion
}

bool App::debug_edit_mode() const {
#ifdef SWISH_DEBUG_UI
    return m_debug_edit_mode;
#else
    return false;
#endif
}

void App::poll_events() {
    // The resize flag is NOT consumed here: the renderer's present step reads it and
    // Renderer::recreateSwapchain clears it. (Clearing it here, before drawFrame, made
    // that check dead and left resizes relying on VK_ERROR_OUT_OF_DATE alone.)
    m_window->pollEvents();
}

// Drive the car (arrow keys), then hand its draw state to the renderer.
void App::update_car(float dt, bool debug_edit) {
    if (!m_car) return;

    step_car_simulation(dt, debug_edit);
#ifdef SWISH_DEBUG_UI
    sync_car_with_debug_panel(dt, debug_edit);
#endif
    update_wiper(dt);
    push_car_to_renderer();
}

// One wiper angle for the frame: the rigged blades (CarEntity::get_draw_calls) and the windshield
// rain clear (WindshieldRainPass) both read it, so the cleared glass is where the blades are.
void App::update_wiper(float dt) {
#ifdef SWISH_DEBUG_UI
    const DebugParams& dp          = m_renderer->debug_params();
    m_wiper_params.lowWipesPerMin  = dp.wiperLowWpm;
    m_wiper_params.highWipesPerMin = dp.wiperHighWpm;
    m_wiper_params.dwellS          = dp.wiperDwellS;
    m_wiper_params.sweepDeg        = dp.wiperSweepDeg;
    m_wiper_params.ease            = dp.wiperEase;
#endif
    m_wiper           = wiper_step(m_wiper, m_wiper_params, dt);
    const float angle = wiper_angle(m_wiper, m_wiper_params);
    m_car->set_wiper_angle(angle);

    WiperFrame frame;
    frame.angle    = angle;
    frame.sweep    = glm::radians(m_wiper_params.sweepDeg);
    frame.active   = wiper_active(m_wiper);
    frame.carModel = m_car->get_model_matrix();
    for (const WiperBlade& b : m_car->get_wiper_blades()) {
        if (frame.bladeCount < frame.blades.size()) frame.blades[frame.bladeCount++] = b;
    }
    m_renderer->set_wiper(frame);
}

void App::step_car_simulation(float dt, bool debug_edit) {
    // Wet-grip physics (Wave 2 step 4, CarPhysics.h::effective_mu) reads
    // the SAME integrated wetness the renderer's wet-road/puddle shading
    // uses — not raw rain intensity — so grip loss outlasts a shower
    // exactly like the visual sheen does. One frame stale at most (this
    // reads last frame's RainSystem::update output); wetness ramps at
    // 0.08/s (wet) or 0.012/s (dry), so that lag is imperceptible.
    m_car->set_wetness(m_renderer->get_wetness());

    if (!debug_edit) {
#ifdef SWISH_DEBUG_UI
        if (!step_driving_tools(dt))  // gamepad / record / replay; false = the unchanged keyboard path
#endif
        {
#ifdef SWISH_DEBUG_UI
            if (m_controlsKeyboard.up || m_controlsKeyboard.down || m_controlsKeyboard.left ||
                m_controlsKeyboard.right) {
                const InputFrame input = m_car->keyboard_input_frame(m_controlsKeyboard, dt);
                m_car->apply_input_frame(input, dt);
                m_car->update(dt);
            } else
#endif
            {
                m_car->handle_input(m_window->getHandle(), dt);
                m_car->update(dt);
            }
        }
    }

#ifdef SWISH_DEBUG_UI
    // Lighting lab: the wall holds the car in (a position clamp; no collision physics).
    if (m_labMode) m_car->set_position(lab_clamp_to_wall(m_car->get_position()));
#endif

    // Finite real corridor: origin rebase, visible-segment window,
    // corridor-space lateral clamp, terminus stops. Replaced the
    // treadmill's chunk/interchange windows and ribbon-follower.
#ifdef SWISH_DEBUG_UI
    if (!m_lookdevMode)
#endif
        m_corridor.update(*m_car, *m_renderer, dt, debug_edit);

    // Snap Y to the road surface so wheels sit on it as the car steers. Runs AFTER the corridor
    // update, which aims the height query at the surface under the car's new station.
    // On the Phase 3a corridor that query is a flat height
    // (RoadGeometry::set_flat_surface) rather than the crown formula,
    // because world X is not a lateral offset on a curving alignment.
#ifdef SWISH_DEBUG_UI
    // The rigid-body chassis owns the car's height (heave); the bicycle model needs the snap.
    const bool car_owns_height = m_car->get_physics_model() == PhysicsModel::RigidBody;
#else
    constexpr bool car_owns_height = false;
#endif
    if (!car_owns_height) {
        Vec3 pos = m_car->get_position();
        pos.y    = RoadGeometry::road_surface_y(pos.x);
        m_car->set_position(pos);
    }

    // Phase-B quarter-car suspension (archive/docs/wheels/phase-b-spec.md): a LATE
    // update run after the corridor update so the body's Y-snap and any
    // origin rebase have settled. Draw-matrix-only — never touches the car's
    // transform (§8).
    m_car->update_wheel_visuals(dt);
    m_car->update_gauges(dt);  // dial needles (named rig only; GaugeNeedles.h)
}

#ifdef SWISH_DEBUG_UI
// Steering-wheel gizmo: feed the current wheel pivot to the debug UI, and
// in edit mode pose the wheel from the gizmo/override angle (the sim is
// frozen then). The draw calls pushed afterwards re-generate with the new angle.
void App::sync_car_with_debug_panel(float dt, bool debug_edit) {
    DebugParams& dp    = m_renderer->debug_params();
    dp.steerPivotWorld = m_car->get_steering_wheel_pivot_world();
    if (m_labMode) {
        // Teleport request from the panel (one-shot), then the light probe under the car.
        const auto& stations = lab_stations();
        if (dp.labTeleport >= 0 && dp.labTeleport < static_cast<int>(stations.size())) {
            const LabStation& st = stations[static_cast<size_t>(dp.labTeleport)];
            m_car->set_position(st.position);
            m_car->set_rotation(Vec3(0.0f, st.yawDeg, 0.0f));
            m_car->halt();
            if (st.timeOfDay >= 0) {  // a place built for night switches the time of day with it
                dp.skyProcedural = true;
                dp.timeOfDay     = st.timeOfDay;
            }
        }
        dp.labTeleport     = -1;
        dp.labProbe        = lab_probe(m_labLights, m_labRoofs, m_car->get_position());
        dp.labSunLuxGround = dp.sunLux * std::max(std::sin(dp.sunElevation), 0.0f);  // from the Elevation slider
    }
    dp.steerMaxDeg = m_car->steer_max();
    sync_driving_tools_panel(dp);

    // Telemetry readout (issue 5). Pure push of live car state into the
    // Telemetry panel — read-only, debug-only. The cockpit gauge cluster
    // can't serve as the readout: it's baked pixels in the GLB atlas with
    // no needle node, so the ImGui panel is the only route without new art.
    dp.telemetrySpeedMps = m_car->get_speed_mps();
    dp.telemetryGear     = m_car->get_gear();
    dp.telemetryRpm      = m_car->get_engine_rpm();
    dp.telemetryRedline  = m_car->get_redline_rpm();
    dp.telemetryMu       = m_car->get_effective_mu();
    sync_hud(dp);
    if (debug_edit && dp.steerOverride)
        m_car->set_steering_angle(dp.steerAngleDeg);
    else
        dp.steerAngleDeg = m_car->get_steering_angle();
    // Spin-axis calibration correction (edit mode); identity otherwise.
    m_car->set_steer_axis_correction((debug_edit && dp.steerAxisEdit)
                                         ? glm::quat(dp.steerQuat.w, dp.steerQuat.x, dp.steerQuat.y, dp.steerQuat.z)
                                         : glm::quat(1.f, 0.f, 0.f, 0.f));
    // Road-wheel tuning (panel defaults = the shipped behavior).
    m_car->set_wheel_tuning(dp.wheelSpinEnabled, dp.wheelSteerEnabled, dp.wheelSpinMul);
    m_car->set_ackermann(dp.wheelAckermann);  // seeded from config/car.toml at startup (AppInit load_car)
    // Physics model (stub): unchecked = bicycle (default / identity). set_physics_model is idempotent.
    m_car->set_physics_model(dp.rigidBodyChassis ? PhysicsModel::RigidBody : PhysicsModel::Bicycle);
    m_car->set_substep_hz(dp.carSubstepHz);
    KeyboardSteerParams ks = m_car->get_keyboard_steer();  // rates/gain stay from config/car.toml
    ks.assist              = dp.carKeyAssist;
    ks.assistSlipDeg       = dp.carKeyAssistSlip;
    ks.throttleRise        = dp.carThrottleRise;
    ks.throttleFall        = dp.carThrottleFall;
    ks.brakeRise           = dp.carBrakeRise;
    ks.brakeFall           = dp.carBrakeFall;
    m_car->set_keyboard_steer(ks);
    m_car->set_chassis_tuning({dp.carTireShape, dp.carKappaPeak, dp.carLoadSens, dp.carAlphaPeakFront,
                               dp.carAlphaPeakRear, dp.carArbFront, dp.carArbRear, dp.carBrakeBias, dp.carDownforce,
                               dp.carTc});
    // Dial needles: the "gauges" sliders over the TOML calibration (the speedo table stays TOML-only).
    GaugeParams gp    = m_car->get_gauge_params();
    gp.tachOffsetDeg  = dp.gaugeTachOffsetDeg;
    gp.tachDegPerKrpm = dp.gaugeTachDegPerKrpm;
    gp.rpmRestDeg     = dp.gaugeRpmRestDeg;
    gp.speedRestDeg   = dp.gaugeSpeedRestDeg;
    gp.needleTauS     = dp.gaugeNeedleTauMs / 1000.f;
    gp.sweepDurationS = dp.gaugeSweepDurationS;
    m_car->set_gauge_params(gp);
    m_car->set_gauge_test(dp.gaugeTestOn, dp.gaugeTestRpm, dp.gaugeTestKmh);

    sync_wheel_fx_with_debug_panel(dt);

    // C2b diamonds: segments are built once, so a slider change rebuilds the scene after 0.5 s without edits.
    const std::array<float, 17> key{dp.corridorDiamonds ? 1.0f : 0.0f,
                                    dp.corridorDipDepthM,
                                    dp.corridorDipHalfM,
                                    dp.corridorSrStripM,
                                    dp.corridorStubM,
                                    dp.edgeWideMedianM,
                                    dp.edgeWallHM,
                                    dp.edgeDitchM,
                                    dp.corridorSigns ? 1.0f : 0.0f,
                                    dp.signSheen,
                                    dp.corridorBridges ? 1.0f : 0.0f,
                                    dp.bridgeClearFt,
                                    dp.corridorDressing ? 1.0f : 0.0f,
                                    dp.dressDensity,
                                    dp.dressFarM,
                                    dp.corridorLandmarks ? 1.0f : 0.0f,
                                    dp.landmarkGraves};
    if (key != m_corridorKey) {
        m_corridorKey  = key;
        m_corridorIdle = 0.0f;
    } else if (m_corridorIdle >= 0.0f && (m_corridorIdle += dt) > 0.5f) {
        m_corridorIdle = -1.0f;
        m_sceneManager->set_active_scene(0);
    }
    // C3a: span-wire signals run a fixed two-phase cycle (props only, no traffic logic)
    m_signalClock += static_cast<double>(dt);
    for (const auto& [seg, l] : m_signalLenses) {
        const bool on = signal_lit(l.group, l.colour, m_signalClock, dp.corridorSignalCycleS);
        m_renderer->set_corridor_draw_glow(seg, l.draw, signal_lens_color(l.colour, on),
                                           on ? signal_lens_nits(l.colour) : 0.0f);
    }
}

// Wheel Phase C / wet-lens debug wiring. Everything here is debug-only; with perWheelSpray off (default) the
// SpraySystem is never touched (legacy single rear-axle emitter), and the lens model only feeds the Telemetry panel.
void App::sync_wheel_fx_with_debug_panel(float dt) {
    DebugParams& dp = m_renderer->debug_params();

    // Slip visuals under the panel's thresholds (degrees -> radians for the struct).
    SlipVisualParams sp;
    sp.skidSlipRatio          = dp.slipSkidRatio;
    sp.skidSlipAngle          = dp.slipSkidAngleDeg * SlipVisualParams::kDegToRad;
    sp.ratioStart             = dp.slipRatioStart;
    sp.ratioFull              = dp.slipRatioFull;
    sp.angleStart             = dp.slipAngleStartDeg * SlipVisualParams::kDegToRad;
    sp.angleFull              = dp.slipAngleFullDeg * SlipVisualParams::kDegToRad;
    sp.sprayBoostMax          = dp.slipSprayBoostMax;
    const CarSlipVisuals slip = m_car->get_wheel_slip_visuals(sp);
    for (std::size_t i = 0; i < 4; ++i) {
        const WheelSlipVisual& w = slip.wheel[i];
        dp.telemetryWheel[i]     = {w.slipRatio, w.slipAngle, w.sprayBoost, w.skid};
    }

    // Per-wheel spray: four emitters from the wheels' contact points; off = hand the system back to the legacy origin
    // once (only on the on -> off edge), never per frame.
    if (dp.perWheelSpray) {
        const float wetness = m_renderer->get_wetness();
        const float cover   = dp.puddlesEnabled ? dp.puddleCoverage : 0.0f;
        const Vec3  vel     = m_car->get_forward() * m_car->get_speed();  // WU/s; lateral slide not modelled yet
        std::array<SprayEmitter, kMaxWheelEmitters> emitters{};
        uint32_t                                    count = 0;
        for (int c = 0; c < 4; ++c) {
            // compute_wheel_spray takes one standing-water scalar, so evaluate each wheel on its own puddle sample.
            std::array<WheelSprayInput, kMaxWheelEmitters> one{};
            one[0].pos                      = m_car->get_wheel_contact_world(c);
            one[0].vel                      = vel;
            one[0].slipSprayMul             = slip.wheel[static_cast<std::size_t>(c)].sprayBoost;
            const float            standing = puddle_amount(one[0].pos.x, one[0].pos.z, cover);
            const WheelSprayResult r        = compute_wheel_spray(one, wetness, standing);
            if (r.count > 0) emitters[count++] = r.emitters[0];
        }
        m_renderer->set_spray_emitters(emitters.data(), count);
        m_wheel_spray_active = true;
    } else if (m_wheel_spray_active) {
        m_renderer->set_spray_emitters(nullptr, 0);
        m_wheel_spray_active = false;
    }

    // Lamp-lens wetness: stepped from the applied rain intensity and the car's speed; panel rows only.
    static constexpr const char* kLensNames[4] = {"Headlight_L_Lens", "Headlight_R_Lens", "TailLight_L_Lens",
                                                  "TailLight_R_Lens"};
    if (!m_lens_wetness_ready) {
        for (const char* n : kLensNames) m_lens_wetness.add_lens(n);
        m_lens_wetness_ready = true;
    }
    LensWetnessParams lp = m_lens_wetness.params();
    lp.wetRate           = dp.lensWetRate;
    lp.clearSpeedStart   = dp.lensClearSpeedStart;
    lp.clearSpeedFull    = dp.lensClearSpeedFull;
    lp.clearRate         = dp.lensClearRate;
    lp.drainRate         = dp.lensDrainRate;
    m_lens_wetness.set_params(lp);
    m_lens_wetness.step(dt, dp.rainIntensity, std::fabs(m_car->get_speed_mps()));
    for (std::size_t i = 0; i < 4; ++i) {
        const LensWetnessState s = m_lens_wetness.get(kLensNames[i]);
        dp.telemetryLens[i]      = {s.coverage, s.dropSize, s.streak};
    }
}
#endif

void App::push_car_to_renderer() {
    m_renderer->update_dynamic_draw_calls(m_car->get_draw_calls());
    m_renderer->update_glass_draw_calls(m_car->get_glass_draw_calls());
    m_renderer->update_windshield_draw_calls(m_car->get_windshield_draw_calls());
#ifdef SWISH_DEBUG_UI
    // Per-object velocity buffer (todo.md): must be read AFTER get_draw_calls()
    // above — it stashes each submesh's model matrix as its last step, so this
    // call sees the matrices as of BEFORE that call (last frame's pose), pairing
    // index-for-index with the draws just pushed.
    m_renderer->update_dynamic_prev_draw_models(m_car->get_prev_draw_models());

    // Car lights (tasks/todo.md "Car lights"): world spot lights for this frame's pose.
    // Panel off (the default) uploads none, so the image is today's.
    {
        const DebugParams&         dp = m_renderer->debug_params();
        std::vector<SpotLightDesc> spots;
        // Which lamps are on. Mode 0 follows the panel switches and the time of day (lighting L6.6); the other
        // modes are the showcase's fixed states: off, parking (tail only), low beam, brake held.
        bool  headOn = false, tailOn = false;
        float brake = dp.carBrakeForce ? 1.f : m_car->get_brake();
        switch (dp.carLightsMode) {
            case 1:
                brake = 0.f;
                break;
            case 2:
                tailOn = true;
                break;
            case 3:
                headOn = tailOn = true;
                break;
            case 4:
                headOn = tailOn = true;
                brake           = 1.f;
                break;
            default: {
                const bool on = dp.carLightsEnabled || dp.lampsOn;
                headOn        = on && dp.carHeadlightsOn;
                tailOn        = on && dp.carTailLightsOn;
                if (!on) brake = 0.f;
                break;
            }
        }
        if (headOn || tailOn || brake > 0.f) {
            CarLightParams params;
            params.headCutoff   = dp.headlightCutoff;
            params.headConeRule = true;  // debug first: a tighter beam is brighter (lighting L6.4)
            params.headLumens   = dp.carHeadLumens;
            params.headRangeM   = dp.carHeadRangeM;
            params.headInnerDeg = std::min(dp.carHeadInnerDeg, dp.carHeadOuterDeg);
            params.headOuterDeg = dp.carHeadOuterDeg;
            params.tailLumens   = dp.carTailLumens;
            params.brakeLumens  = dp.carBrakeLumens;
            params.tailRangeM   = dp.carTailRangeM;
            spots               = m_car->get_spot_lights(CarLightState{headOn, tailOn, brake}, params);
        }
        m_renderer->set_car_spot_lights(spots);
        // Lighting L5.2: the lenses glow while their lamp is on (emissive, so bloom picks them up).
        m_car->set_lens_nits(headOn ? dp.lensNitsHead : 0.f, tailOn ? dp.lensNitsTail : 0.f, dp.lensNitsBrake * brake);

        // Car showcase: the paint look goes through the material overrides of the body-paint slots; fixes and
        // the wet surface are per-draw flags on the car.
        {
            DebugParams&      edit = m_renderer->debug_params();
            const PaintFinish finish =
                static_cast<PaintFinish>(std::clamp(dp.paintFinish, 0, static_cast<int>(PaintFinish::Count) - 1));
            const PaintColour colour =
                static_cast<PaintColour>(std::clamp(dp.paintColour, 0, static_cast<int>(PaintColour::Count) - 1));
            const CarSurface surface =
                static_cast<CarSurface>(std::clamp(dp.carSurface, 0, static_cast<int>(CarSurface::Count) - 1));
            const PaintBase        base{m_car->paint_metalness(), kDefaultClearcoatStrength * dp.clearcoatMul,
                                        dp.paintFlakeDensity};
            const MaterialOverride look = paint_look(finish, colour, surface, base);
            if (look.enabled || m_paint_look_applied) {
                for (const MaterialId mat : m_car->paint_materials())
                    edit.matOverrides[mat] = look.enabled ? look : MaterialOverride{};
                m_paint_look_applied = look.enabled;
            }
            m_car->set_material_fixes(dp.materialFixes);
            // L10.2 lens rain: with car_rain_wet, a wet road also beads the car's exterior (body and lamp lenses), in
            // proportion to the road wetness; the showcase "Wet" surface still forces it fully on.
            const float rainWet = dp.carRainWet ? m_renderer->get_wetness() : 0.0f;
            m_car->set_surface_wet(std::max(surface == CarSurface::Wet ? 1.f : 0.f, rainWet));
        }
        m_car->set_exterior_wettable(dp.carWettable || dp.carSurface == 1 ||
                                     (dp.carRainWet && m_renderer->get_wetness() > 0.02f));  // lighting L10.1 / L10.2
    }
#endif

    // Pass car velocity to renderer so rain streaks lean forward at speed.
    // Convention (CarEntity.h): forward = (cos yaw_rad, 0, -sin yaw_rad)
    float yaw_rad = m_car->get_rotation().y * (util::units::kPi / 180.0f);
    Vec3  carFwd(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
    m_renderer->set_car_velocity(carFwd * m_car->get_speed());
    // Car world position drives the GPU road-spray spawn origin (rear axle).
    m_renderer->set_car_position(m_car->get_position());
}

void App::update_camera(float dt, bool debug_edit) {
    m_camera_ctl.update(m_renderer->get_camera(), m_car.get(), dt, debug_edit);
#ifdef SWISH_DEBUG_UI
    {
        DebugParams& dp = m_renderer->debug_params();
        // Car showcase: the car's body as a box, for the grounding shadow under area lights. A sports car is
        // about 4.5 x 1.3 x 1.9 m with 0.12 m of ground clearance.
        using util::units::m;
        CarOccluder box;
        box.center  = m_car->get_position() + Vec3(0.0f, m(0.72f), 0.0f);
        box.half    = Vec3(m(2.25f), m(0.60f), m(0.95f));
        box.yawRad  = glm::radians(m_car->get_rotation().y);
        box.enabled = dp.groundingShadow;
        m_renderer->set_car_occluder(box);
        // Turntable: orbit the camera around the parked car, always looking at it.
        if (dp.turntable) {
            if (Camera* cam = m_renderer->get_camera()) {
                dp.turntableAngle  = std::fmod(dp.turntableAngle + dp.turntableSpeed * dt, 360.0f);
                const float a      = glm::radians(dp.turntableAngle);
                const Vec3  target = m_car->get_position() + Vec3(0.0f, m(0.6f), 0.0f);
                const Vec3  eye    = target + Vec3(std::cos(a) * m(dp.turntableRadius), m(dp.turntableHeight - 0.6f),
                                                   std::sin(a) * m(dp.turntableRadius));
                const Vec3  d      = glm::normalize(target - eye);
                cam->set_position(eye);
                cam->set_yaw(glm::degrees(std::atan2(d.z, d.x)));
                cam->set_pitch(glm::degrees(std::asin(d.y)));
            }
        }
    }
#endif
}

// Release GPU resources while the device is still alive (reverse order); the
// owned objects themselves are destroyed automatically (reverse declaration
// order) when App is destroyed after run() returns — no manual delete needed.
void App::shutdown() {
    m_renderer->wait_for_idle();
    m_car.reset();
    m_renderer->destroy_dynamic_geometry();
    m_textureManager->cleanup();
    m_modelManager->cleanup();
    m_renderer->cleanup();
    m_window->cleanup();
}

}  // namespace swish
