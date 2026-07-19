#pragma once

#include "../../scene/Treadmill/Treadmill.h"  // endless-road state (owned by App)

#include <memory>

struct GLFWwindow;

namespace swish {

class Window;
class Renderer;
class TextureManager;
class SceneManager;
class ModelManager;
class Camera;
class CarEntity;

class App {
public:
    App();
    ~App();

    int run();

private:
    // Small edge-detect helper: collapses the repeated "down && !prev" pattern
    // used by every key toggle in run() into a single call —
    // `if (m_r_key.pressed(window, GLFW_KEY_R)) { ... }`.
    struct KeyEdge {
        bool prev = false;
        bool pressed(GLFWwindow* window, int key);
    };

    // Owned subsystems. unique_ptr gives deterministic, exception-safe teardown:
    // construction order defines destruction order (reverse), so a throw during
    // run() can't leak or double-free these (the old raw new/delete pairs, split
    // across run() and ~App, were a use-after-free waiting to happen).
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<Renderer>       m_renderer;
    std::unique_ptr<TextureManager> m_textureManager;
    std::unique_ptr<SceneManager>   m_sceneManager;
    std::unique_ptr<ModelManager>   m_modelManager;
    std::unique_ptr<CarEntity>      m_car;

    // Mouse state for delta calculation
    bool  m_first_mouse     = true;
    float m_last_mouse_x    = 0.0f;
    float m_last_mouse_y    = 0.0f;
    bool  m_cursor_captured = true;
    KeyEdge m_esc_key;

    // Endless-road illusion (origin rebase, chunk window, interchanges, ribbon
    // follower). All the treadmill state that used to live here directly now lives
    // in Treadmill; App just owns it and calls update() once per frame.
    Treadmill m_treadmill;

    // Camera mode: cockpit (eye follows the car) vs free-fly. C toggles.
    bool  m_cockpit    = true;
    KeyEdge m_c_key;
    float m_look_yaw   = 0.0f;  // mouse-look offsets relative to car heading
    float m_look_pitch = 0.0f;

    // Rain (R key cycles off → light → heavy → off)
    int  m_rain_level = 0;
    KeyEdge m_r_key;

    // Windshield wiper (V key toggles continuous sweep on/off)
    bool m_wiper_enabled = false;
    KeyEdge m_v_key;

    // Clear-day weather preset (G key toggles a bright sunny day; forces rain off)
    bool m_clear_day  = false;
    KeyEdge m_g_key;

#ifdef SWISH_DEBUG_UI
    // Debug edit-mode (backtick `): frees the cursor for the ImGui panel and
    // freezes the sim so you can tune a static frame.
    bool m_debug_edit_mode = false;
    KeyEdge m_backtick_key;
#endif

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    // Owns the GLFW user pointer once App::run sets it, so the framebuffer
    // resize callback must also be App's — otherwise GLFW would still call
    // Window::framebufferResizeCallback with an App* miscast as Window*,
    // corrupting App fields. See git history for the segfault this fixed.
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

    // ── Accessors (REFACTOR.md §3b) ─────────────────────────────────────
    // Each setter is the single place that keeps the field in sync with the
    // renderer/car side effect it implies, so callers (run()'s key-edge blocks,
    // the launch preset) can't set one without the other. Getters are trivial
    // reads; kept private since nothing outside App needs this state today.
    int  rain_level() const { return m_rain_level; }
    void set_rain_level(int level);

    bool clear_day() const { return m_clear_day; }
    void set_clear_day(bool clear_day);

    bool wiper_enabled() const { return m_wiper_enabled; }
    void set_wiper(bool enabled);

    bool cockpit() const { return m_cockpit; }
    void set_cockpit(bool cockpit);

    bool cursor_captured() const { return m_cursor_captured; }
    void set_cursor_captured(bool captured);
};

}  // namespace swish
