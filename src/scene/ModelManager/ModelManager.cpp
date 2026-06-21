// tinygltf is header-only. We provide one translation unit that defines
// the implementation. stb_image is already defined in StbImage.cpp, so
// we tell tinygltf not to re-include or re-define it.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE_WRITE

// tinygltf internally calls stbi_* functions; stb_image.h must be visible
// so it can see the declarations (implementation is in StbImage.cpp).
#include "stb_image.h"
#include <tiny_gltf.h>

#include "ModelManager.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../../renderer/TextureManager/TextureManager.h"
#include "../../renderer/Vertex.h"
#include "../../scene/SceneTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace swish {

namespace {

template <typename T>
const T* gltf_accessor_data(const tinygltf::Model& model, int accessor_idx) {
    const auto& acc    = model.accessors[accessor_idx];
    const auto& view   = model.bufferViews[acc.bufferView];
    const auto& buffer = model.buffers[view.buffer];
    return reinterpret_cast<const T*>(buffer.data.data() + view.byteOffset + acc.byteOffset);
}

size_t gltf_accessor_count(const tinygltf::Model& model, int accessor_idx) {
    return model.accessors[accessor_idx].count;
}

// Local transform of a glTF node: either the explicit matrix or T·R·S.
glm::mat4 gltf_node_local_matrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        // glTF matrices are column-major, same as glm.
        glm::dmat4 m = glm::make_mat4(node.matrix.data());
        return glm::mat4(m);
    }
    glm::mat4 t(1.f), r(1.f), s(1.f);
    if (node.translation.size() == 3)
        t = glm::translate(glm::mat4(1.f),
                           glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    if (node.rotation.size() == 4)  // glTF order: x, y, z, w
        r = glm::mat4_cast(glm::quat(static_cast<float>(node.rotation[3]),
                                     static_cast<float>(node.rotation[0]),
                                     static_cast<float>(node.rotation[1]),
                                     static_cast<float>(node.rotation[2])));
    if (node.scale.size() == 3)
        s = glm::scale(glm::mat4(1.f),
                       glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    return t * r * s;
}

// Depth-first walk computing each node's world transform.
void gltf_walk_nodes(const tinygltf::Model& model, int node_idx, const glm::mat4& parent,
                     std::vector<glm::mat4>& world, std::vector<bool>& visited) {
    const auto& node = model.nodes[node_idx];
    glm::mat4   w    = parent * gltf_node_local_matrix(node);
    world[node_idx]   = w;
    visited[node_idx] = true;
    for (int child : node.children)
        gltf_walk_nodes(model, child, w, world, visited);
}

// +90° about Y mesh normalization: (x, y, z) → (z, y, −x).
glm::mat4 norm_matrix_y90(const glm::mat4& M) {
    glm::mat4 out(1.f);
    for (int c = 0; c < 3; c++) {
        glm::vec3 v(M[c][0], M[c][1], M[c][2]);
        out[c] = glm::vec4(v.z, v.y, -v.x, 0.f);
    }
    glm::vec3 t(M[3][0], M[3][1], M[3][2]);
    out[3] = glm::vec4(t.z, t.y, -t.x, 1.f);
    return out;
}

// Upload one decoded image to TextureManager; returns name on success, "" on failure.
std::string upload_gltf_image(const tinygltf::Model& model, int image_idx,
                               const std::string& name, TextureManager& tex_mgr) {
    if (image_idx < 0 || image_idx >= static_cast<int>(model.images.size()))
        return "";

    const auto& img = model.images[image_idx];
    if (img.image.empty())
        return "";

    std::vector<uint8_t> rgba;
    if (img.component == 4) {
        rgba = img.image;
    } else if (img.component == 3) {
        rgba.resize(static_cast<size_t>(img.width) * img.height * 4);
        for (int i = 0; i < img.width * img.height; i++) {
            rgba[i * 4 + 0] = img.image[i * 3 + 0];
            rgba[i * 4 + 1] = img.image[i * 3 + 1];
            rgba[i * 4 + 2] = img.image[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
    } else {
        return "";
    }

    tex_mgr.register_from_pixels(name, rgba, static_cast<uint32_t>(img.width),
                                  static_cast<uint32_t>(img.height));
    return name;
}

}  // namespace

ModelManager::ModelManager(Renderer& renderer) : m_renderer(&renderer) {}

std::unique_ptr<CarEntity> ModelManager::load_car(const std::string& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model    gltf;
    std::string        err, warn;

    bool ok = loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    if (!ok || !err.empty())
        throw std::runtime_error("ModelManager::load_car: '" + path + "': " + err);
    if (gltf.meshes.empty())
        throw std::runtime_error("ModelManager::load_car: no meshes in '" + path + "'");

    TextureManager* tex_mgr = m_renderer->get_texture_manager();

    // ── Register PBR textures for each material slot ──────────────────
    constexpr int kMaxCarMaterials = 20;
    int           num_mats = std::min(static_cast<int>(gltf.materials.size()), kMaxCarMaterials);

    for (int mi = 0; mi < num_mats; mi++) {
        const auto& mat  = gltf.materials[mi];
        std::string slot = "car_" + std::to_string(mi);

        // Albedo
        int albedo_img = -1;
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0)
            albedo_img = gltf.textures[mat.pbrMetallicRoughness.baseColorTexture.index].source;
        if (upload_gltf_image(gltf, albedo_img, slot, *tex_mgr).empty())
            tex_mgr->register_from_pixels(slot, {200, 200, 200, 255}, 1, 1);

        // Normal map
        int normal_img = -1;
        if (mat.normalTexture.index >= 0)
            normal_img = gltf.textures[mat.normalTexture.index].source;
        std::string normal_name = slot + "_normal";
        if (upload_gltf_image(gltf, normal_img, normal_name, *tex_mgr).empty())
            tex_mgr->register_from_pixels(normal_name, {128, 128, 255, 255}, 1, 1);

        // Roughness (metallicRoughness texture)
        int rough_img = -1;
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            rough_img = gltf.textures[mat.pbrMetallicRoughness.metallicRoughnessTexture.index].source;
        std::string rough_name = slot + "_roughness";
        if (upload_gltf_image(gltf, rough_img, rough_name, *tex_mgr).empty())
            tex_mgr->register_from_pixels(rough_name, {200, 200, 200, 255}, 1, 1);
    }

    // ── Compute world transforms via a node walk ──────────────────────
    // Vertices are baked with each node's transform RELATIVE to the node
    // named "RootNode": the chain above it (Sketchfab axis conversion +
    // FBX unit scale) nets ~10x and must NOT be baked, while the chains
    // below it place pivoted parts (steering wheel, wipers, …) correctly.
    // RootNode-relative space is the asset's raw mesh space: meters, Y-up.
    std::vector<glm::mat4> node_world(gltf.nodes.size(), glm::mat4(1.f));
    std::vector<bool>      node_visited(gltf.nodes.size(), false);
    const auto& scene = gltf.scenes[gltf.defaultScene >= 0 ? gltf.defaultScene : 0];
    for (int root : scene.nodes)
        gltf_walk_nodes(gltf, root, glm::mat4(1.f), node_world, node_visited);

    glm::mat4 ref_inverse(1.f);
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        if (gltf.nodes[ni].name == "RootNode") {
            ref_inverse = glm::inverse(node_world[ni]);
            break;
        }
    }

    // ── Steering wheel articulation ───────────────────────────────────
    // Store the normalized SteeringWheel_Pivot frame; runtime steer is
    // sw_pivot_frame * R_local(θ) * inverse(sw_pivot_frame) so the child
    // offset baked into vertices stays rig-correct (no wobble).
    int       steering_pivot_idx = -1;
    glm::mat4 sw_pivot_frame(1.f);
    bool      found_sw_pivot = false;
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        if (gltf.nodes[ni].name == "SteeringWheel_Pivot") {
            steering_pivot_idx = static_cast<int>(ni);
            break;
        }
    }
    if (steering_pivot_idx >= 0) {
        sw_pivot_frame = ref_inverse * node_world[steering_pivot_idx];
        found_sw_pivot = true;
    }

    // ── Build combined MeshData from all mesh nodes ───────────────────
    MeshData             mesh;
    std::vector<Submesh> submeshes;
    int                  skipped_glass = 0;

    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        const auto& node = gltf.nodes[ni];
        if (!node_visited[ni] || node.mesh < 0)
            continue;

        glm::mat4 xform      = ref_inverse * node_world[ni];
        glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(xform)));

    for (const auto& prim : gltf.meshes[node.mesh].primitives) {
        if (prim.mode != TINYGLTF_MODE_TRIANGLES || prim.indices < 0)
            continue;

        // Glass (alphaMode BLEND) would render opaque in the G-buffer and
        // wall off the cockpit view. Skip it until the forward transparent
        // pass exists (plan/car_system_port.md Phase 5).
        if (prim.material >= 0 &&
            gltf.materials[prim.material].alphaMode == "BLEND") {
            skipped_glass++;
            continue;
        }

        auto pos_it  = prim.attributes.find("POSITION");
        if (pos_it == prim.attributes.end())
            continue;

        auto norm_it = prim.attributes.find("NORMAL");
        auto uv_it   = prim.attributes.find("TEXCOORD_0");
        auto tan_it  = prim.attributes.find("TANGENT");

        uint32_t     base_vertex = mesh.getVertexCount();
        uint32_t     idx_start   = mesh.getIndexCount();
        size_t       vert_count  = gltf_accessor_count(gltf, pos_it->second);
        const float* positions   = gltf_accessor_data<float>(gltf, pos_it->second);
        const float* normals     = norm_it != prim.attributes.end()
                                       ? gltf_accessor_data<float>(gltf, norm_it->second) : nullptr;
        const float* uvs         = uv_it != prim.attributes.end()
                                       ? gltf_accessor_data<float>(gltf, uv_it->second) : nullptr;
        const float* tangents    = tan_it != prim.attributes.end()
                                       ? gltf_accessor_data<float>(gltf, tan_it->second) : nullptr;

        for (size_t vi = 0; vi < vert_count; vi++) {
            Vertex v{};
            glm::vec3 p = {positions[vi*3+0], positions[vi*3+1], positions[vi*3+2]};
            v.position  = glm::vec3(xform * glm::vec4(p, 1.f));
            v.normal    = normals  ? glm::normalize(normal_mat *
                                         glm::vec3(normals[vi*3+0], normals[vi*3+1], normals[vi*3+2]))
                                   : glm::vec3(0.f, 1.f, 0.f);
            v.uv        = uvs      ? glm::vec2(uvs[vi*2+0], uvs[vi*2+1])
                                   : glm::vec2(0.f, 0.f);
            if (tangents) {
                glm::vec3 t = glm::normalize(glm::mat3(xform) *
                                  glm::vec3(tangents[vi*4+0], tangents[vi*4+1], tangents[vi*4+2]));
                v.tangent   = glm::vec4(t, tangents[vi*4+3]);
            } else {
                v.tangent   = glm::vec4(1.f, 0.f, 0.f, 1.f);
            }
            mesh.addVertex(v);
        }

        // Indices — handle all common component types
        const auto& idx_acc = gltf.accessors[prim.indices];
        size_t      n_idx   = idx_acc.count;
        if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* idx = gltf_accessor_data<uint32_t>(gltf, prim.indices);
            for (size_t i = 0; i < n_idx; i++) mesh.addIndex(base_vertex + idx[i]);
        } else if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* idx = gltf_accessor_data<uint16_t>(gltf, prim.indices);
            for (size_t i = 0; i < n_idx; i++) mesh.addIndex(base_vertex + idx[i]);
        } else if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* idx = gltf_accessor_data<uint8_t>(gltf, prim.indices);
            for (size_t i = 0; i < n_idx; i++) mesh.addIndex(base_vertex + idx[i]);
        }

        Submesh sm{};
        sm.indexOffset = idx_start;
        sm.indexCount  = static_cast<uint32_t>(n_idx);
        sm.color       = Vec4(1.f, 1.f, 1.f, 1.f);
        sm.material    = (prim.material >= 0 && prim.material < kMaxCarMaterials)
                             ? static_cast<MaterialId>(MAT_CAR_0 + prim.material)
                             : MAT_DEFAULT;
        sm.is_steering_wheel = found_sw_pivot && (node.name == "Steering_Wheel");
        submeshes.push_back(sm);
    }
    }

    if (mesh.empty())
        throw std::runtime_error("ModelManager::load_car: no usable geometry in '" + path + "'");

    // ── Normalize mesh space ──────────────────────────────────────────
    // The asset's nose points down mesh +Z, but the entity convention is
    // nose = +X at yaw 0, so that R_y(yaw)·(+X) equals the physics forward
    // vector and the body lines up with the direction of travel. Rotate
    // +90° about Y: (x, y, z) → (z, y, −x), directions included.
    for (auto& v : mesh.getVertices()) {
        v.position = glm::vec3(v.position.z, v.position.y, -v.position.x);
        v.normal   = glm::vec3(v.normal.z, v.normal.y, -v.normal.x);
        v.tangent  = glm::vec4(v.tangent.z, v.tangent.y, -v.tangent.x, v.tangent.w);
    }

    // ── Ground the model ──────────────────────────────────────────────
    // The glTF origin sits at axle height, not at the tire contact patch.
    // Shift all vertices so the lowest point lands at y = 0; then an entity
    // position of y = 0 means "tires resting on the road surface".
    glm::vec3 bb_min(std::numeric_limits<float>::max());
    glm::vec3 bb_max(std::numeric_limits<float>::lowest());
    for (const auto& v : mesh.getVertices()) {
        bb_min = glm::min(bb_min, v.position);
        bb_max = glm::max(bb_max, v.position);
    }
    for (auto& v : mesh.getVertices())
        v.position.y -= bb_min.y;

    if (found_sw_pivot) {
        sw_pivot_frame           = norm_matrix_y90(sw_pivot_frame);
        sw_pivot_frame[3].y     -= bb_min.y;
        for (auto& sm : submeshes) {
            if (sm.is_steering_wheel)
                sm.sw_pivot_frame = sw_pivot_frame;
        }
    }

    glm::vec3 size = bb_max - bb_min;
    std::cout << "ModelManager::load_car: '" << path << "' bbox (m) length "
              << size.x << " x height " << size.y << " x width " << size.z
              << ", grounded by " << -bb_min.y
              << ", glass primitives skipped: " << skipped_glass;
    if (found_sw_pivot) {
        std::cout << ", steering pivot " << sw_pivot_frame[3][0] << " " << sw_pivot_frame[3][1] << " "
                  << sw_pivot_frame[3][2];
    }
    std::cout << std::endl;

    auto car = std::make_unique<CarEntity>();
    car->set_mesh_data(std::move(mesh));
    for (const auto& sm : submeshes)
        car->add_submesh(sm);

    return car;
}

}  // namespace swish
