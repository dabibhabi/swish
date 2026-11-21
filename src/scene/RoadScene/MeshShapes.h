#pragma once

// MeshShapes: small solid shapes on top of MeshBuilder (boxes, an oriented box, a ball), for furniture that is
// not a swept band: lamp poles and heads, tunnel walls, overpass decks, calibration cards. Every face points
// outward, so a shape shades and casts sun shadows from any side. Header-only, no Vulkan.
// Used by scene/LightingLab and the corridor street lamps (scene/Corridor/CorridorScene.cpp).

#include "scene/RoadScene/MeshBuilder.h"
#include "utils/units/Units.h"

#include <array>
#include <cmath>
#include <utility>

namespace swish::mesh_shapes {

// One quad with corners p0, p1 (first row) and p2, p3 (second row, p2 beside p0). The winding is chosen so the
// front face points along `n` (front = (p1 - p0) x (p2 - p0), the convention MeshBuilder::addZFace uses).
// `normals` overrides the per-vertex normals (smooth shading); UVs repeat every 4 m.
inline void quad(MeshBuilder& b, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, const Vec3& n, const Vec4& color, MaterialId mat,
                 const std::array<Vec3, 4>* normals = nullptr) {
    std::array<Vec3, 4> nn = normals ? *normals : std::array<Vec3, 4>{n, n, n, n};
    if (glm::dot(glm::cross(p1 - p0, p2 - p0), n) < 0.0f) {
        std::swap(p0, p1);
        std::swap(p2, p3);
        std::swap(nn[0], nn[1]);
        std::swap(nn[2], nn[3]);
    }
    const Vec3  t    = glm::normalize(p1 - p0);
    const Vec3  bt   = glm::normalize(p2 - p0);
    const float tile = util::units::m(4.0f);
    const auto  uv   = [&](const Vec3& p) { return Vec2(glm::dot(p - p0, t) / tile, glm::dot(p - p0, bt) / tile); };
    const Vec4  tan(t, 1.0f);
    b.add_quad({p0, nn[0], uv(p0), tan}, {p1, nn[1], uv(p1), tan}, {p2, nn[2], uv(p2), tan}, {p3, nn[3], uv(p3), tan},
               QuadWinding::Fan, color, mat);
}

// A box given by its centre and three half-extent vectors (mutually perpendicular; any orientation). WU.
// Emits six draw calls (one per face).
inline void oriented_box(MeshBuilder& b, const Vec3& c, const Vec3& hx, const Vec3& hy, const Vec3& hz,
                         const Vec4& color, MaterialId mat) {
    const auto face = [&](const Vec3& axis, const Vec3& u, const Vec3& v) {
        const Vec3 o = c + axis;
        quad(b, o - u - v, o + u - v, o - u + v, o + u + v, glm::normalize(axis), color, mat);
    };
    face(hx, hy, hz);
    face(-hx, hy, hz);
    face(hy, hx, hz);
    face(-hy, hx, hz);
    face(hz, hx, hy);
    face(-hz, hx, hy);
}

// An axis-aligned box from its low and high corners. WU.
inline void box(MeshBuilder& b, const Vec3& lo, const Vec3& hi, const Vec4& color, MaterialId mat) {
    const Vec3 h = 0.5f * (hi - lo);
    oriented_box(b, 0.5f * (lo + hi), Vec3(h.x, 0.0f, 0.0f), Vec3(0.0f, h.y, 0.0f), Vec3(0.0f, 0.0f, h.z), color, mat);
}

// A smooth-shaded ball (latitude / longitude quads). WU.
inline void ball(MeshBuilder& b, const Vec3& c, float r, const Vec4& color, MaterialId mat) {
    constexpr int kRings = 12, kSegs = 24;
    const auto    dir = [](int ring, int seg) {
        const float th = util::units::kPi * static_cast<float>(ring) / kRings;
        const float ph = 2.0f * util::units::kPi * static_cast<float>(seg) / kSegs;
        return Vec3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
    };
    for (int i = 0; i < kRings; ++i) {
        for (int j = 0; j < kSegs; ++j) {
            const std::array<Vec3, 4> n{dir(i, j), dir(i, j + 1), dir(i + 1, j), dir(i + 1, j + 1)};
            const Vec3                faceN = glm::normalize(n[0] + n[1] + n[2] + n[3]);
            Vec3                      p0 = c + r * n[0], p1 = c + r * n[1], p2 = c + r * n[2], p3 = c + r * n[3];
            // At a pole a row collapses to a point; keep a sliver so the tangent stays finite.
            if (i == 0) p1 = p0 + (p3 - p2) * 1.0e-3f;
            if (i == kRings - 1) p3 = p2 + (p1 - p0) * 1.0e-3f;
            quad(b, p0, p1, p2, p3, faceN, color, mat, &n);
        }
    }
}

}  // namespace swish::mesh_shapes
