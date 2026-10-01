#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorAsset — the baked real-world LIE alignment (Phase 3a).
//
// Produced OFFLINE by tools/lie_corridor.py from an OpenStreetMap extract
// run through SUMO netconvert, then compacted with the landmark-anchored
// heuristic (docs/phase-3-osm-lie-corridor.md §4). The pipeline is:
//
//   Overpass extract  ->  netconvert (.edg/.nod, lon/lat)
//     ->  local tangent plane + compaction  ->  THIS FILE  ->  engine
//
// Both the extract and netconvert's output are cached in data/osm/, and this
// binary is checked in, so a build needs neither the network nor SUMO.
//
// Same contract as RoadConfig/toml_baker: #pragma pack(1), read with one
// fread per block, magic + version checked at load. If you change a field
// here you MUST change the matching struct.pack format in
// tools/lie_corridor.py::write_asset and re-bake.
//
// ── Frames and units ─────────────────────────────────────────────────
// Everything is world units (1 scene-metre = 1000 WU).
//
// Station positions are DOUBLE. Measured, not assumed (spec §7 asked for
// exactly this check): 18 mi = 2.897e7 WU, where float32's ULP is 2.0 WU —
// 2 mm, not the 2 m the design note's ~3e10 WU estimate implied (that figure
// applied WORLD_SCALE twice; 1 mile is 1.609e6 WU, so 18 mi is 2.9e7, not
// 2.9e10). So float32 would in fact survive here. Double is kept anyway
// because it costs 81 KB for the whole corridor and this asset is the shared
// source of truth for BOTH the mesh and the physics — a millimetre of
// disagreement between them is not worth saving four bytes a station over.
// CorridorScene/CorridorDriver explain the rest of the precision chain.
//
// Lateral offsets are measured from the alignment centreline (which runs
// down the middle of the median); POSITIVE = right of travel = eastbound.
// Travel along the alignment is the +station direction, which points -Z.
// ══════════════════════════════════════════════════════════════════════

#pragma pack(push, 1)

struct CorridorHeader {
    static constexpr uint32_t MAGIC   = 0x53574C43;  // "SWLC"
    static constexpr uint32_t VERSION = 1;

    uint32_t magic;
    uint32_t version;
    uint32_t stationCount;
    uint32_t landmarkCount;

    // ── Sampling / tiling (WU) ───────────────────────────────────────
    float stationSpacing;  // uniform arc-length spacing between stations
    float segmentLength;   // one GPU vertex buffer + one render offset
    float tileLength;      // one draw call per cross-section band per tile

    // ── Cross-section (WU, lateral from the centreline) ──────────────
    float laneWidth;
    float medianHalfWidth;  // centreline -> inner edge of the inner shoulder
    float innerShoulder;
    float outerShoulder;
    float barrierWidth;
    float barrierHeight;
    float grassExtent;  // grass beyond the outer shoulder
    float edgeLine;     // painted line width
    float dashLength;
    float dashGap;

    // ── Heights (WU) ─────────────────────────────────────────────────
    float roadY;  // flat carriageway surface (this stretch has no grade)
    float grassY;
    float markingLift;  // paint sits this far above roadY

    // ── Finite-corridor mechanics (WU) ───────────────────────────────
    float turnaroundLength;  // barrier-free U-turn pad at each end
    // Where the drive begins. NOT 0: station 0 is the Queens-Midtown Tunnel's
    // Manhattan portal, whose approach is a real ~100 m-radius loop ramp. The
    // baker picks the first station past that where the road has straightened.
    float startStation;

    // ── Compaction provenance (docs/phase-3-osm-lie-corridor.md §4.2) ─
    float compactionRadius;  // N — landmark radius kept at real scale
    float compactionBlend;   // W — smoothstep width past N
    float compactionFactor;  // c — solved, not authored
    float chordBearingDeg;   // rotation applied to the tangent plane

    double totalLength;  // compacted corridor arc length (WU)
    double realLength;   // arc length before compaction (WU)
    double originLat;    // tangent-plane origin (route midpoint)
    double originLon;
};

// One alignment sample. `tx/tz` is the unit tangent in the engine XZ plane
// (direction of travel); `lanes` is FRACTIONAL so a lane-count change reads
// as a real taper rather than a step.
struct CorridorStation {
    double x, z;
    float  tx, tz;
    float  lanes;
};

// A real interchange, straight from OSM's motorway_junction refs. 3a only
// uses these as compaction anchors + a debug readout; 3b/3c build ramps and
// signage from them.
struct CorridorLandmark {
    double station;
    char   ref[8];
};

#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<CorridorHeader>);
static_assert(sizeof(CorridorHeader) == 136, "CorridorHeader must match tools/lie_corridor.py");
static_assert(sizeof(CorridorStation) == 28, "CorridorStation must match tools/lie_corridor.py");
static_assert(sizeof(CorridorLandmark) == 16, "CorridorLandmark must match tools/lie_corridor.py");

struct CorridorAsset {
    CorridorHeader                header{};
    std::vector<CorridorStation>  stations;
    std::vector<CorridorLandmark> landmarks;
};

// Loads the baked corridor. Throws with an actionable message — a missing or
// stale asset is a hard error, not a silent fall back to the retired
// fictional road (that would ship a fake corridor while claiming otherwise).
[[nodiscard]] inline CorridorAsset load_corridor_asset(const std::string& path) {
    const std::string hint =
        "\n  Re-bake with:  python3 tools/lie_corridor.py bake"
        "\n  (needs no network and no SUMO — data/osm/ is cached)";

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        throw std::runtime_error("CorridorAsset: cannot open " + path + hint);

    CorridorAsset a;
    struct Closer {
        FILE* f;
        ~Closer() { std::fclose(f); }
    } closer{f};

    if (std::fread(&a.header, sizeof(CorridorHeader), 1, f) != 1)
        throw std::runtime_error("CorridorAsset: short header in " + path + hint);
    if (a.header.magic != CorridorHeader::MAGIC)
        throw std::runtime_error("CorridorAsset: bad magic in " + path + hint);
    if (a.header.version != CorridorHeader::VERSION)
        throw std::runtime_error("CorridorAsset: version mismatch in " + path + " (expected " +
                                 std::to_string(CorridorHeader::VERSION) + ", got " + std::to_string(a.header.version) +
                                 ")" + hint);
    if (a.header.stationCount < 2)
        throw std::runtime_error("CorridorAsset: needs >= 2 stations, got " + std::to_string(a.header.stationCount) +
                                 hint);

    a.stations.resize(a.header.stationCount);
    if (std::fread(a.stations.data(), sizeof(CorridorStation), a.stations.size(), f) != a.stations.size())
        throw std::runtime_error("CorridorAsset: short station block in " + path + hint);

    a.landmarks.resize(a.header.landmarkCount);
    if (!a.landmarks.empty() &&
        std::fread(a.landmarks.data(), sizeof(CorridorLandmark), a.landmarks.size(), f) != a.landmarks.size())
        throw std::runtime_error("CorridorAsset: short landmark block in " + path + hint);

    return a;
}

}  // namespace swish
