#pragma once

#include <array>
#include <string_view>

namespace swish {

struct HdriOption {
    std::string_view label;
    std::string_view file;
    float            exposure;
    float            yaw;
};

inline constexpr std::array<HdriOption, 8> kHdriOptions{{
    {"Citrus Orchard Road (sunrise)", "citrus_orchard_road_puresky_2k.hdr", 0.85f, 0.0f},
    {"Quarry Cloudy", "quarry_cloudy_2k.hdr", 0.90f, 0.0f},
    {"Overcast Soil", "overcast_soil_puresky_2k.hdr", 1.00f, 0.0f},
    {"Belfast Sunset", "belfast_sunset_puresky_2k.hdr", 0.75f, 0.6f},
    {"Sunflowers (midday)", "sunflowers_puresky_2k.hdr", 0.85f, 0.0f},
    {"Derelict Airfield", "derelict_airfield_01_2k.hdr", 0.85f, 0.0f},
    {"Quarry 04", "quarry_04_puresky_2k.hdr", 0.85f, 0.0f},
    {"Urban Street 03", "urban_street_03_2k.hdr", 0.85f, 0.0f},
}};

inline int hdri_option_index(std::string_view file) {
    for (size_t i = 0; i < kHdriOptions.size(); ++i)
        if (kHdriOptions[i].file == file) return static_cast<int>(i);
    return -1;
}

}  // namespace swish
