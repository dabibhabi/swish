#include "GaugeConfig.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <toml++/toml.hpp>

namespace swish {

namespace {

void read_float(const toml::table& g, std::string_view key, float& out) {
    if (const auto v = g[key].value<double>())
        out = static_cast<float>(*v);
}

// Both arrays present, same length, numeric, and a valid table, else `lut` is left alone.
void read_speedo(const toml::table& g, SpeedoLut& lut) {
    const toml::array* kmh = g["speedo_kmh"].as_array();
    const toml::array* deg = g["speedo_deg"].as_array();
    if (!kmh && !deg)
        return;
    SpeedoLut next;
    if (kmh && deg && kmh->size() == deg->size() && kmh->size() <= static_cast<size_t>(kSpeedoLutMax)) {
        next.count = static_cast<int>(kmh->size());
        for (size_t i = 0; i < kmh->size(); i++) {
            const auto k = kmh->get(i)->value<double>();
            const auto d = deg->get(i)->value<double>();
            if (!k || !d) {
                next.count = 0;
                break;
            }
            next.kmh[i] = static_cast<float>(*k);
            next.deg[i] = static_cast<float>(*d);
        }
    }
    if (speedo_lut_valid(next))
        lut = next;
    else
        std::cerr << "gauges: speedo_kmh / speedo_deg ignored (need 2-16 equal-length rows, km/h increasing)\n";
}

GaugeParams from_table(const toml::table& tbl, const GaugeParams& fallback) {
    GaugeParams p = fallback;
    const auto* g = tbl["gauges"].as_table();
    if (!g)
        return p;
    read_float(*g, "tach_offset_deg", p.tachOffsetDeg);
    read_float(*g, "tach_deg_per_krpm", p.tachDegPerKrpm);
    read_float(*g, "tach_min_rpm", p.tachMinRpm);
    read_float(*g, "tach_max_rpm", p.tachMaxRpm);
    read_speedo(*g, p.speedo);
    read_float(*g, "rpm_rest_deg", p.rpmRestDeg);
    read_float(*g, "speed_rest_deg", p.speedRestDeg);
    float tau_ms = p.needleTauS * 1000.f;
    read_float(*g, "needle_tau_ms", tau_ms);
    p.needleTauS = tau_ms / 1000.f;
    read_float(*g, "sweep_duration_s", p.sweepDurationS);
    if (!gauge_params_valid(p)) {
        std::cerr << "gauges: invalid [gauges] values, using the defaults\n";
        return fallback;
    }
    return p;
}

}  // namespace

GaugeParams parse_gauge_params(std::string_view toml_text, const GaugeParams& fallback) {
    try {
        return from_table(toml::parse(toml_text), fallback);
    } catch (const toml::parse_error& err) {
        std::cerr << "gauges: TOML parse error: " << err.description() << "\n";
        return fallback;
    }
}

GaugeParams load_gauge_params(const std::string& path, const GaugeParams& fallback) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "gauges: '" << path << "' not found, using the built-in calibration\n";
        return fallback;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_gauge_params(ss.str(), fallback);
}

}  // namespace swish
