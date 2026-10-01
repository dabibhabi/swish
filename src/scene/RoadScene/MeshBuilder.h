#pragma once

#include "scene/SceneTypes.h"

#include <cstdint>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// MeshBuilder — Encapsulates vertex/index buffer construction.
// ══════════════════════════════════════════════════════════════════════

// Index order of the two triangles of a 4-vertex quad (v0 v1 / v2 v3 = two rows of the quad). The three
// orders the builders have always used differ in triangle winding and in the diagonal, so they stay
// distinct (goldens pin the exact indices):
//   Cross : 0 2 1 | 1 2 3   upward-facing horizontal quads, and vertical faces whose normal points -X / -Z
//   Strip : 0 1 2 | 1 3 2   stations run near -> far (ribbon, sweep band, sweep dashes)
//   Fan   : 0 1 2 | 2 1 3   vertical faces whose normal points +X / +Z
enum class QuadWinding : uint8_t { Cross, Strip, Fan };

class MeshBuilder {
public:
    MeshBuilder(MeshData& mesh, std::vector<DrawCall>& draws) : m_mesh(mesh), m_draws(draws) {}

    // ── Shared quad emission ──────────────────────────────────────────
    // push_quad appends 4 vertices then 6 indices (see QuadWinding); add_quad also records the one-quad draw
    // call. Every quad the builder or a RoadScene generator emits goes through these, so the index order and
    // the draw-call bounds live in one place. Vertices are the caller's, unmodified (no arithmetic here).
    void push_quad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3, QuadWinding winding);
    void add_quad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3, QuadWinding winding,
                  const Vec4& color, MaterialId material);

    // Rectangle in a constant-Z plane spanning x0..x1, y0..y1, unit UVs (v = 1 at y0), normal (0, 0, nz). The
    // bridge fascia, parapets and pier ends face the approaching driver (+Z / -Z), which addVerticalFace
    // (constant X) cannot do. nz > 0 uses Fan winding, otherwise Cross.
    void addZFace(float x0, float x1, float y0, float y1, float z, float nz, const Vec4& color,
                  MaterialId material = MAT_DEFAULT);

    void addHorizontalQuad(float leftX, float rightX, float y, float zStart, float zEnd, const Vec3& normal,
                           const Vec4& color, MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    void addVerticalFace(float x, float height, float zStart, float zEnd, const Vec3& normal, const Vec4& color,
                         MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    void addDashedLine(float leftX, float rightX, float y, float zStart, float zEnd, float dashLen, float gapLen,
                       const Vec3& normal, const Vec4& color, MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    void addSlopedQuad(float leftX, float rightX, float yLeft, float yRight, float zStart, float zEnd,
                       const Vec3& normal, const Vec4& color, MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    // Ribbon primitive: a drivable surface swept along a 3D centreline. Walks the
    // polyline, offsets each station ±halfWidth along the horizontal lateral
    // normal, and emits an up-facing triangle strip (one quad = one draw call, so
    // it culls like everything else) with UVs tiled by arc length. This is the
    // building block for curved ramps / connectors (Layer 3) and graded interchange
    // ramps (Layer 4); a straight centreline reproduces a flat lane. `up`-based
    // lateral means it stays horizontal even where the centreline climbs.
    void emit_ribbon(const std::vector<Vec3>& centerline, float halfWidth, const Vec4& color,
                     MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    // ── Swept cross-section primitives (Phase 3a real corridor) ───────
    // Generalisations of emit_ribbon for an irregular, real-world alignment
    // whose cross-section varies along the route (lane-count tapers). Two
    // differences from emit_ribbon, both required by a 18 mi corridor:
    //   • the lateral extent is PER-STATION and ASYMMETRIC (latL[i]..latR[i]),
    //     not a single symmetric halfWidth, so one band can be "lane 3's
    //     asphalt" and taper as lanes are added;
    //   • the whole sweep is ONE draw call, not one per quad.
    // Frames come from build_sweep_frames so every band in a cross-section
    // shares identical tangents (no slivers between neighbours).
    //
    // `y` is the surface height; `latL` must be <= `latR` for an up-facing
    // normal. Returns false (emitting nothing) for a degenerate input.
    bool emit_sweep_band(const std::vector<SweepFrame>& frames, const std::vector<float>& latL,
                         const std::vector<float>& latR, float y, const Vec4& color, MaterialId material = MAT_DEFAULT,
                         float tileSize = 0.0f);

    // A vertical wall swept along the frames at a fixed lateral offset, from
    // `yBase` to `yBase + height`. `facingRight` picks the winding so the
    // normal points along +right (true) or -right (false).
    bool emit_sweep_wall(const std::vector<SweepFrame>& frames, float lateral, float yBase, float height,
                         bool facingRight, const Vec4& color, MaterialId material = MAT_DEFAULT, float tileSize = 0.0f);

    // Dashes laid along the frames' arc length: one quad per dash, all
    // batched into a single draw call. `phase` is the arc-length offset of
    // the first dash's start, so consecutive tiles continue the same cycle
    // instead of restarting it (the intro->chunk seam bug from Layer 1).
    void emit_sweep_dashes(const std::vector<SweepFrame>& frames, float latL, float latR, float y, float dashLen,
                           float gapLen, float phase, const Vec4& color, MaterialId material = MAT_DEFAULT,
                           float tileSize = 0.0f);

private:
    void pushDrawCall(uint32_t indexOffset, const Vec4& color, MaterialId material);

    // Same as pushDrawCall but for a multi-quad range (pushDrawCall is the
    // indexCount == 6 special case). The real-corridor generator batches a
    // whole cross-section band across a tile into ONE draw so a 18 mi road
    // costs thousands of draws, not hundreds of thousands — the bounding
    // sphere is still computed from the actual index range, so per-draw
    // frustum/distance culling works exactly as before.
    void pushDrawCallRange(uint32_t indexOffset, uint32_t indexCount, const Vec4& color, MaterialId material);

    // Closes an emit_sweep_* batch that began at index `indexOffset`: records ONE draw for everything emitted
    // since, or returns false (recording nothing) when nothing was.
    bool pushBatchDraw(uint32_t indexOffset, const Vec4& color, MaterialId material);

    MeshData&              m_mesh;
    std::vector<DrawCall>& m_draws;
};

// Builds the shared per-station frames for the emit_sweep_* family from a
// centreline and its unit tangents (both in the same local space).
std::vector<SweepFrame> build_sweep_frames(const std::vector<Vec3>& centerline, const std::vector<Vec3>& tangents);

}  // namespace swish
