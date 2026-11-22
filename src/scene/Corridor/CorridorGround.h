#pragma once

#include "CorridorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace swish {

class CorridorNetwork;
struct CorridorLook;

// ══════════════════════════════════════════════════════════════════════
// CorridorGround — one continuous grass surface under the Queens network (N7, debug only).
//
// Before N7 the network had ground only on each road's own verge strip: past it the void showed, and a verge on a
// road above the DEM floated as a plateau. The field here is the DEM (each road's GRND, the LIE's profile, each
// ramp's pier foot) blended by inverse distance, then held between two slopes of every at-grade pavement:
//
//   H = min( max(H_dem, max_f L_f), min_f U_f ),   U_f = y_f - sink + kCut * e,   L_f = y_f - sink - kFill * e
//
// with e the distance past the pavement band (plus one cell diagonal, so a grid triangle touching the band never
// pokes through it). Cut wins over fill: the field never covers pavement. A fine grid (kGroundFine) runs round the
// network, a coarse one (kGroundCoarse, kGroundDrop lower) out to the far plane. Pure geometry, no Vulkan.
// ══════════════════════════════════════════════════════════════════════
class CorridorGround {
public:
    void build(const CorridorNetwork& net);
    bool valid() const { return m_fineN[0] > 0; }

    // Surface height at world (x, z), WU: the fine grid where it is drawn, else the coarse one.
    float height(double x, double z) const;
    // Distance from world (x, z) to the nearest pavement edge not of road `road` (ramps always count), WU; 1e9 when
    // none is within kGroundReach.
    float clearance(double x, double z, int road) const;

    // Grass tiles: fine 32 x 32 cells, coarse 16 x 16, one draw each.
    std::vector<CorridorSegment> segments(const CorridorLook& look) const;

private:
    struct Pave {
        double x, z;
        float  y, hw, sink;  // band [0, hw] from (x, z) at height y
        float  kw;           // keep-out half width for dressing (< 0 = none)
        int    road;         // -1 = a ramp
        bool   bound, fill;  // bounds the field (at grade or a trench); pins it up (at grade)
    };
    struct Dem {
        double x, z;
        float  y;
    };
    template <class T>
    struct Hash {
        double                                      cell = 1.0;
        std::unordered_map<int64_t, std::vector<T>> m;
        int64_t key(int i, int j) const { return (static_cast<int64_t>(i) << 32) ^ static_cast<uint32_t>(j); }
        void    add(const T& v) {
            m[key(static_cast<int>(std::floor(v.x / cell)), static_cast<int>(std::floor(v.z / cell)))].push_back(v);
        }
        template <class F>
        void near(double x, double z, double r, F&& f) const {
            const int i0 = static_cast<int>(std::floor((x - r) / cell)),
                      i1 = static_cast<int>(std::floor((x + r) / cell));
            const int j0 = static_cast<int>(std::floor((z - r) / cell)),
                      j1 = static_cast<int>(std::floor((z + r) / cell));
            for (int i = i0; i <= i1; ++i)
                for (int j = j0; j <= j1; ++j)
                    if (const auto it = m.find(key(i, j)); it != m.end())
                        for (const T& v : it->second) f(v);
        }
    };

    float dem(double x, double z) const;
    float eval(double x, double z, float diag) const;  // the field at one vertex; `diag` = its grid's cell diagonal
    bool  fine(int i, int j) const;                    // fine cell (i, j) drawn
    // Height inside cell (i, j) of a grid (cell size c, vertex heights h, nx + 1 per row) at local (u, v) in [0, 1],
    // on the mesh's own triangles (a c b, b c d).
    static float tri(const std::vector<float>& h, int nx, int i, int j, float u, float v);

    Hash<Pave>         m_pave;
    Hash<Dem>          m_dem;
    float              m_maxHw = 0.0f, m_demMean = 0.0f;
    double             m_x0 = 0.0, m_z0 = 0.0;  // grid origin (both grids)
    int                m_fineN[2] = {0, 0}, m_coarseN[2] = {0, 0};
    std::vector<char>  m_fine;              // fine cell marks, row-major in z
    std::vector<float> m_fineH, m_coarseH;  // vertex heights (fine: NaN where no drawn cell uses the vertex)
};

}  // namespace swish
