#pragma once

// Time-of-day presets (tasks/todo.md "Lighting", phases L0.7 and L2): the data one preset holds, the sun
// direction from azimuth and elevation, and the config/time_of_day.toml reader. Header-only and GPU-free so
// tests/test_time_of_day.cpp can pin each rule. WeatherController loads the six presets from the TOML in debug
// builds (lighting L2.3); see docs/lighting-l2-l10.md.

#include "utils/units/Units.h"  // kRadPerDeg, for sun_dir_from_az_el

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <string_view>
#include <toml++/toml.hpp>

namespace swish {

enum class TimeOfDay : uint8_t { Dawn = 0, Morning, Noon, GoldenHour, Evening, Midnight, Count };

// The table name of each preset in config/time_of_day.toml.
[[nodiscard]] inline std::string_view time_of_day_key(TimeOfDay t) {
    switch (t) {
        case TimeOfDay::Dawn:
            return "dawn";
        case TimeOfDay::Morning:
            return "morning";
        case TimeOfDay::Noon:
            return "noon";
        case TimeOfDay::GoldenHour:
            return "golden_hour";
        case TimeOfDay::Evening:
            return "evening";
        case TimeOfDay::Midnight:
            return "midnight";
        default:
            return "golden_hour";
    }
}

// One preset. Defaults are golden hour, the first preset to sign off.
struct TimeOfDayPreset {
    float sunElevationDeg = 6.0f;      // above the horizon; negative = below (twilight, night)
    float sunAzimuthDeg   = 250.0f;    // see sun_dir_from_az_el for the axis convention
    float sunLux          = 20000.0f;  // direct sun on a surface facing it; 0 when the sun is down
    float moonLux         = 0.0f;      // full moon is about 0.2
    float turbidity       = 3.0f;      // haze: 2 = very clear, 10 = hazy
    float ev100Min        = 11.0f;     // auto-exposure clamp
    float ev100Max        = 14.0f;
    bool  lampsOn         = false;  // street lamps and headlights
};

// Elevation in [-90, 90], lux and turbidity not negative, ev100Min <= ev100Max.
[[nodiscard]] inline bool time_of_day_preset_valid(const TimeOfDayPreset& p) {
    return p.sunElevationDeg >= -90.0f && p.sunElevationDeg <= 90.0f && p.sunLux >= 0.0f && p.moonLux >= 0.0f &&
           p.turbidity >= 0.0f && p.ev100Min <= p.ev100Max;
}

// Unit vector pointing from the scene TOWARD the sun (the convention WeatherController's m_sunDir uses).
// Y is up. Azimuth is measured from +Z toward +X:
//   elevation 90            -> (0, 1, 0)
//   elevation 0, azimuth 0  -> (0, 0, 1)
//   elevation 0, azimuth 90 -> (1, 0, 0)
[[nodiscard]] inline glm::vec3 sun_dir_from_az_el(float azimuthDeg, float elevationDeg) {
    const float az = azimuthDeg * util::units::kRadPerDeg;
    const float el = elevationDeg * util::units::kRadPerDeg;
    return glm::vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
}

// Read the preset table for `which` (keys: sun_elevation_deg, sun_azimuth_deg, sun_lux, moon_lux, turbidity,
// ev100_min, ev100_max, lamps_on). A missing key keeps the fallback's value. A missing table or an invalid
// result returns `fallback` unchanged. Same shape as gauges_from_table in scene/CarConfig/CarConfig.cpp.
[[nodiscard]] inline TimeOfDayPreset time_of_day_from_table(const toml::table& tbl, TimeOfDay which,
                                                            const TimeOfDayPreset& fallback = {}) {
    const toml::table* t = tbl[time_of_day_key(which)].as_table();
    if (!t) return fallback;
    TimeOfDayPreset p = fallback;
    p.sunElevationDeg = (*t)["sun_elevation_deg"].value_or(p.sunElevationDeg);
    p.sunAzimuthDeg   = (*t)["sun_azimuth_deg"].value_or(p.sunAzimuthDeg);
    p.sunLux          = (*t)["sun_lux"].value_or(p.sunLux);
    p.moonLux         = (*t)["moon_lux"].value_or(p.moonLux);
    p.turbidity       = (*t)["turbidity"].value_or(p.turbidity);
    p.ev100Min        = (*t)["ev100_min"].value_or(p.ev100Min);
    p.ev100Max        = (*t)["ev100_max"].value_or(p.ev100Max);
    p.lampsOn         = (*t)["lamps_on"].value_or(p.lampsOn);
    return time_of_day_preset_valid(p) ? p : fallback;
}

// Display names, in enum order (the debug panel and the T key cycle through these).
[[nodiscard]] inline const char* time_of_day_name(TimeOfDay t) {
    switch (t) {
        case TimeOfDay::Dawn:
            return "Dawn";
        case TimeOfDay::Morning:
            return "Morning";
        case TimeOfDay::Noon:
            return "Noon";
        case TimeOfDay::GoldenHour:
            return "Golden hour";
        case TimeOfDay::Evening:
            return "Evening";
        case TimeOfDay::Midnight:
            return "Midnight";
        default:
            return "?";
    }
}

}  // namespace swish
