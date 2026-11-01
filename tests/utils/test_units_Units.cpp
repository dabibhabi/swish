// utils/units/Units.h: single source of the WU scale. Most guarantees are static_asserts in the header itself;
// these cases pin runtime behaviour and bit-identity against the legacy literals.
#include "utils/units/Units.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstring>

namespace u = swish::util::units;

namespace {
bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
}  // namespace

TEST_CASE("Units: 1 m == 1000 WU and 1 ft == 304.8 WU", "[units]") {
    STATIC_REQUIRE(u::m(1.0f) == 1000.0f);
    STATIC_REQUIRE(u::kWuPerMetre == 1000.0f);
    CHECK_THAT(u::ft(1.0f), Catch::Matchers::WithinAbs(304.8f, 1e-3f));
    CHECK_THAT(u::to_m(u::m(3.7f)), Catch::Matchers::WithinRel(3.7f, 1e-6f));
    CHECK_THAT(u::to_ft(u::ft(13.0f)), Catch::Matchers::WithinRel(13.0f, 1e-6f));
}

TEST_CASE("Units: ft() is bit-identical to the legacy `x * (0.3048f * 1000.0f)`", "[units]") {
    const float legacy_kft = 0.3048f * 1000.0f;
    for (float x : {0.06f, 0.15f, 0.5f, 2.67f, 3.0f, 13.0f, 45.0f, 671.5f, 328.5f, 1000.0f})
        CHECK(same_bits(u::ft(x), x * legacy_kft));
}

TEST_CASE("Units: shared pi constants equal the old literals bit-for-bit", "[units]") {
    CHECK(same_bits(u::kPi, 3.14159265358979323846f));
    CHECK(same_bits(u::kTwoPi, 6.28318530717958647692f));
}
