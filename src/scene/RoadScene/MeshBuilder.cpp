#include "MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swish {

namespace {

// Index order per QuadWinding (values relative to the quad's first vertex).
constexpr uint32_t kQuadIndices[3][6] = {
    {0, 2, 1, 1, 2, 3},  // Cross
    {0, 1, 2, 1, 3, 2},  // Strip
    {0, 1, 2, 2, 1, 3},  // Fan
};

// Tangent for a strip quad running `from` -> `to`: the normalised direction, nudged by -1e-6 in Z so a
// zero-length step still normalises. Same arithmetic as the four hand-written copies it replaced.
Vec4 strip_tangent(const Vec3& from, const Vec3& to) {
    const Vec3 seg = glm::normalize(to - from + Vec3(0.0f, 0.0f, -1e-6f));
    return Vec4(seg.x, seg.y, seg.z, 1.0f);
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════
// MeshBuilder implementation
// ══════════════════════════════════════════════════════════════════════

void MeshBuilder::push_quad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3,
                            QuadWinding winding) {
    const uint32_t base = m_mesh.getVertexCount();
    m_mesh.addVertex(v0);
    m_mesh.addVertex(v1);
    m_mesh.addVertex(v2);
    m_mesh.addVertex(v3);
    for (const uint32_t k : kQuadIndices[static_cast<size_t>(winding)])
        m_mesh.addIndex(base + k);
}

void MeshBuilder::add_quad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3, QuadWinding winding,
                           const Vec4& color, MaterialId material) {
    const uint32_t indexOffset = m_mesh.getIndexCount();
    push_quad(v0, v1, v2, v3, winding);
    pushDrawCall(indexOffset, color, material);
}

void MeshBuilder::addZFace(float x0, float x1, float y0, float y1, float z, float nz, const Vec4& color,
                           MaterialId material) {
    const Vec3 n(0.0f, 0.0f, nz);
    const Vec4 tan(1.0f, 0.0f, 0.0f, 1.0f);
    add_quad({Vec3(x0, y0, z), n, Vec2(0.0f, 1.0f), tan}, {Vec3(x1, y0, z), n, Vec2(1.0f, 1.0f), tan},
             {Vec3(x0, y1, z), n, Vec2(0.0f, 0.0f), tan}, {Vec3(x1, y1, z), n, Vec2(1.0f, 0.0f), tan},
             nz > 0.0f ? QuadWinding::Fan : QuadWinding::Cross, color, material);
}

bool MeshBuilder::pushBatchDraw(uint32_t indexOffset, const Vec4& color, MaterialId material) {
    const uint32_t count = m_mesh.getIndexCount() - indexOffset;
    if (count == 0)
        return false;
    pushDrawCallRange(indexOffset, count, color, material);
    return true;
}

void MeshBuilder::pushDrawCall(uint32_t indexOffset, const Vec4& color, MaterialId material) {
    pushDrawCallRange(indexOffset, 6, color, material);
}

void MeshBuilder::pushDrawCallRange(uint32_t indexOffset, uint32_t indexCount, const Vec4& color, MaterialId material) {
    DrawCall dc{};
    dc.indexOffset = indexOffset;
    dc.indexCount  = indexCount;
    dc.color       = color;
    dc.model       = Mat4(1.0f);
    dc.material    = material;

    // Bounding sphere over this draw's index range, for per-draw culling. The
    // quad builders push exactly one quad (6 indices) just before this call;
    // the emit_sweep_* family pushes a whole band. Either way the AABB of the
    // referenced verts encloses the draw and the sphere is its half-diagonal.
    // Full-length surface quads get a huge radius (never culled); small props
    // get tight spheres. See DrawCall / SceneGeometry cull.
    const std::vector<uint32_t>& idx = m_mesh.getIndices();
    const std::vector<Vertex>&   vtx = m_mesh.getVertices();
    Vec3                         mn(std::numeric_limits<float>::max());
    Vec3                         mx(-std::numeric_limits<float>::max());
    for (uint32_t i = indexOffset; i < indexOffset + dc.indexCount && i < idx.size(); ++i) {
        const Vec3& p = vtx[idx[i]].position;
        mn            = glm::min(mn, p);
        mx            = glm::max(mx, p);
    }
    dc.boundsCenter = 0.5f * (mn + mx);
    dc.boundsRadius = 0.5f * glm::length(mx - mn);

    m_draws.push_back(dc);
}

