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

// ══════════════════════════════════════════════════════════════════════
// GLFW callbacks
// ══════════════════════════════════════════════════════════════════════

// Mouse callback — computes the delta and routes it to the camera.
void App::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_cursor_captured)
        return;

    float xf = static_cast<float>(xpos);
    float yf = static_cast<float>(ypos);

    if (app->m_first_mouse) {
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
    if (app->m_camera_ctl.mouse_look(x_offset, y_offset))
        return;

    Camera* camera = app->m_renderer->get_camera();
    if (camera) {
        camera->process_mouse(x_offset, y_offset);
    }
}

void App::framebuffer_resize_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app && app->m_window) {
        app->m_window->mark_resized();
    }
}

// ══════════════════════════════════════════════════════════════════════
// Key table — one row per edge-triggered binding, polled in this order.
// ══════════════════════════════════════════════════════════════════════

void App::init_key_bindings() {
    m_key_bindings = {
        {GLFW_KEY_ESCAPE, {}, &App::on_toggle_cursor_capture},  // toggle cursor lock
        {GLFW_KEY_C, {}, &App::on_cycle_camera_mode},           // cycle camera presets
        {GLFW_KEY_R, {}, &App::on_cycle_rain},                  // off → light → heavy → off
        {GLFW_KEY_V, {}, &App::on_cycle_wiper},                 // wipers: off -> intermittent -> low -> high
        {GLFW_KEY_G, {}, &App::on_cycle_weather},               // HDRI weather presets
#ifdef SWISH_DEBUG_UI
        {GLFW_KEY_GRAVE_ACCENT, {}, &App::on_toggle_debug_edit},  // backtick: edit vs drive
        {GLFW_KEY_F12, {}, &App::on_capture_frame},               // frame capture (Shift: + HDR .pfm)
#endif
    };
}

void App::process_key_bindings() {
    GLFWwindow* window = m_window->getHandle();
    for (KeyBinding& b : m_key_bindings) {
        if (b.edge.pressed(window, b.key))
            (this->*b.action)();
    }
}

void App::on_toggle_cursor_capture() {
    // Edit mode owns the cursor (always free for the panel); Esc there would
    // re-capture it under the panel, so it is ignored until you leave edit mode.
    if (debug_edit_mode())
        return;
    set_cursor_captured(!cursor_captured());
}

void App::on_cycle_camera_mode() {
    set_camera_mode(next_camera_mode(camera_mode()));
}

void App::on_cycle_rain() {
    set_rain_level(rain_level() + 1);
}

void App::on_cycle_wiper() {
    set_wiper_mode(wiper_next_mode(wiper_mode()));
}

void App::on_cycle_weather() {
    set_weather_preset(next_weather_preset(weather_preset()));
}

#ifdef SWISH_DEBUG_UI
// Backtick (`) toggles debug edit-mode: free the cursor for the panel and
// freeze the sim; press again to return to driving.
void App::on_toggle_debug_edit() {
    set_debug_edit_mode(!m_debug_edit_mode);
}

void App::on_capture_frame() {
    GLFWwindow* w = m_window->getHandle();
    const bool  hdr =
        glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    m_renderer->request_capture(hdr);
}

void App::set_debug_edit_mode(bool edit) {
    m_debug_edit_mode = edit;
    // Edit frees the cursor, driving re-captures it. Going through
    // set_cursor_captured keeps m_cursor_captured, the GLFW cursor mode and
    // m_first_mouse in step: setting the GLFW mode directly used to leave
    // m_cursor_captured stale (Esc, `, ` gave a disabled cursor whose mouse
    // deltas were dropped; in edit mode panel drags turned the camera).
    set_cursor_captured(!edit);
    m_renderer->set_debug_edit_mode(edit);
}

void App::sync_debug_edit_mode_from_panel() {
    const bool panel = m_renderer->debug_params().editMode;
    if (panel != m_debug_edit_mode)
        set_debug_edit_mode(panel);
}
#endif

}  // namespace swish
