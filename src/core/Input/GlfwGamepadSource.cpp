#include "GlfwGamepadSource.h"

#include <GLFW/glfw3.h>

namespace swish {

bool GlfwGamepadSource::connected() const {
    return glfwJoystickPresent(m_id) == GLFW_TRUE && glfwJoystickIsGamepad(m_id) == GLFW_TRUE;
}

InputFrame GlfwGamepadSource::sample(float /*dt*/) {
    GLFWgamepadstate st;
    if (!connected() || glfwGetGamepadState(m_id, &st) != GLFW_TRUE) return InputFrame{};
    GamepadAxes ax;
    ax.left_x        = st.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    ax.left_y        = st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    ax.left_trigger  = st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
    ax.right_trigger = st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
    return map_gamepad_axes(ax, m_cfg);
}

}  // namespace swish
