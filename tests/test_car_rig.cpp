// Named-rig car convention (tasks/todo.md "Rigged Porsche" S0c): the pure name /
// ancestry / frame logic in scene/Entity/CarRig.h, run on the real
// assets/blend/porsche_rigged.glb. GPU-free: tinygltf parses the GLB (images are not
// decoded) and this file adapts the nodes exactly as ModelManager::load_car does
// (world matrices from the scene roots, ground = lowest transformed vertex).

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include "scene/Entity/CarRig.h"

#include <tiny_gltf.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <string>
#include <vector>

using namespace swish;
using Catch::Approx;

namespace {

// The loaded rig in the shape the loader works with.
struct LoadedRig {
    tinygltf::Model               gltf;
    std::vector<std::string_view> names;
    std::vector<int>              parent;
    std::vector<Mat4>             world;
    std::vector<bool>             visited;
    float                         ground_y = std::numeric_limits<float>::max();
};

Mat4 local_matrix(const tinygltf::Node& n) {
    if (n.matrix.size() == 16)
        return Mat4(glm::make_mat4(n.matrix.data()));
    Mat4 t(1.f), r(1.f), s(1.f);
    if (n.translation.size() == 3)
        t = glm::translate(Mat4(1.f), Vec3(n.translation[0], n.translation[1], n.translation[2]));
    if (n.rotation.size() == 4)
        r = glm::mat4_cast(glm::quat(static_cast<float>(n.rotation[3]), static_cast<float>(n.rotation[0]),
                                     static_cast<float>(n.rotation[1]), static_cast<float>(n.rotation[2])));
    if (n.scale.size() == 3)
        s = glm::scale(Mat4(1.f), Vec3(n.scale[0], n.scale[1], n.scale[2]));
    return t * r * s;
}

void walk(LoadedRig& rig, int n, const Mat4& parent) {
    const auto i   = static_cast<size_t>(n);
    rig.world[i]   = parent * local_matrix(rig.gltf.nodes[i]);
    rig.visited[i] = true;
    for (int c : rig.gltf.nodes[i].children)
        walk(rig, c, rig.world[i]);
}

bool skip_images(tinygltf::Image*, const int, std::string*, std::string*, int, int, const unsigned char*, int, void*) {
    return true;
}

const LoadedRig& porsche_rig() {
    static const LoadedRig rig = [] {
        LoadedRig          r;
        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(&skip_images, nullptr);
        std::string err, warn;
        const bool  ok = loader.LoadBinaryFromFile(&r.gltf, &err, &warn, ASSET_DIR "blend/porsche_rigged.glb");
        REQUIRE(ok);
        const size_t n = r.gltf.nodes.size();
        r.parent.assign(n, -1);
        r.world.assign(n, Mat4(1.f));
        r.visited.assign(n, false);
        for (size_t i = 0; i < n; i++) {
            r.names.emplace_back(r.gltf.nodes[i].name);
            for (int c : r.gltf.nodes[i].children)
                r.parent[static_cast<size_t>(c)] = static_cast<int>(i);
        }
        const auto& scene = r.gltf.scenes[static_cast<size_t>(r.gltf.defaultScene >= 0 ? r.gltf.defaultScene : 0)];
        for (int root : scene.nodes)
            walk(r, root, Mat4(1.f));
        for (size_t i = 0; i < n; i++) {
            const auto& node = r.gltf.nodes[i];
            if (!r.visited[i] || node.mesh < 0)
                continue;
            for (const auto& prim : r.gltf.meshes[static_cast<size_t>(node.mesh)].primitives) {
                const auto& acc  = r.gltf.accessors[static_cast<size_t>(prim.attributes.at("POSITION"))];
                const auto& view = r.gltf.bufferViews[static_cast<size_t>(acc.bufferView)];
                const auto* p    = reinterpret_cast<const float*>(
                    r.gltf.buffers[static_cast<size_t>(view.buffer)].data.data() + view.byteOffset + acc.byteOffset);
                for (size_t v = 0; v < acc.count; v++)
                    r.ground_y = std::min(r.ground_y, (r.world[i] * Vec4(p[v * 3], p[v * 3 + 1], p[v * 3 + 2], 1.f)).y);
            }
        }
        return r;
    }();
    return rig;
}

std::array<WheelFrame, 4> recover_wheels(const LoadedRig& rig, const std::vector<CarRigTag>& tags) {
    std::array<int, 4> pivot{-1, -1, -1, -1}, spinning{0, 0, 0, 0};
    for (size_t i = 0; i < tags.size(); i++) {
        if (tags[i].role != CarRigRole::Wheel)
            continue;
        const auto c = static_cast<size_t>(tags[i].corner);
        if (tags[i].pivot == static_cast<int>(i))
            pivot[c] = static_cast<int>(i);
        else if (rig.gltf.nodes[i].mesh >= 0)
            spinning[c]++;
    }
    std::array<WheelFrame, 4> out{};
    for (int c = 0; c < 4; c++) {
        const auto cs = static_cast<size_t>(c);
        REQUIRE(pivot[cs] >= 0);
        const auto wf = car_rig_wheel_frame(rig.world[static_cast<size_t>(pivot[cs])], c, spinning[cs], rig.ground_y);
        REQUIRE(wf.has_value());
        out[cs] = *wf;
    }
    return out;
}

}  // namespace

