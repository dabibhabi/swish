#pragma once

#include "core/CameraController/CameraController.h"
#include "scene/CameraMode.h"
#include "scene/Corridor/CorridorDriver.h"  // real LIE corridor (owned by App)
#include "scene/Treadmill/Treadmill.h"      // endless-road state (owned by App)
#include "scene/WeatherPreset.h"

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
    // Small edge-detect helper: `pressed()` is true once per key-down transition,
    // so a held key fires exactly once. Each key binding below owns one.
    struct KeyEdge {
        bool prev = false;
        bool pressed(GLFWwindow* window, int key);
    };

    // One row of the data-driven key table (`m_key_bindings`, built by
    // init_key_bindings()). Rows are polled in table order every frame.
    struct KeyBinding {
        int     key;
        KeyEdge edge;
        void (App::*action)();
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

    // Endless-road illusion (origin rebase, chunk window, interchanges, ribbon
    // follower). All the treadmill state that used to live here directly now lives
    // in Treadmill; App just owns it and calls update() once per frame.
    //
    // RETIRED as the active road by Phase 3a — the corridor below replaced it
    // (docs/phase-3-osm-lie-corridor.md §5). Kept only because the authored
    // intro's exit-gantry helper still reports off intro_len(); nothing uploads
    // chunk or interchange geometry any more, so it drives no draws.
    Treadmill m_treadmill;

    // ── Real-world LIE corridor (Phase 3a) ────────────────────────────
    // The baked OSM-derived alignment plus the finite-corridor mechanics that
    // replaced the treadmill: origin rebase, visible-segment window, corridor-
    // frame drivable bounds, and the terminus stops / U-turn pads.
    CorridorAlignment m_corridor_alignment;
    CorridorDriver    m_corridor;

    // Camera presets (C cycles): cockpit, chase, hood, bumper, roof, side,
    // helicopter, free-fly. Placement, FOV kick, shake and mouse-look state all live in
    // CameraController; see CameraMode.h.
    CameraController m_camera_ctl;

    // Rain (R key cycles off → light → heavy → off)
    int m_rain_level = 0;

    // Windshield wiper (V key toggles continuous sweep on/off)
    bool m_wiper_enabled = false;

    // Weather HDRI preset (G key cycles partly cloudy → cloudy → very cloudy → golden hour)
    WeatherPreset m_weather_preset = WeatherPreset::PartlyCloudy;

    // Legacy clear-day flag (still cleared when rain turns on)
    bool m_clear_day = false;

#ifdef SWISH_DEBUG_UI
    // Debug edit-mode (backtick `): frees the cursor for the ImGui panel and
    // freezes the sim so you can tune a static frame.
    bool m_debug_edit_mode = false;
#endif

    // Key table (Esc, C, R, V, G, and backtick under SWISH_DEBUG_UI), polled in order.
    std::vector<KeyBinding> m_key_bindings;

    // ── Startup (run() step 1: init) ────────────────────────────────────
    void init();
    void init_window_and_renderer();            // Window, Renderer, GLFW callbacks, ImGui (debug)
    void init_textures();                       // material textures + 1x1 defaults + rumble strip
    void build_road_scene(Renderer& renderer);  // Scene callback: corridor alignment/segments + POV camera
    void init_scenes();                         // SceneManager + activate scene 0
    void load_car();                            // SWISH_CAR override, corridor start pose, dynamic geometry
    void init_key_bindings();

    // ── Per-frame stages, in call order (run() step 2: loop) ────────────
    float frame_delta(float& last_frame_time) const;  // wall-clock dt, capped at 1/15 s
    bool  debug_edit_mode() const;                    // always false in release
    void  poll_events();                              // pollEvents + consume the resize flag
    void  process_key_bindings();                     // walks m_key_bindings
    void  update_car(float dt, bool debug_edit);      // drive, Y-snap, corridor, wheel visuals, renderer push
    void  update_camera(float dt, bool debug_edit);   // CameraController::update
    void  shutdown();                                 // reverse-order GPU cleanup

    // update_car() sub-steps
    void step_car_simulation(float dt, bool debug_edit);
    void push_car_to_renderer();
#ifdef SWISH_DEBUG_UI
    void sync_car_with_debug_panel(bool debug_edit);
#endif

    // Key-table actions (see init_key_bindings)
    void on_toggle_cursor_capture();
    void on_cycle_camera_mode();
    void on_cycle_rain();
    void on_toggle_wiper();
    void on_cycle_weather();
#ifdef SWISH_DEBUG_UI
    void on_toggle_debug_edit();
#endif

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    // Owns the GLFW user pointer once App::run sets it, so the framebuffer
    // resize callback must also be App's — otherwise GLFW would still call
    // Window::framebufferResizeCallback with an App* miscast as Window*,
    // corrupting App fields. See git history for the segfault this fixed.
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

    // ── Accessors (archive/src/core/App/REFACTOR.md §3b) ────────
    int  rain_level() const { return m_rain_level; }
    void set_rain_level(int level);

    WeatherPreset weather_preset() const { return m_weather_preset; }
    void          set_weather_preset(WeatherPreset preset);

    bool wiper_enabled() const { return m_wiper_enabled; }
    void set_wiper(bool enabled);

    CameraMode camera_mode() const { return m_camera_ctl.mode(); }
    void       set_camera_mode(CameraMode mode);

    bool cursor_captured() const { return m_cursor_captured; }
    void set_cursor_captured(bool captured);
};

}  // namespace swish
