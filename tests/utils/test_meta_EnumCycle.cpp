// utils/meta/EnumCycle.h: enum_count / next_enum, plus parity with the two real enums that use it.
#include "scene/CameraMode.h"
#include "scene/WeatherPreset.h"
#include "utils/meta/EnumCycle.h"

#include <catch2/catch_test_macros.hpp>

namespace um = swish::util::meta;

namespace {
enum class Mode : uint8_t { A = 0, B, C, Count };
static_assert(um::CountedEnum<Mode>);
static_assert(!um::CountedEnum<int>);
// Count == 0 would make next_enum take a modulo by zero: rejected by the concept.
enum class Empty : uint8_t { Count = 0 };
static_assert(!um::CountedEnum<Empty>);
static_assert(um::enum_count<Mode>() == 3);
static_assert(um::next_enum(Mode::A) == Mode::B);
static_assert(um::next_enum(Mode::C) == Mode::A);
}  // namespace

TEST_CASE("next_enum walks every value once and wraps", "[enumcycle]") {
    Mode m = Mode::A;
    m      = um::next_enum(m);
    CHECK(m == Mode::B);
    m = um::next_enum(m);
    CHECK(m == Mode::C);
    m = um::next_enum(m);
    CHECK(m == Mode::A);
}

TEST_CASE("CameraMode and WeatherPreset keep their old counts and cycle order", "[enumcycle]") {
    using swish::CameraMode;
    using swish::WeatherPreset;
    CHECK(swish::camera_mode_count() == 8);
    CHECK(swish::weather_preset_count() == 4);

    CameraMode c = CameraMode::Cockpit;
    for (int i = 1; i < 8; ++i) {
        c = swish::next_camera_mode(c);
        CHECK(static_cast<int>(c) == i);
    }
    CHECK(swish::next_camera_mode(c) == CameraMode::Cockpit);  // FreeFly wraps to Cockpit

    WeatherPreset w = WeatherPreset::PartlyCloudy;
    for (int i = 1; i < 4; ++i) {
        w = swish::next_weather_preset(w);
        CHECK(static_cast<int>(w) == i);
    }
    CHECK(swish::next_weather_preset(w) == WeatherPreset::PartlyCloudy);
}
