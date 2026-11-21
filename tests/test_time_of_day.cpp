// Time-of-day presets (scene/TimeOfDay/TimeOfDay.h), the learning scaffold for tasks/todo.md "Lighting"
// phases L0.7 and L2. Hidden ([.learning]): `make test` skips them; run
// `build/tests/swish_tests "[learning][time_of_day]"`. They fail until the stubs are filled in.
// When they pass, drop `.learning` from the tags so ctest guards them.

#include "scene/TimeOfDay/TimeOfDay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace swish;
using Catch::Approx;

TEST_CASE("L0: sun direction from azimuth and elevation", "[lighting][time_of_day][L0]") {
    const glm::vec3 up = sun_dir_from_az_el(123.0f, 90.0f);
    CHECK(up.y == Approx(1.0f));

    const glm::vec3 z = sun_dir_from_az_el(0.0f, 0.0f);
    CHECK(z.x == Approx(0.0f).margin(1e-5));
    CHECK(z.y == Approx(0.0f).margin(1e-5));
    CHECK(z.z == Approx(1.0f));

    const glm::vec3 x = sun_dir_from_az_el(90.0f, 0.0f);
    CHECK(x.x == Approx(1.0f));
    CHECK(x.z == Approx(0.0f).margin(1e-5));

    CHECK(sun_dir_from_az_el(250.0f, -4.0f).y < 0.0f);  // below the horizon
    for (float az : {0.0f, 80.0f, 250.0f})
        for (float el : {-40.0f, 6.0f, 60.0f}) CHECK(glm::length(sun_dir_from_az_el(az, el)) == Approx(1.0f));
}

TEST_CASE("L2: preset ranges are validated", "[lighting][time_of_day][L2]") {
    CHECK(time_of_day_preset_valid(TimeOfDayPreset{}));
    TimeOfDayPreset p;
    p.ev100Min = 12.0f;
    p.ev100Max = 8.0f;
    CHECK_FALSE(time_of_day_preset_valid(p));
    p        = {};
    p.sunLux = -1.0f;
    CHECK_FALSE(time_of_day_preset_valid(p));
    p                 = {};
    p.sunElevationDeg = 120.0f;
    CHECK_FALSE(time_of_day_preset_valid(p));
}

TEST_CASE("L2: a preset table is read key by key", "[lighting][time_of_day][L2]") {
    const toml::table     tbl  = toml::parse(R"(
        [noon]
        sun_elevation_deg = 60.0
        sun_lux           = 100000.0
        ev100_min         = 14.0
        ev100_max         = 16.0
        lamps_on          = false
        [midnight]
        sun_elevation_deg = -40.0
        sun_lux           = 0.0
        moon_lux          = 0.2
        ev100_min         = -2.0
        ev100_max         = 4.0
        lamps_on          = true
    )");
    const TimeOfDayPreset noon = time_of_day_from_table(tbl, TimeOfDay::Noon);
    CHECK(noon.sunElevationDeg == Approx(60.0f));
    CHECK(noon.sunLux == Approx(100000.0f));
    CHECK(noon.ev100Max == Approx(16.0f));
    CHECK(noon.turbidity == Approx(TimeOfDayPreset{}.turbidity));  // key absent: fallback value kept

    const TimeOfDayPreset night = time_of_day_from_table(tbl, TimeOfDay::Midnight);
    CHECK(night.moonLux == Approx(0.2f));
    CHECK(night.lampsOn);
}

TEST_CASE("L2: a missing or invalid table falls back", "[lighting][time_of_day][L2]") {
    TimeOfDayPreset fallback;
    fallback.sunLux = 1234.0f;

    const toml::table empty = toml::parse("");
    CHECK(time_of_day_from_table(empty, TimeOfDay::Dawn, fallback).sunLux == Approx(1234.0f));

    const toml::table bad = toml::parse("[dawn]\nsun_lux = 5000.0\nev100_min = 12.0\nev100_max = 8.0\n");
    CHECK(time_of_day_from_table(bad, TimeOfDay::Dawn, fallback).sunLux == Approx(1234.0f));
}

TEST_CASE("L2: the shipped config has six valid presets", "[lighting][time_of_day][L2]") {
    const toml::table tbl = toml::parse_file(ASSET_DIR "../config/time_of_day.toml");
    for (int i = 0; i < static_cast<int>(TimeOfDay::Count); i++) {
        const auto which = static_cast<TimeOfDay>(i);
        INFO(time_of_day_key(which));
        REQUIRE(tbl[time_of_day_key(which)].is_table());
        CHECK(time_of_day_preset_valid(time_of_day_from_table(tbl, which)));
    }
    CHECK(time_of_day_from_table(tbl, TimeOfDay::Noon).sunLux > time_of_day_from_table(tbl, TimeOfDay::Dawn).sunLux);
    CHECK(time_of_day_from_table(tbl, TimeOfDay::Midnight).lampsOn);
    CHECK_FALSE(time_of_day_from_table(tbl, TimeOfDay::Noon).lampsOn);
}
