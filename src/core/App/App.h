#pragma once

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
    Window*         m_window         = nullptr;
    Renderer*       m_renderer       = nullptr;
    TextureManager* m_textureManager = nullptr;
    SceneManager*   m_sceneManager   = nullptr;
    ModelManager*   m_modelManager   = nullptr;
    CarEntity*      m_car            = nullptr;

    // Mouse state for delta calculation
    bool  m_first_mouse     = true;
    float m_last_mouse_x    = 0.0f;
    float m_last_mouse_y    = 0.0f;
    bool  m_cursor_captured = true;

    // Camera mode: cockpit (eye follows the car) vs free-fly. C toggles.
    bool  m_cockpit      = true;
    bool  m_c_key_prev   = false;
    float m_look_yaw     = 0.0f;  // mouse-look offsets relative to car heading
    float m_look_pitch   = 0.0f;

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    // Owns the GLFW user pointer once App::run sets it, so the framebuffer
    // resize callback must also be App's — otherwise GLFW would still call
    // Window::framebufferResizeCallback with an App* miscast as Window*,
    // corrupting App fields. See git history for the segfault this fixed.
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
};

}  // namespace swish
