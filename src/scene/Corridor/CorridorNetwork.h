#pragma once

#include "CorridorAlignment.h"
#include "CorridorGround.h"
#include "CorridorNetworkAsset.h"
#include "CorridorScene.h"
#include "CorridorSigns.h"
#include "CorridorViaduct.h"
#include "CorridorWorkZone.h"
#include "scene/RoadScene/RoadParams.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorNetwork — the Queens network at runtime (N2a, debug-only).
//
// Six alignments in one world frame (road 0 = the LIE, borrowed from App; the
// other five loaded from their N1 assets) plus the junction ramps as free
// paths: world-space polylines with height that join two alignments. The car
// is on exactly one piece (a road or a ramp) and changes piece only where the
// piece it is on stops holding it AND another one holds it at the same height
// (docs/corridor/tuning-notes.md N2a). So a road crossing overhead never grabs
// the car: it is not a candidate, and a ramp over the car is too high.
// Pure geometry (no Vulkan); the mesh lives in CorridorNetworkMesh.cpp.
// ══════════════════════════════════════════════════════════════════════

// One resampled ramp point, world frame (WU).
struct NetRampPoint {
    double x = 0.0, z = 0.0;
    float  y = 0.0f, tx = 0.0f, tz = -1.0f;
    float  u      = 0.0f;  // arc length from the first point
    float  ground = 0.0f;  // DEM ground under it (pier foot)
    float  bank   = 0.0f;  // cross slope: surface at lateral t is y - bank * t (the road's, where it rides on one)
};

struct NetRamp {
    std::vector<NetRampPoint> pts;
    float                     halfWidth = 0.0f;
    float                     pin[2]    = {0.0f, 0.0f};  // arc length lying over the attached road at each end
    double                    endS[2]   = {0.0, 0.0};    // attached road's station at each end (projection hint)
    uint8_t                   from = 255, to = 255, junction = 0;
    uint8_t                   lanes  = 1;      // N3: OSM lanes (a Y leg is two)
    bool                      dead   = false;  // N6: no road is reachable past its end
    bool                      closed = false;  // N6: a local exit off a network road (dead): EXIT CLOSED
    float                     stop   = 1e30f;  // N6: arc length a closed exit holds the car at (its barricades)
    double                    minX = 0.0, maxX = 0.0, minZ = 0.0, maxZ = 0.0;  // bbox incl. half width
    float                     length() const { return pts.empty() ? 0.0f : pts.back().u; }
    float                     end() const { return std::min(length(), stop); }  // drivable arc length
};

// Where the car is. road >= 0 and ramp < 0: on road `road` at (s, lat), committed to `side`. ramp >= 0: on that
// ramp at arc length s, lat right of its centreline.
struct NetPos {
    int    road = 0, ramp = -1;
    double s    = 0.0;
    float  lat  = 0.0f;
    int    side = +1;
    size_t hint = 0;  // road station index for the incremental projection
};

// One step's answer: the new piece, the car's (possibly clamped) world xz, the surface under it.
struct NetStep {
    NetPos pos;
    double x = 0.0, z = 0.0;
    float  y = 0.0f, dydx = 0.0f, dydz = 0.0f;
    bool   clamped = false, held = false;  // pushed back laterally / stopped along the piece (ramp end, wall)
};

class CorridorNetwork {
public:
    // Loads the network table and roads 1..5; `lie` (road 0) is borrowed and must outlive this. Throws like
    // load_corridor_asset. Builds the ramps (ends snapped to the roads, welded to each other).
    // N4: a road under a LIE-over bridge (lie->bridges(), kNetUnder) follows its trench and eases back over underBlend.
    // N5: hugEase = how close to its pin a ramp sunk into a road's shoulder is still lifted.
    // N6: maxGrade = the ramp grade solve's limit; exitGore = a closed exit's barricades this far past its gore.
    void load(const std::string& networkPath, const std::string& assetDir, const CorridorAlignment* lie,
              float underBlend = util::units::m(road::network::kUnderBlend),
              float hugEase = util::units::m(road::network::kHugEase), float maxGrade = road::network::kMaxGrade,
              float exitGore = util::units::m(road::workzone::kExitGore));
    bool valid() const { return m_lie != nullptr; }

    size_t                      road_count() const { return m_roads.size(); }
    const CorridorAlignment&    road(int r) const { return r == 0 ? *m_lie : m_roads[static_cast<size_t>(r)]; }
    const char*                 road_key(int r) const { return m_asset.roads[static_cast<size_t>(r)].key; }
    const std::vector<NetRamp>& ramps() const { return m_ramps; }
    const CorridorNetworkAsset& asset() const { return m_asset; }
    // Drivable station range of road r: between its work-zone walls (else the data ends, minus a margin).
    void drivable(int r, double& lo, double& hi) const;

    // Ramp queries: nearest arc length (unclamped past the ends, along the end tangents) and signed lateral.
    void  ramp_project(const NetRamp& rp, double x, double z, double& u, float& lat) const;
    float ramp_y(const NetRamp& rp, double u) const;
    // Surface height at arc length u, lateral lat (bank included).
    float ramp_surface(const NetRamp& rp, double u, float lat) const;
    float ramp_grade(const NetRamp& rp, double u) const;

