#pragma once

#include "utils/meta/Concepts.h"

#include <cstddef>
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
    static constexpr uint32_t MAGIC        = 0x53574C43;  // "SWLC"
    static constexpr uint32_t VERSION      = 2;           // v2: CorridorStation gains y and bank
    static constexpr uint32_t VERSION_FLAT = 1;           // release asset: 28-byte stations, loaded with y = bank = 0

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
// as a real taper rather than a step. `y` is the centreline height (WU)
// relative to the start station, from USGS 3DEP ground heights after the
// baker's low-pass, grade clamp and vertical-curve limit. `bank` is the cross
// slope: surface height at lateral t (positive right of travel) is
// y - bank * t, so a right-hand curve has bank > 0.
struct CorridorStation {
    double x, z;
    float  tx, tz;
    float  lanes;
    float  y;
    float  bank;
};

// A real interchange, straight from OSM's motorway_junction refs. 3a only
// uses these as compaction anchors + a debug readout; 3b/3c build ramps and
// signage from them.
struct CorridorLandmark {
    double station;
    char   ref[8];
};

// One exit ramp's diverge point, from an OSM motorway_junction node on the
// mainline. The sign text is the `destination` / `destination:ref` of the
// motorway_link way that starts there. Sorted by station; both directions.
struct CorridorExit {
    double  station;
    char    ref[8];  // exit number, e.g. "49N"
    uint8_t westbound;
    uint8_t district;  // index into CorridorAsset::districts
    uint8_t pad[2];
    char    destRef[24];  // route shields, e.g. "NY 110 North"
    char    dest[76];     // place names, "; "-separated
};

// A stretch of the route with its own roadside kit (Queens / Nassau / Suffolk).
struct CorridorDistrict {
    double start;  // first station of the district
    char   name[16];
};

// A continuous service-road stretch on one side (C2a). Eastbound = South SR
// (NY 906A), westbound = North SR (NY 906B); both run one way with their side's mainline.
struct CorridorServiceRoad {
    double  s0, s1;  // station range (WU), s0 < s1
    uint8_t westbound;
    uint8_t lanes;   // 1..3
    uint8_t source;  // 0 = OSM unsigned_ref, 1 = exit-number rule fill
    uint8_t pad[5];
};

// D1 pavement kind per stretch (config/lie_corridor.toml [surface]). Legacy = no SURF block (release asset).
enum class Pavement : uint8_t { Legacy = 0, Concrete = 1, Fresh = 2, Worn = 3, Patched = 4 };

// A stretch's pavement from `start` to the next record's start. Sorted by start.
struct CorridorPavement {
    double  start;  // WU
    uint8_t kind;   // Pavement
    uint8_t pad[7];
};

// D4 crossing kinds and bridge styles (tools/lie_corridor.py XING_KIND / XING_STYLE).
enum class CrossingKind : uint8_t { RoadOver = 0, RailOver = 1, LieOver = 2 };
enum class BridgeStyle : uint8_t { Steel = 0, Concrete = 1, Stone = 2, ThroughGirder = 3 };

// One structure crossing the LIE (D4), from an OSM bridge way over the mainline or a road under an LIE bridge way.
// Sorted by station. skew: angle (rad) of the crossing axis off the mainline normal; width: real deck width (WU)
// across the crossing axis (curb to curb plus walks, or tracks).
struct CorridorCrossing {
    double  station;
    float   skew;
    float   width;
    uint8_t kind;   // CrossingKind
    uint8_t style;  // BridgeStyle
    uint8_t lanes;  // 0 = footway or rail
    uint8_t railUnder;
    char    name[24];
};

// N7 roadside look of a network road from `start` to the next row (config/queens_network.toml `look`). kit = the D5
// kit's exit number (road::dressing::kKits), lamp = road::dressing::Lamp, edge = CorridorEdgeKind.
enum class CorridorEdgeKind : uint8_t { None = 0, Rail = 1, Wall = 2 };
struct CorridorLookRow {
    double  start;
    uint8_t kit, lamp, edge;
    uint8_t pad[5];
};

