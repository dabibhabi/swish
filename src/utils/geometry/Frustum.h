#pragma once

// ── Frustum.h ────────────────────────────────────────────────────────
// Purpose   : view-frustum culling in one place: extract the six inward planes from a
//             view*projection matrix, then test bounding spheres against them.
// Units     : plane normals are unit length, plane.w and sphere radius are in the caller's
//             length unit (WU in the renderer), so `dot(n, c) + w` is a signed distance.
// Complexity: extract O(1) (six normalisations); sphere test O(6); no allocation.
// Precision : float; arithmetic is the legacy Renderer::extractFrustumPlanes and the plane loop
//             of SceneGeometry.cpp isCulled, moved verbatim.
// Example   : glm::vec4 planes[6];
//             namespace ug = swish::util::geometry;
//             ug::extract_frustum_planes(proj * view, planes);
//             if (ug::sphere_outside_frustum(planes, centre, radius)) { /* skip the draw */ }
// See also  : renderer/SceneGeometry/SceneGeometry.cpp (isCulled adds the distance test),
//             renderer/Renderer/Renderer_Record.cpp (main-view cull setup)
// Test      : tests/utils/test_geometry_Frustum.cpp

#include <glm/glm.hpp>
#include <span>

namespace swish::util::geometry {

// Gribb-Hartmann inward planes from a column-major view*proj. Vulkan clips z to [0,w]; reverse-Z
// only swaps which physical distance maps to 0 vs 1 and leaves the clip-space inequalities
// (-w<=x<=w, -w<=y<=w, 0<=z<=w) unchanged, so the standard extraction holds. Planes are
// normalised so `w` is a world-space distance and a sphere test can compare against its radius.
// Order: left, right, bottom, top, near, far.
inline void extract_frustum_planes(const glm::mat4& vp, glm::vec4 planes[6]) {
    // glm is column-major: row i = (vp[0][i], vp[1][i], vp[2][i], vp[3][i]).
    const glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    const glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    const glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    const glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
    planes[0] = r3 + r0;  // left   (x >= -w)
    planes[1] = r3 - r0;  // right  (x <=  w)
    planes[2] = r3 + r1;  // bottom (y >= -w)
    planes[3] = r3 - r1;  // top    (y <=  w)
    planes[4] = r2;       // near   (z >=  0, Vulkan)
    planes[5] = r3 - r2;  // far    (z <=  w)
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0f) planes[i] /= len;
    }
}

// True if the sphere (centre, radius) lies entirely outside at least one plane.
// Planes point inward; a sphere is outside a plane when its signed distance is < -radius.
inline bool sphere_outside_frustum(std::span<const glm::vec4, 6> planes, const glm::vec3& centre, float radius) {
    for (const glm::vec4& pl : planes) {
        if (glm::dot(glm::vec3(pl), centre) + pl.w < -radius) return true;
    }
    return false;
}

}  // namespace swish::util::geometry
