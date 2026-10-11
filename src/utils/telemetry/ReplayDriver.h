#pragma once

// Purpose:    Plays back a recorded sequence of InputFrames at a fixed dt, as an IInputSource.
//             Feeding each frame to CarEntity::apply_input_frame(f, dt) + update(dt) reproduces a
//             recorded run bit-for-bit on a fresh car (same inputs, same dt, same build).
// Units:      dt in seconds; frames per InputFrame.h.
// Complexity: O(1) per step.
// Example:    ReplayDriver r(frames, 1.f / 60.f);
//             while (!r.done()) { car.apply_input_frame(r.next(r.dt()), r.dt()); car.update(r.dt()); }
// See also:   utils/telemetry/TelemetryRing.h, debug/TelemetryRecorder.h.
// Test:       tests/test_telemetry.cpp
//
// Determinism caveat: the car also reads set_wetness()/road bounds/etc.; a faithful replay must
// restore those too. TODO(stub): record + restore wetness, road bounds and the start pose.

#include "utils/input/InputFrame.h"
#include "utils/telemetry/TelemetryRing.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace swish {

class ReplayDriver : public IInputSource {
public:
    ReplayDriver() = default;
    ReplayDriver(std::vector<InputFrame> frames, float dt) : m_frames(std::move(frames)), m_dt(dt) {}

    // Build from the inputs stored in a ring (oldest -> newest).
    static ReplayDriver from_ring(const TelemetryRing& ring, float dt) {
        std::vector<InputFrame> f;
        f.reserve(ring.size());
        for (std::size_t i = 0; i < ring.size(); ++i)
            f.push_back(ring.at(i).input);
        return ReplayDriver(std::move(f), dt);
    }

    // Returns the recorded frame and advances. `dt` is ignored (the recording's own dt() rules);
    // after the end it returns a neutral frame and done() stays true.
    InputFrame next(float /*dt*/) {
        if (m_pos >= m_frames.size())
            return InputFrame{};
        return m_frames[m_pos++];
    }
    InputFrame sample(float dt) override { return next(dt); }

    bool        done() const { return m_pos >= m_frames.size(); }
    void        rewind() { m_pos = 0; }
    float       dt() const { return m_dt; }
    std::size_t size() const { return m_frames.size(); }
    std::size_t position() const { return m_pos; }

private:
    std::vector<InputFrame> m_frames;
    float                   m_dt  = 1.f / 60.f;
    std::size_t             m_pos = 0;
};

}  // namespace swish
