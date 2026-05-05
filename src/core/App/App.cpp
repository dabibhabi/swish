#include "App.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../../renderer/TextureManager/TextureManager.h"
#include "../../scene/Camera/Camera.h"
#include "../../scene/ModelManager/ModelManager.h"
#include "../../scene/RoadScene/RoadScene.h"
#include "../../scene/SceneManager/SceneManager.h"
#include "../Window/Window.h"

#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

namespace swish {

App::App() = default;

App::~App() {
    delete m_sceneManager;
    delete m_modelManager;
    delete m_textureManager;
    delete m_renderer;
    delete m_window;
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

    float x_offset = xf - app->m_last_mouse_x;
    float y_offset = app->m_last_mouse_y - yf;  // reversed: Y grows downward in screen coords
    app->m_last_mouse_x = xf;
    app->m_last_mouse_y = yf;

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
    // ── 1. Window + Renderer core ─────────────────────────────────
    m_window = new Window();
    m_window->init(800, 600, "swish");

    m_renderer = new Renderer();
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

    // ── 3. TextureManager — load all material textures ────────────
    m_textureManager = new TextureManager(m_renderer->services());

    m_textureManager->load_directory(TEXTURE_DIR);

    // Register default textures (1x1 pixels for flat-colored surfaces)
    m_textureManager->register_from_pixels("default", {255, 255, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_normal", {128, 128, 255, 255}, 1, 1);
    m_textureManager->register_from_pixels("default_roughness", {255, 255, 255, 255}, 1, 1);

    // Generate procedural rumble strip texture (alternating dark/light bars)
    {
        constexpr uint32_t rumble_w = 64, rumble_h = 64;
        std::vector<uint8_t> rumble_pixels(rumble_w * rumble_h * 4);
        for (uint32_t y = 0; y < rumble_h; y++) {
            // Alternating 4px dark / 4px light bars along Y
            bool dark = ((y / 4) % 2 == 0);
            uint8_t val = dark ? 80 : 140;
            for (uint32_t x = 0; x < rumble_w; x++) {
                size_t idx = (y * rumble_w + x) * 4;
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

    m_renderer->register_texture_manager(m_textureManager);

    // ── 4. ModelManager (placeholder) ─────────────────────────────
    m_modelManager = new ModelManager(*m_renderer);
    m_renderer->register_model_manager(m_modelManager);

    // ── 5. Build material descriptor sets ─────────────────────────
    m_renderer->rebuild_material_descriptors();

    // ── 6. Define scenes ──────────────────────────────────────────
    auto road_scene = std::make_unique<Scene>([](Renderer& renderer) {
        // Generate road geometry
        RoadScene road;
        auto scene = road.generate();
        renderer.upload_scene_geometry(scene.meshData, scene.drawCalls);
        renderer.set_scene_lights(scene.lights);

        // Setup POV camera on the eastbound LIE
        auto* camera = new Camera();
        camera->set_position(Vec3(6501.0f, 1448.0f, -5000.0f));
        camera->set_yaw(-90.0f);    // looking along -Z (down the road)
        camera->set_pitch(-3.0f);   // slight downward driving angle

        // Collision bounds: keep camera within EB roadway
        // barrier right = barrier_width(2.67ft) + 3ft clearance = ~5.67ft * 304.8 = ~1728 wu
        // EB right = 1728 + 4 * 12.17ft * 304.8 + 10ft * 304.8 = ~1728 + 14834 + 3048 = ~19610 wu
        constexpr float kFt = 0.3048f * 1000.0f;
        float barrier_right = 2.67f * kFt + 3.0f * kFt;
        float eb_road_right = barrier_right + 4.0f * 13.0f * kFt + 10.0f * kFt;
        camera->set_collision_bounds(barrier_right + 100.0f, eb_road_right - 100.0f,
                                     500.0f, 5000.0f);
        camera->set_collision_enabled(true);

        VkExtent2D extent = renderer.services().swapchainExtent;
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera->set_perspective(65.0f, aspect, 10.0f, 2000000.0f);

        renderer.set_camera(camera);
    });

    std::vector<std::unique_ptr<Scene>> scenes;
    scenes.push_back(std::move(road_scene));

    m_sceneManager = new SceneManager(*m_renderer, std::move(scenes));
    m_renderer->register_scene_manager(m_sceneManager);

    // ── 7. Activate first scene ───────────────────────────────────
    m_sceneManager->set_active_scene(0);

    // ── 8. Main loop with delta time ──────────────────────────────
    float last_frame_time = static_cast<float>(glfwGetTime());

    while (!m_window->shouldClose()) {
        float current_time = static_cast<float>(glfwGetTime());
        float delta_time   = current_time - last_frame_time;
        last_frame_time    = current_time;

        m_window->pollEvents();

        // Toggle cursor capture with Escape
        if (glfwGetKey(glfw_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            m_cursor_captured = !m_cursor_captured;
            glfwSetInputMode(glfw_window, GLFW_CURSOR,
                             m_cursor_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            m_first_mouse = true;
            // Small delay to avoid toggle spam
            glfwWaitEventsTimeout(0.2);
        }

        // Process camera input
        Camera* camera = m_renderer->get_camera();
        if (camera && m_cursor_captured) {
            camera->process_keyboard(glfw_window, delta_time);
        }

        m_renderer->drawFrame();
    }

    // ── 9. Cleanup (reverse order) ────────────────────────────────
    m_renderer->wait_for_idle();
    m_textureManager->cleanup();
    m_modelManager->cleanup();
    m_renderer->cleanup();
    m_window->cleanup();

    return 0;
}

}  // namespace swish
