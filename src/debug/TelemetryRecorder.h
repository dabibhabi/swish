#pragma once

// Purpose:    Hook-ready debug recorder: snapshots CarEntity state + the driver InputFrame each
//             step into a TelemetryRing, exports CSV, and replays recorded inputs.
//             sample_from() is ungated (pure, header-only); the TelemetryRecorder class is
//             debug-only (#ifdef SWISH_DEBUG_UI) so release is untouched.
// Units:      see TelemetrySample (pos WU, speed m/s, angles deg, t s).
// Complexity: O(1) per recorded step.
// See also:   utils/telemetry/TelemetryRing.h, utils/telemetry/ReplayDriver.h.
// Test:       tests/test_telemetry.cpp
//
// Intended hook points (NOT wired yet; a later debug-ui agent does this, do not edit here):
//   App.cpp, per fixed/frame step, under #ifdef SWISH_DEBUG_UI:
//     InputFrame f = recorder.replay_active() ? recorder.next_replay_frame(dt) : <live frame>;
//     car.apply_input_frame(f, dt);  car.update(dt);
//     recorder.on_step(car, f, dt);            // after update(); no-op unless recording
//   DebugUI telemetry panel: checkboxes -> set_recording()/start_replay()/stop_replay(),
//     "Save CSV" -> save(), "Load" -> load(path), plot ring via ring().at(i).
//
// TODO(stub): restore start pose + wetness on replay start, replay-vs-live divergence readout,
//             ring plots, fixed-step accumulator ownership, auto-save on crash/exit.

#include "scene/Entity/CarEntity.h"
#include "utils/input/InputFrame.h"
#include "utils/telemetry/ReplayDriver.h"
#include "utils/telemetry/TelemetryRing.h"

#include <filesystem>
#include <fstream>

namespace swish {

// Snapshot the car's current state (call after update()). Read-only accessors only.
inline TelemetrySample sample_from(const CarEntity& car, float t, const InputFrame& input) {
    TelemetrySample s;
    s.t         = t;
    s.x         = car.get_position().x;
    s.y         = car.get_position().y;
    s.z         = car.get_position().z;
    s.yaw_deg   = car.get_rotation().y;
    s.speed_mps = car.get_speed_mps();
    s.gear      = car.get_gear();
    s.rpm       = car.get_engine_rpm();
    s.steer_deg = car.get_steering_angle();
    s.throttle  = car.get_throttle();
    s.brake     = car.get_brake();
    s.mu        = car.get_effective_mu();
    for (int c = 0; c < 4; ++c) {
        s.wheel_spin[static_cast<std::size_t>(c)] = car.get_wheel_spin(c);
        s.susp[static_cast<std::size_t>(c)]       = car.get_corner_suspension(c).x_m;
    }
    s.input = input;
    return s;
}

#ifdef SWISH_DEBUG_UI
class TelemetryRecorder {
public:
    explicit TelemetryRecorder(std::size_t capacity = 3600) : m_ring(capacity) {}

    bool recording() const { return m_recording; }
    void set_recording(bool on) { m_recording = on; }
    bool replay_active() const { return m_replay_active; }
    int  replay_size() const { return static_cast<int>(m_replay.size()); }
    int  replay_position() const { return static_cast<int>(m_replay.position()); }

    // Replay the inputs currently in the ring (oldest -> newest) at fixed dt. Recording is
    // switched off so the replayed run does not overwrite what is being played.
    void start_replay(float dt) {
        m_replay        = ReplayDriver::from_ring(m_ring, dt);
        m_replay_active = !m_replay.done();
        m_recording     = false;
    }
    void stop_replay() { m_replay_active = false; }
    // Next recorded frame; clears replay_active() when the recording is exhausted.
    InputFrame next_replay_frame(float dt) {
        InputFrame f = m_replay.next(dt);
        if (m_replay.done()) m_replay_active = false;
        return f;
    }

    void on_step(const CarEntity& car, const InputFrame& input, float dt) {
        m_time += dt;
        if (m_recording) m_ring.push(sample_from(car, m_time, input));
    }

    void clear() {
        m_ring.clear();
        m_time = 0.f;
    }
    bool save(const std::filesystem::path& path) const { return write_csv(m_ring, path); }
    bool save_default() const { return save(make_timestamped_csv_path()); }
    bool load(const std::filesystem::path& path) {
        std::ifstream f(path);
        return f && read_csv(f, m_ring);
    }

    const TelemetryRing& ring() const { return m_ring; }

private:
    TelemetryRing m_ring;
    ReplayDriver  m_replay;
    bool          m_recording     = false;
    bool          m_replay_active = false;
    float         m_time          = 0.f;
};
#endif  // SWISH_DEBUG_UI

}  // namespace swish
