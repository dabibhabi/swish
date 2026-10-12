#pragma once

// Purpose:    Read the needle calibration (GaugeParams, GaugeNeedles.h) from the [gauges] table of
//             config/gauges.toml. Both builds read it at car load (release has no sliders, so the TOML
//             is its only source); missing keys keep `fallback`, an invalid speedo table is ignored.
// Units:      as config/gauges.toml (deg, rpm, km/h, ms for the follower tau, s for the sweep).
// Example:    car->set_gauge_params(load_gauge_params(std::string(CONFIG_SRC_DIR) + "gauges.toml"));
// See also:   GaugeNeedles.h, config/gauges.toml.
// Test:       tests/test_gauge_needles.cpp ([gauges][config])

#include "GaugeNeedles.h"

#include <string>
#include <string_view>

namespace swish {

// Parse TOML text; returns `fallback` on a parse error (and logs it).
GaugeParams parse_gauge_params(std::string_view toml_text, const GaugeParams& fallback = kGaugeDefaults);

// Read a file; returns `fallback` when the file is missing or unparsable (and logs it).
GaugeParams load_gauge_params(const std::string& path, const GaugeParams& fallback = kGaugeDefaults);

}  // namespace swish
