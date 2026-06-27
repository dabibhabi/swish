#include <catch2/catch_test_macros.hpp>
#include "scene/RoadScene/RoadScene.h"
#include "scene/RoadScene/RoadConfig.h"

using namespace swish;

// Helper: build a RoadScene from a hand-constructed RoadConfig so tests
// don't depend on road.bin being present on disk.
static RoadConfig make_test_config() {
    RoadConfig cfg{};
    cfg.magic   = RoadConfig::MAGIC;
    cfg.version = RoadConfig::VERSION;

    cfg.road_length        = 500000.0f;  // > 1 lamp spacing (500ft * kFt = ~152400)
    cfg.lane_width         = 120.0f;
    cfg.lane_count         = 3;
    cfg.shoulder_width_wb  = 120.0f;
    cfg.shoulder_width_eb  = 120.0f;
    cfg.grass_extent       = 300.0f;
    cfg.barrier_width      = 10.0f;
    cfg.barrier_height     = 40.0f;
    cfg.rail_width         = 4.0f;
    cfg.rail_height        = 30.0f;
    cfg.curb_height        = 6.0f;
    cfg.curb_width         = 6.0f;
    cfg.asphalt_tile       = 200.0f;
    cfg.grass_tile         = 200.0f;
    cfg.concrete_tile      = 200.0f;
    cfg.metal_tile         = 200.0f;
    cfg.marking_y_offset   = 1.0f;
    cfg.dash_length        = 40.0f;
    cfg.dash_gap           = 120.0f;

    // Colors: white with full alpha
    for (int i = 0; i < 4; ++i) {
        cfg.shoulder_tint[i]  = 1.0f;
        cfg.barrier_tint[i]   = 1.0f;
        cfg.rail_tint[i]      = 1.0f;
        cfg.white_marking[i]  = 1.0f;
        cfg.yellow_marking[i] = 1.0f;
        cfg.ground_tint[i]    = 1.0f;
        cfg.grass_tint[i]     = 1.0f;
        cfg.asphalt_tint[i]   = 1.0f;
        cfg.concrete_tint[i]  = 1.0f;
        cfg.metal_tint[i]     = 1.0f;
        cfg.white_tint[i]     = 1.0f;
        cfg.yellow_tint[i]    = 1.0f;
        cfg.black_tint[i]     = 1.0f;
    }

    return cfg;
}

// ── Mesh generation ───────────────────────────────────────────────────

TEST_CASE("RoadScene generate produces non-empty vertex buffer", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    REQUIRE(data.meshData.getVertexCount() > 0);
}

TEST_CASE("RoadScene generate produces non-empty index buffer", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    REQUIRE(data.meshData.getIndexCount() > 0);
}

TEST_CASE("RoadScene generate produces draw calls", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    REQUIRE(data.drawCalls.size() > 0);
}

// ── DrawCall validity ─────────────────────────────────────────────────

TEST_CASE("RoadScene all DrawCall index ranges are in-bounds", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto     data        = scene.generate();
    uint32_t index_count = data.meshData.getIndexCount();
    for (const auto& dc : data.drawCalls) {
        REQUIRE(dc.indexOffset < index_count);
        REQUIRE(dc.indexOffset + dc.indexCount <= index_count);
    }
}

TEST_CASE("RoadScene DrawCall index counts are multiples of 3", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    for (const auto& dc : data.drawCalls) {
        REQUIRE(dc.indexCount % 3 == 0);
    }
}

TEST_CASE("RoadScene DrawCall material IDs are within valid range", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    for (const auto& dc : data.drawCalls) {
        REQUIRE(dc.material < MAT_COUNT);
    }
}

// ── Scaling behavior ──────────────────────────────────────────────────

TEST_CASE("RoadScene wider road produces more geometry", "[roadscene]") {
    RoadConfig narrow_cfg = make_test_config();
    RoadConfig wide_cfg   = make_test_config();
    narrow_cfg.lane_count = 2;
    wide_cfg.lane_count   = 6;

    RoadScene narrow(narrow_cfg);
    RoadScene wide(wide_cfg);
    auto n = narrow.generate();
    auto w = wide.generate();

    REQUIRE(w.meshData.getVertexCount() > n.meshData.getVertexCount());
}

