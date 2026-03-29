#pragma once

struct GLFWwindow;

namespace swish {

class Window;
class Renderer;
class TextureManager;
class SceneManager;
class ModelManager;
class Camera;

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

    // Mouse state for delta calculation
    bool  m_first_mouse = true;
    float m_last_mouse_x = 0.0f;
    float m_last_mouse_y = 0.0f;
    bool  m_cursor_captured = true;

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
};

}  // namespace swish
