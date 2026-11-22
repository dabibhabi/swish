// App startup: init() and its named steps (window/renderer, textures, scene, car).
#include "App.h"
#include "core/Window/Window.h"
#include "renderer/Renderer/Renderer.h"
#include "renderer/TextureManager/TextureManager.h"
#include "scene/Camera/Camera.h"
#include "scene/CarConfig/CarConfig.h"
#include "scene/Corridor/CorridorBridges.h"
#include "scene/Corridor/CorridorDressing.h"
#include "scene/Corridor/CorridorEdges.h"
#include "scene/Corridor/CorridorEnds.h"
#include "scene/Corridor/CorridorLandmarks.h"
#include "scene/Corridor/CorridorScene.h"
#include "scene/Corridor/CorridorSigns.h"
#include "scene/Entity/CarEntity.h"
#include "scene/ModelManager/ModelManager.h"
#include "scene/RoadGeometry/RoadGeometry.h"
#include "scene/RoadScene/MeshBuilder.h"
#include "scene/SceneManager/SceneManager.h"
#include "utils/io/AppPaths.h"
#include "utils/units/Units.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
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
#ifdef SWISH_DEBUG_UI
    if (const char* lookdev = std::getenv("SWISH_LOOKDEV")) m_lookdevMode = lookdev[0] != '\0' && lookdev[0] != '0';
    // SWISH_LAB=1: the lighting lab world instead of the LIE corridor (docs/lighting-lab.md).
    if (const char* lab = std::getenv("SWISH_LAB")) m_labMode = lab[0] != '\0' && lab[0] != '0';
    if (m_labMode) m_lookdevMode = true;  // same no-corridor, big-window paths
#endif
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
#ifdef SWISH_DEBUG_UI
    m_window->init(m_lookdevMode ? 1440 : 800, m_lookdevMode ? 900 : 600,
                   m_labMode ? "swish - lighting lab" : (m_lookdevMode ? "swish - lighting workbench" : "swish"));
#else
    m_window->init(800, 600, "swish");
#endif

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
    m_renderer->debug_init([this](int key, int action) { on_controls_key(key, action); });
#endif
}