void MeshBuilder::addHorizontalQuad(float leftX, float rightX, float y, float zStart, float zEnd, const Vec3& normal,
                                    const Vec4& color, MaterialId material, float tileSize) {
    // A horizontal quad is the sloped quad with equal edge heights: the emitted vertices are identical.
    addSlopedQuad(leftX, rightX, y, y, zStart, zEnd, normal, color, material, tileSize);
}

void MeshBuilder::addVerticalFace(float x, float height, float zStart, float zEnd, const Vec3& normal,
                                  const Vec4& color, MaterialId material, float tileSize) {
    Vec2 uvBotFar  = (tileSize > 0.0f) ? Vec2(zStart / tileSize, 0.0f) : Vec2(0.0f, 0.0f);
    Vec2 uvBotNear = (tileSize > 0.0f) ? Vec2(zEnd / tileSize, 0.0f) : Vec2(1.0f, 0.0f);
    Vec2 uvTopFar  = (tileSize > 0.0f) ? Vec2(zStart / tileSize, height / tileSize) : Vec2(0.0f, 1.0f);
    Vec2 uvTopNear = (tileSize > 0.0f) ? Vec2(zEnd / tileSize, height / tileSize) : Vec2(1.0f, 1.0f);

    // Tangent for vertical faces: along Z direction
    Vec4 vTan = Vec4(0.0f, 0.0f, 1.0f, 1.0f);

    // CCW winding for vertical faces in Vulkan clip space
    const bool facesLeft = (normal.x < 0.0f);
    add_quad({Vec3(x, 0.0f, zStart), normal, uvBotFar, vTan}, {Vec3(x, 0.0f, zEnd), normal, uvBotNear, vTan},
             {Vec3(x, height, zStart), normal, uvTopFar, vTan}, {Vec3(x, height, zEnd), normal, uvTopNear, vTan},
             facesLeft ? QuadWinding::Cross : QuadWinding::Fan, color, material);
}

void MeshBuilder::addDashedLine(float leftX, float rightX, float y, float zStart, float zEnd, float dashLen,
                                float gapLen, const Vec3& normal, const Vec4& color, MaterialId material,
                                float tileSize) {
    float cycle = dashLen + gapLen;
    for (float z = zStart; z + dashLen <= zEnd; z += cycle) {
        addHorizontalQuad(leftX, rightX, y, z, z + dashLen, normal, color, material, tileSize);
    }
}

void MeshBuilder::addSlopedQuad(float leftX, float rightX, float yLeft, float yRight, float zStart, float zEnd,
                                const Vec3& normal, const Vec4& color, MaterialId material, float tileSize) {
    Vec2 uv0, uv1, uv2, uv3;
    if (tileSize > 0.0f) {
        uv0 = Vec2(leftX / tileSize, zStart / tileSize);
        uv1 = Vec2(rightX / tileSize, zStart / tileSize);
        uv2 = Vec2(leftX / tileSize, zEnd / tileSize);
        uv3 = Vec2(rightX / tileSize, zEnd / tileSize);
    } else {
        uv0 = Vec2(0.0f, 0.0f);
        uv1 = Vec2(1.0f, 0.0f);
        uv2 = Vec2(0.0f, 1.0f);
        uv3 = Vec2(1.0f, 1.0f);
    }

    // Tangent for horizontal surfaces: along +X direction. CCW winding for upward-facing quads in Vulkan clip space.
    Vec4 hTan = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    add_quad({Vec3(leftX, yLeft, zStart), normal, uv0, hTan}, {Vec3(rightX, yRight, zStart), normal, uv1, hTan},
             {Vec3(leftX, yLeft, zEnd), normal, uv2, hTan}, {Vec3(rightX, yRight, zEnd), normal, uv3, hTan},
             QuadWinding::Cross, color, material);
}

