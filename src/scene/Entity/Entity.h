#pragma once

#include "../../scene/SceneTypes.h"
#include "../../utils/Types.h"

#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// Entity — base for all scene objects with a world transform.
// ══════════════════════════════════════════════════════════════════════

class Entity {
public:
    virtual ~Entity() = default;
    virtual void update(float /*dt*/) {}

    void set_position(const Vec3& p) { m_position = p; }
    void set_rotation(const Vec3& r) { m_rotation = r; }  // Euler degrees (X=pitch, Y=yaw, Z=roll)
    void set_scale(const Vec3& s) { m_scale = s; }

    const Vec3& get_position() const { return m_position; }
    const Vec3& get_rotation() const { return m_rotation; }
    const Vec3& get_scale() const { return m_scale; }

    Mat4 get_model_matrix() const;

protected:
    Vec3 m_position{0.f, 0.f, 0.f};
    Vec3 m_rotation{0.f, 0.f, 0.f};
    Vec3 m_scale{1.f, 1.f, 1.f};
};

// ══════════════════════════════════════════════════════════════════════
// ModelEntity — Entity with a loaded mesh and per-submesh material
// assignments. Generates DrawCalls stamped with its current transform.
// ══════════════════════════════════════════════════════════════════════

class ModelEntity : public Entity {
public:
    void set_mesh_data(MeshData mesh) { m_mesh = std::move(mesh); }
    void add_submesh(const Submesh& s) { m_submeshes.push_back(s); }

    // Glass (alphaMode=BLEND) submeshes share the same MeshData VBO/IBO as
    // the opaque submeshes but are drawn in a separate forward transparent pass.
    void add_glass_submesh(const Submesh& s) { m_glassSubmeshes.push_back(s); }

    const MeshData& get_mesh_data() const { return m_mesh; }

    // Returns one DrawCall per opaque submesh stamped with the current model matrix.
    virtual std::vector<DrawCall> get_draw_calls() const;

    // Returns DrawCalls for glass (BLEND) submeshes stamped with current model matrix.
    std::vector<DrawCall> get_glass_draw_calls() const;

    const std::vector<Submesh>& get_submeshes() const { return m_submeshes; }
    const std::vector<Submesh>& get_glass_submeshes() const { return m_glassSubmeshes; }

private:
    MeshData             m_mesh;
    std::vector<Submesh> m_submeshes;
    std::vector<Submesh> m_glassSubmeshes;
};

}  // namespace swish
