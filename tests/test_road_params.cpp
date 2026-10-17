// Drift guard: RoadParams.h (compile-time) vs config/road.toml -> road.bin (baked) vs the geometry
// RoadScene actually emits. Any of the three moving alone fails here.
#include "scene/RoadGeometry/RoadGeometry.h"
#include "scene/RoadScene/RoadConfig.h"
#include "scene/RoadScene/RoadParams.h"
#include "scene/RoadScene/RoadScene.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace swish;
using Catch::Approx;
namespace u = swish::util::units;
using RG    = RoadGeometry;

TEST_CASE("RoadParams matches baked road.bin", "[roadparams]") {
    const RoadConfig c = load_road_config(CONFIG_DIR "road.bin");
    CHECK(c.lane_width == Approx(road::kLie.lane_ft * u::kWuPerFt).margin(1e-3f));
    CHECK(static_cast<float>(c.lane_count) == road::kLie.lanes);
    CHECK(c.barrier_width == Approx(road::kLie.barrier_ft * u::kWuPerFt).margin(1e-3f));
    CHECK(c.shoulder_width_eb == Approx(road::kLie.shoulder_eb_ft * u::kWuPerFt).margin(1e-3f));
    CHECK(c.marking_y_offset == road::kLie.marking_y);
    CHECK(RoadConfig::kCrownSlope == road::kCrownSlope);
    CHECK(RG::kCrownSlope == road::kCrownSlope);
    // Tile sizes must keep dividing the chunk length (seamless UVs).
    for (float tile : {c.asphalt_tile, c.grass_tile, c.concrete_tile, c.metal_tile})
        CHECK(std::fmod(RG::kChunkLen, tile) == 0.0f);
}

TEST_CASE("RoadGeometry mainline edges derive from RoadParams and the baked config", "[roadparams]") {
    const RoadConfig c = load_road_config(CONFIG_DIR "road.bin");
    // RoadScene lays the EB road out from barrier_width + 3 ft clearance.
    const float eb_start = c.barrier_width + 3.0f * u::kWuPerFt;
    CHECK(RG::kBarrierRight == Approx(eb_start).margin(1e-2f));
    CHECK(RG::kEbRoadRight ==
          Approx(eb_start + static_cast<float>(c.lane_count) * c.lane_width + c.shoulder_width_eb).margin(1e-2f));
    CHECK(RG::kLaneWidth == Approx(c.lane_width).margin(1e-3f));
}

TEST_CASE("kExit agrees with the generated ramp geometry", "[roadparams]") {
    // Static relationships between the params and RoadGeometry's derived stations.
    const float intro = 4218688.0f;  // road.toml length_m * 1000
    CHECK(RG::intro_exit_ramp_end_z(intro) == -road::kExit.end_frac * intro);
    CHECK(RG::intro_exit_ramp_start_z(intro) - RG::intro_exit_ramp_end_z(intro) ==
          Approx(road::kExit.diverge_ft * u::kWuPerFt).margin(0.5f));
    CHECK(RG::intro_exit_decel_start_z(intro) - RG::intro_exit_ramp_start_z(intro) ==
          Approx(road::kExit.decel_ft * u::kWuPerFt).margin(0.5f));
    CHECK(RG::kIntroExitAsphaltY == road::kExit.asphalt_y);

    // Dynamic: the mesh RoadScene::generate() emits (via generate_exit_ramp) must reach the same
    // stations and the same N Marginal outer edge that RoadGeometry advertises to the physics.
    RoadScene   s(load_road_config(CONFIG_DIR "road.bin"));
    const auto  d     = s.generate();
    const auto& V     = d.meshData.getVertices();
    const float len   = s.get_road_length();
    const float endZ  = RG::intro_exit_ramp_end_z(len);
    const float decel = RG::intro_exit_decel_start_z(len);

    bool hasOuterEdge = false, hasEndZ = false, hasDecelZ = false;
    for (const auto& v : V) {
        if (std::abs(v.position.y - road::kExit.asphalt_y) > 1e-3f) continue;
        hasOuterEdge |= std::abs(v.position.x - RG::kIntroMarginalOuter) < 1.0f;
        hasEndZ |= std::abs(v.position.z - endZ) < 1.0f;
        hasDecelZ |= std::abs(v.position.z - decel) < 1.0f;
    }
    CHECK(hasOuterEdge);
    CHECK(hasEndZ);
    CHECK(hasDecelZ);
}

TEST_CASE("snap_dash_to_chunk tiles the chunk exactly and keeps the dash:gap ratio", "[roadparams]") {
    constexpr auto p = road::snap_dash_to_chunk({3048.0f, 9144.0f}, 300000.0f);
    STATIC_REQUIRE(p.length + p.gap > 11999.0f);
    CHECK(p.length == Approx(3000.0f).margin(0.01f));
    CHECK(p.gap == Approx(9000.0f).margin(0.01f));
    CHECK(p.length / (p.length + p.gap) == Approx(3048.0f / 12192.0f).epsilon(1e-6));
    // 300000 / cycle is (nearly) an integer
    const float n = 300000.0f / (p.length + p.gap);
    CHECK(n == Approx(std::round(n)).margin(1e-3f));
}