void App::init_textures() {
    m_textureManager = std::make_unique<TextureManager>(m_renderer->services());

    m_textureManager->load_directory(util::paths::texture_dir());
#ifdef SWISH_DEBUG_UI
    m_textureManager->load_directory(util::paths::asset_dir() + "signs");       // D3 sign atlases (debug first)
    m_textureManager->load_directory(util::paths::asset_dir() + "structures");  // D4 bridge stone (debug first)
    m_textureManager->load_directory(util::paths::asset_dir() + "dressing");    // D5 trees, facades, skyline
#endif

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
    // Debug-first (tasks/todo.md "LIE full route"): the debug build drives the
    // 30 mi tunnel-to-Riverhead bake with elevation; release keeps the shipped
    // 18 mi flat asset until the owner signs the route off.
    m_corridor_alignment.load(util::paths::asset_dir() +
                              (kDebugBuild ? "corridor/lie_corridor_full.bin" : "corridor/lie_corridor.bin"));
    m_corridor.set_alignment(&m_corridor_alignment);

    const CorridorHeader& ch = m_corridor_alignment.header();
    std::cout << "[corridor] real LIE: " << (ch.totalLength / 1609344.0) << " mi compacted from "
              << (ch.realLength / 1609344.0) << " mi (N = " << (ch.compactionRadius / 1000.0)
              << " m, c = " << ch.compactionFactor << "), " << ch.stationCount << " stations, " << ch.landmarkCount
              << " landmark interchanges" << std::endl;

    // One vertex buffer + one render offset per 1.2 km segment; see
    // CorridorScene for why the subdivision exists (vertex precision and
    // arc-length UV range, not the 2 m quantisation the design note feared).
    std::vector<CorridorPath> paths;
#ifdef SWISH_DEBUG_UI
    const bool withLamps = true;  // lighting L6.1 (debug first): street lamps on the median
    // C2b: a diamond per exit number; the mainline dips under each crossroad. Sliders in the "Corridor" panel.
    const DebugParams& dp = renderer.debug_params();
    m_corridorKey         = {dp.corridorDiamonds ? 1.0f : 0.0f,
                             dp.corridorDipDepthM,
                             dp.corridorDipHalfM,
                             dp.corridorSrStripM,
                             dp.corridorStubM,
                             dp.edgeWideMedianM,
                             dp.edgeWallHM,
                             dp.edgeDitchM,
                             dp.corridorSigns ? 1.0f : 0.0f,
                             dp.signSheen,
                             dp.corridorBridges ? 1.0f : 0.0f,
                             dp.bridgeClearFt,
                             dp.corridorDressing ? 1.0f : 0.0f,
                             dp.dressDensity,
                             dp.dressFarM,
                             dp.corridorLandmarks ? 1.0f : 0.0f,
                             dp.landmarkGraves};
    m_corridor_alignment.set_ends(corridor_ends(m_corridor_alignment));  // C4: tunnel + plaza west, roundabout east
    using util::units::m;
    // D2: the wide median goes in first (crossroads and paths follow its edges); walls and rails then clear the ramps
    const float wideExtra =
        std::max(0.0f, 0.5f * m(dp.edgeWideMedianM) - m_corridor_alignment.header().medianHalfWidth);
    const auto set_edges = [&] {
        CorridorEdges e = corridor_edges(m_corridor_alignment, paths, wideExtra);
        e.ditch         = m(dp.edgeDitchM);
        e.wallH         = m(dp.edgeWallHM);
        e.far           = dp.corridorDressing ? m(dp.dressFarM) : 0.0f;  // D5
        e.dress         = dp.corridorDressing ? dp.dressDensity : 0.0f;
        e.landmarks     = dp.corridorLandmarks;  // D6
        e.graves        = dp.corridorLandmarks && dp.corridorDressing ? dp.landmarkGraves : 0.0f;
        m_corridor_alignment.set_edges(std::move(e));
    };
    set_edges();
    if (dp.corridorDiamonds) {
        m_corridor_alignment.set_grading(corridor_crossroads(m_corridor_alignment, m(dp.corridorDipDepthM)),
                                         m(dp.corridorDipHalfM), m(dp.corridorSrStripM));
        m_corridor_alignment.set_cross_stub(m(dp.corridorStubM));  // C3a
        paths = corridor_diamond_paths(m_corridor_alignment);
    }
    {  // D4: OSM crossings merged with the crossroads (restyled), the rest become bridges; before walls and signs
        std::vector<CorridorCrossroad> cr = m_corridor_alignment.crossroads();
        std::vector<CorridorBridge>    br;
        if (dp.corridorBridges) br = corridor_bridges(m_corridor_alignment, paths, cr, dp.bridgeClearFt);
        if (dp.corridorLandmarks) landmark_styles(cr);  // D6: Van Wyck / Cross Island / NY-135 decks
        if (dp.corridorDiamonds)
            m_corridor_alignment.set_grading(std::move(cr), m(dp.corridorDipHalfM), m(dp.corridorSrStripM));
        int n[3] = {0, 0, 0}, st[4] = {0, 0, 0, 0};
        for (const CorridorBridge& b : br) ++n[static_cast<int>(b.kind)], ++st[static_cast<int>(b.style)];
        std::cout << "[corridor] D4: " << m_corridor_alignment.crossings().size() << " OSM crossings -> " << br.size()
                  << " bridges (road over " << n[0] << ", rail over " << n[1] << ", LIE over " << n[2] << "; steel "
                  << st[0] << ", concrete " << st[1] << ", stone " << st[2] << ", through-girder " << st[3] << ")"
                  << std::endl;
        m_corridor_alignment.set_bridges(std::move(br));
    }
    set_edges();
    {
        const CorridorEdges& e    = m_corridor_alignment.edges();
        double               wall = 0.0;
        size_t               nw = 0, nr = e.rail[0].size() + e.rail[1].size();
        for (const auto& v : e.wall)
            for (const auto& [a, b] : v) wall += b - a, ++nw;
        std::cout << "[corridor] D2: " << nw << " sound walls (" << wall / 1e6 << " km), " << nr
                  << " W-beam runs, wide median " << e.wide0 / 1e6 << ".." << e.wide1 / 1e6 << " km" << std::endl;
    }
    CorridorSigns signs;  // D3
    if (dp.corridorSigns) signs = corridor_signs(m_corridor_alignment, paths);
    signs.sheen = dp.signSheen;
    if (!dp.corridorLandmarks) signs.boards.clear();  // D6
    std::cout << "[corridor] D3: " << signs.gantries.size() << " gantries, " << signs.gores.size() << " gore signs, "
              << signs.markers.size() << " mile markers" << std::endl;
    // N2a: the Queens network loads before the LIE mesh: N7 dresses it, and the LIE's dressing keeps off its ramps
    CorridorNetwork& net = m_corridor.network();
    net.load(util::paths::asset_dir() + "corridor/queens_network_full.bin", util::paths::asset_dir(),
             &m_corridor_alignment, util::units::m(dp.netUnderBlendM), util::units::m(dp.netHugEaseM), dp.netMaxGrade,
             util::units::m(dp.wzExitGoreM));
    if (dp.corridorDressing && dp.netDress) {  // N7
        net.dress(dp.dressDensity, m(dp.dressFarM), m(dp.edgeWallHM), dp.corridorLandmarks ? dp.landmarkGraves : 0.0f);
        CorridorEdges e = m_corridor_alignment.edges();
        e.ground = &net.ground(), e.netRoad = 0;
        m_corridor_alignment.set_edges(std::move(e));
    }
    std::vector<CorridorSegment> segments = build_corridor_segments(m_corridor_alignment, withLamps, paths, &signs);
#else
    const bool                   withLamps = false;
    std::vector<CorridorSegment> segments  = build_corridor_segments(m_corridor_alignment, withLamps, paths);
#endif
    std::vector<CorridorPlacement> places;
    places.reserve(segments.size());
    size_t corridorDraws = 0;
#ifdef SWISH_DEBUG_UI
    m_signalLenses.clear();
#endif
    for (const CorridorSegment& s : segments) {
#ifdef SWISH_DEBUG_UI
        for (const SignalLens& l : s.signals) m_signalLenses.push_back({places.size(), l});  // index = upload order
#endif
        renderer.add_corridor_segment(s.mesh, s.draws);
        places.push_back(placement_of(s));
        corridorDraws += s.draws.size();
    }
#ifdef SWISH_DEBUG_UI
    {  // D5: the skyline card is its own segment; the driver places it each frame
        CorridorSegment sky = skyline_segment(m_corridor_alignment);
        if (!sky.mesh.empty()) {
            renderer.add_corridor_segment(sky.mesh, sky.draws);
            m_corridor.set_skyline(places.size());
            places.push_back(placement_of(sky));
            corridorDraws += sky.draws.size();
        }
    }
    {  // N2a: the Queens network (5 roads + junction ramps), appended after the LIE; the driver follows it off the LIE
        WorkZoneParams wzp;  // N2b sliders (Corridor panel)
        wzp.spacing = dp.wzSignSpacing;
        wzp.drums   = dp.wzDrums;
        wzp.tailM   = dp.wzTailM;
        StructureParams sp;  // N3 sliders (Corridor panel)
        sp.pierSpacing                        = dp.netPierM;
        sp.wallAbove                          = dp.netWallAboveM;
        sp.signs                              = dp.netSigns;
        sp.landmarks                          = dp.netLandmarks;  // N8
        sp.signNits                           = dp.lmSignNits;
        const std::vector<CorridorSegment> ns = build_network_segments(net, wzp, sp);
        size_t                             nd = 0;
        m_corridor.clear_flashers();  // a rebuild re-uploads every segment
        size_t wzd = 0;
        for (const CorridorSegment& s : ns) {
            m_corridor.add_flashers(places.size(), s.signals);  // N2b arrow boards + warning lights
            wzd += s.signals.empty() ? 0 : s.draws.size();      // only work-zone segments carry flashers
            renderer.add_corridor_segment(s.mesh, s.draws);
            places.push_back(placement_of(s));
            nd += s.draws.size();
        }
        corridorDraws += nd;
        std::cout << "[corridor] N2a network: " << net.road_count() - 1 << " roads, " << net.ramps().size()
                  << " ramps, " << ns.size() << " segments, " << nd << " draw calls (N2b work zones " << wzd << ")"
                  << std::endl;
    }
#endif
    m_corridor.set_segments(std::move(places));
    m_corridor.set_paths(std::move(paths));  // C3: drivable; empty in release
    std::cout << "[corridor] " << segments.size() << " segments, " << corridorDraws << " draw calls" << std::endl;

    // Setup POV camera on the eastbound LIE
    auto camera = std::make_unique<Camera>();
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
    renderer.set_camera(std::move(camera));
}

