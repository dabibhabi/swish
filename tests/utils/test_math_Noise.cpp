// utils/math/Noise.h: quasi_noise (camera shake).
#include "utils/math/Noise.h"
#include "utils/units/Units.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstring>

namespace um = swish::util::math;
using Catch::Matchers::WithinAbs;

TEST_CASE("quasi_noise stays within -1 to 1 over a long sweep", "[noise]") {
    for (int i = 0; i < 20000; ++i) {
        const float t = static_cast<float>(i) * 0.0137f;
        const float v = um::quasi_noise(t, 13.0f, 7.0f, 19.0f, 1.7f);
        REQUIRE(v <= 1.0f);
        REQUIRE(v >= -1.0f);
    }
}

TEST_CASE("quasi_noise is deterministic and bit-identical to the legacy CameraController expression", "[noise]") {
    constexpr float kPi = swish::util::units::kPi;
    for (float t : {0.0f, 0.25f, 1.0f, 3.7f, 12.5f}) {
        const float legacy = 0.55f * std::sin(2.0f * kPi * 11.0f * t + 1.7f) +
                             0.30f * std::sin(2.0f * kPi * 5.0f * t + 1.7f * 1.7f + 1.3f) +
                             0.15f * std::sin(2.0f * kPi * 17.0f * t + 1.7f * 2.3f + 2.6f);
        const float a      = um::quasi_noise(t, 11.0f, 5.0f, 17.0f, 1.7f);
        const float b      = um::quasi_noise(t, 11.0f, 5.0f, 17.0f, 1.7f);
        CHECK(std::memcmp(&a, &b, sizeof(float)) == 0);
        CHECK_THAT(a, WithinAbs(legacy, 1e-6f));
    }
}

TEST_CASE("quasi_noise phase decorrelates two axes", "[noise]") {
    const float a = um::quasi_noise(0.4f, 13.0f, 7.0f, 19.0f, 0.0f);
    const float b = um::quasi_noise(0.4f, 13.0f, 7.0f, 19.0f, 3.4f);
    CHECK(std::abs(a - b) > 1e-3f);
}
