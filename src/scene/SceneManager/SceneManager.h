#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace swish {

class Renderer;

// ══════════════════════════════════════════════════════════════════════
// Scene — A callback that populates the world when activated.
//
// Each scene is a function that receives a Renderer& and sets up
// geometry, camera, and any other scene-specific state. It has no state or
// invariants of its own, so it is a type alias, not a class (was a one-method
// std::function wrapper).
// ══════════════════════════════════════════════════════════════════════

using Scene = std::function<void(Renderer&)>;

// ══════════════════════════════════════════════════════════════════════
// SceneManager — Owns all scenes and handles switching between them.
//
// Scene switching: wait for GPU idle → clear old geometry → run new
// scene lambda → rebuild material descriptors.
// ══════════════════════════════════════════════════════════════════════

class SceneManager {
public:
    SceneManager(Renderer& renderer, std::vector<Scene> scenes);
    ~SceneManager();

    void set_active_scene(int index);
    int  get_active_scene() const;

private:
    Renderer*          m_renderer;
    std::vector<Scene> m_scenes;
    int                m_active_scene = -1;
};

}  // namespace swish
