// App driving tools (debug build only): gamepad input source, telemetry record / replay / CSV export,
// and the FFB target readout. The whole file is empty without SWISH_DEBUG_UI, so release is untouched;
// with the panel at its defaults (keyboard, nothing recording) step_driving_tools() returns false and
// the car is driven by CarEntity::handle_input exactly as before.
#include "App.h"

#ifdef SWISH_DEBUG_UI

#include "core/Input/GlfwGamepadSource.h"
#include "core/Window/Window.h"
#include "debug/DebugParams.h"
#include "debug/TelemetryRecorder.h"
#include "renderer/Renderer/Renderer.h"
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/ForceFeedback.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace swish {

namespace {

// Same key mapping and precedence as CarEntity::handle_input (used only while recording a keyboard
// drive, where the InputFrame itself is needed): UP over DOWN, LEFT over RIGHT, steer Rate mode.
InputFrame keyboard_frame(GLFWwindow* window) {
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
    return f;
}

template <std::size_t N>
void set_text(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src ? src : "");
}

}  // namespace

void App::ensure_driving_tools() {
    if (!m_gamepad)
        m_gamepad = std::make_unique<GlfwGamepadSource>();
    if (!m_recorder)
        m_recorder = std::make_unique<TelemetryRecorder>();
}

bool App::step_driving_tools(float dt) {
    ensure_driving_tools();
    DebugParams& dp = m_renderer->debug_params();

    const bool replaying = m_recorder->replay_active();
    const bool gamepad   = dp.drvInputSource == 1;
    if (!replaying && !gamepad && !m_recorder->recording())
        return false;  // keyboard, idle recorder: leave the car to handle_input()

    InputFrame f;
    if (replaying) {
        f = m_recorder->next_replay_frame(dt);
    } else if (gamepad) {
        m_gamepad->config().steer_deadzone = dp.drvDeadzone;
        m_gamepad->config().steer_expo     = dp.drvExpo;
        f                                  = m_gamepad->sample(dt);
    } else {
        f = keyboard_frame(m_window->getHandle());
    }

    m_car->apply_input_frame(f, dt);
    m_car->update(dt);
    m_recorder->on_step(*m_car, f, dt);  // no-op unless recording

    m_drv_stepped = true;
    m_drv_frame   = f;
    return true;
}

// Once per frame (after the car step): act on the panel's controls, then publish readouts.
void App::sync_driving_tools_panel(DebugParams& dp) {
    ensure_driving_tools();
    TelemetryRecorder& rec             = *m_recorder;
    m_gamepad->config().steer_deadzone = dp.drvDeadzone;
    m_gamepad->config().steer_expo     = dp.drvExpo;

    // Replay: rising edge starts, falling edge stops, and the end of the clip un-ticks the box.
    if (dp.drvReplay && !m_prev_replay) {
        rec.start_replay(1.f / 60.f);  // dt is informational: recorded frames play one per frame
        if (!rec.replay_active()) {
            dp.drvReplay = false;
            set_text(dp.drvStatus, "nothing to replay: record a clip first");
        } else {
            set_text(dp.drvStatus, "replaying (start pose not restored)");
        }
    } else if (!dp.drvReplay && m_prev_replay) {
        rec.stop_replay();
    } else if (dp.drvReplay && !rec.replay_active()) {
        dp.drvReplay = false;
        set_text(dp.drvStatus, "replay finished");
    }
    m_prev_replay = dp.drvReplay;

    // Record: rising edge starts a fresh clip; recording and replay are mutually exclusive.
    if (dp.drvReplay)
        dp.drvRecord = false;
    if (dp.drvRecord && !m_prev_record)
        rec.clear();
    rec.set_recording(dp.drvRecord);
    m_prev_record = dp.drvRecord;

    if (dp.drvExportRequested) {
        dp.drvExportRequested            = false;
        const std::filesystem::path path = make_timestamped_csv_path();
        char                        msg[sizeof(dp.drvStatus)];
        if (rec.save(path))
            std::snprintf(msg, sizeof(msg), "saved %s (%zu rows)", path.filename().string().c_str(), rec.ring().size());
        else
            std::snprintf(msg, sizeof(msg), "EXPORT FAILED: %s", path.string().c_str());
        set_text(dp.drvStatus, msg);
    }

    // Readouts.
    dp.drvGamepadConnected = m_gamepad->connected();
    set_text(dp.drvGamepadName, dp.drvGamepadConnected ? glfwGetGamepadName(0) : "");
    const float steerNorm = m_car->get_steering_angle() / CarEntity::steer_max();
    if (m_drv_stepped) {
        dp.drvAxisSteer    = m_drv_frame.steer_mode == SteerMode::Target ? m_drv_frame.steer : steerNorm;
        dp.drvAxisThrottle = m_drv_frame.throttle;
        dp.drvAxisBrake    = m_drv_frame.brake;
    } else if (dp.drvInputSource == 1) {
        const InputFrame f = m_gamepad->sample(0.f);  // edit mode: still show the stick for tuning
        dp.drvAxisSteer    = f.steer;
        dp.drvAxisThrottle = f.throttle;
        dp.drvAxisBrake    = f.brake;
    } else {
        dp.drvAxisSteer    = steerNorm;
        dp.drvAxisThrottle = m_car->get_throttle();
        dp.drvAxisBrake    = m_car->get_brake();
    }
    m_drv_stepped = false;

    dp.drvRingSize     = static_cast<int>(rec.ring().size());
    dp.drvRingCapacity = static_cast<int>(rec.ring().capacity());
    dp.drvReplaySize   = rec.replay_size();
    dp.drvReplayPos    = rec.replay_position();

    // FFB target (computed only, never sent anywhere): front-axle self-aligning torque.
    CarParams cp;
    cp.tireMu                = m_car->get_effective_mu();
    const float     steerDeg = std::clamp(m_car->get_steering_angle(), -CarEntity::steer_max(), CarEntity::steer_max());
    const FfbOutput ffb      = self_aligning_torque(m_car->get_speed_mps(), m_car->get_lateral_velocity(),
                                                    m_car->get_yaw_rate(), glm::radians(steerDeg), cp, TireParams{});
    dp.drvFfbTorqueNm        = ffb.torque_nm;
    dp.drvFfbNormalized      = ffb.normalized;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
