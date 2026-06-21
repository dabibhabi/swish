#pragma once

#include "../Entity/CarEntity.h"

#include <memory>
#include <string>

namespace swish {

class Renderer;

// ══════════════════════════════════════════════════════════════════════
// ModelManager — Loads 3D models from GLB/glTF files.
//
// Extracts vertex data, indices, and embedded PBR textures. Registers
// textures in the Renderer's TextureManager under "car_N[_normal|
// _roughness]" names (mapped to MAT_CAR_0..7 slots).
// ══════════════════════════════════════════════════════════════════════

class ModelManager {
public:
    explicit ModelManager(Renderer& renderer);
    ~ModelManager() = default;

    // Load a GLB file and return a CarEntity ready to be uploaded.
    // Registers all embedded textures in the TextureManager. Call
    // renderer.rebuild_material_descriptors() after this to bind them.
    std::unique_ptr<CarEntity> load_car(const std::string& path);

    void cleanup() {}

private:
    Renderer* m_renderer;
};

}  // namespace swish