void App::init_scenes() {
    std::vector<Scene> scenes;
#ifdef SWISH_DEBUG_UI
    if (m_labMode)
        scenes.push_back([this](Renderer& renderer) { build_lab_scene(renderer); });
    else if (m_lookdevMode)
        scenes.push_back([this](Renderer& renderer) { build_lookdev_scene(renderer); });
    else
#endif
        scenes.push_back([this](Renderer& renderer) { build_road_scene(renderer); });

    m_sceneManager = std::make_unique<SceneManager>(*m_renderer, std::move(scenes));

    // Activate first scene
    m_sceneManager->set_active_scene(0);
}

#ifdef SWISH_DEBUG_UI
namespace {
// The first-launch studio pad is kept intact in width, material, and camera.
// Extend only its drive axis; the car is clamped before the far-plane horizon.
constexpr float kLookdevBaseRoadHalfLengthWU = 250000.0f;
constexpr float kLookdevRoadScale            = 5.0f;
constexpr float kLookdevRoadHalfLengthWU     = kLookdevBaseRoadHalfLengthWU * kLookdevRoadScale;
constexpr float kLookdevRoadHalfWidthWU      = 250000.0f;
constexpr float kLookdevCameraFarWU          = 600000.0f;
constexpr float kLookdevRoadEndMarginWU      = 50000.0f;
constexpr float kLookdevDriveHalfLengthWU    = kLookdevRoadHalfLengthWU - kLookdevCameraFarWU - kLookdevRoadEndMarginWU;
static_assert(kLookdevRoadScale == 5.0f && kLookdevDriveHalfLengthWU > 0.0f);
static_assert(kLookdevRoadHalfLengthWU - kLookdevDriveHalfLengthWU > kLookdevCameraFarWU);
}  // namespace

