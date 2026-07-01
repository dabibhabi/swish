#pragma once

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
    bool  m_esc_key_prev    = false;

    // Camera mode: cockpit (eye follows the car) vs free-fly. C toggles.
    bool  m_cockpit    = true;
    bool  m_c_key_prev = false;
    float m_look_yaw   = 0.0f;  // mouse-look offsets relative to car heading
    float m_look_pitch = 0.0f;

    // Rain (R key cycles off → light → heavy → off)
    int  m_rain_level = 0;
    bool m_r_key_prev = false;

    // Windshield wiper (V key toggles continuous sweep on/off)
    bool m_wiper_enabled = false;
    bool m_v_key_prev    = false;

    // Clear-day weather preset (G key toggles a bright sunny day; forces rain off)
    bool m_clear_day  = false;
    bool m_g_key_prev = false;

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    // Owns the GLFW user pointer once App::run sets it, so the framebuffer
    // resize callback must also be App's — otherwise GLFW would still call
    // Window::framebufferResizeCallback with an App* miscast as Window*,
    // corrupting App fields. See git history for the segfault this fixed.
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
};

}  // namespace swish
