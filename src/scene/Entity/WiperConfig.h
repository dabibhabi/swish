#pragma once

// WiperConfig: the binary bake of config/wiper.toml (toml_baker wiper <in> <out>, CMake writes
// build/config/wiper.bin). Release reads only this bake; debug builds start from it and tune the
// same numbers live (Tunables.def group "wiper"). Same pattern as RoadConfig / road.bin.

#include "Wiper.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

namespace swish {

#pragma pack(push, 1)
struct WiperConfig {
    static constexpr uint32_t MAGIC   = 0x53575750;  // "SWWP"
    static constexpr uint32_t VERSION = 1;

    uint32_t magic;
    uint32_t version;
    float    low_wipes_per_min;
    float    high_wipes_per_min;
    float    intermittent_dwell_s;
    float    sweep_deg;
    float    ease;
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<WiperConfig>, "WiperConfig must be trivially copyable for binary I/O");
static_assert(sizeof(WiperConfig) == 28, "WiperConfig layout changed: bump WiperConfig::VERSION and re-bake");

[[nodiscard]] inline WiperParams wiper_params_from_config(const WiperConfig& c) {
    WiperParams p;
    p.lowWipesPerMin  = c.low_wipes_per_min;
    p.highWipesPerMin = c.high_wipes_per_min;
    p.dwellS          = c.intermittent_dwell_s;
    p.sweepDeg        = c.sweep_deg;
    p.ease            = c.ease;
    return p;
}

// The baked wiper settings, or WiperParams{} (== the TOML defaults) with a warning when the
// file is missing or stale. A wiper is never worth refusing to start over.
[[nodiscard]] inline WiperParams load_wiper_params(const std::string& path) {
    WiperConfig c{};
    FILE*       f = std::fopen(path.c_str(), "rb");
    const bool  ok =
        f && std::fread(&c, sizeof(c), 1, f) == 1 && c.magic == WiperConfig::MAGIC && c.version == WiperConfig::VERSION;
    if (f)
        std::fclose(f);
    if (!ok) {
        std::fprintf(stderr, "WiperConfig: cannot read %s; using built-in defaults\n", path.c_str());
        return WiperParams{};
    }
    return wiper_params_from_config(c);
}

}  // namespace swish
