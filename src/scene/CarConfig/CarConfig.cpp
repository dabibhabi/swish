#include "CarConfig.h"

#include <algorithm>
#include <iostream>
#include <toml++/toml.hpp>

namespace swish {

CarConfig CarConfig::load(const std::string& path) {
    CarConfig   cfg;
    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        std::cerr << "[CarConfig] " << path << ": " << e.description() << " (using defaults)\n";
        return cfg;
    }
    cfg.wheels.ackermann = std::clamp(tbl["wheels"]["ackermann"].value_or(cfg.wheels.ackermann), 0.f, 1.f);
    return cfg;
}

}  // namespace swish