// A simple, drivable studio pad that keeps the full car/material/lighting path
// while skipping the LIE asset, corridor, and road scene setup.
void App::build_lookdev_scene(Renderer& renderer) {
    RoadGeometry::set_flat_surface(0.0f);

    MeshData              mesh;
    std::vector<DrawCall> draws;
    MeshBuilder           builder(mesh, draws);
    builder.addHorizontalQuad(-kLookdevRoadHalfLengthWU, kLookdevRoadHalfLengthWU, 0.0f, -kLookdevRoadHalfWidthWU,
                              kLookdevRoadHalfWidthWU, Vec3(0.0f, 1.0f, 0.0f), Vec4(0.26f, 0.28f, 0.31f, 1.0f),
                              MAT_ASPHALT, 20000.0f);
    renderer.upload_scene_geometry(mesh, draws);

    auto camera = std::make_unique<Camera>();
    camera->set_position(Vec3(-6500.0f, 2200.0f, 0.0f));
    camera->set_yaw(0.0f);
    camera->set_pitch(-7.0f);
    camera->set_collision_enabled(false);
    const VkExtent2D extent = renderer.services().swapchainExtent;
    const float      aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    camera->set_perspective(55.0f, aspect, 10.0f, kLookdevCameraFarWU);
    m_camera_ctl.set_base_fov(camera->get_fov());  // read before the move
    renderer.set_camera(std::move(camera));
    m_camera_ctl.set_mode(CameraMode::Chase);
}
#endif

#ifdef SWISH_DEBUG_UI
// The lighting lab (tasks/todo.md "Lighting lab"): a walled, paved 2-mile box with a station per scenario.
// Geometry, lamps and roof volumes come from scene/LightingLab (pure); this only uploads them.
void App::build_lab_scene(Renderer& renderer) {
    RoadGeometry::set_flat_surface(0.0f);

    LightingLabWorld world = build_lighting_lab();
    renderer.upload_scene_geometry(world.mesh, world.draws);
    renderer.set_scene_lights(world.lights);
    renderer.set_rect_lights(world.rects);  // the showcase places' area lights
    m_labLights = world.lights;
    m_labRoofs  = world.roofs;

    DebugParams& dp = renderer.debug_params();
    dp.labActive    = true;
    dp.rectsTotal   = static_cast<int>(world.rects.size());
    dp.labRoofCount = static_cast<int>(std::min<size_t>(world.roofs.size(), dp.labRoofs.size()));
    for (int i = 0; i < dp.labRoofCount; ++i) dp.labRoofs[static_cast<size_t>(i)] = world.roofs[static_cast<size_t>(i)];
    // Dusk and night need the pre-exposure to follow the exposure (a fixed one bands in the dark).
    dp.adaptivePreExposure = true;
    std::cout << "[lab] lighting lab: " << world.draws.size() << " draw calls, " << world.lights.size() << " lamps, "
              << world.roofs.size() << " roof volumes" << std::endl;

    auto camera = std::make_unique<Camera>();
    camera->set_position(Vec3(0.0f, 2200.0f, 6500.0f));
    camera->set_yaw(-90.0f);  // looking along -Z, down the lanes
    camera->set_pitch(-7.0f);
    camera->set_collision_enabled(false);
    const VkExtent2D extent = renderer.services().swapchainExtent;
    const float      aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    camera->set_perspective(55.0f, aspect, 10.0f, 1500000.0f);  // 1.5 km: the far lanes stay visible
    m_camera_ctl.set_base_fov(camera->get_fov());
    renderer.set_camera(std::move(camera));
    m_camera_ctl.set_mode(CameraMode::Chase);
}
#endif

