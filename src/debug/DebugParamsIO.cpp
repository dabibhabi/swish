#include "DebugParamsIO.h"

#ifdef SWISH_DEBUG_UI

#include <toml++/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace swish::debugio {

namespace {

namespace fs = std::filesystem;

// vec3 → 3-element TOML array (stored as doubles; float precision is fine here).
toml::array arr3(const glm::vec3& v) { return toml::array{v.x, v.y, v.z}; }

// Read a 3-element array node into `v`, keeping `v` if absent/malformed.
void rd3(toml::node_view<toml::node> n, glm::vec3& v) {
    const toml::array* a = n.as_array();
    if (!a || a->size() != 3)
        return;
    v.x = static_cast<float>(a->get(0)->value_or(static_cast<double>(v.x)));
    v.y = static_cast<float>(a->get(1)->value_or(static_cast<double>(v.y)));
    v.z = static_cast<float>(a->get(2)->value_or(static_cast<double>(v.z)));
}

}  // namespace

std::string presets_dir() { return std::string(CONFIG_DIR) + "presets/"; }

bool save(const DebugParams& p, const std::string& name) {
    std::error_code ec;
    fs::create_directories(presets_dir(), ec);  // idempotent; ignore "already exists"

    // Build the document grouped exactly like the panel so hand-editing is easy.
    toml::table tbl{
        {"grade",
         toml::table{{"exposure", p.exposure},
                     {"bloom_threshold", p.bloomThreshold},
                     {"bloom_intensity", p.bloomIntensity},
                     {"brightness", p.brightness},
                     {"contrast", p.contrast},
                     {"saturation", p.saturation},
                     {"temperature", p.temperature},
                     {"tint", p.tint}}},
        {"sky",
         toml::table{{"horizon_overcast", arr3(p.skyHorizonOvercast)},
                     {"horizon_clear", arr3(p.skyHorizonClear)},
                     {"zenith_overcast", arr3(p.skyZenithOvercast)},
                     {"zenith_clear", arr3(p.skyZenithClear)},
                     {"clarity", p.clarity},
                     {"sun_disc_exp_min", p.sunDiscExpMin},
                     {"sun_disc_exp_max", p.sunDiscExpMax},
                     {"sun_disc_str_min", p.sunDiscStrMin},
                     {"sun_disc_str_max", p.sunDiscStrMax}}},
        {"sun",
         toml::table{{"color", arr3(p.sunColor)},
                     {"ambient", p.sunAmbient},
                     {"azimuth", p.sunAzimuth},
                     {"elevation", p.sunElevation}}},
        {"fog",
         toml::table{{"color", arr3(p.fogColor)}, {"dist63", p.fogDist63}, {"max", p.fogMax}}},
        {"reflection", toml::table{{"env_gloss_exp", p.envGlossExp}}},
        {"ssao",
         toml::table{{"enabled", p.ssaoEnabled},
                     {"radius", p.ssaoRadius},
                     {"bias", p.ssaoBias},
                     {"intensity", p.ssaoIntensity}}},
        {"shadow",
         toml::table{{"bias", p.shadowBias},
                     {"floor", p.shadowFloor},
                     {"half_extent", p.shadowHalfExtent},
                     {"depth_range", p.shadowDepthRange},
                     {"depth_bias_const", p.depthBiasConst},
                     {"depth_bias_slope", p.depthBiasSlope}}},
        {"wet",
         toml::table{{"rain_intensity", p.rainIntensity},
                     {"porosity", p.wetPorosity},
                     {"roughness", p.wetRoughness},
                     {"streak_len", p.streakLen}}},
        {"car",
         toml::table{{"override", p.carOverride},
                     {"metalness", p.carMetalness},
                     {"paint", arr3(p.carPaint)},
                     {"roughness_mul", p.carRoughnessMul}}},
        {"quality", toml::table{{"ssaa_scale", p.ssaaScale}}},
    };

    std::ofstream f(presets_dir() + name + ".toml", std::ios::trunc);
    if (!f)
        return false;
    f << "# swish debug preset — edit freely; missing keys keep their in-app value\n" << tbl << '\n';
    return static_cast<bool>(f);
}

