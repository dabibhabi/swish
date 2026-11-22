#pragma once

#include "utils/meta/Concepts.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// CorridorNetworkAsset — the Queens network table (N1, debug-only).
//
// Baked by tools/queens_network.py (config/queens_network.toml) next to one
// CorridorAsset per road (assets/corridor/queens_<id>_full.bin, the same v2
// station format as the LIE). This file links them: which road is which
// asset, where two roads meet, where each road's work-zone wall stands, and
// the OSM ramp polylines at each junction. Nothing loads it at runtime yet (N2).
//
// Layout: u32 magic, u32 version, then tagged blocks (u32 tag, u32 count,
// `count` records) until EOF, the same rule as CorridorAsset's trailing
// blocks. #pragma pack(1); the struct.pack formats in write_network MUST match.
// All positions are in the LIE asset's world frame (WU).
// ══════════════════════════════════════════════════════════════════════

#pragma pack(push, 1)

// Road table: id 0 is the LIE. `asset` is relative to ASSET_DIR.
struct NetworkRoad {
    uint8_t id;
    uint8_t pad[7];
    char    key[16];  // config key: "lie", "bqe", ...
    char    asset[40];
};

// Where road A at station sA meets road B at station sB. The placed point (x, z) is the real junction pushed
// through the LIE warp; each road's centreline at s, offset t along its right normal, lands on it (t ~ 0 on a
// crossing road; on the LIE, t is the junction's lateral off the eastbound-carriageway centreline).
struct NetworkJunction {
    double  sA, sB;
    double  x, z;
    float   tA, tB;
    uint8_t roadA, roadB, id;
    uint8_t pad[5];
    char    name[16];
};

// A work-zone wall: the drivable end of a road. end 0 = low-s end, 1 = high-s end. The road past it is the
// undrivable horizon tail.
struct NetworkWall {
    double  s;
    uint8_t road, end;
    uint8_t pad[6];
};

// One ramp at a junction: OSM motorway_link ways joined end to start with no fork (N3), points [first, first +
// count) in the RPTS block. from / to = the road whose mainline shares its first / last OSM node, 255 = another
// ramp or a local street. osmId = its first way.
struct NetworkRamp {
    int64_t  osmId;
    uint32_t first, count;
    uint8_t  junction, from, to;
    int8_t   layer;  // highest OSM layer along it (bridge = 1 when untagged)
    uint8_t  lanes;  // fewest OSM lanes along it (untagged = 1)
    uint8_t  stub;   // N6: 1 = a closed local exit's stub (its link chain from the exit node, nowhere to go)
    uint8_t  pad[2];
};

// A warped ramp point. y: DEM ground + layer height, on the LIE's datum (relative to its start station).
struct NetworkRampPoint {
    double  x, z;
    float   y;
    int8_t  layer;
    uint8_t pad[3];
};

// N8: a landmark (road::netmarks::Kind) on road `road`: points at stations s (WU, may lie past the data ends) and
// laterals lat (WU, + = right of +s, real metres kept), ground y (WU, LIE datum). One-point kinds repeat the point.
struct NetworkLandmark {
    double  s[2];
    float   lat[2];
    float   y;
    uint8_t kind, road;
    uint8_t pad[2];
};

#pragma pack(pop)

static_assert(swish::util::meta::TriviallyPackable<NetworkJunction>);
static_assert(sizeof(NetworkRoad) == 64, "NetworkRoad must match tools/queens_network.py");
static_assert(sizeof(NetworkJunction) == 64, "NetworkJunction must match tools/queens_network.py");
static_assert(sizeof(NetworkWall) == 16, "NetworkWall must match tools/queens_network.py");
static_assert(sizeof(NetworkRamp) == 24, "NetworkRamp must match tools/queens_network.py");
static_assert(sizeof(NetworkRampPoint) == 24, "NetworkRampPoint must match tools/queens_network.py");
static_assert(sizeof(NetworkLandmark) == 32, "NetworkLandmark must match tools/queens_network.py");

inline constexpr uint32_t kNetworkMagic   = 0x4E4C5753;  // "SWLN"
inline constexpr uint32_t kNetworkVersion = 1;
inline constexpr uint32_t kNetworkRoadTag = 0x44414F52;  // "ROAD"
inline constexpr uint32_t kNetworkJnctTag = 0x54434E4A;  // "JNCT"
inline constexpr uint32_t kNetworkWallTag = 0x4C4C4157;  // "WALL"
inline constexpr uint32_t kNetworkRampTag = 0x504D4152;  // "RAMP"
inline constexpr uint32_t kNetworkRptsTag = 0x53545052;  // "RPTS"
inline constexpr uint32_t kNetworkLmrkTag = 0x4B524D4C;  // "LMRK" (N8)

struct CorridorNetworkAsset {
    std::vector<NetworkRoad>      roads;
    std::vector<NetworkJunction>  junctions;
    std::vector<NetworkWall>      walls;
    std::vector<NetworkRamp>      ramps;
    std::vector<NetworkRampPoint> rampPoints;
    std::vector<NetworkLandmark>  landmarks;  // N8
};

// Throws on a missing or malformed file, like load_corridor_asset.
[[nodiscard]] inline CorridorNetworkAsset load_corridor_network(const std::string& path) {
    const std::string hint = "\n  Re-bake with:  python3 tools/queens_network.py bake_net";
    FILE*             f    = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("CorridorNetwork: cannot open " + path + hint);
    struct Closer {
        FILE* f;
        ~Closer() { std::fclose(f); }
    } closer{f};

    uint32_t head[2];
    if (std::fread(head, sizeof(head), 1, f) != 1 || head[0] != kNetworkMagic || head[1] != kNetworkVersion)
        throw std::runtime_error("CorridorNetwork: bad magic or version in " + path + hint);

    CorridorNetworkAsset a;
    auto                 read = [&](auto& v, uint32_t n) {
        v.resize(n);
        return v.empty() || std::fread(v.data(), sizeof(v[0]), n, f) == n;
    };
    uint32_t block[2];
    while (std::fread(block, sizeof(block), 1, f) == 1) {
        bool ok = false;
        if (block[0] == kNetworkRoadTag)
            ok = read(a.roads, block[1]);
        else if (block[0] == kNetworkJnctTag)
            ok = read(a.junctions, block[1]);
        else if (block[0] == kNetworkWallTag)
            ok = read(a.walls, block[1]);
        else if (block[0] == kNetworkRampTag)
            ok = read(a.ramps, block[1]);
        else if (block[0] == kNetworkRptsTag)
            ok = read(a.rampPoints, block[1]);
        else if (block[0] == kNetworkLmrkTag)
            ok = read(a.landmarks, block[1]);
        // Unknown tag = unknown record size: nothing after it can be read safely.
        if (!ok) throw std::runtime_error("CorridorNetwork: bad block in " + path + hint);
    }
    // Text and indices come from a file: terminate strings, bounds-check ramp spans.
    for (NetworkRoad& r : a.roads) r.key[15] = r.asset[39] = '\0';
    for (NetworkJunction& j : a.junctions) j.name[15] = '\0';
    for (const NetworkRamp& r : a.ramps)
        if (static_cast<uint64_t>(r.first) + r.count > a.rampPoints.size())
            throw std::runtime_error("CorridorNetwork: ramp points out of range in " + path + hint);
    for (const NetworkLandmark& l : a.landmarks)
        if (l.road >= a.roads.size())
            throw std::runtime_error("CorridorNetwork: landmark road out of range in " + path + hint);
    return a;
}

}  // namespace swish