TEST_CASE("RoadScene longer road produces more geometry", "[roadscene]") {
    RoadConfig short_cfg = make_test_config();
    RoadConfig long_cfg  = make_test_config();
    short_cfg.road_length = 1000.0f;
    long_cfg.road_length  = 10000.0f;

    RoadScene shorter(short_cfg);
    RoadScene longer(long_cfg);
    auto s = shorter.generate();
    auto l = longer.generate();

    REQUIRE(l.meshData.getVertexCount() > s.meshData.getVertexCount());
}

// ── Lights ────────────────────────────────────────────────────────────

TEST_CASE("RoadScene generates street lamps", "[roadscene]") {
    RoadScene scene(make_test_config());
    auto data = scene.generate();
    REQUIRE(data.lights.size() > 0);
}

TEST_CASE("RoadScene longer road generates more lamps", "[roadscene]") {
    RoadConfig short_cfg = make_test_config();
    RoadConfig long_cfg  = make_test_config();
    // lamp_spacing = 500ft * kFt = ~152400 world units
    // short road has 0 lamps, long road has several
    short_cfg.road_length = 100000.0f;   // < 1 lamp spacing
    long_cfg.road_length  = 1000000.0f;  // > 6 lamp spacings

    RoadScene shorter(short_cfg);
    RoadScene longer(long_cfg);
    REQUIRE(longer.generate().lights.size() > shorter.generate().lights.size());
}

// ── MeshBuilder dashed line behavior ─────────────────────────────────

TEST_CASE("MeshBuilder dashed lines respect dash+gap cycle", "[roadscene][meshbuilder]") {
    // With a very short road and known dash/gap, we can bound how many quads
    // are added. Each dash = 1 quad = 4 vertices.
    RoadConfig cfg       = make_test_config();
    cfg.road_length      = 500.0f;
    cfg.dash_length      = 100.0f;
    cfg.dash_gap         = 400.0f;
    // lane_count = 3 (from make_test_config)

    RoadScene scene(cfg);
    auto data = scene.generate();
    // The road is 500 units. dash=100, gap=400, cycle=500 → exactly 1 dash fits.
    // With 2 dashed marking lines (per lane separator), baseline is at least 1 group.
    REQUIRE(data.meshData.getVertexCount() > 0);

    // Upper-bound check: num_dashes = floor(500 / (100+400)) = 1
    // Each dash is a quad = 4 vertices; lanes = 3 separators at most = 3
    // max_dash_vertices = 1 * 3 * 4 = 12; add generous overhead for road base geometry.
    constexpr uint32_t num_dashes          = 1;
    constexpr uint32_t lane_count          = 3;
    constexpr uint32_t max_dash_vertices   = num_dashes * lane_count * 4;
    constexpr uint32_t reasonable_overhead = 10000;  // road base, shoulders, barriers, etc.
    REQUIRE(data.meshData.getVertexCount() <= max_dash_vertices + reasonable_overhead);
}

// ── Getter/setter round-trips ─────────────────────────────────────────

TEST_CASE("RoadScene set_lane_count affects get_lane_count", "[roadscene]") {
    RoadScene scene(make_test_config());
    scene.set_lane_count(5);
    REQUIRE(scene.get_lane_count() == 5);
}

TEST_CASE("RoadScene set_road_length affects get_road_length", "[roadscene]") {
    RoadScene scene(make_test_config());
    scene.set_road_length(12345.0f);
    REQUIRE(scene.get_road_length() == 12345.0f);
}

// ── Degenerate input ──────────────────────────────────────────────────

TEST_CASE("RoadScene zero-lane config produces no geometry", "[RoadScene]") {
    RoadConfig cfg        = make_test_config();
    cfg.lane_count        = 0;
    cfg.road_length       = 1000.f;
    RoadScene scene(cfg);
    auto data = scene.generate();
    REQUIRE(data.meshData.getIndexCount() == 0);
}

TEST_CASE("RoadScene zero-length config produces no geometry", "[RoadScene]") {
    RoadConfig cfg        = make_test_config();
    cfg.lane_count        = 2;
    cfg.road_length       = 0.f;
    RoadScene scene(cfg);
    auto data = scene.generate();
    REQUIRE(data.meshData.getVertexCount() == 0);
}
