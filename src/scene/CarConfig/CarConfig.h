#pragma once

// Car visual configuration read from config/car.toml at startup (both builds).
// Purpose:  the release-build values of car visual tunables (today: Ackermann); debug
//           builds seed the matching Tunables.def sliders from the same file.
// Loader:   CarConfig::load (CarConfig.cpp, toml++). Missing file or key -> the member
//           defaults below, which mirror config/car.toml.
// Test:     tests/test_wheel_articulation.cpp ([car][config])

#include <string>

namespace swish {

// [wheels] table.
struct CarWheelConfig {
    float ackermann = 1.0f;  // 0 = parallel front steer, 1 = pure Ackermann (draw-only)
};

struct CarConfig {
    CarWheelConfig wheels{};

    // Parse `path` (config/car.toml). Never throws: a missing or unparsable file prints one
    // line to stderr and returns the defaults; values are clamped to their valid range.
    static CarConfig load(const std::string& path);
};

}  // namespace swish
