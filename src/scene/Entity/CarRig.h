#pragma once

// Named-rig car convention (tasks/todo.md "Rigged Porsche", S0c): the rig's pivot
// Empties are found by exact node name, and every mesh under one inherits its role.
// Header-only and GPU/GLFW-free (like CarLights.h) so tests/test_car_rig.cpp runs it
// on assets/blend/porsche_rigged.glb without a renderer. ModelManager::load_car is
// the only producer.
//
// Asset convention (glTF, Y-up, nose +Z, driver/left +X: the Porsche's authored frame,
// so no RootNode and no pre-rotation). All pivots are siblings under the scene root:
//   Wheel_FL/FR/RL/RR        hub pivot, axle = local X; _Rim/_Rotor/_Tyre children spin.
//   Caliper_FL/FR/RL/RR      at the same hub; steers and travels with the upright, never spins.
//   SteeringWheel            column = local Z.
//   Needle_RPM/Needle_Speed  sweep axis = local -Z (the dial normal, toward the driver).
//   Wiper_Driver/_Passenger  spindle = local +Y, parked at 0.
//
// Frames: every pivot is normalized like sw_pivot_frame / WheelFrame. Input is the
// pivot's world matrix in the authored frame (RootNode-relative where one exists);
// norm_matrix_y90 swaps (x,y,z) -> (z,y,-x) into car space (nose +X, up +Y, right +Z,
// metres), the origin drops by the mesh's ground height, and the columns lose their
// scale. Local axes survive the swap: column k of the normalized frame is the image
// of the node's local axis k.

#include "CarRigGeometry.h"
#include "scene/SceneTypes.h"  // WheelFrame, RigPart
#include "utils/types/Types.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace swish {

enum class CarRigRole : uint8_t { None, Wheel, Caliper, SteeringWheel, Part };

// One node's role in the named rig. `pivot` is the index of the pivot node the role
// came from (the node itself or its nearest named ancestor), -1 for None.
struct CarRigTag {
    CarRigRole role   = CarRigRole::None;
    int8_t     corner = -1;  // 0 FL · 1 FR · 2 BL · 3 BR (Wheel / Caliper only)
    RigPart    part   = RigPart::None;
    int        pivot  = -1;
};

// Pivot node name -> role, exact match (the child meshes `Wheel_FL_Rim` etc. are not pivots).
inline CarRigTag car_rig_pivot_from_node(std::string_view name) {
    static constexpr std::string_view kCorner[4] = {"FL", "FR", "RL", "RR"};  // rig RL/RR = BL/BR
    for (int8_t c = 0; c < 4; c++) {
        if (name.size() == 8 && name.substr(0, 6) == "Wheel_" && name.substr(6) == kCorner[c])
            return {CarRigRole::Wheel, c, RigPart::None, -1};
        if (name.size() == 10 && name.substr(0, 8) == "Caliper_" && name.substr(8) == kCorner[c])
            return {CarRigRole::Caliper, c, RigPart::None, -1};
    }
    if (name == "SteeringWheel")
        return {CarRigRole::SteeringWheel, -1, RigPart::None, -1};
    if (name == "Needle_RPM")
        return {CarRigRole::Part, -1, RigPart::NeedleRpm, -1};
    if (name == "Needle_Speed")
        return {CarRigRole::Part, -1, RigPart::NeedleSpeed, -1};
    if (name == "Wiper_Driver")
        return {CarRigRole::Part, -1, RigPart::WiperDriver, -1};
    if (name == "Wiper_Passenger")
        return {CarRigRole::Part, -1, RigPart::WiperPassenger, -1};
    return {};
}

// The convention is accepted when all four Wheel_* pivots exist.
inline bool car_rig_detect(const std::vector<std::string_view>& names) {
    bool seen[4] = {false, false, false, false};
    for (const auto n : names) {
        const CarRigTag t = car_rig_pivot_from_node(n);
        if (t.role == CarRigRole::Wheel)
            seen[t.corner] = true;
    }
    return seen[0] && seen[1] && seen[2] && seen[3];
}

// Each node's role by ancestry: the nearest named pivot at or above it. parent[i] is
// node i's parent index (-1 at a root); names and parent are the same length.
inline std::vector<CarRigTag> car_rig_tag_nodes(const std::vector<std::string_view>& names,
                                                const std::vector<int>&              parent) {
    std::vector<CarRigTag> out(names.size());
    for (size_t i = 0; i < names.size(); i++) {
        for (int n = static_cast<int>(i), hops = 0; n >= 0 && hops <= static_cast<int>(names.size());
             n = parent[static_cast<size_t>(n)], hops++) {
            CarRigTag t = car_rig_pivot_from_node(names[static_cast<size_t>(n)]);
            if (t.role != CarRigRole::None) {
                t.pivot = n;
                out[i]  = t;
                break;
            }
        }
    }
    return out;
}

