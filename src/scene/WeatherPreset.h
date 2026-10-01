#pragma once

#include "utils/meta/EnumCycle.h"

#include <cstdint>
#include <string_view>

namespace swish {

// Driving-weather looks driven by a matching Radiance HDRI under assets/Sky/
// plus sun/clarity/ambient that roughly match that env. Cycle with G in App.
enum class WeatherPreset : uint8_t { PartlyCloudy = 0, Cloudy, VeryCloudy, GoldenHour, Count };

inline constexpr uint8_t weather_preset_count() {
    return util::meta::enum_count<WeatherPreset>();
}

inline WeatherPreset next_weather_preset(WeatherPreset p) {
    return util::meta::next_enum(p);
}

inline std::string_view weather_preset_name(WeatherPreset p) {
    switch (p) {
        case WeatherPreset::PartlyCloudy:
            return "partly cloudy";
        case WeatherPreset::Cloudy:
            return "cloudy";
        case WeatherPreset::VeryCloudy:
            return "very cloudy";
        case WeatherPreset::GoldenHour:
            return "golden hour";
        default:
            return "unknown";
    }
}

// "Clear day" presets: a sunny sky that reads wrong with rain falling.
constexpr bool is_clear_day(WeatherPreset p) {
    return p == WeatherPreset::PartlyCloudy || p == WeatherPreset::GoldenHour;
}

// The preset to show once rain is at `rain` (0..1): rain cancels a clear-day sky
// (it becomes Cloudy); any other preset is kept.
constexpr WeatherPreset preset_for_rain(WeatherPreset p, float rain) {
    return (rain > 0.0f && is_clear_day(p)) ? WeatherPreset::Cloudy : p;
}

// Filename under ASSET_DIR "Sky/" for the preset's environment map.
inline std::string_view weather_preset_hdri_file(WeatherPreset p) {
    switch (p) {
        case WeatherPreset::PartlyCloudy:
            return "citrus_orchard_road_puresky_2k.hdr";
        case WeatherPreset::Cloudy:
            return "quarry_cloudy_2k.hdr";
        case WeatherPreset::VeryCloudy:
            return "overcast_soil_puresky_2k.hdr";
        case WeatherPreset::GoldenHour:
            return "belfast_sunset_puresky_2k.hdr";
        default:
            return "citrus_orchard_road_puresky_2k.hdr";
    }
}

}  // namespace swish
