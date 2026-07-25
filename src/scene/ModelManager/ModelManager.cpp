// tinygltf is header-only. We provide one translation unit that defines
// the implementation. stb_image is already defined in StbImage.cpp, so
// we tell tinygltf not to re-include or re-define it.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE_WRITE

// tinygltf internally calls stbi_* functions; stb_image.h must be visible
// so it can see the declarations (implementation is in StbImage.cpp).
#include "ModelManager.h"

#include "../../renderer/Renderer/Renderer.h"
#include "../../renderer/TextureManager/TextureManager.h"
#include "../../renderer/Vertex.h"
#include "../../scene/SceneTypes.h"
#include "stb_image.h"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
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
        t = glm::translate(glm::mat4(1.f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    if (node.rotation.size() == 4)  // glTF order: x, y, z, w
        r = glm::mat4_cast(glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                     static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
    if (node.scale.size() == 3)
        s = glm::scale(glm::mat4(1.f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    return t * r * s;
}

// Depth-first walk computing each node's world transform.
void gltf_walk_nodes(const tinygltf::Model& model, int node_idx, const glm::mat4& parent, std::vector<glm::mat4>& world,
                     std::vector<bool>& visited) {
    const auto& node  = model.nodes[node_idx];
    glm::mat4   w     = parent * gltf_node_local_matrix(node);
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

// ── Road-wheel piece classification ────────────────────────────────
// The GLB has no per-wheel pivot nodes. Instead, ALL wheel geometry hangs
// under one node whose name starts with "Combined3DWheel" (tires/rims/
// discs — these spin) and all caliper geometry under "CombinedCalliperZone"
// (steer with the front uprights, never spin; the asset spells it with a
// double L). Membership is inherited down the subtree.
enum class WheelPieceClass : uint8_t { None, Spinning, Caliper };

void classify_wheel_nodes(const tinygltf::Model& model, int node_idx, WheelPieceClass current,
                          std::vector<WheelPieceClass>& out) {
    const auto& node = model.nodes[node_idx];
    if (node.name.rfind("Combined3DWheel", 0) == 0)
        current = WheelPieceClass::Spinning;
    else if (node.name.rfind("CombinedCalliperZone", 0) == 0)
        current = WheelPieceClass::Caliper;
    out[node_idx] = current;
    for (int child : node.children)
        classify_wheel_nodes(model, child, current, out);
}

template <typename T>
static void transcodeIndices(const tinygltf::Model& gltf, int accessor, size_t n_idx, uint32_t base_vertex,
                             MeshData& mesh) {
    const T* idx = gltf_accessor_data<T>(gltf, accessor);
    for (size_t i = 0; i < n_idx; i++)
        mesh.addIndex(base_vertex + static_cast<uint32_t>(idx[i]));
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
    int           num_mats         = std::min(static_cast<int>(gltf.materials.size()), kMaxCarMaterials);

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
    const auto&            scene = gltf.scenes[gltf.defaultScene >= 0 ? gltf.defaultScene : 0];
    for (int root : scene.nodes)
        gltf_walk_nodes(gltf, root, glm::mat4(1.f), node_world, node_visited);

    // Wheel/caliper membership by ancestry (see classify_wheel_nodes).
    std::vector<WheelPieceClass> node_wheel_class(gltf.nodes.size(), WheelPieceClass::None);
    for (int root : scene.nodes)
        classify_wheel_nodes(gltf, root, WheelPieceClass::None, node_wheel_class);

    glm::mat4 ref_inverse(1.f);

    // ── Steering wheel articulation ───────────────────────────────────
    // The Steering_Wheel node's origin sits at the hub center (confirmed in
    // Blender). Use that matrix as the conjugation frame:
    //   dc.model = car_model * (M * R_local(θ) * M^-1)
    // This rotates the already-baked vertices around their own hub in local
    // axis space. Scale is stripped before storing so M is a pure rotation +
    // translation and glm::inverse stays numerically stable.
    int       steering_wheel_idx = -1;
    glm::mat4 sw_pivot_frame(1.f);
    bool      found_sw_pivot = false;
    for (size_t ni = 0; ni < gltf.nodes.size(); ni++) {
        if (gltf.nodes[ni].name == "RootNode")
            ref_inverse = glm::inverse(node_world[ni]);
        if (gltf.nodes[ni].name == "Steering_Wheel")
            steering_wheel_idx = static_cast<int>(ni);
    }
    if (steering_wheel_idx >= 0 && node_visited[steering_wheel_idx]) {
        sw_pivot_frame = ref_inverse * node_world[steering_wheel_idx];
        found_sw_pivot = true;
    }

    // ── Build combined MeshData from all mesh nodes ───────────────────
    // Opaque and glass geometry share the same VBO/IBO. Glass primitives
    // (alphaMode=BLEND) are tracked in a separate submesh list so the
    // G-buffer pass can skip them while the GlassPass can draw them.
    MeshData             mesh;
    std::vector<Submesh> submeshes;       // opaque (G-buffer)
    std::vector<Submesh> glassSubmeshes;  // BLEND (forward transparent pass)

    // ── Road-wheel corner recovery ────────────────────────────────────
    // Wheel/caliper pieces are grouped by their frame translation: measured,
    // the piece translations take exactly four values (bit-identical within
    // a corner) = the wheel centers, in raw mesh space meters — front
    // ±(0.725, 0.345, 1.196), rear ±(0.708, 0.362, −1.256). Corner is the
    // sign pattern (+z = nose, +x = driver/left side); the spread and range
    // checks are tripwires for a future re-export, tripping a loud fallback
    // to the rigid pre-feature look rather than mis-pivoted wheels.
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

            const bool isGlass = (prim.material >= 0 && gltf.materials[prim.material].alphaMode == "BLEND");

            // Windshield = the OUTER exterior glass pane only ("Window_Geo"),
            // excluding the red taillight glass ("RED_GLASS"). The cabin-facing
            // inner pane ("WindowInside_Geo") is deliberately NOT tagged — painting
            // rain on it puts droplets inside the cabin (see issue.md §2a). Note
            // "WindowInside_Geo" does not contain the substring "Window_Geo", so
            // dropping that OR-term is sufficient to exclude it. The combined outer
            // mesh also holds side/rear glass; the windshield rain shader confines
            // drops to the forward-facing pane via a surface-normal mask.
            const bool isWindshield = isGlass && node.name.find("Window_Geo") != std::string::npos &&
                                      node.name.find("RED_GLASS") == std::string::npos;

            // Interior cabin geometry — node names "Kit1_Interior_Geo_..." and
            // "Kit1_InteriorTilling_Geo_..." both contain "Interior". Match
            // case-insensitively, but never tag glass (the cabin wash is opaque).
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
                } else {
                    v.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
                }
                mesh.addVertex(v);
            }

            // Indices — handle all common component types
            const auto& idx_acc = gltf.accessors[prim.indices];
            size_t      n_idx   = idx_acc.count;
            switch (idx_acc.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    transcodeIndices<uint32_t>(gltf, prim.indices, n_idx, base_vertex, mesh);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    transcodeIndices<uint16_t>(gltf, prim.indices, n_idx, base_vertex, mesh);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    transcodeIndices<uint8_t>(gltf, prim.indices, n_idx, base_vertex, mesh);
                    break;
                default:
                    break;
            }

            Submesh sm{};
            sm.indexOffset       = idx_start;
            sm.indexCount        = static_cast<uint32_t>(n_idx);
            sm.is_glass          = isGlass;
            sm.is_windshield     = isWindshield;
            sm.is_interior       = isInterior;
            sm.is_steering_wheel = !isGlass && found_sw_pivot && (node.name == "Steering_Wheel");

            // ── Road-wheel tagging + corner accumulation ──────────────
            if (!isGlass && wclass != WheelPieceClass::None) {
                const glm::vec3 t(xform[3]);
                // Plausibility window (meters, raw mesh space) around the
                // measured centers; anything outside invalidates recovery.
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
                            acc.frame            = xform;
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
                // Glass base color from the glTF material factor (RGBA).
                if (isGlass) {
                    const auto& bc = gltf.materials[prim.material].pbrMetallicRoughness.baseColorFactor;
                    sm.color = Vec4(static_cast<float>(bc[0]), static_cast<float>(bc[1]), static_cast<float>(bc[2]),
                                    static_cast<float>(bc[3]));
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

    // ── Normalize mesh space + bounding-box scan (single pass) ───────
    // The asset's nose points down mesh +Z, but the entity convention is
    // nose = +X at yaw 0, so that R_y(yaw)·(+X) equals the physics forward
    // vector and the body lines up with the direction of travel. Rotate
    // +90° about Y: (x, y, z) → (z, y, −x), directions included.
    // Track bb_min simultaneously so we avoid a second O(n) pass.
    glm::vec3 bb_min(std::numeric_limits<float>::max());
    glm::vec3 bb_max(std::numeric_limits<float>::lowest());
    for (auto& v : mesh.getVertices()) {
        v.position = glm::vec3(v.position.z, v.position.y, -v.position.x);
        v.normal   = glm::vec3(v.normal.z, v.normal.y, -v.normal.x);
        v.tangent  = glm::vec4(v.tangent.z, v.tangent.y, -v.tangent.x, v.tangent.w);
        bb_min     = glm::min(bb_min, v.position);
        bb_max     = glm::max(bb_max, v.position);
    }

    // ── Ground the model ──────────────────────────────────────────────
    // The glTF origin sits at axle height, not at the tire contact patch.
    // Shift all vertices so the lowest point lands at y = 0; then an entity
    // position of y = 0 means "tires resting on the road surface".
    for (auto& v : mesh.getVertices())
        v.position.y -= bb_min.y;

    if (found_sw_pivot) {
        sw_pivot_frame = norm_matrix_y90(sw_pivot_frame);
        sw_pivot_frame[3].y -= bb_min.y;
        // Strip accumulated scale from the pivot node so conjugation is
        // a pure rotation (M * R * M^-1). The SteeringWheel_Pivot carries
        // scale ≈ 0.1 which would make inverse(M) blow up to ≈ 10×.
        for (int c = 0; c < 3; c++)
            sw_pivot_frame[c] = glm::vec4(glm::normalize(glm::vec3(sw_pivot_frame[c])), 0.f);
        for (auto& sm : submeshes) {
            if (sm.is_steering_wheel)
                sm.sw_pivot_frame = sw_pivot_frame;
        }
    }

    // ── Finalize road-wheel frames (mirrors the sw_pivot treatment) ───
    std::array<WheelFrame, 4> wheel_frames{};
    for (int c = 0; wheels_ok && c < 4; c++) {
        const auto& acc = wheel_corners[c];
        // A healthy corner: seen, frame from a spinning piece, a real piece
        // population, and bit-identical translations (≤ 1 mm spread).
        if (!acc.seen || !acc.frame_from_wheel || acc.wheel_pieces < 50 || acc.max_spread > 1e-3f) {
            wheels_ok = false;
            break;
        }
        glm::mat4 F = norm_matrix_y90(acc.frame);
        F[3].y -= bb_min.y;
        for (int col = 0; col < 3; col++)
            F[col] = glm::vec4(glm::normalize(glm::vec3(F[col])), 0.f);

        WheelFrame wf;
        wf.frame  = F;
        wf.radius = F[3].y;  // grounded center height = rolling radius (m)
        // The axle is the frame-local +X. Forward rolling is a rotation about
        // −ẑ in car space (+X nose, +Y up, +Z right), so the sign flips with
        // the corner's baked 180° side flip.
        const glm::vec3 axle(glm::normalize(glm::vec3(F[0])));
        wf.spin_sign = axle.z > 0.f ? -1.f : 1.f;
        // The axle must be lateral-ish (camber only tilts it 1–2°) and the
        // radius must look like a 992 tire, else recovery is untrustworthy.
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

    auto car = std::make_unique<CarEntity>();
    car->set_mesh_data(std::move(mesh));
    for (const auto& sm : submeshes)
        car->add_submesh(sm);
    for (const auto& sm : glassSubmeshes)
        car->add_glass_submesh(sm);
    if (wheels_ok)
        car->set_wheel_frames(wheel_frames);

    return car;
}

}  // namespace swish
