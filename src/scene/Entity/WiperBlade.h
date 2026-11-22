#pragma once

// Rigged wiper blades (tasks/todo.md "Rigged Porsche" S1b): where the Wiper_* arms of a
// named-rig car are, measured from the loaded mesh, and the per-frame wiper state the
// renderer reads. Header-only and GPU-free (tests/test_car_rig.cpp measures the real GLB).

#include "CarRig.h"  // rig_part_local_axis
#include "Wiper.h"
#include "scene/SceneTypes.h"
#include "utils/types/Types.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace swish {

// ── Blade geometry (named-rig cars) ──────────────────────────────────────────
// One rigged arm in car space (metres, nose +X, up +Y): its Wiper_* pivot frame
// (Submesh::rig_pivot_frame) and the arm tip, the mesh vertex farthest from the spindle,
// in that frame's LOCAL coordinates. The wiped part of the arm runs from
// kWiperBladeInnerFrac of the tip out to the tip: on the 993 RS mesh the rubber starts
// ~0.16 m out on a 0.53 m arm.
inline constexpr float kWiperBladeInnerFrac = 0.3f;

struct WiperBlade {
    Mat4    frame = Mat4(1.f);
    Vec3    tip   = Vec3(0.f);
    RigPart part  = RigPart::None;
};

// The car-space point at fraction `frac` of the way from the spindle to the tip, with the
// blade swept by `angle` (rad) about the spindle (the same rotation CarEntity draws).
[[nodiscard]] inline Vec3 wiper_blade_point(const WiperBlade& b, float angle, float frac) {
    return Vec3(b.frame * glm::rotate(Mat4(1.f), angle, rig_part_local_axis(b.part)) * Vec4(b.tip * frac, 1.f));
}

// The arm tip in the pivot frame's local coordinates: of the car-space points, the one
// farthest from the spindle axis (local Y), measured perpendicular to it.
[[nodiscard]] inline Vec3 wiper_tip_local(const Mat4& frame, const std::vector<Vec3>& car_points) {
    const Mat4 inv = glm::inverse(frame);
    Vec3       tip(0.f);
    float      best = -1.f;
    for (const Vec3& p : car_points) {
        const Vec3  l = Vec3(inv * Vec4(p, 1.f));
        const float r = l.x * l.x + l.z * l.z;
        if (r > best) {
            best = r;
            tip  = l;
        }
    }
    return tip;
}

// The rigged wiper arms of a loaded car (driver first), measured from the mesh. Empty for a
// car without Wiper_* pivots: the windshield pass then uses its screen-space blade.
[[nodiscard]] inline std::vector<WiperBlade> wiper_blades_from_mesh(const MeshData&             mesh,
                                                                    const std::vector<Submesh>& submeshes) {
    std::vector<WiperBlade> out;
    for (const RigPart part : {RigPart::WiperDriver, RigPart::WiperPassenger}) {
        std::vector<Vec3> pts;
        Mat4              frame(1.f);
        for (const Submesh& s : submeshes) {
            if (s.rig_part != part) continue;
            frame = s.rig_pivot_frame;
            for (uint32_t i = s.indexOffset; i < s.indexOffset + s.indexCount && i < mesh.getIndexCount(); i++)
                pts.push_back(mesh.getVertices()[mesh.getIndices()[i]].position);
        }
        if (!pts.empty()) out.push_back({frame, wiper_tip_local(frame, pts), part});
    }
    return out;
}

// One frame of wiper state for the renderer (App -> Renderer -> WindshieldRainPass).
struct WiperFrame {
    float                     angle    = 0.f;  // rad off park (wiper_angle)
    float                     sweep    = 0.f;  // rad, park -> top (maps the screen-space blade)
    bool                      active   = false;
    Mat4                      carModel = Mat4(1.f);  // car space -> world (carries the WU/m scale)
    std::array<WiperBlade, 2> blades{};
    uint32_t                  bladeCount = 0;  // 0 = no rigged blades: screen-space fallback
};

}  // namespace swish