bool load(DebugParams& p, const std::string& name) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(presets_dir() + name + ".toml");
    } catch (const toml::parse_error&) {
        return false;
    }

    p.exposure       = tbl["grade"]["exposure"].value_or(p.exposure);
    p.bloomThreshold = tbl["grade"]["bloom_threshold"].value_or(p.bloomThreshold);
    p.bloomIntensity = tbl["grade"]["bloom_intensity"].value_or(p.bloomIntensity);
    p.brightness     = tbl["grade"]["brightness"].value_or(p.brightness);
    p.contrast       = tbl["grade"]["contrast"].value_or(p.contrast);
    p.saturation     = tbl["grade"]["saturation"].value_or(p.saturation);
    p.temperature    = tbl["grade"]["temperature"].value_or(p.temperature);
    p.tint           = tbl["grade"]["tint"].value_or(p.tint);

    rd3(tbl["sky"]["horizon_overcast"], p.skyHorizonOvercast);
    rd3(tbl["sky"]["horizon_clear"], p.skyHorizonClear);
    rd3(tbl["sky"]["zenith_overcast"], p.skyZenithOvercast);
    rd3(tbl["sky"]["zenith_clear"], p.skyZenithClear);
    p.clarity       = tbl["sky"]["clarity"].value_or(p.clarity);
    p.sunDiscExpMin = tbl["sky"]["sun_disc_exp_min"].value_or(p.sunDiscExpMin);
    p.sunDiscExpMax = tbl["sky"]["sun_disc_exp_max"].value_or(p.sunDiscExpMax);
    p.sunDiscStrMin = tbl["sky"]["sun_disc_str_min"].value_or(p.sunDiscStrMin);
    p.sunDiscStrMax = tbl["sky"]["sun_disc_str_max"].value_or(p.sunDiscStrMax);

    rd3(tbl["sun"]["color"], p.sunColor);
    p.sunAmbient   = tbl["sun"]["ambient"].value_or(p.sunAmbient);
    p.sunAzimuth   = tbl["sun"]["azimuth"].value_or(p.sunAzimuth);
    p.sunElevation = tbl["sun"]["elevation"].value_or(p.sunElevation);

    rd3(tbl["fog"]["color"], p.fogColor);
    p.fogDist63 = tbl["fog"]["dist63"].value_or(p.fogDist63);
    p.fogMax    = tbl["fog"]["max"].value_or(p.fogMax);

    p.envGlossExp = tbl["reflection"]["env_gloss_exp"].value_or(p.envGlossExp);

    p.ssaoEnabled   = tbl["ssao"]["enabled"].value_or(p.ssaoEnabled);
    p.ssaoRadius    = tbl["ssao"]["radius"].value_or(p.ssaoRadius);
    p.ssaoBias      = tbl["ssao"]["bias"].value_or(p.ssaoBias);
    p.ssaoIntensity = tbl["ssao"]["intensity"].value_or(p.ssaoIntensity);

    p.shadowBias       = tbl["shadow"]["bias"].value_or(p.shadowBias);
    p.shadowFloor      = tbl["shadow"]["floor"].value_or(p.shadowFloor);
    p.shadowHalfExtent = tbl["shadow"]["half_extent"].value_or(p.shadowHalfExtent);
    p.shadowDepthRange = tbl["shadow"]["depth_range"].value_or(p.shadowDepthRange);
    p.depthBiasConst   = tbl["shadow"]["depth_bias_const"].value_or(p.depthBiasConst);
    p.depthBiasSlope   = tbl["shadow"]["depth_bias_slope"].value_or(p.depthBiasSlope);

    p.rainIntensity = tbl["wet"]["rain_intensity"].value_or(p.rainIntensity);
    p.wetPorosity   = tbl["wet"]["porosity"].value_or(p.wetPorosity);
    p.wetRoughness  = tbl["wet"]["roughness"].value_or(p.wetRoughness);
    p.streakLen     = tbl["wet"]["streak_len"].value_or(p.streakLen);

    p.carOverride     = tbl["car"]["override"].value_or(p.carOverride);
    p.carMetalness    = tbl["car"]["metalness"].value_or(p.carMetalness);
    rd3(tbl["car"]["paint"], p.carPaint);
    p.carRoughnessMul = tbl["car"]["roughness_mul"].value_or(p.carRoughnessMul);

    p.ssaaScale = tbl["quality"]["ssaa_scale"].value_or(p.ssaaScale);
    return true;
}

std::vector<std::string> list_presets() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(presets_dir(), ec)) {
        if (ec)
            break;
        if (e.is_regular_file() && e.path().extension() == ".toml")
            names.push_back(e.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace swish::debugio

#endif  // SWISH_DEBUG_UI
