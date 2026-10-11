// Physics bench on the real CarEntity::update (GPU-free). Tolerances vs the 992 Turbo S reference are
// DOCUMENTED TARGETS printed in a table; the tests only assert: finishes, finite, deterministic.
#include "bench/Bench.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace swish;

TEST_CASE("bench: scenarios finish, are finite and deterministic", "[bench]") {
    const std::vector<bench::BenchResult> a = {bench::zero_to_100(), bench::braking_100_0(), bench::top_speed(),
                                               bench::skidpad_40m()};
    const std::vector<bench::BenchResult> b = {bench::zero_to_100(), bench::braking_100_0(), bench::top_speed(),
                                               bench::skidpad_40m()};
    bench::print_table(a);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        INFO(a[i].name);
        CHECK(a[i].finished);
        CHECK(a[i].finite);
        CHECK(std::isfinite(a[i].measured));
        CHECK(a[i].measured > 0.0);
        CHECK(a[i].measured == b[i].measured);  // bitwise-identical rerun
    }
}
