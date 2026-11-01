#pragma once

// Purpose:    Device-agnostic driver command for ONE simulation step, plus the interface every
//             input device (keyboard, gamepad, replay, bench script) implements.
// Units:      steer [-1,1] right-positive; throttle/brake [0,1] pedal fractions. No SI/WU here.
// Complexity: O(1).
// Example:    InputFrame f; f.throttle = 1.f; f.steer = -1.f; car.apply_input_frame(f, dt);
// See also:   scene/Entity/CarEntity.h (apply_input_frame), utils/input/GamepadMapping.h.
// Test:       tests/test_input.cpp
//
// Additive stub: tasks/todo.md Ch07 (Commands.def / InputSampler) will absorb this type.

namespace swish {

// How CarEntity interprets InputFrame::steer.
enum class SteerMode {
    Target,  // analog: steer * max-lock is a target angle, approached at the steer rate
    Rate,    // digital (keyboard): only the SIGN of steer matters; +/-1 ramps at the steer
             // rate, 0 returns to centre at the (faster) return rate. Bit-identical to the
             // pre-InputFrame keyboard path.
};

struct InputFrame {
    float     steer      = 0.f;  // [-1, 1], right positive
    float     throttle   = 0.f;  // [0, 1]
    float     brake      = 0.f;  // [0, 1]
    SteerMode steer_mode = SteerMode::Target;
};

class IInputSource {
public:
    virtual ~IInputSource() = default;
    // Sample the device for the step about to be simulated (dt seconds).
    virtual InputFrame sample(float dt) = 0;
};

}  // namespace swish
