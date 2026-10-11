#pragma once

// Purpose:    IInputSource backed by glfwGetGamepadState (first connected gamepad).
//             Deliberately NOT in the test target; the mapping it uses is pure and tested.
// See also:   utils/input/GamepadMapping.h, utils/input/InputFrame.h.

#include "utils/input/GamepadMapping.h"
#include "utils/input/InputFrame.h"

namespace swish {

class GlfwGamepadSource final : public IInputSource {
public:
    explicit GlfwGamepadSource(GamepadConfig cfg = GamepadConfig{}, int joystick_id = 0)
        : m_cfg(cfg), m_id(joystick_id) {}

    // True when the joystick is present and has a gamepad mapping.
    bool connected() const;

    // Returns an all-zero frame (Target mode) when no gamepad is connected.
    InputFrame sample(float dt) override;

    GamepadConfig& config() { return m_cfg; }

private:
    GamepadConfig m_cfg;
    int           m_id;
};

}  // namespace swish
