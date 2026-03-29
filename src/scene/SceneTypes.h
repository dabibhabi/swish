#pragma once

#include "../renderer/Vertex.h"
#include "../utils/Types.h"

#include <vector>

namespace swish {

// ── Material IDs ──────────────────────────────────────────────────────
enum MaterialId : uint32_t {
    MAT_ASPHALT  = 0,
    MAT_GRASS    = 1,
    MAT_CONCRETE = 2,
    MAT_METAL    = 3,
    MAT_DEFAULT  = 4,
    MAT_RUMBLE   = 5,
    MAT_DIRT     = 6,
    MAT_TREE     = 7,
    MAT_SIGN_0   = 8,
    MAT_SIGN_1   = 9,
    MAT_SIGN_2   = 10,
    MAT_SIGN_3   = 11,
    MAT_SIGN_4   = 12,
    MAT_SIGN_5   = 13,
    MAT_SIGN_6   = 14,
    MAT_SIGN_7   = 15,
    MAT_COUNT
};

// Matches the UBO in basic.vert/basic.frag (set=0, binding=0)
struct CameraUBO {
    Mat4 view;
    Mat4 proj;
    Vec4 camPos;    // xyz = camera world position, w = unused
    Vec4 sunDir;    // xyz = normalized sun direction, w = intensity
    Vec4 sunColor;  // rgb = sun color, a = ambient strength
};

// ── MeshData ──────────────────────────────────────────────────────────
// Encapsulates vertex and index buffers with controlled access.
// Vertices and indices are only added through push methods, ensuring
// the caller always gets correct base indices for quad construction.
class MeshData {
public:
    uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(m_indices.size()); }

    const std::vector<Vertex>&   getVertices() const { return m_vertices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    bool empty() const { return m_vertices.empty() || m_indices.empty(); }

    void addVertex(const Vertex& v) { m_vertices.push_back(v); }
    void addIndex(uint32_t i) { m_indices.push_back(i); }

private:
    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
};

// ── DrawCall ──────────────────────────────────────────────────────────
// One draw call = one material applied to a range of indices.
struct DrawCall {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    Vec4       color;
    Mat4       model;
    MaterialId material;
};

// Matches the expanded push constant block in both shaders
struct PushConstantData {
    Mat4 model;
    Vec4 color;
};

}  // namespace swish
