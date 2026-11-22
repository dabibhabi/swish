// utils/units/Quantity.h + the typed conversions in Units.h: every operator and conversion must match the raw
// float expression bit for bit (the FMA rule: Quantity is for APIs, never inside a hot expression).
#include "utils/meta/Concepts.h"
#include "utils/units/Quantity.h"
#include "utils/units/Units.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <glm/trigonometric.hpp>
#include <limits>
#include <type_traits>
#include <vector>

namespace uu = swish::util::units;

namespace {
template <class A, class B>
concept Addable = requires(A a, B b) { a + b; };

static_assert(Addable<uu::Metres, uu::Metres> && !Addable<uu::Metres, uu::Seconds> && !Addable<uu::Metres, float>);
static_assert(!std::is_convertible_v<float, uu::Wu> && !std::is_convertible_v<uu::Wu, float>);
static_assert(sizeof(uu::Metres) == sizeof(float) && alignof(uu::Metres) == alignof(float));
static_assert(swish::util::meta::TriviallyPackable<uu::Rpm>);
static_assert(std::is_same_v<decltype(uu::Metres{} / uu::Metres{}), float>);
static_assert((uu::Metres{1.0f} + uu::Metres{2.0f}).value() == 3.0f);

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

std::vector<float> sweep() {
    return {0.0f,
            -0.0f,
            1.0f,
            -1.0f,
            0.1f,
            3.14159f,
            -2.5f,
            1234.567f,
            1.0e6f,
            -7.0e5f,
            4.0e7f,
            1e-30f,
            std::numeric_limits<float>::denorm_min(),
            -std::numeric_limits<float>::denorm_min(),
            0.3048f,
            1000.0f,
            6.0f,
            3.0f};
}
}  // namespace

TEST_CASE("Quantity arithmetic matches raw float bit for bit", "[units][quantity]") {
    for (float a : sweep())
        for (float b : sweep()) {
            CHECK(bits((uu::Metres{a} + uu::Metres{b}).value()) == bits(a + b));
            CHECK(bits((uu::Metres{a} - uu::Metres{b}).value()) == bits(a - b));
            CHECK(bits((uu::Metres{a} * b).value()) == bits(a * b));
            CHECK(bits((b * uu::Metres{a}).value()) == bits(b * a));
            CHECK(bits((-uu::Metres{a}).value()) == bits(-a));
            if (b != 0.0f) CHECK(bits((uu::Metres{a} / b).value()) == bits(a / b));
        }
    uu::Metres x{5.0f};
    x += uu::Metres{2.0f};
    x -= uu::Metres{1.0f};
    x *= 2.0f;
    x /= 4.0f;
    CHECK(x.value() == 3.0f);
}

TEST_CASE("Quantity ratio of the same tag is a plain float", "[units][quantity]") {
    const auto r = uu::Metres{6.0f} / uu::Metres{3.0f};
    STATIC_REQUIRE(std::is_same_v<std::remove_cv_t<decltype(r)>, float>);
    CHECK(r == 2.0f);
}

TEST_CASE("Quantity compares like its value", "[units][quantity]") {
    for (float a : sweep())
        for (float b : sweep()) {
            CHECK((uu::Metres{a} < uu::Metres{b}) == (a < b));
            CHECK((uu::Metres{a} > uu::Metres{b}) == (a > b));
            CHECK((uu::Metres{a} == uu::Metres{b}) == (a == b));
        }
}

TEST_CASE("Typed conversions match the raw Units helpers", "[units][quantity]") {
    for (float x : sweep()) {
        CHECK(bits(uu::to_wu(uu::Metres{x}).value()) == bits(uu::m(x)));
        CHECK(bits(uu::to_metres(uu::Wu{x}).value()) == bits(uu::to_m(x)));
    }
}

TEST_CASE("Degree and radian conversions match glm bit for bit", "[units][quantity]") {
    for (int i = -2880; i <= 2880; ++i) {
        const float d = static_cast<float>(i) * 0.25f;
        CHECK(bits(uu::to_radians(uu::Degrees{d}).value()) == bits(glm::radians(d)));
        CHECK(bits(uu::to_degrees(uu::Radians{d}).value()) == bits(glm::degrees(d)));
    }
}
