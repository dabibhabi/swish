// utils/math/Hash.h: hash01 (per-tree jitter).
#include "utils/math/Hash.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

namespace um = swish::util::math;
using Catch::Matchers::WithinAbs;

TEST_CASE("hash01 golden values (pins the tree layout)", "[hash]") {
    // x = slot * 2654435761u; x ^= x >> 16; x / UINT32_MAX  (values computed independently).
    CHECK(um::hash01(0) == 0.0f);
    CHECK_THAT(um::hash01(1), WithinAbs(0.61804056f, 1e-6f));
    CHECK_THAT(um::hash01(2), WithinAbs(0.23606580f, 1e-6f));
    CHECK_THAT(um::hash01(5000), WithinAbs(0.16993538f, 1e-6f));
    CHECK_THAT(um::hash01(10000), WithinAbs(0.33987075f, 1e-6f));
}

TEST_CASE("hash01 is in 0 to 1 deterministic and well spread", "[hash]") {
    std::set<float> seen;
    for (int slot = -500; slot < 500; ++slot) {
        const float v = um::hash01(slot);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
        REQUIRE(v == um::hash01(slot));
        seen.insert(v);
    }
    CHECK(seen.size() > 990);  // essentially no collisions over 1000 slots
}