    // A ramp holding world (x, z) within kHandoffDy of height y, reached from road `road`; -1 when none. N3: with a
    // heading (hx, hz), only a ramp running that way (ramps are one-way); (0, 0) = any.
    int enter_ramp(int road, double x, double z, float y, float inset, NetPos& out, float hx = 0.0f,
                   float hz = 0.0f) const;

    // Advance the car from `cur` to world (x, z) with surface height y last frame: stay on the current piece while it
    // holds the car, else hand off to a piece that holds it at the same height, else clamp to the current piece.
    NetStep step(const NetPos& cur, double x, double z, float y, float inset) const;

    // Car pose on road r at station s, right-hand lane of the +s carriageway.
    NetPos place(int r, double s) const;

    // N2b: the closure at every wall (the U-turn gaps and paint ranges are set on the roads at load).
    std::vector<WorkZone> work_zones(const WorkZoneParams& p) const;
    // N3: viaducts and trenches of road r (from its GRND block; their verge is cut at load).
    const std::vector<RoadStructure>& structures(int r) const { return m_structs[static_cast<size_t>(r)]; }

    // N7: builds the network ground and dresses roads 1..5 from their LOOK rows: D5 kits (density, far = the D5
    // sliders), lamps, W-beam / sound walls (wallH) off the ramps and structures, verge cuts where a ramp runs over a
    // verge; graves = the D6 headstone density. The LIE (borrowed, const) gets the ground through App (keep-out only).
    void                  dress(float density, float far, float wallH, float graves = 0.0f);
    const CorridorGround& ground() const { return m_ground; }

private:
    void build_ramps();
    void clear_ramps();  // N3: vertical clearance at crossings
    void grade_ramps();  // N6: grade-limited profile (kMaxGrade) inside the clearance bounds

    const CorridorAlignment*                m_lie = nullptr;
    std::vector<CorridorAlignment>          m_roads;  // [0] unused (the LIE is borrowed)
    CorridorNetworkAsset                    m_asset;
    std::vector<NetRamp>                    m_ramps;
    std::vector<std::vector<RoadStructure>> m_structs;          // per road, N3
    std::vector<std::vector<int>>           m_siblings;         // per ramp: ramps sharing its start or end (N3 forks)
    float                                   m_hugEase  = 0.0f;  // N5
    float                                   m_maxGrade = road::network::kMaxGrade, m_exitGore = 0.0f;  // N6
    CorridorGround                          m_ground;                                                  // N7
};

// N3-N6 guide signs of road r (1..5): its kGuideSigns rows, then every exit (closed or real) by add_exit_signs.
CorridorSigns network_signs(const CorridorNetwork& net, int r);
// N6: the ramp leaving road r for the exit record at station s on carriageway side (gore -1 = none leaves there).
ExitSite net_exit_site(const CorridorNetwork& net, int r, double s, int side);

// N9 (CorridorNetHud.cpp): what the HUD strip says off the LIE. tab: "EXIT 39", "EXIT 39 CLOSED", "EXIT CLOSED",
// "ROAD ENDS", "TO" (empty = nothing ahead); warn = orange (work zone); dist in real WU (compaction undone).
struct NetHud {
    std::string district, shield, road;  // "Queens"; "I-278" ("" = parkway or ramp); "BQE E" / "Ramp"
    std::string tab, top, dest;          // top = shield line ("I-678 S"), dest = place names (" \xC2\xB7 "-joined)
    double      dist = 0.0;
    bool        warn = false;
};
// The signed cardinal direction of road r for a car on carriageway side (+1 = rising s): 'N' 'S' 'E' 'W'.
char   net_direction(int r, int side);
NetHud network_hud(const CorridorNetwork& net, const NetPos& p, float roadEndsWu);
// Minimap lines, true world x, z: roads 1..5 over their drivable span, then every ramp.
std::vector<std::vector<Vec2>> network_map_lines(const CorridorNetwork& net);

// N8 (CorridorNetLandmarks.cpp): decision 9 landmarks from the LMRK block, one segment each (bounds unset); the Throgs
// Neck segment has `far` set (the driver scales it about the camera like the D5 skyline).
std::vector<CorridorSegment> network_landmark_segments(const CorridorNetwork& net, const CorridorLook& look,
                                                       const StructureParams& sp);
// N8: a landmark's lateral on its road: a building (gas station, stadiums, terminal) slides to the nearest lateral, 10
// m steps, whose footprint clears every road and ramp (the compaction can move a ramp under it); others as baked.
float landmark_lat(const CorridorNetwork& net, const NetworkLandmark& l);
// N8: road r's OSM crossing at s is drawn by a landmark (the Jamaica Yard bridge), not by emit_overpass.
bool landmark_replaces(const CorridorNetwork& net, int r, double s);
// N8: keep-out circles (world x, z, radius; WU) over landmark footprints for trees and buildings (CorridorGround).
std::vector<std::array<double, 3>> landmark_keepout(const CorridorNetwork& net);

// Mesh: roads 1..5 through build_corridor_segments, one segment per junction for its ramps, one per work zone
// (CorridorNetworkMesh.cpp).
std::vector<CorridorSegment> build_network_segments(const CorridorNetwork& net, const WorkZoneParams& wz = {},
                                                    const StructureParams& sp = {});

}  // namespace swish
