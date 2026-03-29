#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// RoadConfig — Binary-serializable road scene configuration.
//
// All values are stored in world units (pre-converted from feet/meters).
// Produced by toml_baker from a TOML source file.
// ══════════════════════════════════════════════════════════════════════

#pragma pack(push, 1)
struct RoadConfig {
    static constexpr uint32_t MAGIC   = 0x53575243;  // "SWRC"
    static constexpr uint32_t VERSION = 3;

    // ── Header ────────────────────────────────────────────────────────
    uint32_t magic;
    uint32_t version;

    // ── Road geometry (world units) ───────────────────────────────────
    float road_length;
    float lane_width;
    int32_t lane_count;
    float shoulder_width_wb;
    float shoulder_width_eb;
    float grass_extent;

    // ── Barrier ───────────────────────────────────────────────────────
    float barrier_width;
    float barrier_height;

    // ── Guardrail ─────────────────────────────────────────────────────
    float rail_width;
    float rail_height;

    // ── Curb ──────────────────────────────────────────────────────────
    float curb_height;
    float curb_width;

    // ── Texture tiling (world units) ──────────────────────────────────
    float asphalt_tile;
    float grass_tile;
    float concrete_tile;
    float metal_tile;

    // ── Markings ──────────────────────────────────────────────────────
    float marking_y_offset;
    float dash_length;
    float dash_gap;
    
    // ── Crown slope ──────────────────────────────────────────────────────
    static constexpr float m_crown_slope = 0.02f;

    // ── Colors [r, g, b, a] ──────────────────────────────────────────
    float shoulder_tint[4];
    float barrier_tint[4];
    float rail_tint[4];
    float white_marking[4];
    float yellow_marking[4];

    // ── Material texture tints ───────────────────────────────────────
    float ground_tint[4];
    float grass_tint[4];
    float asphalt_tint[4];
    float concrete_tint[4];
    float metal_tint[4];
    float white_tint[4];
    float yellow_tint[4];
    float black_tint[4];
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<RoadConfig>,
              "RoadConfig must be trivially copyable for binary I/O");

// ── Load from binary file ─────────────────────────────────────────────

inline RoadConfig load_road_config(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("RoadConfig: cannot open " + path);
    }

    RoadConfig cfg{};
    size_t read = std::fread(&cfg, sizeof(RoadConfig), 1, f);
    std::fclose(f);

    if (read != 1) {
        throw std::runtime_error("RoadConfig: failed to read " + path);
    }
    if (cfg.magic != RoadConfig::MAGIC) {
        throw std::runtime_error("RoadConfig: bad magic in " + path);
    }
    if (cfg.version != RoadConfig::VERSION) {
        throw std::runtime_error("RoadConfig: version mismatch in " + path +
                                 " (expected " + std::to_string(RoadConfig::VERSION) +
                                 ", got " + std::to_string(cfg.version) + ")");
    }

    return cfg;
}

}  // namespace swish
