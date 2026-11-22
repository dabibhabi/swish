// utils/container/VectorOps.h: affine(), filled().
#include "utils/container/VectorOps.h"

#include <catch2/catch_test_macros.hpp>

namespace uc = swish::util::container;

TEST_CASE("affine computes src*k + bias element-wise", "[container]") {
    const std::vector<float> src{0.0f, 1.0f, 2.0f, -3.0f};
    const auto               out = uc::affine(src, 2.0f, 0.5f);
    REQUIRE(out.size() == src.size());
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == 2.5f);
    CHECK(out[2] == 4.5f);
    CHECK(out[3] == -5.5f);
}

TEST_CASE("affine of an empty vector is empty the input is untouched", "[container]") {
    CHECK(uc::affine({}, 3.0f, 1.0f).empty());
    const std::vector<float> src{1.0f, 2.0f};
    (void)uc::affine(src, -1.0f, 0.0f);
    CHECK(src == std::vector<float>{1.0f, 2.0f});
}

TEST_CASE("filled makes n copies", "[container]") {
    const auto v = uc::filled(5, 7.5f);
    REQUIRE(v.size() == 5);
    for (float x : v) CHECK(x == 7.5f);
    CHECK(uc::filled(0, 1.0f).empty());
}
