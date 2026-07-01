#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/RainSystem/RainPhysics.h"

using namespace swish;
using Catch::Matchers::WithinAbs;

// Exercises the rain microphysics (P0 #7 terminal velocity, #8 drop-size
// distribution). Pure math — mirrors rain.vert's GLSL. No GPU.

TEST_CASE("Marshall-Palmer slope decreases with rain rate", "[rain]") {
    // Heavier rain → smaller Λ → larger typical drops.
    REQUIRE(marshall_palmer_lambda(5.0f) > marshall_palmer_lambda(25.0f));
}

TEST_CASE("Drop diameter: smaller u yields larger drops (monotone inverse CDF)", "[rain]") {
    const float R = 20.0f;
    REQUIRE(sample_drop_diameter(0.9f, R) < sample_drop_diameter(0.1f, R));
    // All samples stay within the physical clamp.
    REQUIRE(sample_drop_diameter(1.0f, R) >= 0.1f);
    REQUIRE(sample_drop_diameter(0.0f, R) <= 6.0f);
}

TEST_CASE("Mean sampled diameter tracks Marshall-Palmer 1/Lambda", "[rain]") {
    const float R      = 25.0f;
    const float lambda = marshall_palmer_lambda(R);
    // E[D] of an exponential is 1/Λ. Average the inverse-CDF over a uniform grid.
    const int N = 20000;
    double    sum = 0.0;
    for (int i = 0; i < N; ++i) {
        float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(N);
        sum += sample_drop_diameter(u, R);
    }
    float mean = static_cast<float>(sum / N);
    REQUIRE_THAT(mean, WithinAbs(1.0f / lambda, 0.15f));  // ≈0.50 mm at R=25
}

TEST_CASE("Gunn-Kinzer velocity increases with diameter and is bounded", "[rain]") {
    float v05 = gunn_kinzer_speed(0.5f);
    float v2  = gunn_kinzer_speed(2.0f);
    float v5  = gunn_kinzer_speed(5.0f);
    REQUIRE(v05 < v2);
    REQUIRE(v2 < v5);
    // Physical band: sub-mm drops ~2 m/s, biggest approach the 9.65 m/s asymptote.
    REQUIRE(v05 >= 0.5f);
    REQUIRE(v5 <= 9.65f);
    // The old model used a single 9 m/s for ALL drops; typical drops fall slower.
    REQUIRE(gunn_kinzer_speed(1.0f) < 9.0f);
}

TEST_CASE("Intensity maps linearly to rain rate", "[rain]") {
    REQUIRE_THAT(intensity_to_rain_rate(0.0f, 25.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(intensity_to_rain_rate(1.0f, 25.0f), WithinAbs(25.0f, 1e-6f));
    REQUIRE_THAT(intensity_to_rain_rate(0.5f, 25.0f), WithinAbs(12.5f, 1e-6f));
}
