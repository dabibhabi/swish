#include "App.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../../renderer/TextureManager/TextureManager.h"
#include "../../scene/Camera/Camera.h"
#include "../../scene/Entity/CarEntity.h"
#include "../../scene/ModelManager/ModelManager.h"
#include "../../scene/RoadGeometry/RoadGeometry.h"
#include "../../scene/RoadScene/RoadScene.h"
#include "../../scene/SceneManager/SceneManager.h"
#include "../Window/Window.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace swish {

App::App() = default;

// Defined here (not defaulted in the header) so the unique_ptr members see the
// complete subsystem types. Members destruct in reverse declaration order.
App::~App() = default;

// ══════════════════════════════════════════════════════════════════════
// Accessors (REFACTOR.md §3b) — each setter is the single place that keeps
// the field in sync with the renderer/car side effect it implies.
// ══════════════════════════════════════════════════════════════════════

void App::set_rain_level(int level) {
    static constexpr float kRainLevels[] = {0.0f, 0.35f, 1.0f};
    m_rain_level                         = ((level % 3) + 3) % 3;
    const float rain                     = kRainLevels[m_rain_level];
    m_renderer->set_rain_intensity(rain);
    // Drive the interior cabin wash from the same rain level so the cockpit
    // reads light gray as rain rises (off → light → heavy).
    if (m_car) m_car->set_rain_intensity(rain);
    // Rain and the clear-day preset are mutually exclusive — an azure sunny
    // sky with rain falling reads wrong, so turning rain on cancels clear day.
    if (rain > 0.0f && m_clear_day) {
        m_clear_day = false;
        m_renderer->set_clear_day(false);
    }
}

void App::set_clear_day(bool clear_day) {
    m_clear_day = clear_day;
    m_renderer->set_clear_day(clear_day);
    // Clear day is dry — reset the rain cycle (both world rain and cabin wash).
    if (clear_day) set_rain_level(0);
}

void App::set_wiper(bool enabled) {
    m_wiper_enabled = enabled;
    m_renderer->set_wiper_enabled(enabled);
}

void App::set_cockpit(bool cockpit) {
    m_cockpit    = cockpit;
    m_look_yaw   = 0.f;
    m_look_pitch = 0.f;
}

void App::set_cursor_captured(bool captured) {
    m_cursor_captured = captured;
    glfwSetInputMode(m_window->getHandle(), GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    m_first_mouse = true;
}

bool App::KeyEdge::pressed(GLFWwindow* window, int key) {
    bool down = glfwGetKey(window, key) == GLFW_PRESS;
    bool edge = down && !prev;
    prev      = down;
    return edge;
}

// ══════════════════════════════════════════════════════════════════════
// Mouse callback — computes delta and sends to camera
// ══════════════════════════════════════════════════════════════════════

void App::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_cursor_captured) return;

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

    if (app->m_cockpit) {
        // Mouse look relative to the car heading, clamped so you can't
        // look through the headrest or flip over the roof.
        constexpr float kSens = 0.1f;
        app->m_look_yaw       = std::clamp(app->m_look_yaw + x_offset * kSens, -150.f, 150.f);
        app->m_look_pitch     = std::clamp(app->m_look_pitch + y_offset * kSens, -60.f, 60.f);
        return;
    }

    Camera* camera = app->m_renderer->get_camera();
    if (camera) { camera->process_mouse(x_offset, y_offset); }
}

void App::framebuffer_resize_callback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app && app->m_window) { app->m_window->mark_resized(); }
}

// ══════════════════════════════════════════════════════════════════════
// Main application loop
// ══════════════════════════════════════════════════════════════════════

