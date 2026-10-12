#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "ModelManager.h"

#include "renderer/Renderer/Renderer.h"
#include "renderer/TextureManager/TextureManager.h"
#include "renderer/Vertex.h"
#include "scene/Entity/CarLights.h"
#include "scene/Entity/CarRig.h"
#include "scene/Entity/WiperBlade.h"
#include "scene/SceneTypes.h"
#include "stb_image.h"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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
        t = glm::translate(glm::mat4(1.f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    if (node.rotation.size() == 4)  // glTF order: x, y, z, w
        r = glm::mat4_cast(glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                     static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
    if (node.scale.size() == 3)
        s = glm::scale(glm::mat4(1.f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    return t * r * s;
}

void gltf_walk_nodes(const tinygltf::Model& model, int node_idx, const glm::mat4& parent, std::vector<glm::mat4>& world,
                     std::vector<bool>& visited) {
    const auto& node  = model.nodes[node_idx];
    glm::mat4   w     = parent * gltf_node_local_matrix(node);
    world[node_idx]   = w;
    visited[node_idx] = true;
    for (int child : node.children)
        gltf_walk_nodes(model, child, w, world, visited);
}

enum class WheelPieceClass : uint8_t { None, Spinning, Caliper };

// Two naming conventions:
//   Porsche GLB   — Combined3DWheel* (spins) / CombinedCalliperZone* (steer-only).
//   Explicit rig  — hub_XX (steer pivot) → wheel_XX (spins; tire child inherits)
//                   and brake_XX (steer-only). hub_out records each node's hub
//                   ancestor so rig pieces pivot on the hub, not their own origin.
//   Hellcat       — HELCAT_SUSP_XX (origin = hub centre) → wheel_XX / tire_XX (spin,
//                   siblings) and brake_XX (steer-only), all under the SUSP node.
// hub_prefix == nullptr selects the Porsche convention.
void classify_wheel_nodes(const tinygltf::Model& model, int node_idx, WheelPieceClass current, int hub,
                          const char* hub_prefix, std::vector<WheelPieceClass>& out, std::vector<int>& hub_out) {
    const auto& node = model.nodes[node_idx];
    if (!hub_prefix) {
        if (node.name.rfind("Combined3DWheel", 0) == 0)
            current = WheelPieceClass::Spinning;
        else if (node.name.rfind("CombinedCalliperZone", 0) == 0)
            current = WheelPieceClass::Caliper;
    } else {
        if (node.name.rfind(hub_prefix, 0) == 0)
            hub = node_idx;
        else if (hub >= 0 && (node.name.rfind("wheel_", 0) == 0 || node.name.rfind("tire_", 0) == 0))
            current = WheelPieceClass::Spinning;
        else if (hub >= 0 && node.name.rfind("brake_", 0) == 0)
            current = WheelPieceClass::Caliper;
    }
    out[node_idx]     = current;
    hub_out[node_idx] = hub;
    for (int child : node.children)
        classify_wheel_nodes(model, child, current, hub, hub_prefix, out, hub_out);
}

// Rig-convention wheel frame in RootNode-relative (pre-normalization) space:
// col0 = axle (the piece's local +Z — Blender's local Y after the exporter's
// Y-up swap), col1 = up orthogonalized against it, col3 = hub pivot. Proper
// rotation regardless of the piece's mirror scale; spin_sign absorbs axle sign.
glm::mat4 rig_wheel_frame(const glm::mat4& piece_xform, const glm::vec3& hub_pos) {
    const glm::vec3 axle = glm::normalize(glm::vec3(piece_xform[2]));
    const glm::vec3 up   = glm::normalize(glm::vec3(0.f, 1.f, 0.f) - axle * axle.y);
    glm::mat4       F(1.f);
    F[0] = glm::vec4(axle, 0.f);
    F[1] = glm::vec4(up, 0.f);
    F[2] = glm::vec4(glm::cross(axle, up), 0.f);
    F[3] = glm::vec4(hub_pos, 1.f);
    return F;
}

// flip_winding swaps each triangle's last two indices (glTF: a node transform
// with negative determinant reverses winding, so mirrored parts need it restored
// or back-face culling eats the visible side).
template <typename T>
static void transcodeIndices(const tinygltf::Model& gltf, int accessor, size_t n_idx, uint32_t base_vertex,
                             MeshData& mesh, bool flip_winding) {
    const T* idx = gltf_accessor_data<T>(gltf, accessor);
    for (size_t i = 0; i < n_idx; i++) {
        const size_t src = flip_winding ? (i - i % 3) + (3 - i % 3) % 3 : i;  // 0,2,1 per triangle
        mesh.addIndex(base_vertex + static_cast<uint32_t>(idx[src]));
    }
}

// Upload one decoded image to TextureManager; returns name on success, "" on failure.
std::string upload_gltf_image(const tinygltf::Model& model, int image_idx, const std::string& name,
                              TextureManager& tex_mgr) {
    if (image_idx < 0 || image_idx >= static_cast<int>(model.images.size()))
        return "";

    const auto& img = model.images[image_idx];
    if (img.image.empty())
        return "";

    std::vector<uint8_t> rgba;
    if (img.component == 4) {
        rgba = img.image;
    } else if (img.component == 3) {
        const int pixel_count = img.width * img.height;
        rgba.resize(static_cast<size_t>(pixel_count) * 4);
        for (int i = 0; i < pixel_count; i++) {
            rgba[i * 4 + 0] = img.image[i * 3 + 0];
            rgba[i * 4 + 1] = img.image[i * 3 + 1];
            rgba[i * 4 + 2] = img.image[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
    } else {
        return "";
    }

    tex_mgr.register_from_pixels(name, rgba, static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height));
    return name;
}

// glTF metallicRoughnessTexture packs AO/unused in R, roughness in G, metallic in B.
// gbuffer/basic sample roughness from .r (road greyscale maps), so expand G→RGB here and
// bake roughnessFactor so the shader stays a single .r read for all materials.
std::string upload_gltf_roughness(const tinygltf::Model& model, int image_idx, const std::string& name,
                                  float roughness_factor, TextureManager& tex_mgr) {
    const float rf = std::clamp(roughness_factor, 0.0f, 1.0f);

    if (image_idx < 0 || image_idx >= static_cast<int>(model.images.size()) || model.images[image_idx].image.empty()) {
        const uint8_t v = static_cast<uint8_t>(std::lround(rf * 255.0f));
        tex_mgr.register_from_pixels(name, {v, v, v, 255}, 1, 1);
        return name;
    }

    const auto& img   = model.images[image_idx];
    const int   comps = img.component;
    if (comps < 2 || comps > 4)
        return "";

    const int            pixel_count = img.width * img.height;
    std::vector<uint8_t> rgba(static_cast<size_t>(pixel_count) * 4);
    for (int i = 0; i < pixel_count; i++) {
        const uint8_t g = img.image[static_cast<size_t>(i) * static_cast<size_t>(comps) + 1];
        const uint8_t v =
            static_cast<uint8_t>(std::lround(std::clamp((static_cast<float>(g) / 255.0f) * rf, 0.0f, 1.0f) * 255.0f));
        rgba[static_cast<size_t>(i) * 4 + 0] = v;
        rgba[static_cast<size_t>(i) * 4 + 1] = v;
        rgba[static_cast<size_t>(i) * 4 + 2] = v;
        rgba[static_cast<size_t>(i) * 4 + 3] = 255;
    }

    tex_mgr.register_from_pixels(name, rgba, static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height));
    return name;
}

std::vector<uint32_t> gltf_read_indices(const tinygltf::Model& gltf, int accessor) {
    const size_t          n = gltf.accessors[accessor].count;
    std::vector<uint32_t> out(n);
    switch (gltf.accessors[accessor].componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const auto* p = gltf_accessor_data<uint32_t>(gltf, accessor);
            for (size_t i = 0; i < n; i++)
                out[i] = p[i];
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const auto* p = gltf_accessor_data<uint16_t>(gltf, accessor);
            for (size_t i = 0; i < n; i++)
                out[i] = p[i];
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const auto* p = gltf_accessor_data<uint8_t>(gltf, accessor);
            for (size_t i = 0; i < n; i++)
                out[i] = p[i];
            break;
        }
        default:
            out.clear();
            break;
    }
    return out;
}

// Per-vertex tangents from UV derivatives, for primitives that ship no TANGENT
// attribute (the Hellcat export). Per-triangle accumulate, Gram-Schmidt against the
// normal, handedness w so that cross(n, t) * w points toward DECREASING v (glTF's
// normal-map "up": green = +Y, image origin top-left). Primitive-local space.
std::vector<glm::vec4> generate_tangents(const float* pos, const float* nrm, const float* uv, size_t nv,
                                         const std::vector<uint32_t>& idx) {
    std::vector<glm::vec3> tan(nv, glm::vec3(0.f)), bit(nv, glm::vec3(0.f));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        if (a >= nv || b >= nv || c >= nv)
            continue;
        const glm::vec3 p0(pos[a * 3], pos[a * 3 + 1], pos[a * 3 + 2]);
        const glm::vec3 e1  = glm::vec3(pos[b * 3], pos[b * 3 + 1], pos[b * 3 + 2]) - p0;
        const glm::vec3 e2  = glm::vec3(pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2]) - p0;
        const float     du1 = uv[b * 2] - uv[a * 2], dv1 = uv[b * 2 + 1] - uv[a * 2 + 1];
        const float     du2 = uv[c * 2] - uv[a * 2], dv2 = uv[c * 2 + 1] - uv[a * 2 + 1];
        const float     det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-20f)
            continue;
        const float     r  = 1.f / det;
        const glm::vec3 t  = (e1 * dv2 - e2 * dv1) * r;
        const glm::vec3 bt = (e2 * du1 - e1 * du2) * r;
        for (uint32_t v : {a, b, c}) {
            tan[v] += t;
            bit[v] += bt;
        }
    }
    std::vector<glm::vec4> out(nv);
    for (size_t v = 0; v < nv; v++) {
        const glm::vec3 n(nrm[v * 3], nrm[v * 3 + 1], nrm[v * 3 + 2]);
        glm::vec3       t = tan[v] - n * glm::dot(n, tan[v]);
        if (glm::dot(t, t) < 1e-16f) {  // degenerate UVs: any vector perpendicular to n
            t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1.f, 0.f, 0.f))
                                     : glm::cross(n, glm::vec3(0.f, 1.f, 0.f));
        }
        t             = glm::normalize(t);
        const float w = glm::dot(glm::cross(n, t), bit[v]) > 0.f ? -1.f : 1.f;
        out[v]        = glm::vec4(t, w);
    }
    return out;
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

    const auto has_node = [&](const char* name) {
        return std::any_of(gltf.nodes.begin(), gltf.nodes.end(),
                           [&](const tinygltf::Node& n) { return n.name == name; });
    };
    // Named-rig convention (assets/blend/porsche_rigged.glb, scene/Entity/CarRig.h):
    // Wheel_* / Caliper_* / SteeringWheel / Needle_* / Wiper_* pivots by exact name,
    // authored in the Porsche frame (nose +Z, left +X) with no RootNode, so it rides
    // the Porsche path's transforms. Tried first; the conventions below are fallbacks.
    std::vector<std::string_view> node_names;
    std::vector<int>              node_parent(gltf.nodes.size(), -1);
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        node_names.emplace_back(gltf.nodes[ni].name);
        for (int child : gltf.nodes[ni].children)
            node_parent[static_cast<size_t>(child)] = static_cast<int>(ni);
    }
    const bool                   named_rig = car_rig_detect(node_names);
    const std::vector<CarRigTag> rig_tags =
        named_rig ? car_rig_tag_nodes(node_names, node_parent) : std::vector<CarRigTag>(gltf.nodes.size());

    // Explicit-rig convention: a "Chassis" root plus hub_FL/FR/RL/RR. Authored nose
    // +X, up +Y, right +Z — already car space.
    const bool chassis_rig = !named_rig && has_node("Chassis") && has_node("hub_FL") && has_node("hub_FR") &&
                             has_node("hub_RL") && has_node("hub_RR");
    // Hellcat convention (assets/Charger/hellcat_car.glb, car-only Blender export):
    // HELCAT_SUSP_xx empties at the hub centres (rolling stock hangs off them) and a
    // `steering_wheel` mesh under HELCAT_ASM_SteeringSpin. Same car-space axes.
    const bool hellcat = !named_rig && !chassis_rig && has_node("HELCAT_SUSP_FL") && has_node("HELCAT_SUSP_FR") &&
                         has_node("HELCAT_SUSP_RL") && has_node("HELCAT_SUSP_RR") && has_node("steering_wheel") &&
                         has_node("HELCAT_ASM_SteeringSpin");
    const bool rig     = chassis_rig || hellcat;

    // ── Register PBR textures for each material slot ──────────────────
    constexpr int kMaxCarMaterials = MAT_CAR_LAST - MAT_CAR_0 + 1;
    int           num_mats         = std::min(static_cast<int>(gltf.materials.size()), kMaxCarMaterials);

    for (int mi = 0; mi < num_mats; mi++) {
        const auto& mat  = gltf.materials[mi];
        std::string slot = "car_" + std::to_string(mi);

        int albedo_img = -1;
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0)
            albedo_img = gltf.textures[mat.pbrMetallicRoughness.baseColorTexture.index].source;
        // Hellcat: untextured → white; the glTF baseColorFactor rides on Submesh::color
        // (gbuffer multiplies albedo × color), so the 200-grey placeholder would dim it.
        if (upload_gltf_image(gltf, albedo_img, slot, *tex_mgr).empty())
            tex_mgr->register_from_pixels(
                slot, hellcat ? std::vector<uint8_t>{255, 255, 255, 255} : std::vector<uint8_t>{200, 200, 200, 255}, 1,
                1);

        int normal_img = -1;
        if (mat.normalTexture.index >= 0)
            normal_img = gltf.textures[mat.normalTexture.index].source;
        std::string normal_name = slot + "_normal";
        if (upload_gltf_image(gltf, normal_img, normal_name, *tex_mgr).empty())
            tex_mgr->register_from_pixels(normal_name, {128, 128, 255, 255}, 1, 1);

        // Roughness: glTF ORM G-channel × roughnessFactor → greyscale (sampled as .r).
        int rough_img = -1;
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            rough_img = gltf.textures[mat.pbrMetallicRoughness.metallicRoughnessTexture.index].source;
        const float roughness_factor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
        std::string rough_name       = slot + "_roughness";
        if (upload_gltf_roughness(gltf, rough_img, rough_name, roughness_factor, *tex_mgr).empty()) {
            const uint8_t v = static_cast<uint8_t>(std::lround(std::clamp(roughness_factor, 0.0f, 1.0f) * 255.0f));
            tex_mgr->register_from_pixels(rough_name, {v, v, v, 255}, 1, 1);
        }
    }

    std::vector<glm::mat4> node_world(gltf.nodes.size(), glm::mat4(1.f));
    std::vector<bool>      node_visited(gltf.nodes.size(), false);
    const auto&            scene = gltf.scenes[gltf.defaultScene >= 0 ? gltf.defaultScene : 0];
    for (int root : scene.nodes)
        gltf_walk_nodes(gltf, root, glm::mat4(1.f), node_world, node_visited);

    // Rig glass is grouped under a "Glass" node rather than flagged BLEND in its
    // materials (they're all OPAQUE), so mark that subtree by ancestry.
    std::vector<bool> node_in_glass(gltf.nodes.size(), false);
    if (rig) {
        const std::function<void(int)> mark = [&](int n) {
            node_in_glass[n] = true;
            for (int c : gltf.nodes[n].children)
                mark(c);
        };
        // The Hellcat has no "Glass" group node: tag its six panes by node name instead.
        static constexpr const char* kHellcatGlass[] = {"windshield", "rear_windshield", "window_FL",
                                                        "window_FR",  "rear_window_L",   "rear_window_R"};
        for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
            const std::string& nm = gltf.nodes[ni].name;
            if (hellcat ? std::any_of(std::begin(kHellcatGlass), std::end(kHellcatGlass),
                                      [&](const char* g) { return nm == g; })
                        : nm == "Glass")
                mark(static_cast<int>(ni));
        }
    }

    // Wheel/caliper membership by ancestry (see classify_wheel_nodes).
    std::vector<WheelPieceClass> node_wheel_class(gltf.nodes.size(), WheelPieceClass::None);
    std::vector<int>             node_hub(gltf.nodes.size(), -1);
    for (int root : scene.nodes)
        classify_wheel_nodes(gltf, root, WheelPieceClass::None, -1, hellcat ? "HELCAT_SUSP_" : (rig ? "hub_" : nullptr),
                             node_wheel_class, node_hub);

    // Everything below is written against the Porsche's authored frame (nose +Z,
    // left +X) and ends with norm_matrix_y90 into car space. A rig asset is
    // already in car space, so pre-rotate it −90° about Y into the Porsche frame;
    // the shared +90° normalization then cancels it exactly.
    glm::mat4 ref_inverse =
        rig ? glm::rotate(glm::mat4(1.f), glm::radians(-90.f), glm::vec3(0.f, 1.f, 0.f)) : glm::mat4(1.f);

    const char* sw_node_name       = rig ? "steering_wheel" : "Steering_Wheel";
    int         steering_wheel_idx = -1;
    glm::mat4   sw_pivot_frame(1.f);
    bool        found_sw_pivot = false;
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        if (!rig && gltf.nodes[ni].name == "RootNode")
            ref_inverse = glm::inverse(node_world[ni]);
        if (gltf.nodes[ni].name == sw_node_name)
            steering_wheel_idx = static_cast<int>(ni);
    }
    if (steering_wheel_idx >= 0 && node_visited[steering_wheel_idx]) {
        sw_pivot_frame = ref_inverse * node_world[steering_wheel_idx];
        found_sw_pivot = true;
    }
    // Named rig: the frame comes from the SteeringWheel pivot (the mesh is its child), and
    // the four hub pivots are kept for the wheel frames built after grounding.
    std::array<int, 4> rig_wheel_pivot{-1, -1, -1, -1};
    for (size_t ni = 0; named_rig && ni < gltf.nodes.size(); ni++) {
        const CarRigTag& t = rig_tags[ni];
        if (t.pivot != static_cast<int>(ni) || !node_visited[ni])
            continue;
        if (t.role == CarRigRole::Wheel)
            rig_wheel_pivot[static_cast<size_t>(t.corner)] = static_cast<int>(ni);
        if (t.role == CarRigRole::SteeringWheel) {
            sw_pivot_frame = ref_inverse * node_world[ni];
            found_sw_pivot = true;
        }
    }
    if (hellcat) {
        // CarEntity turns the wheel about the pivot frame's +Z. The Hellcat's spin axis
        // is the SteeringSpin node's local +Y in glTF space (Blender local Z after the
        // exporter's Y-up swap): the column axis, pointing forward-down. Build an
        // orthonormal frame (Z = axis, X = up × Z, Y = Z × X) at its origin.
        for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
            if (gltf.nodes[ni].name != "HELCAT_ASM_SteeringSpin" || !node_visited[ni])
                continue;
            const glm::vec3 axis = glm::normalize(glm::vec3(node_world[ni][1]));
            const glm::vec3 x    = glm::normalize(glm::cross(glm::vec3(0.f, 1.f, 0.f), axis));
            glm::mat4       F(1.f);
            F[0]           = glm::vec4(x, 0.f);
            F[1]           = glm::vec4(glm::cross(axis, x), 0.f);
            F[2]           = glm::vec4(axis, 0.f);
            F[3]           = node_world[ni][3];
            sw_pivot_frame = ref_inverse * F;
            found_sw_pivot = (steering_wheel_idx >= 0 && node_visited[steering_wheel_idx]);
            break;
        }
    }

    MeshData             mesh;
    std::vector<Submesh> submeshes;       // opaque (G-buffer)
    std::vector<Submesh> glassSubmeshes;  // BLEND (forward transparent pass)

    struct CornerAccum {
        bool      seen = false;
        glm::vec3 center{0.f};
        glm::mat4 frame{1.f};  // captured from a SPINNING piece: proper rotation
                               // (left calipers carry a mirror scale — never used)
        bool  frame_from_wheel = false;
        int   wheel_pieces = 0, caliper_pieces = 0;
        float max_spread = 0.f;
    };
    CornerAccum wheel_corners[4];  // 0 FL · 1 FR · 2 BL · 3 BR
    bool        wheels_ok = true;
    const auto  corner_of = [](const glm::vec3& t) { return (t.z > 0.f ? 0 : 2) + (t.x > 0.f ? 0 : 1); };

    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        const auto& node = gltf.nodes[ni];
        if (!node_visited[ni] || node.mesh < 0)
            continue;

        glm::mat4 xform      = ref_inverse * node_world[ni];
        glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(xform)));

        const WheelPieceClass wclass = node_wheel_class[ni];

        for (const auto& prim : gltf.meshes[node.mesh].primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES || prim.indices < 0)
                continue;

            const bool rigGlass = rig && node_in_glass[ni];
            const bool isGlass = rigGlass || (prim.material >= 0 && gltf.materials[prim.material].alphaMode == "BLEND");
            const bool isWindshield = isGlass && (rig         ? node.name == "windshield"
                                                  : named_rig ? car_rig_is_windshield_node(node.name)
                                                              : node.name.find("Window_Geo") != std::string::npos &&
                                                                    node.name.find("RED_GLASS") == std::string::npos);

            std::string lowerName = node.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool isInterior = !isGlass && lowerName.find("interior") != std::string::npos;

            auto pos_it = prim.attributes.find("POSITION");
            if (pos_it == prim.attributes.end())
                continue;

            auto norm_it = prim.attributes.find("NORMAL");
            auto uv_it   = prim.attributes.find("TEXCOORD_0");
            auto tan_it  = prim.attributes.find("TANGENT");

            uint32_t     base_vertex = mesh.getVertexCount();
            uint32_t     idx_start   = mesh.getIndexCount();
            size_t       vert_count  = gltf_accessor_count(gltf, pos_it->second);
            const float* positions   = gltf_accessor_data<float>(gltf, pos_it->second);
            const float* normals =
                norm_it != prim.attributes.end() ? gltf_accessor_data<float>(gltf, norm_it->second) : nullptr;
            const float* uvs =
                uv_it != prim.attributes.end() ? gltf_accessor_data<float>(gltf, uv_it->second) : nullptr;
            const float* tangents =
                tan_it != prim.attributes.end() ? gltf_accessor_data<float>(gltf, tan_it->second) : nullptr;

            // Hellcat ships no TANGENT: derive them from the UVs (skipped for UV-less meshes).
            std::vector<glm::vec4> gen_tangents;
            if (hellcat && !tangents && uvs && normals)
                gen_tangents =
                    generate_tangents(positions, normals, uvs, vert_count, gltf_read_indices(gltf, prim.indices));
            const float gen_w_sign = glm::determinant(glm::mat3(xform)) < 0.f ? -1.f : 1.f;  // mirrors flip handedness

            for (size_t vi = 0; vi < vert_count; vi++) {
                Vertex    v{};
                glm::vec3 p = {positions[vi * 3 + 0], positions[vi * 3 + 1], positions[vi * 3 + 2]};
                v.position  = glm::vec3(xform * glm::vec4(p, 1.f));
                v.normal    = normals ? glm::normalize(normal_mat * glm::vec3(normals[vi * 3 + 0], normals[vi * 3 + 1],
                                                                              normals[vi * 3 + 2]))
                                      : glm::vec3(0.f, 1.f, 0.f);
                v.uv        = uvs ? glm::vec2(uvs[vi * 2 + 0], uvs[vi * 2 + 1]) : glm::vec2(0.f, 0.f);
                if (tangents) {
                    glm::vec3 t = glm::normalize(
                        glm::mat3(xform) * glm::vec3(tangents[vi * 4 + 0], tangents[vi * 4 + 1], tangents[vi * 4 + 2]));
                    v.tangent = glm::vec4(t, tangents[vi * 4 + 3]);
                } else if (!gen_tangents.empty()) {
                    const glm::vec4& g = gen_tangents[vi];
                    v.tangent          = glm::vec4(glm::normalize(glm::mat3(xform) * glm::vec3(g)), g.w * gen_w_sign);
                } else {
                    v.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
                }
                mesh.addVertex(v);
            }

            const auto& idx_acc = gltf.accessors[prim.indices];
            size_t      n_idx   = idx_acc.count;
            // Rig assets mirror doors/wheels/brakes by negative scale; the Porsche
            // path is left as-is so its output stays byte-identical.
            const bool flip = rig && glm::determinant(glm::mat3(xform)) < 0.f;
            switch (idx_acc.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    transcodeIndices<uint32_t>(gltf, prim.indices, n_idx, base_vertex, mesh, flip);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    transcodeIndices<uint16_t>(gltf, prim.indices, n_idx, base_vertex, mesh, flip);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    transcodeIndices<uint8_t>(gltf, prim.indices, n_idx, base_vertex, mesh, flip);
                    break;
                default:
                    break;
            }

            const CarRigTag& rtag = rig_tags[ni];

            Submesh sm{};
            sm.indexOffset       = idx_start;
            sm.indexCount        = static_cast<uint32_t>(n_idx);
            sm.is_glass          = isGlass;
            sm.is_windshield     = isWindshield;
            sm.is_interior       = isInterior;
            sm.is_steering_wheel = !isGlass && found_sw_pivot &&
                                   (named_rig ? rtag.role == CarRigRole::SteeringWheel : node.name == sw_node_name);

            if (named_rig) {
                // Corner and role come from the ancestor pivot's name, not the translation.
                if (!isGlass && (rtag.role == CarRigRole::Wheel || rtag.role == CarRigRole::Caliper)) {
                    const bool spins = rtag.role == CarRigRole::Wheel;
                    auto&      acc   = wheel_corners[static_cast<size_t>(rtag.corner)];
                    (spins ? acc.wheel_pieces : acc.caliper_pieces)++;
                    sm.wheel_corner = rtag.corner;
                    sm.wheel_spins  = spins;
                }
                if (rtag.role == CarRigRole::Part) {
                    sm.rig_part        = rtag.part;
                    sm.rig_pivot_frame = ref_inverse * node_world[rtag.pivot];  // normalized after grounding
                }
            } else if (!isGlass && wclass != WheelPieceClass::None) {
                // Rig pieces pivot on their hub (brakes sit offset from it).
                const glm::vec3 t = rig && node_hub[ni] >= 0 ? glm::vec3((ref_inverse * node_world[node_hub[ni]])[3])
                                                             : glm::vec3(xform[3]);

                if (std::abs(t.x) < 0.4f || std::abs(t.x) > 1.2f || std::abs(t.z) < 0.8f || std::abs(t.z) > 1.8f ||
                    t.y < 0.2f || t.y > 0.6f) {
                    wheels_ok = false;
                } else {
                    const int c   = corner_of(t);
                    auto&     acc = wheel_corners[c];
                    if (!acc.seen) {
                        acc.seen   = true;
                        acc.center = t;
                    }
                    acc.max_spread = std::max(acc.max_spread, glm::length(t - acc.center));
                    if (wclass == WheelPieceClass::Spinning) {
                        acc.wheel_pieces++;
                        if (!acc.frame_from_wheel) {
                            acc.frame            = rig ? rig_wheel_frame(xform, t) : xform;
                            acc.frame_from_wheel = true;
                        }
                    } else {
                        acc.caliper_pieces++;
                    }
                    sm.wheel_corner = static_cast<int8_t>(c);
                    sm.wheel_spins  = (wclass == WheelPieceClass::Spinning);
                }
            }

            if (prim.material >= 0 && prim.material < kMaxCarMaterials) {
                sm.material = static_cast<MaterialId>(MAT_CAR_0 + prim.material);

                if (rigGlass && !hellcat) {
                    sm.color = Vec4(0.f, 0.f, 0.f, 0.25f);  // Porsche window tint (its BLEND factor)
                } else if (isGlass) {
                    const auto& bc = gltf.materials[prim.material].pbrMetallicRoughness.baseColorFactor;
                    sm.color = Vec4(static_cast<float>(bc[0]), static_cast<float>(bc[1]), static_cast<float>(bc[2]),
                                    static_cast<float>(bc[3]));
                } else if (hellcat) {
                    const auto& bc = gltf.materials[prim.material].pbrMetallicRoughness.baseColorFactor;
                    sm.color =
                        Vec4(static_cast<float>(bc[0]), static_cast<float>(bc[1]), static_cast<float>(bc[2]), 1.f);
                } else {
                    sm.color = Vec4(1.f, 1.f, 1.f, 1.f);
                }
            } else {
                sm.material = MAT_DEFAULT;
                sm.color    = Vec4(1.f, 1.f, 1.f, 1.f);
            }

            if (isGlass)
                glassSubmeshes.push_back(sm);
            else
                submeshes.push_back(sm);
        }
    }

    if (mesh.empty())
        throw std::runtime_error("ModelManager::load_car: no usable geometry in '" + path + "'");

    glm::vec3 bb_min(std::numeric_limits<float>::max());
    glm::vec3 bb_max(std::numeric_limits<float>::lowest());
    mesh.forEachVertex([&](Vertex& v) {
        v.position = glm::vec3(v.position.z, v.position.y, -v.position.x);
        v.normal   = glm::vec3(v.normal.z, v.normal.y, -v.normal.x);
        v.tangent  = glm::vec4(v.tangent.z, v.tangent.y, -v.tangent.x, v.tangent.w);
        bb_min     = glm::min(bb_min, v.position);
        bb_max     = glm::max(bb_max, v.position);
    });

    mesh.forEachVertex([&](Vertex& v) { v.position.y -= bb_min.y; });

    if (found_sw_pivot && named_rig) {
        sw_pivot_frame = car_rig_steering_frame(sw_pivot_frame, bb_min.y);  // clockwise on right steer
    } else if (found_sw_pivot) {
        sw_pivot_frame = norm_matrix_y90(sw_pivot_frame);
        sw_pivot_frame[3].y -= bb_min.y;

        for (int c = 0; c < 3; c++)
            sw_pivot_frame[c] = glm::vec4(glm::normalize(glm::vec3(sw_pivot_frame[c])), 0.f);

        // CarEntity turns the wheel about the pivot's local +Z, which the Porsche
        // authors pointing FORWARD (+X). The rig's steering_wheel points it back at
        // the driver, which would turn it the wrong way; spin the frame 180° about
        // its local Y (still a proper rotation, rest pose unchanged).
        if (rig && sw_pivot_frame[2].x < 0.f) {
            sw_pivot_frame[0] = -sw_pivot_frame[0];
            sw_pivot_frame[2] = -sw_pivot_frame[2];
        }
    }
    if (found_sw_pivot) {
        for (auto& sm : submeshes) {
            if (sm.is_steering_wheel)
                sm.sw_pivot_frame = sw_pivot_frame;
        }
    }
    // Named-rig needles / wipers: same normalization as the steering pivot.
    for (auto* list : {&submeshes, &glassSubmeshes}) {
        for (auto& sm : *list) {
            if (sm.rig_part != RigPart::None)
                sm.rig_pivot_frame = normalize_pivot_frame(sm.rig_pivot_frame, bb_min.y);
        }
    }

    std::array<WheelFrame, 4> wheel_frames{};
    for (int c = 0; named_rig && c < 4; c++) {
        // Named rig: each frame is its Wheel_XX hub pivot (CarRig.h), checked per corner.
        const int  p  = rig_wheel_pivot[static_cast<size_t>(c)];
        const auto wf = p < 0 ? std::nullopt
                              : car_rig_wheel_frame(ref_inverse * node_world[static_cast<size_t>(p)], c,
                                                    wheel_corners[c].wheel_pieces, bb_min.y);
        if (wf)
            wheel_frames[static_cast<size_t>(c)] = *wf;
        else
            wheels_ok = false;
    }
    for (int c = 0; !named_rig && wheels_ok && c < 4; c++) {
        const auto& acc = wheel_corners[c];

        if (!acc.seen || !acc.frame_from_wheel || acc.wheel_pieces < (rig ? 1 : 50) || acc.max_spread > 1e-3f) {
            wheels_ok = false;
            break;
        }
        glm::mat4 F = norm_matrix_y90(acc.frame);  // Frame
        F[3].y -= bb_min.y;
        for (int col = 0; col < 3; col++)
            F[col] = glm::vec4(glm::normalize(glm::vec3(F[col])), 0.f);

        WheelFrame wf;
        wf.frame  = F;
        wf.radius = F[3].y;  // grounded center height = rolling radius (m)

        const glm::vec3 axle(glm::normalize(glm::vec3(F[0])));
        wf.spin_sign = axle.z > 0.f ? -1.f : 1.f;

        if (std::abs(axle.z) < 0.9f || wf.radius < 0.25f || wf.radius > 0.45f)
            wheels_ok = false;
        wheel_frames[c] = wf;
    }
    if (!wheels_ok) {
        // Loud fallback: untag everything → wheels render rigid (the exact
        // pre-feature look) instead of spinning about wrong pivots.
        for (auto& sm : submeshes) {
            sm.wheel_corner = -1;
            sm.wheel_spins  = false;
        }
        std::cout << "ModelManager::load_car: WHEEL RECOVERY FAILED — corner grouping/frame checks "
                     "tripped; wheels render rigid"
                  << std::endl;
    } else {
        static constexpr const char* kCornerName[4] = {"FL", "FR", "BL", "BR"};
        for (int c = 0; c < 4; c++) {
            const auto&     wf = wheel_frames[c];
            const glm::vec3 axle(glm::vec3(wf.frame[0]));
            std::cout << "ModelManager::load_car: wheel " << kCornerName[c] << " center (" << wf.frame[3].x << ", "
                      << wf.frame[3].y << ", " << wf.frame[3].z << ") m, radius " << wf.radius << " m, spin sign "
                      << wf.spin_sign << ", camber " << glm::degrees(std::acos(std::min(1.f, std::abs(axle.z))))
                      << " deg, pieces " << wheel_corners[c].wheel_pieces << "+" << wheel_corners[c].caliper_pieces
                      << std::endl;
        }
    }

    glm::vec3 size = bb_max - bb_min;
    std::cout << "ModelManager::load_car: '" << path << "' bbox (m) length " << size.x << " x height " << size.y
              << " x width " << size.z << ", grounded by " << -bb_min.y
              << ", glass primitives: " << glassSubmeshes.size() << " (windshield: "
              << std::count_if(glassSubmeshes.begin(), glassSubmeshes.end(),
                               [](const Submesh& s) { return s.is_windshield; })
              << ")";
    if (found_sw_pivot) {
        std::cout << ", steering pivot " << sw_pivot_frame[3][0] << " " << sw_pivot_frame[3][1] << " "
                  << sw_pivot_frame[3][2];
    }
    std::cout << std::endl;

    // Car lights (tasks/todo.md "Car lights"): the rig's `<Anchor>_Lamp` nodes, by exact
    // name (CarLights.h). Same frame as the vertices above: RootNode-relative, the
    // (x,y,z) -> (z,y,-x) swap into car space, then grounded. The lamp node's local -Z
    // is the beam. Assets without these nodes get no car lights.
    std::vector<CarLightAnchor> light_anchors;
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        const auto kind = car_light_kind_from_node(gltf.nodes[ni].name);
        if (!kind || !node_visited[ni])
            continue;
        const glm::mat4 M = ref_inverse * node_world[ni];
        const glm::vec3 p(M[3]);
        const glm::vec3 d = -glm::normalize(glm::vec3(M[2]));
        light_anchors.push_back({*kind, glm::vec3(p.z, p.y - bb_min.y, -p.x), glm::vec3(d.z, d.y, -d.x)});
    }
    if (!light_anchors.empty())
        std::cout << "ModelManager::load_car: " << light_anchors.size() << " car light anchors" << std::endl;

    auto car = std::make_unique<CarEntity>();
    car->set_mesh_data(std::move(mesh));
    for (const auto& sm : submeshes)
        car->add_submesh(sm);
    for (const auto& sm : glassSubmeshes)
        car->add_glass_submesh(sm);
    if (wheels_ok) {
        car->set_wheel_frames(wheel_frames);
        car->set_rig_geometry(measure_car_rig_geometry(wheel_frames, named_rig));
    }
    if (named_rig && wheels_ok) {
        const CarRigGeometry& g = *car->get_rig_geometry();
        std::cout << "ModelManager::load_car: named rig, wheelbase " << g.wheelbase_m << " m, track " << g.track_front_m
                  << " / " << g.track_rear_m << " m, tyre radius " << g.tyre_radius_front_m << " m" << std::endl;
    }
    car->set_light_anchors(std::move(light_anchors));
    car->set_wiper_blades(wiper_blades_from_mesh(car->get_mesh_data(), car->get_submeshes()));  // S1b
    for (const WiperBlade& b : car->get_wiper_blades())
        std::cout << "ModelManager::load_car: wiper arm reach " << std::sqrt(b.tip.x * b.tip.x + b.tip.z * b.tip.z)
                  << " m" << std::endl;

    return car;
}

}  // namespace swish