// The authored frame (nose +Z, left +X) -> car space (nose +X, right +Z): (x,y,z) -> (z,y,-x)
// on every column. Shared with the heuristic Porsche path in ModelManager.
inline Mat4 norm_matrix_y90(const Mat4& M) {
    Mat4 out(1.f);
    for (int c = 0; c < 3; c++) {
        Vec3 v(M[c][0], M[c][1], M[c][2]);
        out[c] = Vec4(v.z, v.y, -v.x, 0.f);
    }
    Vec3 t(M[3][0], M[3][1], M[3][2]);
    out[3] = Vec4(t.z, t.y, -t.x, 1.f);
    return out;
}

// norm_matrix_y90, grounded by ground_y (the mesh's min y before grounding), scale-stripped.
inline Mat4 normalize_pivot_frame(const Mat4& authored, float ground_y) {
    Mat4 F = norm_matrix_y90(authored);
    F[3].y -= ground_y;
    for (int c = 0; c < 3; c++)
        F[c] = Vec4(glm::normalize(Vec3(F[c])), 0.f);
    return F;
}

// The sweep axis of a cabin part, as a LOCAL axis of its rig_pivot_frame.
inline Vec3 rig_part_local_axis(RigPart p) {
    switch (p) {
        case RigPart::NeedleRpm:
        case RigPart::NeedleSpeed:
            return Vec3(0.f, 0.f, -1.f);
        case RigPart::WiperDriver:
        case RigPart::WiperPassenger:
            return Vec3(0.f, 1.f, 0.f);
        case RigPart::None:
            break;
    }
    return Vec3(0.f);
}

// Hub pivot (authored frame) -> WheelFrame, or nullopt when a check trips. The frame
// is rebuilt orthonormal from the pivot's local X (the axle) so a mirrored or scaled
// pivot still yields a proper rotation: col0 = axle, col1 = up orthogonalized against
// it, col2 = col0 x col1. Same checks as the heuristic path: hub inside the named
// corner's box, axle within ~26 deg of the car's lateral (Z) axis, radius 0.25..0.45 m,
// and at least one spinning piece (the rig threshold, not the Porsche's 50).
inline std::optional<WheelFrame> car_rig_wheel_frame(const Mat4& pivot, int corner, int spinning_pieces,
                                                     float ground_y) {
    const Vec3 t(pivot[3]);
    const int  box_corner = (t.z > 0.f ? 0 : 2) + (t.x > 0.f ? 0 : 1);
    if (spinning_pieces < 1 || box_corner != corner || std::abs(t.x) < 0.4f || std::abs(t.x) > 1.2f ||
        std::abs(t.z) < 0.8f || std::abs(t.z) > 1.8f || t.y < 0.2f || t.y > 0.6f)
        return std::nullopt;

    const Vec3 axle = glm::normalize(Vec3(pivot[0]));
    const Vec3 up   = glm::normalize(Vec3(0.f, 1.f, 0.f) - axle * axle.y);
    Mat4       A(1.f);
    A[0] = Vec4(axle, 0.f);
    A[1] = Vec4(up, 0.f);
    A[2] = Vec4(glm::cross(axle, up), 0.f);
    A[3] = Vec4(t, 1.f);

    WheelFrame wf;
    wf.frame     = normalize_pivot_frame(A, ground_y);
    wf.radius    = wf.frame[3].y;  // grounded hub height = rolling radius (m)
    const Vec3 a = Vec3(wf.frame[0]);
    wf.spin_sign = a.z > 0.f ? -1.f : 1.f;
    if (std::abs(a.z) < 0.9f || wf.radius < 0.25f || wf.radius > 0.45f)
        return std::nullopt;
    return wf;
}

// SteeringWheel pivot (authored frame) -> sw_pivot_frame. CarEntity turns the wheel by
// R_z(-sw_angle) about the frame's local +Z, with sw_angle > 0 = steering right. Seen by
// the driver (looking along +X), that is clockwise only when +Z points BACK at the
// driver, so a column authored pointing forward is turned 180 deg about its local Y
// (still a proper rotation; the rest pose is unchanged).
inline Mat4 car_rig_steering_frame(const Mat4& pivot, float ground_y) {
    Mat4 F = normalize_pivot_frame(pivot, ground_y);
    if (F[2].x > 0.f) {
        F[0] = -F[0];
        F[2] = -F[2];
    }
    return F;
}

inline CarRigGeometry measure_car_rig_geometry(const std::array<WheelFrame, 4>& w, bool named_rig) {
    const auto     hub = [&](int c) { return Vec3(w[static_cast<size_t>(c)].frame[3]); };
    CarRigGeometry g;
    g.wheelbase_m         = 0.5f * (hub(0).x + hub(1).x) - 0.5f * (hub(2).x + hub(3).x);
    g.track_front_m       = std::abs(hub(1).z - hub(0).z);
    g.track_rear_m        = std::abs(hub(3).z - hub(2).z);
    g.tyre_radius_front_m = 0.5f * (w[0].radius + w[1].radius);
    g.tyre_radius_rear_m  = 0.5f * (w[2].radius + w[3].radius);
    g.named_rig           = named_rig;
    return g;
}

}  // namespace swish
