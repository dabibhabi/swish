// ═══════════════════════════════════════════════════════════════════════
// toml_baker — Converts road.toml → road.bin (packed RoadConfig struct)
//
// Usage: toml_baker <input.toml> <output.bin>
//
// Unit conversion:
//   kFt = 0.3048 * 1000.0 = 304.8  (feet → world units)
//   WORLD_SCALE = 1000.0            (meters → world units)
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include <toml++/toml.hpp>

// Must match the struct in RoadConfig.h exactly
#pragma pack(push, 1)
struct RoadConfig {
    uint32_t magic;
    uint32_t version;

    float road_length;
    float lane_width;
    int32_t lane_count;
    float shoulder_width_wb;
    float shoulder_width_eb;
    float grass_extent;

    float barrier_width;
    float barrier_height;

    float rail_width;
    float rail_height;

    float curb_height;
    float curb_width;

    float asphalt_tile;
    float grass_tile;
    float concrete_tile;
    float metal_tile;

    float marking_y_offset;
    float dash_length;
    float dash_gap;

    float shoulder_tint[4];
    float barrier_tint[4];
    float rail_tint[4];
    float white_marking[4];
    float yellow_marking[4];

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

static constexpr float WORLD_SCALE = 1000.0f;
static constexpr float kFt         = 0.3048f * WORLD_SCALE;
static constexpr uint32_t MAGIC    = 0x53575243;
static constexpr uint32_t VERSION  = 3;

static void read_color(const toml::array* arr, float out[4]) {
    if (!arr || arr->size() != 4) {
        std::cerr << "Error: color array must have exactly 4 elements\n";
        std::exit(1);
    }
    for (int i = 0; i < 4; i++) {
        out[i] = static_cast<float>(arr->get(i)->value_or(0.0));
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: toml_baker <input.toml> <output.bin>\n";
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    toml::table tbl;
    try {
        tbl = toml::parse_file(input_path);
    } catch (const toml::parse_error& err) {
        std::cerr << "TOML parse error: " << err << "\n";
        return 1;
    }

    RoadConfig cfg{};
    cfg.magic   = MAGIC;
    cfg.version = VERSION;

    // ── Road geometry ─────────────────────────────────────────────────
    cfg.road_length      = tbl["road"]["length_m"].value_or(1000.0) * WORLD_SCALE;
    cfg.lane_width       = tbl["road"]["lane_width_ft"].value_or(12.17) * kFt;
    cfg.lane_count       = tbl["road"]["lane_count"].value_or(3);
    cfg.shoulder_width_wb = tbl["road"]["shoulder_width_wb_ft"].value_or(3.0) * kFt;
    cfg.shoulder_width_eb = tbl["road"]["shoulder_width_eb_ft"].value_or(10.0) * kFt;
    cfg.grass_extent     = tbl["road"]["grass_extent_ft"].value_or(500.0) * kFt;

    // ── Barrier ───────────────────────────────────────────────────────
    cfg.barrier_width  = tbl["barrier"]["width_ft"].value_or(2.67) * kFt;
    cfg.barrier_height = tbl["barrier"]["height_ft"].value_or(2.67) * kFt;

    // ── Guardrail ─────────────────────────────────────────────────────
    cfg.rail_width  = tbl["guardrail"]["width_ft"].value_or(0.5) * kFt;
    cfg.rail_height = tbl["guardrail"]["height_ft"].value_or(2.25) * kFt;

    // ── Curb ──────────────────────────────────────────────────────────
    cfg.curb_height = tbl["curb"]["height_ft"].value_or(0.5) * kFt;
    cfg.curb_width  = tbl["curb"]["width_ft"].value_or(0.5) * kFt;

    // ── Tiling (already in world units) ───────────────────────────────
    cfg.asphalt_tile  = tbl["tiling"]["asphalt"].value_or(3000.0);
    cfg.grass_tile    = tbl["tiling"]["grass"].value_or(4000.0);
    cfg.concrete_tile = tbl["tiling"]["concrete"].value_or(1500.0);
    cfg.metal_tile    = tbl["tiling"]["metal"].value_or(1000.0);

    // ── Markings ──────────────────────────────────────────────────────
    cfg.marking_y_offset = tbl["markings"]["y_offset"].value_or(2.0);
    cfg.dash_length      = tbl["markings"]["dash_length_ft"].value_or(10.0) * kFt;
    cfg.dash_gap         = tbl["markings"]["dash_gap_ft"].value_or(30.0) * kFt;

    // ── Colors ────────────────────────────────────────────────────────
    read_color(tbl["colors"]["shoulder_tint"].as_array(), cfg.shoulder_tint);
    read_color(tbl["colors"]["barrier_tint"].as_array(), cfg.barrier_tint);
    read_color(tbl["colors"]["rail_tint"].as_array(), cfg.rail_tint);
    read_color(tbl["colors"]["white_marking"].as_array(), cfg.white_marking);
    read_color(tbl["colors"]["yellow_marking"].as_array(), cfg.yellow_marking);

    // ── Material texture tints ────────────────────────────────────────
    read_color(tbl["colors"]["ground_tint"].as_array(), cfg.ground_tint);
    read_color(tbl["colors"]["grass_tint"].as_array(), cfg.grass_tint);
    read_color(tbl["colors"]["asphalt_tint"].as_array(), cfg.asphalt_tint);
    read_color(tbl["colors"]["concrete_tint"].as_array(), cfg.concrete_tint);
    read_color(tbl["colors"]["metal_tint"].as_array(), cfg.metal_tint);
    read_color(tbl["colors"]["white_tint"].as_array(), cfg.white_tint);
    read_color(tbl["colors"]["yellow_tint"].as_array(), cfg.yellow_tint);
    read_color(tbl["colors"]["black_tint"].as_array(), cfg.black_tint);

    // ── Write binary ──────────────────────────────────────────────────
    FILE* f = std::fopen(output_path, "wb");
    if (!f) {
        std::cerr << "Error: cannot open output file: " << output_path << "\n";
        return 1;
    }

    size_t written = std::fwrite(&cfg, sizeof(RoadConfig), 1, f);
    std::fclose(f);

    if (written != 1) {
        std::cerr << "Error: failed to write config\n";
        return 1;
    }

    std::cout << "Baked " << input_path << " -> " << output_path
              << " (" << sizeof(RoadConfig) << " bytes)\n";
    return 0;
}
