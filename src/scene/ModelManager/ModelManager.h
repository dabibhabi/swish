#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace swish {

class Renderer;

// ══════════════════════════════════════════════════════════════════════
// ModelManager — Placeholder for future 3D model loading (glTF/glb).
//
// Will eventually load car models from the cars/ directory.
// For now, provides the interface only.
// ══════════════════════════════════════════════════════════════════════

class ModelManager {
public:
    explicit ModelManager(Renderer& renderer);
    ~ModelManager();

    void load_directory(const std::string& dir);
    void cleanup();

private:
    Renderer* m_renderer;
};

}  // namespace swish
