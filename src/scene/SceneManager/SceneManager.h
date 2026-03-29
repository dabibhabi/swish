#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace swish {

class Renderer;

// ══════════════════════════════════════════════════════════════════════
// Scene — A lambda wrapper that populates the world when activated.
//
// Each scene is a function that receives a Renderer& and sets up
// geometry, camera, and any other scene-specific state.
// ══════════════════════════════════════════════════════════════════════

class Scene {
public:
    explicit Scene(std::function<void(Renderer&)> on_load);
    void run(Renderer& renderer);

private:
    std::function<void(Renderer&)> m_on_load;
};

// ══════════════════════════════════════════════════════════════════════
// SceneManager — Owns all scenes and handles switching between them.
//
// Scene switching: wait for GPU idle → clear old geometry → run new
// scene lambda → rebuild material descriptors.
// ══════════════════════════════════════════════════════════════════════

class SceneManager {
public:
    SceneManager(Renderer& renderer, std::vector<std::unique_ptr<Scene>> scenes);
    ~SceneManager();

    void set_active_scene(int index);
    int  get_active_scene() const;

private:
    Renderer*                             m_renderer;
    std::vector<std::unique_ptr<Scene>>   m_scenes;
    int                                   m_active_scene = -1;
};

}  // namespace swish
