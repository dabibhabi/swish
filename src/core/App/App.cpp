#include "App.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../../renderer/TextureManager/TextureManager.h"
#include "../../scene/Camera/Camera.h"
#include "../../scene/Entity/CarEntity.h"
#include "../../scene/ModelManager/ModelManager.h"
#include "../../scene/RoadScene/RoadScene.h"
#include "../../scene/SceneManager/SceneManager.h"
#include "../Window/Window.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace swish {

// ── Endless-road treadmill tuning ────────────────────────────────────
// CHUNK_LEN is 300 m and a multiple of every surface tile size (asphalt 3000,
// grass 4000, concrete 1500, metal 1000 → LCM 12000; 300000 = 25·12000), so the
// canonical chunk's surface UVs tile seamlessly. The active window spans a few
// chunks behind the car and enough ahead to reach the camera far plane; the
// origin is rebased (by a whole number of chunk lengths) whenever the car's
// render-frame Z exceeds the threshold, keeping float32 coordinates precise.
namespace {
constexpr float kChunkLen        = 300000.0f;  // 300 m
constexpr int   kChunksAhead     = 8;          // 8·300 m = 2.4 km ≈ camera far plane
constexpr int   kChunksBehind    = 2;
constexpr float kRebaseThreshold = 1200000.0f;  // rebase when |render Z| exceeds ~1.2 km
constexpr float kCameraFar       = 2400000.0f;  // ≈ kChunksAhead·kChunkLen (chunks fill to here)

// Total arc length of a ribbon centreline.
float ribbonLength(const Ribbon& r) {
    float len = 0.0f;
    for (size_t i = 1; i < r.pts.size(); ++i)
        len += glm::length(r.pts[i] - r.pts[i - 1]);
    return len;
}

// Position + unit tangent at arc length s along a ribbon (clamped to its ends).
void ribbonSample(const Ribbon& r, float s, Vec3& pos, Vec3& tan) {
    if (r.pts.size() < 2) {
        pos = r.pts.empty() ? Vec3(0.0f) : r.pts[0];
        tan = Vec3(0.0f, 0.0f, -1.0f);
        return;
    }
    s = std::max(0.0f, s);
    for (size_t i = 1; i < r.pts.size(); ++i) {
        float seg = glm::length(r.pts[i] - r.pts[i - 1]);
        if (s <= seg || i == r.pts.size() - 1) {
            float u = (seg > 1e-4f) ? std::min(s / seg, 1.0f) : 0.0f;
            pos     = r.pts[i - 1] + (r.pts[i] - r.pts[i - 1]) * u;
            tan     = glm::normalize(r.pts[i] - r.pts[i - 1]);
            return;
        }
        s -= seg;
    }
}
}  // namespace

App::App() = default;

// Defined here (not defaulted in the header) so the unique_ptr members see the
// complete subsystem types. Members destruct in reverse declaration order.
App::~App() = default;

// ══════════════════════════════════════════════════════════════════════
// Mouse callback — computes delta and sends to camera
// ══════════════════════════════════════════════════════════════════════

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

    if (app->m_cockpit) {
        // Mouse look relative to the car heading, clamped so you can't
        // look through the headrest or flip over the roof.
        constexpr float kSens = 0.1f;
        app->m_look_yaw       = std::clamp(app->m_look_yaw + x_offset * kSens, -150.f, 150.f);
        app->m_look_pitch     = std::clamp(app->m_look_pitch + y_offset * kSens, -60.f, 60.f);
        return;
    }

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
// Main application loop
// ══════════════════════════════════════════════════════════════════════