// Load the car (the rigged 993 Carrera RS unless SWISH_CAR names another GLB) and upload it.
void App::load_car() {
    // SWISH_CAR=Charger/test_car.glb or SWISH_CAR=Porsche/porsche.glb (relative to assets/) or an
    // absolute path swaps in another vehicle; unset → the rigged Porsche (blend/porsche_rigged.glb).
    std::string car_path = util::paths::asset_dir() + "blend/porsche_rigged.glb";
    if (const char* override_path = std::getenv("SWISH_CAR"); override_path && *override_path)
        car_path = override_path[0] == '/' ? override_path : util::paths::asset_dir() + override_path;
    auto car_entity = m_modelManager->load_car(car_path);

    // A named-rig car (porsche_rigged.glb, the 993 Carrera RS) drives with its own physics profile,
    // its wheelbase and tyre radius measured from the hub pivots; every other car keeps the 992 default.
    if (const auto& rig = car_entity->get_rig_geometry(); rig && rig->named_rig)
        car_entity->apply_profile(car_profiles::rs_993(&*rig));

    // Car visual config (config/car.toml): the release values; debug seeds its sliders from them.
    // [gauges] is the dial-needle calibration (only Needle_* submeshes read it); [wipers] drives
    // the wiper cycle for every car (rigged blades and the shader's clear region).
    const CarConfig car_cfg = CarConfig::load(util::paths::config_dir() + "car.toml");
    car_entity->set_ackermann(car_cfg.wheels.ackermann);
    car_entity->set_gauge_params(car_cfg.gauges);
    car_entity->set_keyboard_steer(car_cfg.keyboardSteer);
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

#ifdef SWISH_DEBUG_UI
    if (m_labMode) {
        const LabStation& start = lab_stations()[static_cast<size_t>(LabStationId::Start)];
        m_car->set_position(start.position);
        m_car->set_rotation(Vec3(0.0f, start.yawDeg, 0.0f));
        m_car->set_road_bounds(-1e9f, 1e9f);  // the lab wall clamps x and z (App::step_car_simulation)
    } else if (m_lookdevMode) {
        m_car->set_position(Vec3(0.0f));
        m_car->set_rotation(Vec3(0.0f));
        m_car->set_road_bounds(-kLookdevDriveHalfLengthWU, kLookdevDriveHalfLengthWU);
    } else
#endif
    {
        // Position/heading/lane come from the corridor's west end rather than
        // a hardcoded pose. Corridor bounds own the lateral clamp.
        m_corridor.place_at_start(*m_car);  // debug: SWISH_SPAWN moves it (CorridorDriver::spawn_from_env)
#ifdef SWISH_DEBUG_UI
        if (const char* cam = std::getenv("SWISH_CAMERA"))  // screenshots: "chase", "hood", "helicopter", ...
            for (CameraMode m = CameraMode::Cockpit; m != CameraMode::Count;
                 m            = static_cast<CameraMode>(static_cast<int>(m) + 1))
                if (camera_mode_name(m) == cam) m_camera_ctl.set_mode(m);
        if (const char* h = std::getenv("SWISH_HELI")) {  // screenshots: "radius_m,yaw_deg,pitch_deg" (overheads)
            float r = 55.0f, yaw = 0.0f, pitch = 0.0f;
            std::sscanf(h, "%f,%f,%f", &r, &yaw, &pitch);
            m_camera_ctl.set_heli(util::units::m(r), yaw, pitch);
        }
        if (const char* tod = std::getenv("SWISH_TOD")) {  // screenshots: TimeOfDay index (4 = Evening / dusk)
            m_renderer->debug_params().skyProcedural = true;
            m_renderer->debug_params().timeOfDay     = std::atoi(tod);
        }
#endif
    }

    // Rebuild material descriptors to include the car's textures.
    m_renderer->rebuild_material_descriptors();

    // Upload the car mesh as dynamic geometry.
    // Glass submeshes share the same VBO/IBO — no second upload needed.
    m_renderer->upload_dynamic_geometry(m_car->get_mesh_data(), m_car->get_draw_calls());
    m_renderer->update_glass_draw_calls(m_car->get_glass_draw_calls());
    m_renderer->update_windshield_draw_calls(m_car->get_windshield_draw_calls());
}

}  // namespace swish