#pragma pack(pop)

static_assert(swish::util::meta::TriviallyPackable<CorridorHeader>);
static_assert(sizeof(CorridorHeader) == 136, "CorridorHeader must match tools/lie_corridor.py");
static_assert(sizeof(CorridorStation) == 36, "CorridorStation must match tools/lie_corridor.py");
static_assert(sizeof(CorridorLandmark) == 16, "CorridorLandmark must match tools/lie_corridor.py");
static_assert(sizeof(CorridorExit) == 120, "CorridorExit must match tools/lie_corridor.py");
static_assert(sizeof(CorridorDistrict) == 24, "CorridorDistrict must match tools/lie_corridor.py");
static_assert(sizeof(CorridorServiceRoad) == 24, "CorridorServiceRoad must match tools/lie_corridor.py");
static_assert(sizeof(CorridorPavement) == 16, "CorridorPavement must match tools/lie_corridor.py");
static_assert(sizeof(CorridorCrossing) == 44, "CorridorCrossing must match tools/lie_corridor.py");
static_assert(sizeof(CorridorLookRow) == 16, "CorridorLookRow must match tools/lie_corridor.py");

// Optional trailing blocks: u32 tag, u32 count, then `count` records.
inline constexpr uint32_t kCorridorExitTag     = 0x54495845;  // "EXIT"
inline constexpr uint32_t kCorridorDistrictTag = 0x54534944;  // "DIST"
inline constexpr uint32_t kCorridorSrvrTag     = 0x52565253;  // "SRVR"
inline constexpr uint32_t kCorridorSurfTag     = 0x46525553;  // "SURF"
inline constexpr uint32_t kCorridorXingTag     = 0x474E4958;  // "XING"
inline constexpr uint32_t kCorridorGrndTag     = 0x444E5247;  // "GRND": N3 DEM ground per station (WU)
inline constexpr uint32_t kCorridorLookTag     = 0x4B4F4F4C;  // "LOOK": N7 roadside look rows (network roads)

struct CorridorAsset {
    CorridorHeader                   header{};
    std::vector<CorridorStation>     stations;
    std::vector<CorridorLandmark>    landmarks;
    std::vector<CorridorExit>        exits;         // empty for a v1 asset
    std::vector<CorridorDistrict>    districts;     // empty for a v1 asset
    std::vector<CorridorServiceRoad> serviceRoads;  // empty when the block is absent
    std::vector<CorridorPavement>    pavement;      // empty when the block is absent
    std::vector<CorridorCrossing>    crossings;     // empty when the block is absent
    std::vector<float>               ground;        // N3: one per station when present (network roads with a profile)
    std::vector<CorridorLookRow>     look;          // N7: empty when the block is absent
};