int App::run() {
    // ── Road geometry constants (world units, must match road.bin) ───
    constexpr float kFt           = 0.3048f * 1000.0f;
    constexpr float kBarrierRight = (2.67f + 3.0f) * kFt;  // ~1728 WU
    constexpr float kLaneWidth    = 13.0f * kFt;           // ~3962 WU
    constexpr float kLaneCount    = 4.0f;
    constexpr float kCrownSlope   = 0.02f;  // RoadConfig::m_crown_slope
    constexpr float kMarkingY     = 5.0f;   // marking_y_offset from road.bin
    constexpr float kEbRoadRight  = kBarrierRight + kLaneCount * kLaneWidth + 10.0f * kFt;

    // EB frontage ("marginal") road X window + surface lift (matches generate_service_roads:
    // ~55 ft berm past the mainline edge, two 12 ft lanes, lifted 6 WU above the grass).
    const float kEbServiceInner = kEbRoadRight + 50.0f * kFt;
    const float kEbServiceOuter = kEbRoadRight + 92.0f * kFt;
    const float kServiceY       = 6.0f;

    // Computes road surface Y at world X using the crown formula. Symmetric about the
    // median (uses |x|) so it works on BOTH carriageways — EB (x>0) is byte-identical
    // to before; WB (x<0) mirrors it, needed once the car can cross via the median.
    // On the EB frontage road (reached via the interchange service ramp) it's flat at
    // the service lift.
    auto road_surface_y = [&](float x) -> float {
        if (x >= kEbServiceInner - 200.f && x <= kEbServiceOuter + 200.f)
            return kServiceY;
        float lanes_from_inner = (std::abs(x) - kBarrierRight) / kLaneWidth;
        float clamped          = std::max(0.f, std::min(kLaneCount, lanes_from_inner));
        return kCrownSlope * (kLaneCount - clamped) * kLaneWidth + kMarkingY;
    };

    // Guided drivable lateral bounds at the car's current (x, trueZ): normally the
    // EB (x>0) or WB (x<0) carriageway; inside a chunk crossover window the bounds
    // open across the median so the car can cross to the other side. In the authored
    // intro (trueZ ≥ −introLen) it's always EB — the pre-Layer-3 behavior.
    const float kWbInnerBound   = -kBarrierRight - 100.f;
    const float kWbOuterBound   = -kEbRoadRight + 100.f;
    const float kEbInnerBound   = kBarrierRight + 100.f;
    const float kEbOuterBound   = kEbRoadRight - 100.f;
    auto        drivable_bounds = [&](float x, double trueZ, float& minX, float& maxX) {
        minX = kEbInnerBound;  // default EB (also the intro)
        maxX = kEbOuterBound;
        if (trueZ >= -static_cast<double>(m_introLen))
            return;
        // Endless region: is the car within its chunk's crossover window?
        double toEndless  = -static_cast<double>(m_introLen) - trueZ;  // ≥ 0
        int    k          = static_cast<int>(std::floor(toEndless / static_cast<double>(kChunkLen)));
        double localZ     = trueZ - (-static_cast<double>(m_introLen) - static_cast<double>(k) * kChunkLen);
        double xoverLocal = -0.5 * static_cast<double>(kChunkLen);
        double halfWindow = 45.0 * kFt + 30.0 * kFt;  // crossover half-length + merge margin
        if (x >= kEbServiceInner - 200.f) {           // on the EB frontage road (via the service ramp)
            minX = kEbServiceInner;
            maxX = kEbServiceOuter;
        } else if (std::abs(localZ - xoverLocal) < halfWindow) {
            minX = kWbOuterBound;  // open across the median
            maxX = kEbOuterBound;
        } else if (x < 0.0f) {
            minX = kWbOuterBound;  // committed to WB
            maxX = kWbInnerBound;
        }
    };

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
        m_sceneLights = scene.lights;  // kept for render-frame shifting on rebase
        renderer.set_scene_lights(m_sceneLights);

        // Endless-road: the authored road above is the "intro"; past its far end
        // we tile ONE canonical chunk (uploaded once, instanced per frame by the
        // treadmill in run()). See kChunkLen / m_originShift.
        m_introLen = road.get_road_length();
        auto chunk = road.generate_chunk(kChunkLen);
        renderer.upload_chunk_geometry(chunk.meshData, chunk.drawCalls);

        // Elevated diamond interchange: one canonical mesh, instanced sparsely (~1.5 mi
        // apart) in the endless region by the treadmill.
        auto interchange = road.generate_interchange_scene();
        renderer.upload_interchange_geometry(interchange.meshData, interchange.drawCalls);
        m_ixRibbons = road.interchange_ribbons();  // shared centrelines for the drivable follower

        // Setup POV camera on the eastbound LIE
        auto* camera = new Camera();
        camera->set_position(Vec3(6501.0f, 1448.0f, -5000.0f));
        camera->set_yaw(-90.0f);   // looking along -Z (down the road)
        camera->set_pitch(-3.0f);  // slight downward driving angle

        // Collision bounds: keep camera within EB roadway
        camera->set_collision_bounds(kBarrierRight + 100.0f, kEbRoadRight - 100.0f, 500.0f, 5000.0f);
        camera->set_collision_enabled(true);

        VkExtent2D extent = renderer.services().swapchainExtent;
        float      aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera->set_perspective(65.0f, aspect, 10.0f, kCameraFar);  // endless road: window ≈ kChunksAhead chunks

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
        car_entity->set_position(Vec3(kCarX, road_surface_y(kCarX), -20000.0f));
        car_entity->set_rotation(Vec3(0.f, 90.f, 0.f));  // +90° = nose down -Z, same way the camera looks
        car_entity->set_scale(Vec3(1000.f, 1000.f, 1000.f));
        car_entity->set_road_bounds(kBarrierRight + 100.f, kEbRoadRight - 100.f);

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
    m_renderer->set_clear_day(false);  // applies the tuned overcast weather + bakes IBL
    m_rain_level = 2;                   // R-key cycle resumes correctly (off → light → heavy)
    m_renderer->set_rain_intensity(1.0f);
    if (m_car)
        m_car->set_rain_intensity(1.0f);

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
        if (m_window->wasResized()) {
            m_window->resetResizedFlag();
        }

        // Toggle cursor capture with Escape (edge-detected — no toggle spam)
        bool esc_down = glfwGetKey(glfw_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (esc_down && !m_esc_key_prev) {
            m_cursor_captured = !m_cursor_captured;
            glfwSetInputMode(glfw_window, GLFW_CURSOR, m_cursor_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            m_first_mouse = true;
        }
        m_esc_key_prev = esc_down;

        // Toggle cockpit / free-fly camera with C (edge-detected)
        bool c_down = glfwGetKey(glfw_window, GLFW_KEY_C) == GLFW_PRESS;
        if (c_down && !m_c_key_prev) {
            m_cockpit    = !m_cockpit;
            m_look_yaw   = 0.f;
            m_look_pitch = 0.f;
        }
        m_c_key_prev = c_down;

        // R key cycles rain intensity: off → light → heavy → off
        bool r_down = glfwGetKey(glfw_window, GLFW_KEY_R) == GLFW_PRESS;
        if (r_down && !m_r_key_prev) {
            static constexpr float kRainLevels[] = {0.0f, 0.35f, 1.0f};
            m_rain_level                         = (m_rain_level + 1) % 3;
            const float rain                     = kRainLevels[m_rain_level];
            m_renderer->set_rain_intensity(rain);
            // Drive the interior cabin wash from the same rain level so the
            // cockpit reads light gray as rain rises (off → light → heavy).
            if (m_car)
                m_car->set_rain_intensity(rain);
            // Rain and the clear-day preset are mutually exclusive — an azure sunny
            // sky with rain falling reads wrong, so turning rain on cancels clear day.
            if (rain > 0.0f && m_clear_day) {
                m_clear_day = false;
                m_renderer->set_clear_day(false);
            }
        }
        m_r_key_prev = r_down;

        // V key toggles the windshield wiper (edge-detected continuous sweep)
        bool v_down = glfwGetKey(glfw_window, GLFW_KEY_V) == GLFW_PRESS;
        if (v_down && !m_v_key_prev) {
            m_wiper_enabled = !m_wiper_enabled;
            m_renderer->set_wiper_enabled(m_wiper_enabled);
        }
        m_v_key_prev = v_down;

        // G key toggles the clear-day preset (bright sunny sky). A clear day is
        // dry, so it also resets the rain cycle to off.
        bool g_down = glfwGetKey(glfw_window, GLFW_KEY_G) == GLFW_PRESS;
        if (g_down && !m_g_key_prev) {
            m_clear_day = !m_clear_day;
            m_renderer->set_clear_day(m_clear_day);
            if (m_clear_day) {
                m_rain_level = 0;  // clear day is dry — reset the rain cycle
                if (m_car)
                    m_car->set_rain_intensity(0.0f);
            }
        }
        m_g_key_prev = g_down;

#ifdef SWISH_DEBUG_UI
        // Backtick (`) toggles debug edit-mode: free the cursor for the panel and
        // freeze the sim; press again to return to driving.
        bool bt_down = glfwGetKey(glfw_window, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
        if (bt_down && !m_backtick_prev) {
            m_debug_edit_mode = !m_debug_edit_mode;
            glfwSetInputMode(glfw_window, GLFW_CURSOR, m_debug_edit_mode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            m_first_mouse = true;
            m_renderer->set_debug_edit_mode(m_debug_edit_mode);
        }
        m_backtick_prev = bt_down;
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
            pos.y    = road_surface_y(pos.x);
            m_car->set_position(pos);

            // ── Endless-road treadmill ───────────────────────────────
            // 1) Origin rebase: keep the car's render-frame Z small (float32
            //    precision) by shifting the world back toward 0 in whole chunk
            //    lengths — the periodic tile looks identical afterwards, so the
            //    rebase is invisible. The camera is welded from the car below,
            //    so it follows automatically.
            if (std::abs(pos.z) > kRebaseThreshold) {
                float shift = std::round(pos.z / kChunkLen) * kChunkLen;
                pos.z -= shift;
                m_car->set_position(pos);
                m_originShift -= static_cast<double>(shift);
                // Shift the intro lamp point-lights into the new render frame so
                // they stay attached to their posts (lighting works in render frame).
                for (LightDesc& l : m_sceneLights)
                    l.position.z -= shift;
                m_renderer->set_scene_lights(m_sceneLights);
            }

            // 2) Active chunk window around the car (render-frame Z offsets for
            //    the canonical chunk). renderZ = trueZ + m_originShift; chunk
            //    slot k spans trueZ [-introLen - k·L, -introLen - (k+1)·L].
            const double carTrueZ = static_cast<double>(pos.z) - m_originShift;

            // Guided drivable bounds (Layer 3): feed the car the lateral bounds for
            // its current surface — opens across the median inside a crossover window so
            // it can cross to the opposite carriageway; else the EB/WB roadway. The car's
            // existing X-clamp (CarEntity::update) enforces them next frame.
            float bMinX = 0.0f, bMaxX = 0.0f;
            drivable_bounds(pos.x, carTrueZ, bMinX, bMaxX);
            m_car->set_road_bounds(bMinX, bMaxX);

            const int kCar   = static_cast<int>(std::floor((-static_cast<double>(m_introLen) - carTrueZ) / kChunkLen));
            const int kStart = std::max(0, kCar - kChunksBehind);
            const int kEnd   = kCar + kChunksAhead;
            std::vector<float> slotOffsets;
            for (int k = kStart; k <= kEnd; ++k) {
                double slotTrueStart = -static_cast<double>(m_introLen) - static_cast<double>(k) * kChunkLen;
                slotOffsets.push_back(static_cast<float>(slotTrueStart + m_originShift));
            }
            m_renderer->set_road_chunks(static_cast<float>(m_originShift), slotOffsets);

            // Sparse elevated interchanges (~1.5 mi apart) around the car; interchange j
            // is centred at trueZ = −introLen − (j+1)·spacing. Culling drops the far ones.
            const float  kInterchangeSpacing = 8.0f * kChunkLen;
            const double nIx = (-static_cast<double>(m_introLen) - carTrueZ) / static_cast<double>(kInterchangeSpacing);
            const int    jNear = static_cast<int>(std::floor(nIx));
            std::vector<float> ixOffsets;
            for (int j = std::max(0, jNear - 2); j <= jNear + 1; ++j) {
                double ixTrueZ = -static_cast<double>(m_introLen) - static_cast<double>(j + 1) * kInterchangeSpacing;
                ixOffsets.push_back(static_cast<float>(ixTrueZ + m_originShift));
            }
            m_renderer->set_interchanges(ixOffsets);

            // ── Interchange ribbon-follower (drivable ramps / deck) ───
            // Off the interchange the car drives freely (above); on it, the car
            // tracks a graded ribbon — arc-length advanced by its speed, lateral by
            // steering, with Y/heading/pitch taken from the curve — so it climbs the
            // ramp, crosses the deck, and descends onto the opposite mainline or the
            // frontage road. Guided, not on-rails: you still steer within the ribbon
            // and pick branches at junctions.
            if (!m_ixRibbons.empty() && !debug_edit) {
                const float spd   = m_car->get_speed();
                const int   steer = (glfwGetKey(glfw_window, GLFW_KEY_LEFT) == GLFW_PRESS ? -1 : 0) +
                                  (glfwGetKey(glfw_window, GLFW_KEY_RIGHT) == GLFW_PRESS ? 1 : 0);
                if (m_carRibbon >= 0) {
                    const float ixZ = static_cast<float>(m_ixTrueZ + m_originShift);  // render-frame Z of the instance
                    const Ribbon* r = &m_ixRibbons[m_carRibbon];
                    m_ribbonS += spd * delta_time;
                    const float latLim = std::max(0.f, r->halfWidth - 1500.f);
                    m_ribbonT =
                        std::clamp(m_ribbonT + static_cast<float>(steer) * 9000.f * delta_time, -latLim, latLim);

                    const float len = ribbonLength(*r);
                    if (m_ribbonS >= len) {  // reached the end → junction / exit
                        const int   nxt      = (r->branch >= 0 && steer > 0) ? r->branch : r->next;
                        const float overflow = m_ribbonS - len;
                        if (nxt < 0) {  // leave the interchange onto free road
                            Vec3 p, tn;
                            ribbonSample(*r, len, p, tn);
                            m_car->set_position(p + Vec3(0.f, 0.f, ixZ));
                            Vec3 th = glm::normalize(Vec3(tn.x, 0.f, tn.z));
                            m_car->set_rotation(Vec3(0.f, glm::degrees(std::atan2(-th.z, th.x)), 0.f));
                            m_carRibbon = -1;
                        } else {
                            m_carRibbon = nxt;
                            m_ribbonS   = overflow;
                            m_ribbonT   = 0.f;
                        }
                    }
                    if (m_carRibbon >= 0) {
                        r = &m_ixRibbons[m_carRibbon];
                        Vec3 p, tn;
                        ribbonSample(*r, m_ribbonS, p, tn);
                        Vec3 rightH = glm::cross(tn, Vec3(0.f, 1.f, 0.f));
                        if (glm::length(rightH) > 1e-4f)
                            rightH = glm::normalize(rightH);
                        m_car->set_position(p + Vec3(0.f, 0.f, ixZ) + rightH * m_ribbonT);
                        Vec3        th    = glm::normalize(Vec3(tn.x, 0.f, tn.z));
                        const float yaw   = glm::degrees(std::atan2(-th.z, th.x));
                        const float pitch = glm::degrees(std::asin(std::clamp(tn.y, -1.f, 1.f)));
                        m_car->set_rotation(Vec3(0.f, yaw, pitch));  // .z = pitch (car nose is +X)
                        m_car->set_road_bounds(-1e9f, 1e9f);         // guided → no lateral clamp
                    }
                } else {  // free: attach to an on-ramp when the car reaches its entry
                    const Vec3 entry = m_ixRibbons[0].pts.front();
                    const Vec3 cp    = m_car->get_position();
                    for (float ixOff : ixOffsets) {
                        if (std::abs((cp.z - ixOff) - entry.z) < 7000.f && std::abs(cp.x - entry.x) < 5000.f &&
                            spd > 500.f) {
                            m_carRibbon = 0;
                            m_ixTrueZ   = static_cast<double>(ixOff) - m_originShift;
                            m_ribbonS   = 0.f;
                            m_ribbonT   = std::clamp(cp.x - entry.x, -(m_ixRibbons[0].halfWidth - 1500.f),
                                                     m_ixRibbons[0].halfWidth - 1500.f);
                            break;
                        }
                    }
                }
            }

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