TEST_CASE("named-rig pivots come from exact node names", "[car_rig]") {
    REQUIRE(car_rig_pivot_from_node("Wheel_FL").role == CarRigRole::Wheel);
    REQUIRE(car_rig_pivot_from_node("Wheel_RR").corner == 3);  // rig RR = BR
    REQUIRE(car_rig_pivot_from_node("Caliper_RL").role == CarRigRole::Caliper);
    REQUIRE(car_rig_pivot_from_node("Caliper_RL").corner == 2);
    REQUIRE(car_rig_pivot_from_node("SteeringWheel").role == CarRigRole::SteeringWheel);
    REQUIRE(car_rig_pivot_from_node("Needle_Speed").part == RigPart::NeedleSpeed);
    REQUIRE(car_rig_pivot_from_node("Wiper_Passenger").part == RigPart::WiperPassenger);
    // Child meshes and other conventions are not pivots.
    REQUIRE(car_rig_pivot_from_node("Wheel_FL_Tyre").role == CarRigRole::None);
    REQUIRE(car_rig_pivot_from_node("SteeringWheel_Mesh").role == CarRigRole::None);
    REQUIRE(car_rig_pivot_from_node("Steering_Wheel").role == CarRigRole::None);
    REQUIRE(car_rig_pivot_from_node("hub_FL").role == CarRigRole::None);
    REQUIRE(car_rig_pivot_from_node("wheel_fl").role == CarRigRole::None);

    REQUIRE(car_rig_detect({"Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"}));
    REQUIRE_FALSE(car_rig_detect({"Wheel_FL", "Wheel_FR", "Wheel_RL", "Caliper_RR"}));
    REQUIRE_FALSE(car_rig_detect({"hub_FL", "hub_FR", "hub_RL", "hub_RR", "Chassis"}));
}

TEST_CASE("sweep axes are local axes of the pivot frame", "[car_rig]") {
    REQUIRE(rig_part_local_axis(RigPart::NeedleRpm) == Vec3(0.f, 0.f, -1.f));
    REQUIRE(rig_part_local_axis(RigPart::NeedleSpeed) == Vec3(0.f, 0.f, -1.f));
    REQUIRE(rig_part_local_axis(RigPart::WiperDriver) == Vec3(0.f, 1.f, 0.f));
    REQUIRE(rig_part_local_axis(RigPart::None) == Vec3(0.f));
}

TEST_CASE("porsche_rigged.glb: all four corners recovered from the hub pivots", "[car_rig]") {
    const LoadedRig& rig = porsche_rig();
    REQUIRE(car_rig_detect(rig.names));
    REQUIRE(rig.ground_y == Approx(0.015f).margin(0.002f));

    const auto tags   = car_rig_tag_nodes(rig.names, rig.parent);
    const auto wheels = recover_wheels(rig, tags);
    for (int c = 0; c < 4; c++) {
        const WheelFrame& wf = wheels[static_cast<size_t>(c)];
        REQUIRE(wf.radius == Approx(0.320f).margin(0.003f));
        REQUIRE(glm::determinant(glm::mat3(wf.frame)) == Approx(1.f).margin(1e-5f));  // proper rotation
        // Car space: nose +X, right +Z. Corner order 0 FL · 1 FR · 2 BL · 3 BR.
        REQUIRE((wf.frame[3].x > 0.f) == (c < 2));
        REQUIRE((wf.frame[3].z > 0.f) == (c % 2 == 1));
        // The axle is local X; spin_sign rolls the wheel forward (same rule as the heuristic path).
        REQUIRE(std::abs(wf.frame[0].z) == Approx(1.f).margin(1e-4f));
        REQUIRE(wf.spin_sign == (wf.frame[0].z > 0.f ? -1.f : 1.f));
    }

    const CarRigGeometry g = measure_car_rig_geometry(wheels, true);
    REQUIRE(g.named_rig);
    REQUIRE(g.wheelbase_m == Approx(2.259f).margin(0.003f));
    REQUIRE(g.track_front_m == Approx(1.404f).margin(0.003f));
    REQUIRE(g.track_rear_m == Approx(1.404f).margin(0.003f));
    REQUIRE(g.tyre_radius_front_m == Approx(0.320f).margin(0.003f));
    REQUIRE(g.tyre_radius_rear_m == Approx(0.320f).margin(0.003f));
}