void MeshBuilder::emit_ribbon(const std::vector<Vec3>& centerline, float halfWidth, const Vec4& color,
                              MaterialId material, float tileSize) {
    const size_t n = centerline.size();
    if (n < 2)
        return;

    const Vec3         up(0.0f, 1.0f, 0.0f);
    std::vector<Vec3>  L(n), R(n);    // left / right edge points
    std::vector<float> arc(n, 0.0f);  // cumulative arc length (for V tiling)

    for (size_t i = 0; i < n; ++i) {
        Vec3 tangent = (i == 0)       ? centerline[1] - centerline[0]
                       : (i == n - 1) ? centerline[n - 1] - centerline[n - 2]
                                      : centerline[i + 1] - centerline[i - 1];
        tangent.y    = 0.0f;  // keep the lateral offset horizontal even on a graded centreline
        if (glm::length(tangent) < 1e-4f)
            tangent = Vec3(0.0f, 0.0f, -1.0f);
        tangent    = glm::normalize(tangent);
        Vec3 right = glm::normalize(glm::cross(tangent, up));  // +X-ish for −Z travel
        L[i]       = centerline[i] - right * halfWidth;
        R[i]       = centerline[i] + right * halfWidth;
        if (i > 0)
            arc[i] = arc[i - 1] + glm::length(centerline[i] - centerline[i - 1]);
    }

    const float uR = (tileSize > 0.0f) ? (2.0f * halfWidth / tileSize) : 1.0f;

    for (size_t i = 0; i + 1 < n; ++i) {
        float v0   = (tileSize > 0.0f) ? arc[i] / tileSize : 0.0f;
        float v1   = (tileSize > 0.0f) ? arc[i + 1] / tileSize : 1.0f;
        Vec4  vtan = strip_tangent(centerline[i], centerline[i + 1]);

        // Up-facing: stations run near→far (+Z→−Z), the reverse of addHorizontalQuad's
        // vertex order, so the winding is flipped relative to it to keep the normal up.
        add_quad({L[i], up, Vec2(0.0f, v0), vtan}, {R[i], up, Vec2(uR, v0), vtan}, {L[i + 1], up, Vec2(0.0f, v1), vtan},
                 {R[i + 1], up, Vec2(uR, v1), vtan}, QuadWinding::Strip, color, material);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Swept cross-section primitives (Phase 3a real corridor)
// ══════════════════════════════════════════════════════════════════════

std::vector<SweepFrame> build_sweep_frames(const std::vector<Vec3>& centerline, const std::vector<Vec3>& tangents) {
    const size_t            n = std::min(centerline.size(), tangents.size());
    std::vector<SweepFrame> out(n);
    const Vec3              up(0.0f, 1.0f, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        Vec3 t = tangents[i];
        t.y    = 0.0f;  // keep the lateral offset horizontal (same rule as emit_ribbon)
        if (glm::length(t) < 1e-6f)
            t = Vec3(0.0f, 0.0f, -1.0f);
        t             = glm::normalize(t);
        out[i].center = centerline[i];
        out[i].right  = glm::normalize(glm::cross(t, up));  // +X-ish for −Z travel
        out[i].arc    = (i == 0) ? 0.0f : out[i - 1].arc + glm::length(centerline[i] - centerline[i - 1]);
    }
    return out;
}

bool MeshBuilder::emit_sweep_band(const std::vector<SweepFrame>& f, const std::vector<float>& latL,
                                  const std::vector<float>& latR, float y, const Vec4& color, MaterialId material,
                                  float tileSize) {
    const size_t n = f.size();
    if (n < 2 || latL.size() < n || latR.size() < n)
        return false;

    const uint32_t idx0 = m_mesh.getIndexCount();
    const Vec3     up(0.0f, 1.0f, 0.0f);

    for (size_t i = 0; i + 1 < n; ++i) {
        // A zero-width slice (e.g. a shoulder band where the taper has closed
        // it) would emit degenerate triangles; skip it rather than ship
        // zero-area geometry that still costs a vertex fetch.
        if (latR[i] - latL[i] < 1e-3f && latR[i + 1] - latL[i + 1] < 1e-3f)
            continue;

        const Vec3 aL = f[i].center + f[i].right * latL[i] + up * y;
        const Vec3 aR = f[i].center + f[i].right * latR[i] + up * y;
        const Vec3 bL = f[i + 1].center + f[i + 1].right * latL[i + 1] + up * y;
        const Vec3 bR = f[i + 1].center + f[i + 1].right * latR[i + 1] + up * y;

        // UVs from lateral offset and arc length, so the texture keeps a fixed
        // world scale around curves and across lane tapers.
        const float inv = (tileSize > 0.0f) ? 1.0f / tileSize : 0.0f;
        const Vec2  uAL(latL[i] * inv, f[i].arc * inv);
        const Vec2  uAR(latR[i] * inv, f[i].arc * inv);
        const Vec2  uBL(latL[i + 1] * inv, f[i + 1].arc * inv);
        const Vec2  uBR(latR[i + 1] * inv, f[i + 1].arc * inv);

        const Vec4 vtan = strip_tangent(aL, bL);

        // Same winding as emit_ribbon: stations run near→far, so it is flipped
        // relative to addHorizontalQuad to keep the normal up.
        push_quad({aL, up, uAL, vtan}, {aR, up, uAR, vtan}, {bL, up, uBL, vtan}, {bR, up, uBR, vtan},
                  QuadWinding::Strip);
    }

    return pushBatchDraw(idx0, color, material);
}

bool MeshBuilder::emit_sweep_wall(const std::vector<SweepFrame>& f, float lateral, float yBase, float height,
                                  bool facingRight, const Vec4& color, MaterialId material, float tileSize) {
    const size_t n = f.size();
    if (n < 2 || height <= 0.0f)
        return false;

    const uint32_t idx0 = m_mesh.getIndexCount();
    const Vec3     up(0.0f, 1.0f, 0.0f);

    for (size_t i = 0; i + 1 < n; ++i) {
        const Vec3 a   = f[i].center + f[i].right * lateral;
        const Vec3 b   = f[i + 1].center + f[i + 1].right * lateral;
        const Vec3 nrm = facingRight ? f[i].right : -f[i].right;

        const float inv = (tileSize > 0.0f) ? 1.0f / tileSize : 0.0f;
        const Vec2  uA0(f[i].arc * inv, 0.0f), uB0(f[i + 1].arc * inv, 0.0f);
        const Vec2  uA1(f[i].arc * inv, height * inv), uB1(f[i + 1].arc * inv, height * inv);

        const Vec4 vtan = strip_tangent(a, b);

        push_quad({a + up * yBase, nrm, uA0, vtan}, {b + up * yBase, nrm, uB0, vtan},
                  {a + up * (yBase + height), nrm, uA1, vtan}, {b + up * (yBase + height), nrm, uB1, vtan},
                  facingRight ? QuadWinding::Fan : QuadWinding::Cross);
    }

    return pushBatchDraw(idx0, color, material);
}

void MeshBuilder::emit_sweep_dashes(const std::vector<SweepFrame>& f, float latL, float latR, float y, float dashLen,
                                    float gapLen, float phase, const Vec4& color, MaterialId material, float tileSize) {
    const size_t n = f.size();
    if (n < 2 || dashLen <= 0.0f || gapLen < 0.0f)
        return;

    const float cycle = dashLen + gapLen;
    const float arc0  = f.front().arc;
    const float arc1  = f.back().arc;
    if (cycle <= 0.0f)
        return;

    // First dash start at or after arc0, on the global cycle set by `phase`.
    const float    k    = std::ceil((arc0 - phase) / cycle);
    const uint32_t idx0 = m_mesh.getIndexCount();
    const Vec3     up(0.0f, 1.0f, 0.0f);

    // Interpolate a frame at an arbitrary arc length inside this tile.
    auto frame_at = [&](float s) {
        size_t i = 0;
        while (i + 2 < n && f[i + 1].arc < s)
            ++i;
        const float seg = f[i + 1].arc - f[i].arc;
        const float u   = (seg > 1e-6f) ? std::clamp((s - f[i].arc) / seg, 0.0f, 1.0f) : 0.0f;
        SweepFrame  out;
        out.center = f[i].center + (f[i + 1].center - f[i].center) * u;
        out.right  = glm::normalize(f[i].right + (f[i + 1].right - f[i].right) * u);
        out.arc    = s;
        return out;
    };

    for (float s = phase + k * cycle; s + dashLen <= arc1; s += cycle) {
        const SweepFrame a = frame_at(s);
        const SweepFrame b = frame_at(s + dashLen);

        const Vec3 aL = a.center + a.right * latL + up * y;
        const Vec3 aR = a.center + a.right * latR + up * y;
        const Vec3 bL = b.center + b.right * latL + up * y;
        const Vec3 bR = b.center + b.right * latR + up * y;

        const float inv  = (tileSize > 0.0f) ? 1.0f / tileSize : 0.0f;
        const Vec4  vtan = strip_tangent(aL, bL);

        push_quad({aL, up, Vec2(latL * inv, s * inv), vtan}, {aR, up, Vec2(latR * inv, s * inv), vtan},
                  {bL, up, Vec2(latL * inv, (s + dashLen) * inv), vtan},
                  {bR, up, Vec2(latR * inv, (s + dashLen) * inv), vtan}, QuadWinding::Strip);
    }

    pushBatchDraw(idx0, color, material);
}

}  // namespace swish