int App::run() {
    // Road geometry math + treadmill constants now live in RoadGeometry (pure,
    // static); endless-road state + per-frame update live in m_treadmill.

    // ── 1. Window + Renderer core ─────────────────────────────────
    m_window = std::make_unique<Window>();
    m_window->init(800, 600, "swish");

    m_renderer = std::make_unique<Renderer>();
    m_renderer->init(*m_window);

    // ── 2. Wire input callbacks ───────────────────────────────────
    GLFWwindow* glfw_window = m_window->getHandle();
    glfwSetWindowUserPointer(glfw_window, this);  // override Window's user pointer for App
    glfwSetCursorPosCallback(glfw_window, mouse_callback);
    // Replace Window's resize callback too. Window::init registered a static
    // that reinterprets the user pointer as Window*; now that App owns the
    // pointer, that callback would write through a wrong-typed pointer.
    glfwSetFramebufferSizeCallback(glfw_window, framebuffer_resize_callback);
    glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

#ifdef SWISH_DEBUG_UI
    // ImGui must init AFTER our GLFW callbacks above — its backend chains onto them.
    m_renderer->debug_init();
#endif

    // ── 3. TextureManager — load all material textures ────────────
    m_textureManager = std::make_unique<TextureManager>(m_renderer->services());

    m_textureManager->load_directory(TEXTURE_DIR);

    // Register default textures (1x1 pixels for flat-colored surfaces)
    m_textureManager->register_from_pixels("default", {255, 255, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_normal", {128, 128, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_roughness", {255, 255, 255, 255}, 1, 1);

    // Generate procedural rumble strip texture (alternating dark/light bars)
    {
        constexpr uint32_t   rumble_w = 64, rumble_h = 64;
        std::vector<uint8_t> rumble_pixels(rumble_w * rumble_h * 4);
        for (uint32_t y = 0; y < rumble_h; y++) {
            // Alternating 4px dark / 4px light bars along Y
            bool    dark = ((y / 4) % 2 == 0);
            uint8_t val  = dark ? 80 : 140;
            for (uint32_t x = 0; x < rumble_w; x++) {
                size_t idx             = (y * rumble_w + x) * 4;
                rumble_pixels[idx + 0] = val;
                rumble_pixels[idx + 1] = val;
                rumble_pixels[idx + 2] = val;
                rumble_pixels[idx + 3] = 255;
            }
        }
        m_textureManager->register_from_pixels("rumble", rumble_pixels, rumble_w, rumble_h);
        m_textureManager->register_from_pixels("rumble_normal", {128, 128, 255, 255}, 1, 1);
        m_textureManager->register_from_pixels("rumble_roughness", {200, 200, 200, 255}, 1, 1);
    }

    m_renderer->register_texture_manager(m_textureManager.get());

    // ── 4. ModelManager (placeholder) ─────────────────────────────
    m_modelManager = std::make_unique<ModelManager>(*m_renderer);
    m_renderer->register_model_manager(m_modelManager.get());

    // ── 5. Build material descriptor sets ─────────────────────────
    m_renderer->rebuild_material_descriptors();

    // ── 6. Define scenes ──────────────────────────────────────────
    auto road_scene = std::make_unique<Scene>([this](Renderer& renderer) {
        // Generate road geometry
        RoadScene road;
        auto      scene = road.generate();
        renderer.upload_scene_geometry(scene.meshData, scene.drawCalls);
        renderer.set_scene_lights(scene.lights);

        // Endless-road: the authored road above is the "intro"; past its far end
        // we tile ONE canonical chunk (uploaded once, instanced per frame by the
        // treadmill in run()). The treadmill keeps the lamp lights so it can shift
        // them into the render frame on each origin rebase.
        const float introLen = road.get_road_length();
        m_treadmill.set_intro(introLen, scene.lights);
        auto chunk = road.generate_chunk(RoadGeometry::kChunkLen);
        renderer.upload_chunk_geometry(chunk.meshData, chunk.drawCalls);

        // Elevated diamond interchange: one canonical mesh, instanced sparsely (~1.5 mi
        // apart) in the endless region by the treadmill.
        auto interchange = road.generate_interchange_scene();
        renderer.upload_interchange_geometry(interchange.meshData, interchange.drawCalls);
        m_treadmill.set_ribbons(road.interchange_ribbons());  // shared centrelines for the follower

        // Setup POV camera on the eastbound LIE
        auto* camera = new Camera();
        camera->set_position(Vec3(6501.0f, 1448.0f, -5000.0f));
        camera->set_yaw(-90.0f);   // looking along -Z (down the road)
        camera->set_pitch(-3.0f);  // slight downward driving angle

        // Collision bounds: keep camera within EB roadway
        camera->set_collision_bounds(RoadGeometry::kBarrierRight + 100.0f, RoadGeometry::kEbRoadRight - 100.0f,
                                     500.0f, 5000.0f);
        camera->set_collision_enabled(true);

        VkExtent2D extent = renderer.services().swapchainExtent;
        float      aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        // endless road: window ≈ kChunksAhead chunks
        camera->set_perspective(65.0f, aspect, 10.0f, RoadGeometry::kCameraFar);

        renderer.set_camera(camera);
    });

    std::vector<std::unique_ptr<Scene>> scenes;
    scenes.push_back(std::move(road_scene));

    m_sceneManager = std::make_unique<SceneManager>(*m_renderer, std::move(scenes));
    m_renderer->register_scene_manager(m_sceneManager.get());

    // ── 7. Activate first scene ───────────────────────────────────
    m_sceneManager->set_active_scene(0);

    // ── 8. Load the Porsche car ───────────────────────────────────
    {
        auto car_entity = m_modelManager->load_car(std::string(ASSET_DIR) + "Porsche/porsche.glb");

        // Place the car on the road ahead of the camera start position.
        // Camera starts at (6501, 1448, -5000) facing -Z.
        // Put the car 15m (~15000 WU) ahead; Y from crown formula so wheels sit on road.
        // Scale converts glTF meters to world units (1m = 1000 WU).
        constexpr float kCarX = 6501.0f;
        car_entity->set_position(Vec3(kCarX, RoadGeometry::road_surface_y(kCarX), -20000.0f));
        car_entity->set_rotation(Vec3(0.f, 90.f, 0.f));  // +90° = nose down -Z, same way the camera looks
        car_entity->set_scale(Vec3(1000.f, 1000.f, 1000.f));
        car_entity->set_road_bounds(RoadGeometry::kBarrierRight + 100.f, RoadGeometry::kEbRoadRight - 100.f);

        m_car = std::move(car_entity);

        // Rebuild material descriptors to include the car's textures.
        m_renderer->rebuild_material_descriptors();

        // Upload the car mesh as dynamic geometry.
        // Glass submeshes share the same VBO/IBO — no second upload needed.
        m_renderer->upload_dynamic_geometry(m_car->get_mesh_data(), m_car->get_draw_calls());
        m_renderer->update_glass_draw_calls(m_car->get_glass_draw_calls());
        m_renderer->update_windshield_draw_calls(m_car->get_windshield_draw_calls());
    }

    // ── 8b. Initial look: reproduce the debug-tuned "lie" preset ──────
    // The release build has no runtime preset loader (the whole TOML system is
    // #ifdef SWISH_DEBUG_UI), so the lie.toml look is baked into the compiled-in
    // defaults (DebugParams.h / lighting.frag / Renderer grade literals). The two
    // remaining pieces are runtime state, applied here on launch:
    //   • overcast weather w/ 0.242 clarity + 0.823 ambient (set_clear_day's else branch)
    //   • a wet road at full rain (the preset is a rain scene: rain_intensity = 1.0)
    set_clear_day(false);  // applies the tuned overcast weather + bakes IBL
    set_rain_level(2);      // R-key cycle resumes correctly (off → light → heavy)

    // ── 9. Main loop with delta time ──────────────────────────────
    float last_frame_time = static_cast<float>(glfwGetTime());

    while (!m_window->shouldClose()) {
        float current_time = static_cast<float>(glfwGetTime());
        float delta_time   = current_time - last_frame_time;
        last_frame_time    = current_time;
        delta_time         = std::min(delta_time, 1.0f / 15.0f);  // cap at ~67ms to prevent physics explosion

        // In debug edit-mode the scene freezes so you can tune a static frame.
        bool debug_edit = false;
#ifdef SWISH_DEBUG_UI
        debug_edit = m_debug_edit_mode;
#endif

        m_window->pollEvents();

        // Note: drawFrame's handlePresent also checks wasResized, but checking here
        // ensures the flag is consumed even on frames where drawing is skipped.
        if (m_window->wasResized()) { m_window->resetResizedFlag(); }

        // Toggle cursor capture with Escape (edge-detected — no toggle spam)
        if (m_esc_key.pressed(glfw_window, GLFW_KEY_ESCAPE)) { set_cursor_captured(!cursor_captured()); }

        // Toggle cockpit / free-fly camera with C (edge-detected)
        if (m_c_key.pressed(glfw_window, GLFW_KEY_C)) { set_cockpit(!cockpit()); }

        // R key cycles rain intensity: off → light → heavy → off
        if (m_r_key.pressed(glfw_window, GLFW_KEY_R)) { set_rain_level(rain_level() + 1); }

        // V key toggles the windshield wiper (edge-detected continuous sweep)
        if (m_v_key.pressed(glfw_window, GLFW_KEY_V)) { set_wiper(!wiper_enabled()); }

        // G key toggles the clear-day preset (bright sunny sky). A clear day is
        // dry, so it also resets the rain cycle to off.
        if (m_g_key.pressed(glfw_window, GLFW_KEY_G)) { set_clear_day(!clear_day()); }

#ifdef SWISH_DEBUG_UI
        // Backtick (`) toggles debug edit-mode: free the cursor for the panel and
        // freeze the sim; press again to return to driving.
        if (m_backtick_key.pressed(glfw_window, GLFW_KEY_GRAVE_ACCENT)) {
            m_debug_edit_mode = !m_debug_edit_mode;
            glfwSetInputMode(glfw_window, GLFW_CURSOR, m_debug_edit_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            m_first_mouse = true;
            m_renderer->set_debug_edit_mode(m_debug_edit_mode);
        }
#endif

        // Process camera input (WASD) — free-fly mode only
        Camera* camera = m_renderer->get_camera();
        if (camera && m_cursor_captured && !m_cockpit && !debug_edit) {
            camera->process_keyboard(glfw_window, delta_time);
        }

        // Drive the car (arrow keys)
        if (m_car) {
            if (!debug_edit) {
                m_car->handle_input(glfw_window, delta_time);
                m_car->update(delta_time);
            }

            // Snap Y to road crown so wheels sit on the surface as the car steers.
            Vec3 pos = m_car->get_position();
            pos.y    = RoadGeometry::road_surface_y(pos.x);
            m_car->set_position(pos);

            // Endless-road illusion: origin rebase, chunk/interchange windows,
            // drivable bounds, and the interchange ribbon-follower. All the state
            // lives in m_treadmill now; it mutates the car and pushes chunk/light
            // data to the renderer exactly as the inline code did.
            m_treadmill.update(*m_car, *m_renderer, glfw_window, delta_time, debug_edit);

#ifdef SWISH_DEBUG_UI
            // Steering-wheel gizmo: feed the current wheel pivot to the debug UI, and
            // in edit mode pose the wheel from the gizmo/override angle (the sim is
            // frozen then). The draw calls below re-generate with the new angle.
            {
                DebugParams& dp    = m_renderer->debug_params();
                dp.steerPivotWorld = m_car->get_steering_wheel_pivot_world();
                dp.steerMaxDeg     = CarEntity::steer_max();
                if (debug_edit && dp.steerOverride)
                    m_car->set_steering_angle(dp.steerAngleDeg);
                else
                    dp.steerAngleDeg = m_car->get_steering_angle();
                // Spin-axis calibration correction (edit mode); identity otherwise.
                m_car->set_steer_axis_correction(
                    (debug_edit && dp.steerAxisEdit)
                        ? glm::quat(dp.steerQuat.w, dp.steerQuat.x, dp.steerQuat.y, dp.steerQuat.z)
                        : glm::quat(1.f, 0.f, 0.f, 0.f));
            }
#endif

            m_renderer->update_dynamic_draw_calls(m_car->get_draw_calls());
            m_renderer->update_glass_draw_calls(m_car->get_glass_draw_calls());
            m_renderer->update_windshield_draw_calls(m_car->get_windshield_draw_calls());

            // Pass car velocity to renderer so rain streaks lean forward at speed.
            // Convention (CarEntity.h): forward = (cos yaw_rad, 0, -sin yaw_rad)
            float yaw_rad = m_car->get_rotation().y * (3.14159265f / 180.0f);
            Vec3  carFwd(std::cos(yaw_rad), 0.f, -std::sin(yaw_rad));
            m_renderer->set_car_velocity(carFwd * m_car->get_speed());
            // Car world position drives the GPU road-spray spawn origin (rear axle).
            m_renderer->set_car_position(m_car->get_position());
        }

        // Cockpit camera: the eye rides the car at the driver's seat.
        // kSeatEye is authored in the car's normalized mesh space (meters,
        // nose +X, ground y = 0, driver side -Z); the entity scale (1000)
        // converts it to world units through the model matrix. The steering
        // wheel center sits at (0.18, 0.76, -0.34) in that space; the eye
        // goes behind and above it (roof tops out at 1.29).
        if (camera && m_cockpit && m_car) {
            const Vec3 kSeatEye(-0.32f, 1.05f, -0.34f);
            Vec3       eye = Vec3(m_car->get_model_matrix() * Vec4(kSeatEye, 1.f));
            camera->set_position(eye);
            // Camera forward = (cos yaw, ·, sin yaw); car forward =
            // (cos heading, 0, -sin heading) -> camera yaw = -heading.
            camera->set_yaw(-m_car->get_rotation().y + m_look_yaw);
            camera->set_pitch(m_look_pitch);
        }

        m_renderer->drawFrame(delta_time);
    }

    // ── 10. Cleanup (reverse order) ───────────────────────────────
    // Release GPU resources while the device is still alive; the owned objects
    // themselves are destroyed automatically (reverse declaration order) when
    // App is destroyed after run() returns — no manual delete needed.
    m_renderer->wait_for_idle();
    m_car.reset();
    m_renderer->destroy_dynamic_geometry();
    m_textureManager->cleanup();
    m_modelManager->cleanup();
    m_renderer->cleanup();
    m_window->cleanup();

    return 0;
}

}  // namespace swish
