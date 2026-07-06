#pragma once

#include "../../scene/SceneTypes.h"  // LightDesc (endless-road lamp rebase)

#include <memory>
#include <vector>

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

    // Endless-road treadmill. m_introLen = authored road length (WU); the road is
    // tiled with one canonical chunk past its far end. m_originShift = accumulated
    // origin-rebase (WU, double): renderZ = trueZ + m_originShift, kept a whole
    // number of chunk lengths so the periodic tile looks identical after a rebase.
    float  m_introLen    = 0.0f;
    double m_originShift = 0.0;
    // Intro lamp point-lights, kept so they can be shifted into the render frame
    // on each origin rebase (else they detach from their posts after driving far).
    std::vector<LightDesc> m_sceneLights;

    // Interchange ribbon-follower (Layer 4 drivable). When the car enters a ramp it
    // "attaches" to a ribbon and its position/heading/pitch are driven by that curve
    // (arc-length s advanced by speed, lateral t by steering) until a junction/exit.
    // m_ixRibbons are interchange-local; m_ixTrueZ is the attached instance's true Z
    // (→ render offset = m_ixTrueZ + m_originShift, survives rebases).
    std::vector<Ribbon> m_ixRibbons;
    int                 m_carRibbon = -1;  // -1 = free driving
    float               m_ribbonS   = 0.0f;
    float               m_ribbonT   = 0.0f;
    double              m_ixTrueZ   = 0.0;

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

#ifdef SWISH_DEBUG_UI
    // Debug edit-mode (backtick `): frees the cursor for the ImGui panel and
    // freezes the sim so you can tune a static frame.
    bool m_debug_edit_mode = false;
    bool m_backtick_prev   = false;
#endif

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    // Owns the GLFW user pointer once App::run sets it, so the framebuffer
    // resize callback must also be App's — otherwise GLFW would still call
    // Window::framebufferResizeCallback with an App* miscast as Window*,
    // corrupting App fields. See git history for the segfault this fixed.
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
};

}  // namespace swish
