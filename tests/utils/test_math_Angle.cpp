// utils/math/Angle.h: each wrap is a verbatim copy of the idiom it replaced; the legacy ones are checked against a
// hand copy of the old expression bit for bit, and the new wrap_deg_180 against its documented range.
#include "utils/math/Angle.h"
#include "utils/units/Units.h"

#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace um = swish::util::math;
namespace uu = swish::util::units;

namespace {
std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// Hand copies of the old expressions (WheelKinematics.h, Smoothing.h, CarEntity.cpp).
float old_wrap_angle_2pi(float theta) {
    theta = std::fmod(theta, uu::kTwoPi);
    return theta < 0.0f ? theta + uu::kTwoPi : theta;
}
float old_smoothing_delta(float x, float target) { return std::fmod(target - x + 540.0f, 360.0f) - 180.0f; }
float old_heading_wrap(float y) { return std::fmod(y + 180.f, 360.f) - 180.f; }

std::vector<float> rad_sweep() {
    std::vector<float> v{
        0.0f,   -0.0f,   uu::kPi, -uu::kPi, uu::kTwoPi, -uu::kTwoPi, 3.0f * uu::kTwoPi, -5.0f * uu::kTwoPi,
        1.0e4f, -1.0e4f, 0.001f,  -0.001f};
    for (int i = -400; i <= 400; ++i) v.push_back(static_cast<float>(i) * 0.137f);
    return v;
}
std::vector<float> deg_sweep() {
    std::vector<float> v{0.0f, -0.0f, 179.0f, -179.0f, 180.0f, -180.0f, 359.0f, -359.0f, 540.0f, -540.0f, 720.0f};
    for (int i = -400; i <= 400; ++i) v.push_back(static_cast<float>(i) * 1.37f);
    return v;
}
}  // namespace

TEST_CASE("wrap_2pi matches the old wrap_angle_2pi bit for bit", "[angle]") {
    for (float r : rad_sweep()) { CHECK(bits(um::wrap_2pi(r)) == bits(old_wrap_angle_2pi(r))); }
    CHECK(um::wrap_2pi(-0.0f) == 0.0f);
    for (float r : rad_sweep()) {
        const float w = um::wrap_2pi(r);
        CHECK(w >= 0.0f);
        CHECK(w <= uu::kTwoPi);
    }
}

TEST_CASE("shortest_delta_deg matches the old Smoothing.h expression", "[angle]") {
    for (float from : deg_sweep())
        for (float to : deg_sweep())
            CHECK(bits(um::shortest_delta_deg(from, to)) == bits(old_smoothing_delta(from, to)));
    CHECK(um::shortest_delta_deg(179.0f, -179.0f) == 2.0f);
    CHECK(um::shortest_delta_deg(-179.0f, 179.0f) == -2.0f);
}

TEST_CASE("wrap_heading_deg_legacy keeps today's range", "[angle]") {
    for (float d : deg_sweep()) CHECK(bits(um::wrap_heading_deg_legacy(d)) == bits(old_heading_wrap(d)));
    CHECK(um::wrap_heading_deg_legacy(-190.0f) == -190.0f);
    CHECK(um::wrap_heading_deg_legacy(-541.0f) == -181.0f);
    CHECK(um::wrap_heading_deg_legacy(190.0f) == -170.0f);
}

TEST_CASE("wrap_deg_180 lands in the half-open range above -180 up to 180", "[angle]") {
    CHECK(um::wrap_deg_180(-190.0f) == 170.0f);
    CHECK(um::wrap_deg_180(180.0f) == 180.0f);
    CHECK(um::wrap_deg_180(-180.0f) == 180.0f);
    CHECK(um::wrap_deg_180(540.0f) == 180.0f);
    CHECK(um::wrap_deg_180(-306.68567f) == Catch::Approx(53.31433f).margin(1e-4));
    for (float d : deg_sweep()) {
        const float w = um::wrap_deg_180(d);
        CHECK(w > -180.0f);
        CHECK(w <= 180.0f);
    }
}
