// App startup: init() and its named steps (window/renderer, textures, scene, car).
#include "App.h"
#include "core/Window/Window.h"
#include "renderer/Renderer/Renderer.h"
#include "renderer/TextureManager/TextureManager.h"
#include "scene/Camera/Camera.h"
#include "scene/CarConfig/CarConfig.h"
#include "scene/Corridor/CorridorScene.h"
#include "scene/Entity/CarEntity.h"
#include "scene/ModelManager/ModelManager.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "scene/SceneManager/SceneManager.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Startup — init() and its named steps
// ══════════════════════════════════════════════════════════════════════

namespace {
// Procedural rumble-strip albedo: alternating 4px dark / 4px light bars along Y.
std::vector<uint8_t> make_rumble_pixels(uint32_t w, uint32_t h) {
    std::vector<uint8_t> pixels(w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        bool    dark = ((y / 4) % 2 == 0);
        uint8_t val  = dark ? 80 : 140;
        for (uint32_t x = 0; x < w; x++) {
            size_t idx      = (y * w + x) * 4;
            pixels[idx + 0] = val;
            pixels[idx + 1] = val;
            pixels[idx + 2] = val;
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
}  // namespace

void App::init() {
    // Road geometry math lives in RoadGeometry (pure, static); the drivable road
    // and its per-frame update live in m_corridor (CorridorDriver).
    init_window_and_renderer();
    init_textures();

    // ModelManager (placeholder)
    m_modelManager = std::make_unique<ModelManager>(*m_renderer);

    // Build material descriptor sets
    m_renderer->rebuild_material_descriptors();

    init_scenes();
    load_car();
    init_key_bindings();

    // Weather HDRI already applied in Renderer::init (default partly cloudy).
    // G cycles presets at runtime. Start dry so rain is an explicit R-key choice.
    set_rain_level(0);
}

void App::init_window_and_renderer() {
    m_window = std::make_unique<Window>();
    m_window->init(800, 600, "swish");

    m_renderer = std::make_unique<Renderer>();
    m_renderer->init(*m_window);

    // Wire input callbacks.
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
}

void App::init_textures() {
    m_textureManager = std::make_unique<TextureManager>(m_renderer->services());

    m_textureManager->load_directory(TEXTURE_DIR);

    // Register default textures (1x1 pixels for flat-colored surfaces)
    m_textureManager->register_from_pixels("default", {255, 255, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_normal", {128, 128, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_roughness", {255, 255, 255, 255}, 1, 1);

    // Generate procedural rumble strip texture (alternating dark/light bars)
    constexpr uint32_t rumble_w = 64, rumble_h = 64;
    m_textureManager->register_from_pixels("rumble", make_rumble_pixels(rumble_w, rumble_h), rumble_w, rumble_h);
    m_textureManager->register_from_pixels("rumble_normal", {128, 128, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("rumble_roughness", {200, 200, 200, 255}, 1, 1);

    m_renderer->register_texture_manager(m_textureManager.get());
}

// Scene callback: real-world LIE corridor (Phase 3a) + the POV camera.
void App::build_road_scene(Renderer& renderer) {
    // Replaces the authored 4.2 km intro + canonical-chunk treadmill +
    // elevated-diamond interchanges outright (docs/phase-3-osm-lie-corridor.md
    // §3 decision #1, §5). The alignment is OpenStreetMap data run through
    // SUMO netconvert and landmark-compacted offline by
    // tools/lie_corridor.py; nothing about the road's shape is authored here.
    //
    // A missing or stale asset throws rather than silently falling back to
    // the retired fictional road — shipping a fake corridor while claiming
    // it is the real LIE is the one outcome worse than not booting.
    m_corridor_alignment.load(std::string(ASSET_DIR) + "corridor/lie_corridor.bin");
    m_corridor.set_alignment(&m_corridor_alignment);

    const CorridorHeader& ch = m_corridor_alignment.header();
    std::cout << "[corridor] real LIE: " << (ch.totalLength / 1609344.0) << " mi compacted from "
              << (ch.realLength / 1609344.0) << " mi (N = " << (ch.compactionRadius / 1000.0)
              << " m, c = " << ch.compactionFactor << "), " << ch.stationCount << " stations, " << ch.landmarkCount
              << " landmark interchanges" << std::endl;

    // One vertex buffer + one render offset per 1.2 km segment; see
    // CorridorScene for why the subdivision exists (vertex precision and
    // arc-length UV range, not the 2 m quantisation the design note feared).
    std::vector<CorridorSegment>   segments = build_corridor_segments(m_corridor_alignment);
    std::vector<CorridorPlacement> places;
    places.reserve(segments.size());
    size_t corridorDraws = 0;
    for (const CorridorSegment& s : segments) {
        renderer.add_corridor_segment(s.mesh, s.draws);
        places.push_back(placement_of(s));
        corridorDraws += s.draws.size();
    }
    m_corridor.set_segments(std::move(places));
    std::cout << "[corridor] " << segments.size() << " segments, " << corridorDraws << " draw calls" << std::endl;

    // Setup POV camera on the eastbound LIE
    auto* camera = new Camera();
    camera->set_position(Vec3(6501.0f, 1448.0f, -5000.0f));
    camera->set_yaw(-90.0f);   // looking along -Z (down the road)
    camera->set_pitch(-3.0f);  // slight downward driving angle

    // Free-fly collision bounds assumed an axis-aligned roadway; on a real
    // curving alignment a world-X window is meaningless, so the camera is
    // unbounded and the CORRIDOR confines the car instead (CorridorDriver).
    camera->set_collision_enabled(false);

    VkExtent2D extent = renderer.services().swapchainExtent;
    float      aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    // endless road: window ≈ kChunksAhead chunks
    camera->set_perspective(65.0f, aspect, 10.0f, RoadGeometry::kCameraFar);
    // Single source of truth for the cockpit FOV-kick's "rest" value
    // (F1) — read back rather than duplicating the 65.0f literal.
    m_camera_ctl.set_base_fov(camera->get_fov());

    renderer.set_camera(camera);
}

void App::init_scenes() {
    std::vector<Scene> scenes;
    scenes.push_back([this](Renderer& renderer) { build_road_scene(renderer); });

    m_sceneManager = std::make_unique<SceneManager>(*m_renderer, std::move(scenes));

    // Activate first scene
    m_sceneManager->set_active_scene(0);
}

// Load the car (the rigged 993 Carrera RS unless SWISH_CAR names another GLB) and upload it.
void App::load_car() {
    // SWISH_CAR=Charger/test_car.glb or SWISH_CAR=Porsche/porsche.glb (relative to assets/) or an
    // absolute path swaps in another vehicle; unset → the rigged Porsche (blend/porsche_rigged.glb).
    std::string car_path = std::string(ASSET_DIR) + "blend/porsche_rigged.glb";
    if (const char* override_path = std::getenv("SWISH_CAR"); override_path && *override_path)
        car_path = override_path[0] == '/' ? override_path : std::string(ASSET_DIR) + override_path;
    auto car_entity = m_modelManager->load_car(car_path);

    // A named-rig car (porsche_rigged.glb, the 993 Carrera RS) drives with its own physics profile,
    // its wheelbase and tyre radius measured from the hub pivots; every other car keeps the 992 default.
    if (const auto& rig = car_entity->get_rig_geometry(); rig && rig->named_rig)
        car_entity->apply_profile(car_profiles::rs_993(&*rig));

    // Car visual config (config/car.toml): the release values; debug seeds its sliders from them.
    // [gauges] is the dial-needle calibration (only Needle_* submeshes read it); [wipers] drives
    // the wiper cycle for every car (rigged blades and the shader's clear region).
    const CarConfig car_cfg = CarConfig::load(std::string(CONFIG_DIR) + "car.toml");
    car_entity->set_ackermann(car_cfg.wheels.ackermann);
    car_entity->set_gauge_params(car_cfg.gauges);
    m_wiper_params = car_cfg.wipers;
#ifdef SWISH_DEBUG_UI
    {
        const GaugeParams& gp  = car_cfg.gauges;
        DebugParams&       dp  = m_renderer->debug_params();
        dp.wheelAckermann      = car_cfg.wheels.ackermann;
        dp.gaugeTachOffsetDeg  = gp.tachOffsetDeg;
        dp.gaugeTachDegPerKrpm = gp.tachDegPerKrpm;
        dp.gaugeRpmRestDeg     = gp.rpmRestDeg;
        dp.gaugeSpeedRestDeg   = gp.speedRestDeg;
        dp.gaugeNeedleTauMs    = gp.needleTauS * 1000.f;
        dp.gaugeSweepDurationS = gp.sweepDurationS;
        dp.wiperLowWpm         = m_wiper_params.lowWipesPerMin;
        dp.wiperHighWpm        = m_wiper_params.highWipesPerMin;
        dp.wiperDwellS         = m_wiper_params.dwellS;
        dp.wiperSweepDeg       = m_wiper_params.sweepDeg;
        dp.wiperEase           = m_wiper_params.ease;
    }
#endif

    // Scale converts glTF meters to world units (1m = 1000 WU).
    car_entity->set_scale(Vec3(1000.f, 1000.f, 1000.f));
    m_car = std::move(car_entity);

    // Position/heading/lane come from the corridor's west end (the
    // Queens-Midtown Tunnel portal) rather than a hardcoded pose — a real
    // alignment starts wherever OSM says it starts, and its heading there
    // is not axis-aligned. This also opens the car's world-X clamp: the
    // lateral limit is a corridor-space quantity now (CorridorDriver).
    m_corridor.place_at_start(*m_car);

    // Rebuild material descriptors to include the car's textures.
    m_renderer->rebuild_material_descriptors();

    // Upload the car mesh as dynamic geometry.
    // Glass submeshes share the same VBO/IBO — no second upload needed.
    m_renderer->upload_dynamic_geometry(m_car->get_mesh_data(), m_car->get_draw_calls());
    m_renderer->update_glass_draw_calls(m_car->get_glass_draw_calls());
    m_renderer->update_windshield_draw_calls(m_car->get_windshield_draw_calls());
}

}  // namespace swish
