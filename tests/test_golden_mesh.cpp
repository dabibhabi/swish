// Golden-MESH tests: pin RoadScene::generate(), generate_chunk() and
// generate_interchange_scene() so a refactor of RoadScene cannot silently change
// the emitted geometry. Fixture: tests/golden/road_mesh.txt.
//
// Toolchain-bound: positions are quantised to 0.01 WU before hashing, but the
// values were generated with the project's Apple clang + libm; a different
// compiler/stdlib may legitimately need regeneration (SWISH_UPDATE_GOLDENS=1).
#include "golden_util.h"
#include "scene/RoadScene/RoadConfig.h"
#include "scene/RoadScene/RoadScene.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace swish;

namespace {

// Hand-built config (no road.bin dependency). Independent copy of the one in
// test_road_scene.cpp so that file can evolve without invalidating goldens.
RoadConfig golden_config() {
    RoadConfig cfg{};
    cfg.magic              = RoadConfig::MAGIC;
    cfg.version            = RoadConfig::VERSION;
    cfg.road_length        = 500000.0f;
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

void summarize(const std::string& p, const RoadScene::SceneData& d, golden::KV& kv) {
    const auto& V = d.meshData.getVertices();
    const auto& I = d.meshData.getIndices();
    kv[p + ".vertices"]  = std::to_string(V.size());
    kv[p + ".indices"]   = std::to_string(I.size());
    kv[p + ".drawcalls"] = std::to_string(d.drawCalls.size());
    kv[p + ".lights"]    = std::to_string(d.lights.size());

    golden::Fnv hv, hi, hd, hl;
    float       lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
    float       hiB[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest()};
    for (const auto& v : V) {
        hv.q(v.position.x); hv.q(v.position.y); hv.q(v.position.z);
        hv.q(v.normal.x);   hv.q(v.normal.y);   hv.q(v.normal.z);
        hv.q(v.uv.x);       hv.q(v.uv.y);
        for (int k = 0; k < 3; ++k) {
            lo[k]  = std::min(lo[k], v.position[k]);
            hiB[k] = std::max(hiB[k], v.position[k]);
        }
    }
    for (uint32_t i : I)
        hi.u32(i);
    std::map<int, uint64_t> perMat;
    for (const auto& dc : d.drawCalls) {
        hd.u32(dc.indexOffset);
        hd.u32(dc.indexCount);
        hd.u32(static_cast<uint32_t>(dc.material));
        for (int k = 0; k < 4; ++k)
            hd.q(dc.color[k]);
        hd.q(dc.boundsRadius);
        perMat[static_cast<int>(dc.material)] += dc.indexCount;
    }
    for (const auto& l : d.lights) {
        hl.q(l.position.x); hl.q(l.position.y); hl.q(l.position.z);
        hl.q(l.intensity);  hl.q(l.radius);
    }
    kv[p + ".vertex_hash"]   = golden::hex(hv.h);
    kv[p + ".index_hash"]    = golden::hex(hi.h);
    kv[p + ".drawcall_hash"] = golden::hex(hd.h);
    kv[p + ".light_hash"]    = golden::hex(hl.h);

    std::ostringstream b, m;
    for (int k = 0; k < 3; ++k)
        b << (k ? " " : "") << std::lround(lo[k] * 100.0) << ' ' << std::lround(hiB[k] * 100.0);
    kv[p + ".bounds_x100"] = b.str();
    for (auto& [mat, n] : perMat)
        m << mat << ':' << n << ' ';
    kv[p + ".material_index_counts"] = m.str();
}

void check(const golden::KV& actual) {
    const std::string file = "road_mesh.txt";
    if (golden::update_mode()) {
        golden::write(file, actual, "RoadScene golden-mesh summary (tests/test_golden_mesh.cpp)");
        SUCCEED("golden rewritten");
        return;
    }
    const golden::KV expected = golden::read(file);
    REQUIRE_FALSE(expected.empty());
    for (const auto& [k, v] : actual) {
        INFO("key: " << k);
        auto it = expected.find(k);
        REQUIRE(it != expected.end());
        CHECK(it->second == v);
    }
}

}  // namespace

TEST_CASE("Golden mesh: RoadScene::generate / generate_chunk / generate_interchange_scene", "[golden][mesh]") {
    golden::KV kv;
    {
        RoadScene s(golden_config());
        summarize("generate", s.generate(), kv);
    }
    {
        RoadScene s(golden_config());
        summarize("chunk4000", s.generate_chunk(4000.0f), kv);
    }
    {
        RoadScene s(golden_config());
        summarize("interchange", s.generate_interchange_scene(), kv);
    }
    check(kv);
}

TEST_CASE("Golden mesh: generation is deterministic within a process", "[golden][mesh]") {
    golden::KV a, b;
    RoadScene  s1(golden_config()), s2(golden_config());
    summarize("g", s1.generate(), a);
    summarize("g", s2.generate(), b);
    CHECK(a == b);
    summarize("i", s1.generate_interchange_scene(), a);
    summarize("i", s2.generate_interchange_scene(), b);
    CHECK(a == b);
}