// Loads the baked corridor. Throws with an actionable message — a missing or
// stale asset is a hard error, not a silent fall back to the retired
// fictional road (that would ship a fake corridor while claiming otherwise).
[[nodiscard]] inline CorridorAsset load_corridor_asset(const std::string& path) {
    const std::string hint =
        "\n  Re-bake with:  python3 tools/lie_corridor.py bake"
        "\n  (needs no network and no SUMO — data/osm/ is cached)";

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("CorridorAsset: cannot open " + path + hint);

    CorridorAsset a;
    struct Closer {
        FILE* f;
        ~Closer() { std::fclose(f); }
    } closer{f};

    if (std::fread(&a.header, sizeof(CorridorHeader), 1, f) != 1)
        throw std::runtime_error("CorridorAsset: short header in " + path + hint);
    if (a.header.magic != CorridorHeader::MAGIC) throw std::runtime_error("CorridorAsset: bad magic in " + path + hint);
    const bool flat = a.header.version == CorridorHeader::VERSION_FLAT;
    if (a.header.version != CorridorHeader::VERSION && !flat)
        throw std::runtime_error("CorridorAsset: version mismatch in " + path + " (expected " +
                                 std::to_string(CorridorHeader::VERSION) + ", got " + std::to_string(a.header.version) +
                                 ")" + hint);
    if (a.header.stationCount < 2)
        throw std::runtime_error("CorridorAsset: needs >= 2 stations, got " + std::to_string(a.header.stationCount) +
                                 hint);

    a.stations.resize(a.header.stationCount);
    if (flat) {
        // v1 record = the first 28 bytes of the v2 record; y and bank stay 0.
        constexpr size_t kFlatSize = offsetof(CorridorStation, y);
        for (CorridorStation& st : a.stations)
            if (std::fread(&st, kFlatSize, 1, f) != 1)
                throw std::runtime_error("CorridorAsset: short station block in " + path + hint);
    } else if (std::fread(a.stations.data(), sizeof(CorridorStation), a.stations.size(), f) != a.stations.size())
        throw std::runtime_error("CorridorAsset: short station block in " + path + hint);

    a.landmarks.resize(a.header.landmarkCount);
    if (!a.landmarks.empty() &&
        std::fread(a.landmarks.data(), sizeof(CorridorLandmark), a.landmarks.size(), f) != a.landmarks.size())
        throw std::runtime_error("CorridorAsset: short landmark block in " + path + hint);

    // Trailing blocks until EOF. An unknown tag is an error, not skipped: the
    // record size is unknown, so nothing after it could be read safely.
    uint32_t block[2];
    while (std::fread(block, sizeof(block), 1, f) == 1) {
        bool ok = false;
        if (block[0] == kCorridorExitTag) {
            a.exits.resize(block[1]);
            ok = a.exits.empty() || std::fread(a.exits.data(), sizeof(CorridorExit), block[1], f) == block[1];
        } else if (block[0] == kCorridorDistrictTag) {
            a.districts.resize(block[1]);
            ok = a.districts.empty() ||
                 std::fread(a.districts.data(), sizeof(CorridorDistrict), block[1], f) == block[1];
        } else if (block[0] == kCorridorSrvrTag) {
            a.serviceRoads.resize(block[1]);
            ok = a.serviceRoads.empty() ||
                 std::fread(a.serviceRoads.data(), sizeof(CorridorServiceRoad), block[1], f) == block[1];
        } else if (block[0] == kCorridorSurfTag) {
            a.pavement.resize(block[1]);
            ok = a.pavement.empty() || std::fread(a.pavement.data(), sizeof(CorridorPavement), block[1], f) == block[1];
        } else if (block[0] == kCorridorXingTag) {
            a.crossings.resize(block[1]);
            ok = a.crossings.empty() ||
                 std::fread(a.crossings.data(), sizeof(CorridorCrossing), block[1], f) == block[1];
        } else if (block[0] == kCorridorGrndTag) {
            a.ground.resize(block[1]);
            ok = block[1] == a.header.stationCount &&
                 std::fread(a.ground.data(), sizeof(float), block[1], f) == block[1];
        } else if (block[0] == kCorridorLookTag) {
            a.look.resize(block[1]);
            ok = a.look.empty() || std::fread(a.look.data(), sizeof(CorridorLookRow), block[1], f) == block[1];
        }
        if (!ok) throw std::runtime_error("CorridorAsset: bad trailing block in " + path + hint);
    }
    // Text fields come from a file: force termination before anyone reads them as C strings.
    for (CorridorExit& e : a.exits) e.ref[7] = e.destRef[23] = e.dest[75] = '\0';
    for (CorridorDistrict& d : a.districts) d.name[15] = '\0';
    for (CorridorCrossing& c : a.crossings) c.name[23] = '\0';

    return a;
}

}  // namespace swish
