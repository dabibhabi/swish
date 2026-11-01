#pragma once

// Car visual configuration read from config/car.toml at startup (both builds).
// Purpose:  the release-build values of car tunables ([wheels] Ackermann, [gauges] needle
//           calibration, [wipers] rates and sweep, [keyboard_steer] arrow-key steering); debug builds seed the
//           matching Tunables.def sliders from the same file.
// Loader:   CarConfig::load (CarConfig.cpp, toml++). Missing file or key -> the member
//           defaults below, which mirror config/car.toml.
// Test:     tests/test_wheel_articulation.cpp ([car][config]), tests/test_gauge_needles.cpp ([gauges][config])

#include "scene/Entity/GaugeNeedles.h"   // GaugeParams, kGaugeDefaults
#include "scene/Entity/KeyboardSteer.h"  // KeyboardSteerParams
#include "scene/Entity/Wiper.h"          // WiperParams

#include <string>
#include <string_view>

namespace swish {

// [wheels] table.
struct CarWheelConfig {
    float ackermann = 1.0f;  // 0 = parallel front steer, 1 = pure Ackermann (draw-only)
};

struct CarConfig {
    CarWheelConfig wheels{};
    // [gauges] table: dial-needle calibration (only cars with Needle_* pivots use it). Missing keys keep
    // kGaugeDefaults; an invalid speedo table is ignored; invalid values reset the table to the defaults.
    GaugeParams gauges = kGaugeDefaults;
    // [wipers] table: V-cycle rates, intermittent dwell, sweep and ease (Wiper.h). Missing keys keep the
    // WiperParams defaults; out-of-range values are clamped.
    WiperParams wipers{};
    // [keyboard_steer] table: arrow-key stick rates and the speed limit on full stick (KeyboardSteer.h).
    // Missing keys keep kKeyboardSteerDefaults; out-of-range values are clamped.
    KeyboardSteerParams keyboardSteer = kKeyboardSteerDefaults;

    // Parse `path` (config/car.toml). Never throws: a missing or unparsable file prints one
    // line to stderr and returns the defaults; values are clamped to their valid range.
    static CarConfig load(const std::string& path);
    // Same, from TOML text (tests).
    static CarConfig parse(std::string_view toml_text);
};

}  // namespace swish
