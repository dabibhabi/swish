// App input: the data-driven key table, its actions, and the GLFW mouse / resize callbacks.
#include "App.h"
#include "core/Window/Window.h"
#include "renderer/Renderer/Renderer.h"
#include "scene/Camera/Camera.h"
#include "scene/CameraMode.h"

#include <GLFW/glfw3.h>

namespace swish {

bool App::KeyEdge::pressed(GLFWwindow* window, int key) {
    bool down = glfwGetKey(window, key) == GLFW_PRESS;
    bool edge = down && !prev;
    prev      = down;
    return edge;
}

void App::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_cursor_captured) return;

    float xf = static_cast<float>(xpos);
    float yf = static_cast<float>(ypos);

    if (app->m_first_mouse) {  // todo look into rain and wipers
        app->m_last_mouse_x = xf;
        app->m_last_mouse_y = yf;
        app->m_first_mouse  = false;
        return;
    }

    float x_offset      = xf - app->m_last_mouse_x;
    float y_offset      = app->m_last_mouse_y - yf;  // reversed: Y grows downward in screen coords
    app->m_last_mouse_x = xf;
    app->m_last_mouse_y = yf;

    // Car-attached modes consume the delta as mouse-look offsets.
    if (app->m_camera_ctl.mouse_look(x_offset, y_offset)) return;

    Camera* camera = app->m_renderer->get_camera();
    if (camera) camera->process_mouse(x_offset, y_offset);
}

void App::framebuffer_resize_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app && app->m_window) return app->m_window->mark_resized();
}

void App::init_key_bindings() {
    m_key_bindings = {
        {GLFW_KEY_ESCAPE, {}, &App::on_toggle_cursor_capture},
        {GLFW_KEY_C, {}, &App::on_cycle_camera_mode},
        {GLFW_KEY_R, {}, &App::on_cycle_rain},
        {GLFW_KEY_V, {}, &App::on_cycle_wiper},
        {GLFW_KEY_G, {}, &App::on_cycle_weather},
#ifdef SWISH_DEBUG_UI
        {GLFW_KEY_GRAVE_ACCENT, {}, &App::on_toggle_debug_edit},
        {GLFW_KEY_F12, {}, &App::on_capture_frame},
#endif
    };
}

void App::process_key_bindings() {
    GLFWwindow* window = m_window->getHandle();
    for (KeyBinding& b : m_key_bindings) {
        if (b.edge.pressed(window, b.key)) (this->*b.action)();
    }
}

void App::on_toggle_cursor_capture() {
    if (debug_edit_mode()) return;

    set_cursor_captured(!cursor_captured());
}

void App::on_cycle_camera_mode() { set_camera_mode(next_camera_mode(camera_mode())); }

void App::on_cycle_rain() { set_rain_level(rain_level() + 1); }

void App::on_cycle_wiper() { set_wiper_mode(wiper_next_mode(wiper_mode())); }

void App::on_cycle_weather() { set_weather_preset(next_weather_preset(weather_preset())); }

#ifdef SWISH_DEBUG_UI

void App::on_toggle_debug_edit() { set_debug_edit_mode(!m_debug_edit_mode); }

void App::on_capture_frame() {
    GLFWwindow* w = m_window->getHandle();
    const bool  hdr =
        glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    m_renderer->request_capture(hdr);
}

void App::set_debug_edit_mode(bool edit) {
    m_debug_edit_mode = edit;
    set_cursor_captured(!edit);
    m_renderer->set_debug_edit_mode(edit);
}

void App::sync_debug_edit_mode_from_panel() {
    const bool panel = m_renderer->debug_params().editMode;
    if (panel != m_debug_edit_mode) set_debug_edit_mode(panel);
}
#endif

}  // namespace swish
