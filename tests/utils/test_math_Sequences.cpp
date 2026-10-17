// utils/math/Sequences.h: Halton sequence (TAA jitter).
#include "utils/math/Sequences.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace um = swish::util::math;
using Catch::Matchers::WithinAbs;

TEST_CASE("halton base 2 is the van der Corput sequence", "[sequences]") {
    CHECK(um::halton(0, 2) == 0.0f);
    CHECK_THAT(um::halton(1, 2), WithinAbs(0.5f, 1e-7f));
    CHECK_THAT(um::halton(2, 2), WithinAbs(0.25f, 1e-7f));
    CHECK_THAT(um::halton(3, 2), WithinAbs(0.75f, 1e-7f));
    CHECK_THAT(um::halton(4, 2), WithinAbs(0.125f, 1e-7f));
}

TEST_CASE("halton base 3 golden values", "[sequences]") {
    CHECK_THAT(um::halton(1, 3), WithinAbs(1.0f / 3.0f, 1e-6f));
    CHECK_THAT(um::halton(2, 3), WithinAbs(2.0f / 3.0f, 1e-6f));
    CHECK_THAT(um::halton(3, 3), WithinAbs(1.0f / 9.0f, 1e-6f));
    CHECK_THAT(um::halton(4, 3), WithinAbs(1.0f / 3.0f + 1.0f / 9.0f, 1e-6f));
}

TEST_CASE("halton stays in 0 to 1 and the 8-sample TAA window has no repeats", "[sequences]") {
    for (uint32_t i = 0; i < 4096; ++i) {
        CHECK(um::halton(i, 2) >= 0.0f);
        CHECK(um::halton(i, 2) < 1.0f);
        CHECK(um::halton(i, 3) < 1.0f);
    }
    for (uint32_t a = 1; a <= 8; ++a)
        for (uint32_t b = a + 1; b <= 8; ++b) CHECK(um::halton(a, 2) != um::halton(b, 2));
}
