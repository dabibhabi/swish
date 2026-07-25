#include "SceneManager.h"

#include "../../renderer/Renderer/Renderer.h"

#include <stdexcept>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Scene
// ══════════════════════════════════════════════════════════════════════

Scene::Scene(std::function<void(Renderer&)> on_load) : m_on_load(std::move(on_load)) {}

void Scene::run(Renderer& renderer) {
    m_on_load(renderer);
}

// ══════════════════════════════════════════════════════════════════════
// SceneManager
// ══════════════════════════════════════════════════════════════════════

SceneManager::SceneManager(Renderer& renderer, std::vector<std::unique_ptr<Scene>> scenes)
    : m_renderer(&renderer), m_scenes(std::move(scenes)) {}

SceneManager::~SceneManager() = default;

void SceneManager::set_active_scene(int index) {
    if (index < 0 || index >= static_cast<int>(m_scenes.size())) {
        throw std::runtime_error("SceneManager: invalid scene index " + std::to_string(index));
    }

    // Wait for GPU to finish all work before switching.
    m_renderer->wait_for_idle();

    // Clean up old scene geometry.
    m_renderer->destroy_scene_geometry();

    // Run the new scene's setup lambda.
    m_scenes[index]->run(*m_renderer);
    m_active_scene = index;
}

int SceneManager::get_active_scene() const {
    return m_active_scene;
}

}  // namespace swish