TEST_CASE("porsche_rigged.glb: wheel pieces spin, calipers only steer", "[car_rig]") {
    const LoadedRig& rig         = porsche_rig();
    const auto       tags        = car_rig_tag_nodes(rig.names, rig.parent);
    int              spinning[4] = {0, 0, 0, 0}, calipers[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < tags.size(); i++) {
        if (rig.gltf.nodes[i].mesh < 0)
            continue;
        if (tags[i].role == CarRigRole::Wheel)
            spinning[tags[i].corner]++;
        if (tags[i].role == CarRigRole::Caliper) {
            calipers[tags[i].corner]++;
            // A caliper is a sibling of its wheel, never under it: it must not inherit the spin.
            REQUIRE(rig.names[static_cast<size_t>(tags[i].pivot)].substr(0, 8) == "Caliper_");
        }
    }
    for (int c = 0; c < 4; c++) {
        REQUIRE(spinning[c] == 3);  // _Rim, _Rotor, _Tyre
        REQUIRE(calipers[c] == 1);  // _Body
    }
}

TEST_CASE("porsche_rigged.glb: steering wheel turns clockwise on right steer", "[car_rig]") {
    const LoadedRig& rig    = porsche_rig();
    const auto       tags   = car_rig_tag_nodes(rig.names, rig.parent);
    int              pivot  = -1;
    int              meshes = 0;
    for (size_t i = 0; i < tags.size(); i++) {
        if (tags[i].role != CarRigRole::SteeringWheel)
            continue;
        if (tags[i].pivot == static_cast<int>(i))
            pivot = static_cast<int>(i);
        else if (rig.gltf.nodes[i].mesh >= 0)
            meshes++;
    }
    REQUIRE(pivot >= 0);
    REQUIRE(meshes == 1);

    const Mat4 F = car_rig_steering_frame(rig.world[static_cast<size_t>(pivot)], rig.ground_y);
    REQUIRE(glm::determinant(glm::mat3(F)) == Approx(1.f).margin(1e-5f));
    // The column is close to the car's long axis and the pivot sits in the cabin, driver side (-Z).
    REQUIRE(std::abs(F[2].x) > 0.9f);
    REQUIRE(F[3].z < 0.f);

    // CarEntity::get_draw_calls: model = F * R_z(-sw_angle) * F^-1, sw_angle > 0 = right.
    // The rim's top must move toward the car's right (+Z): clockwise seen from the seat.
    const Mat4 R   = glm::rotate(Mat4(1.f), glm::radians(-10.f), Vec3(0.f, 0.f, 1.f));
    const Vec3 top = Vec3(F[3]) + 0.18f * Vec3(F[1]);
    const Vec3 moved(F * R * glm::inverse(F) * Vec4(top, 1.f));
    REQUIRE(moved.z > top.z);
}

TEST_CASE("porsche_rigged.glb: needles and wipers are tagged with their pivot frames", "[car_rig]") {
    const LoadedRig& rig      = porsche_rig();
    const auto       tags     = car_rig_tag_nodes(rig.names, rig.parent);
    int              count[5] = {0, 0, 0, 0, 0};
    for (size_t i = 0; i < tags.size(); i++) {
        if (tags[i].role != CarRigRole::Part || rig.gltf.nodes[i].mesh < 0)
            continue;
        count[static_cast<int>(tags[i].part)]++;
        const Mat4 F      = normalize_pivot_frame(rig.world[static_cast<size_t>(tags[i].pivot)], rig.ground_y);
        const Vec3 axis   = glm::normalize(glm::mat3(F) * rig_part_local_axis(tags[i].part));
        const bool needle = tags[i].part == RigPart::NeedleRpm || tags[i].part == RigPart::NeedleSpeed;
        if (needle)
            REQUIRE(axis.x < -0.9f);  // dial normal faces the driver (backward, -X)
        else
            REQUIRE(axis.y > 0.3f);  // spindle leans up off the cowl (S0a re-aligns it to the glass)
        REQUIRE(F[3].y > 0.8f);      // dash / cowl height above the ground
    }
    REQUIRE(count[static_cast<int>(RigPart::NeedleRpm)] == 1);
    REQUIRE(count[static_cast<int>(RigPart::NeedleSpeed)] == 1);
    REQUIRE(count[static_cast<int>(RigPart::WiperDriver)] == 1);
    REQUIRE(count[static_cast<int>(RigPart::WiperPassenger)] == 1);
}
