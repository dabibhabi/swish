#include "utils/io/AppPaths.h"
#include "DebugParamsIO.h"

#ifdef SWISH_DEBUG_UI

#include "Tunables.h"
#include "utils/debug/TomlGlm.h"
#include "utils/meta/Tags.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <toml++/toml.hpp>

namespace swish::debugio {

namespace {

namespace fs = std::filesystem;

using util::debug::read_vec3;
using util::debug::vec3_to_toml;

// Per-kind TOML encode / decode: one overload per value-kind tag (utils/meta/Tags.h). A new kind adds one
// `put` and one `get` here (plus the matching widget / dump in DebugUI.cpp); the loops below never change.
void put(toml::table& g, const tunables::Tunable& t, tag::flag_t, bool v) { g.insert_or_assign(t.key, v); }
void put(toml::table& g, const tunables::Tunable& t, tag::real_t, float v) {
    g.insert_or_assign(t.key, static_cast<double>(v));
}
void get(const toml::node_view<const toml::node>& node, tag::flag_t, bool& v) { v = node.value_or(v); }
void get(const toml::node_view<const toml::node>& node, tag::real_t, float& v) { v = node.value_or(v); }

// Registry-driven groups (src/debug/Tunables.def: ssr, reflection, wet porosity/roughness/puddles). Same TOML
// tables/keys and merge semantics as the hand-written groups: absent / wrong-typed keys keep the current value and
// out-of-range values are applied verbatim (no clamping).
void save_registry(toml::table& tbl, const DebugParams& p) {
    for (const tunables::Tunable& t : tunables::all()) {
        toml::table* g = tbl.get_as<toml::table>(t.group);
        if (!g) g = tbl.emplace(t.group, toml::table{}).first->second.as_table();
        tunables::visit(t, p, [&](auto kind, auto v) { put(*g, t, kind, v); });
    }
}

void load_registry(const toml::table& tbl, DebugParams& p) {
    for (const tunables::Tunable& t : tunables::all()) {
        const auto node = tbl[t.group][t.key];
        tunables::visit(t, p, [&](auto kind, auto& v) { get(node, kind, v); });
    }
}

// Pre-2026-09-29 preset home, inside the build tree (wiped by `make clean` / a fresh build
// dir). Kept read-only as a fallback so existing presets stay reachable.
std::string legacy_presets_dir() { return util::paths::config_dir() + "presets/"; }

std::string with_trailing_slash(std::string dir) {
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return dir;
}

// Resolves <name>.toml for reading: the per-user dir first, then the legacy build/ dir.
// When neither has it, returns the per-user path (so the parse error names the new home).
std::string resolve_preset_path(const std::string& name) {
    const std::string primary = presets_dir() + name + ".toml";
    std::error_code   ec;
    if (fs::exists(primary, ec)) return primary;
    const std::string legacy = legacy_presets_dir() + name + ".toml";
    if (fs::exists(legacy, ec)) return legacy;
    return primary;
}

void collect_preset_names(const std::string& dir, std::vector<std::string>& names) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (e.is_regular_file() && e.path().extension() == ".toml") names.push_back(e.path().stem().string());
    }
}

}  // namespace

// Per-user, OUTSIDE the build tree (owner decision C3): presets are personal tuning, must
// survive `make clean` / new build dirs / git worktrees, and are never committed.
//   1. $SWISH_PRESETS_DIR                     (explicit override; tests use it)
//   2. $XDG_CONFIG_HOME/swish/presets/
//   3. $HOME/.config/swish/presets/
//   4. legacy CONFIG_DIR/presets/             (only when no home directory is known)
std::string presets_dir() {
    if (const char* dir = std::getenv("SWISH_PRESETS_DIR"); dir && *dir) return with_trailing_slash(dir);
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return with_trailing_slash(xdg) + "swish/presets/";
    if (const char* home = std::getenv("HOME"); home && *home)
        return with_trailing_slash(home) + ".config/swish/presets/";
    return legacy_presets_dir();
}

bool save(const DebugParams& p, const std::string& name) {
    std::error_code ec;
    fs::create_directories(presets_dir(), ec);  // idempotent; ignore "already exists"

    // Build the document grouped exactly like the panel so hand-editing is easy.
    toml::table tbl{
        {"grade", toml::table{{"exposure", p.exposure},
                              {"bloom_threshold", p.bloomThreshold},
                              {"bloom_intensity", p.bloomIntensity},
                              {"brightness", p.brightness},
                              {"contrast", p.contrast},
                              {"saturation", p.saturation},
                              {"temperature", p.temperature},
                              {"tint", p.tint},
                              {"auto_exposure", p.autoExposure},
                              {"ae_key", p.aeKey},
                              {"ae_speed", p.aeSpeed},
                              {"ae_min", p.aeMin},
                              {"ae_max", p.aeMax}}},
        {"sky", toml::table{{"horizon_overcast", vec3_to_toml(p.skyHorizonOvercast)},
                            {"horizon_clear", vec3_to_toml(p.skyHorizonClear)},
                            {"zenith_overcast", vec3_to_toml(p.skyZenithOvercast)},
                            {"zenith_clear", vec3_to_toml(p.skyZenithClear)},
                            {"clarity", p.clarity},
                            {"sun_disc_exp_min", p.sunDiscExpMin},
                            {"sun_disc_exp_max", p.sunDiscExpMax},
                            {"sun_disc_str_min", p.sunDiscStrMin},
                            {"sun_disc_str_max", p.sunDiscStrMax}}},
        {"sun", toml::table{{"color", vec3_to_toml(p.sunColor)},
                            {"ambient", p.sunAmbient},
                            {"azimuth", p.sunAzimuth},
                            {"elevation", p.sunElevation}}},
        {"fog", toml::table{{"color", vec3_to_toml(p.fogColor)}, {"dist63", p.fogDist63}, {"max", p.fogMax}}},
        {"ssao", toml::table{{"enabled", p.ssaoEnabled},
                             {"radius", p.ssaoRadius},
                             {"bias", p.ssaoBias},
                             {"intensity", p.ssaoIntensity}}},
        {"steering", toml::table{{"axis_edit", p.steerAxisEdit},
                                 {"euler", vec3_to_toml(p.steerEuler)},
                                 {"quat", toml::array{p.steerQuat.x, p.steerQuat.y, p.steerQuat.z, p.steerQuat.w}}}},
        {"shadow", toml::table{{"bias", p.shadowBias},
                               {"floor", p.shadowFloor},
                               {"light_size", p.shadowLightSize},
                               {"half_extent", p.shadowHalfExtent},
                               {"depth_range", p.shadowDepthRange},
                               {"depth_bias_const", p.depthBiasConst},
                               {"depth_bias_slope", p.depthBiasSlope},
                               {"csm_far", p.csmShadowFar},
                               {"csm_lambda", p.csmLambda}}},
        {"wet", toml::table{{"rain_intensity", p.rainIntensity},
                            {"streak_len", p.streakLen},
                            {"spray_enabled", p.sprayEnabled},
                            {"spray_density", p.sprayDensity},
                            {"spray_lifetime", p.sprayLifetime},
                            {"spray_size", p.spraySize},
                            {"spray_opacity", p.sprayOpacity}}},
        {"car", toml::table{{"override", p.carOverride},
                            {"metalness", p.carMetalness},
                            {"paint", vec3_to_toml(p.carPaint)},
                            {"roughness_mul", p.carRoughnessMul}}},
        {"quality", toml::table{{"ssaa_scale", p.ssaaScale},
                                {"taa_enabled", p.taaEnabled},
                                {"taa_history_blend", p.taaHistoryBlend},
                                {"motion_blur_enabled", p.motionBlurEnabled},
                                {"motion_blur_scale", p.motionBlurScale}}},
        {"dof", toml::table{{"enabled", p.dofEnabled},
                            {"focus_dist", p.dofFocusDist},
                            {"focus_range", p.dofFocusRange},
                            {"max_coc", p.dofMaxCoC}}},
        {"paint", toml::table{{"clearcoat_mul", p.clearcoatMul},
                              {"flake_density_mul", p.flakeDensityMul},
                              {"clearcoat_roughness", p.clearcoatRoughness},
                              {"flake_roughness", p.flakeRoughness},
                              {"flake_scale", p.flakeScale},
                              {"flake_jitter", p.flakeJitter}}},
        {"wheel_spray", toml::table{{"per_wheel", p.perWheelSpray}}},
        {"lens_wet", toml::table{{"wet_rate", p.lensWetRate},
                                 {"clear_speed_start", p.lensClearSpeedStart},
                                 {"clear_speed_full", p.lensClearSpeedFull},
                                 {"clear_rate", p.lensClearRate},
                                 {"drain_rate", p.lensDrainRate}}},
        {"slip", toml::table{{"skid_ratio", p.slipSkidRatio},
                             {"skid_angle_deg", p.slipSkidAngleDeg},
                             {"ratio_start", p.slipRatioStart},
                             {"ratio_full", p.slipRatioFull},
                             {"angle_start_deg", p.slipAngleStartDeg},
                             {"angle_full_deg", p.slipAngleFullDeg},
                             {"spray_boost_max", p.slipSprayBoostMax}}},
    };

    // Per-material overrides → an array of tables ([[materials]]), enabled slots only.
    toml::array mats;
    for (uint32_t i = 0; i < MAT_COUNT; ++i) {
        const MaterialOverride& o = p.matOverrides[i];
        if (!o.enabled) continue;
        mats.push_back(toml::table{{"slot", static_cast<int64_t>(i)},
                                   {"metalness", o.metalness},
                                   {"roughness_mul", o.roughnessMul},
                                   {"color", vec3_to_toml(o.color)},
                                   {"clearcoat", o.clearcoat},
                                   {"flake_density", o.flakeDensity}});
    }
    tbl.insert_or_assign("materials", std::move(mats));

    save_registry(tbl, p);

    std::ofstream f(presets_dir() + name + ".toml", std::ios::trunc);
    if (!f) return false;
    f << "# swish debug preset — edit freely; missing keys keep their in-app value\n" << tbl << '\n';
    return static_cast<bool>(f);
}

bool load(DebugParams& p, const std::string& name) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(resolve_preset_path(name));
    } catch (const toml::parse_error&) { return false; }

    p.exposure       = tbl["grade"]["exposure"].value_or(p.exposure);
    p.bloomThreshold = tbl["grade"]["bloom_threshold"].value_or(p.bloomThreshold);
    p.bloomIntensity = tbl["grade"]["bloom_intensity"].value_or(p.bloomIntensity);
    p.brightness     = tbl["grade"]["brightness"].value_or(p.brightness);
    p.contrast       = tbl["grade"]["contrast"].value_or(p.contrast);
    p.saturation     = tbl["grade"]["saturation"].value_or(p.saturation);
    p.temperature    = tbl["grade"]["temperature"].value_or(p.temperature);
    p.tint           = tbl["grade"]["tint"].value_or(p.tint);
    p.autoExposure   = tbl["grade"]["auto_exposure"].value_or(p.autoExposure);
    p.aeKey          = tbl["grade"]["ae_key"].value_or(p.aeKey);
    p.aeSpeed        = tbl["grade"]["ae_speed"].value_or(p.aeSpeed);
    p.aeMin          = tbl["grade"]["ae_min"].value_or(p.aeMin);
    p.aeMax          = tbl["grade"]["ae_max"].value_or(p.aeMax);

    read_vec3(tbl["sky"]["horizon_overcast"], p.skyHorizonOvercast);
    read_vec3(tbl["sky"]["horizon_clear"], p.skyHorizonClear);
    read_vec3(tbl["sky"]["zenith_overcast"], p.skyZenithOvercast);
    read_vec3(tbl["sky"]["zenith_clear"], p.skyZenithClear);
    p.clarity       = tbl["sky"]["clarity"].value_or(p.clarity);
    p.sunDiscExpMin = tbl["sky"]["sun_disc_exp_min"].value_or(p.sunDiscExpMin);
    p.sunDiscExpMax = tbl["sky"]["sun_disc_exp_max"].value_or(p.sunDiscExpMax);
    p.sunDiscStrMin = tbl["sky"]["sun_disc_str_min"].value_or(p.sunDiscStrMin);
    p.sunDiscStrMax = tbl["sky"]["sun_disc_str_max"].value_or(p.sunDiscStrMax);

    read_vec3(tbl["sun"]["color"], p.sunColor);
    p.sunAmbient   = tbl["sun"]["ambient"].value_or(p.sunAmbient);
    p.sunAzimuth   = tbl["sun"]["azimuth"].value_or(p.sunAzimuth);
    p.sunElevation = tbl["sun"]["elevation"].value_or(p.sunElevation);

    read_vec3(tbl["fog"]["color"], p.fogColor);
    p.fogDist63 = tbl["fog"]["dist63"].value_or(p.fogDist63);
    p.fogMax    = tbl["fog"]["max"].value_or(p.fogMax);

    p.ssaoEnabled   = tbl["ssao"]["enabled"].value_or(p.ssaoEnabled);
    p.ssaoRadius    = tbl["ssao"]["radius"].value_or(p.ssaoRadius);
    p.ssaoBias      = tbl["ssao"]["bias"].value_or(p.ssaoBias);
    p.ssaoIntensity = tbl["ssao"]["intensity"].value_or(p.ssaoIntensity);

    // Per-material overrides: only touch them if the file defines [[materials]]
    // (preserves the merge policy — an older preset without them won't clobber the
    // current table). When present, it fully replaces the override state.
    if (const toml::array* mats = tbl["materials"].as_array()) {
        for (uint32_t i = 0; i < MAT_COUNT; ++i) p.matOverrides[i] = MaterialOverride{};
        for (const toml::node& node : *mats) {
            const toml::table* t = node.as_table();
            if (!t) continue;
            int slot = static_cast<int>((*t)["slot"].value_or(-1));
            if (slot < 0 || slot >= static_cast<int>(MAT_COUNT)) continue;
            MaterialOverride& o = p.matOverrides[slot];
            o.enabled           = true;
            o.metalness         = (*t)["metalness"].value_or(o.metalness);
            o.roughnessMul      = (*t)["roughness_mul"].value_or(o.roughnessMul);
            if (const toml::array* c = (*t)["color"].as_array(); c && c->size() == 3) {
                o.color.x = static_cast<float>(c->get(0)->value_or(static_cast<double>(o.color.x)));
                o.color.y = static_cast<float>(c->get(1)->value_or(static_cast<double>(o.color.y)));
                o.color.z = static_cast<float>(c->get(2)->value_or(static_cast<double>(o.color.z)));
            }
            o.clearcoat    = (*t)["clearcoat"].value_or(o.clearcoat);
            o.flakeDensity = (*t)["flake_density"].value_or(o.flakeDensity);
        }
    }

    p.clearcoatMul       = tbl["paint"]["clearcoat_mul"].value_or(p.clearcoatMul);
    p.flakeDensityMul    = tbl["paint"]["flake_density_mul"].value_or(p.flakeDensityMul);
    p.clearcoatRoughness = tbl["paint"]["clearcoat_roughness"].value_or(p.clearcoatRoughness);
    p.flakeRoughness     = tbl["paint"]["flake_roughness"].value_or(p.flakeRoughness);
    p.flakeScale         = tbl["paint"]["flake_scale"].value_or(p.flakeScale);
    p.flakeJitter        = tbl["paint"]["flake_jitter"].value_or(p.flakeJitter);

    p.perWheelSpray       = tbl["wheel_spray"]["per_wheel"].value_or(p.perWheelSpray);
    p.lensWetRate         = tbl["lens_wet"]["wet_rate"].value_or(p.lensWetRate);
    p.lensClearSpeedStart = tbl["lens_wet"]["clear_speed_start"].value_or(p.lensClearSpeedStart);
    p.lensClearSpeedFull  = tbl["lens_wet"]["clear_speed_full"].value_or(p.lensClearSpeedFull);
    p.lensClearRate       = tbl["lens_wet"]["clear_rate"].value_or(p.lensClearRate);
    p.lensDrainRate       = tbl["lens_wet"]["drain_rate"].value_or(p.lensDrainRate);
    p.slipSkidRatio       = tbl["slip"]["skid_ratio"].value_or(p.slipSkidRatio);
    p.slipSkidAngleDeg    = tbl["slip"]["skid_angle_deg"].value_or(p.slipSkidAngleDeg);
    p.slipRatioStart      = tbl["slip"]["ratio_start"].value_or(p.slipRatioStart);
    p.slipRatioFull       = tbl["slip"]["ratio_full"].value_or(p.slipRatioFull);
    p.slipAngleStartDeg   = tbl["slip"]["angle_start_deg"].value_or(p.slipAngleStartDeg);
    p.slipAngleFullDeg    = tbl["slip"]["angle_full_deg"].value_or(p.slipAngleFullDeg);
    p.slipSprayBoostMax   = tbl["slip"]["spray_boost_max"].value_or(p.slipSprayBoostMax);

    load_registry(tbl, p);  // registry groups (Tunables.def)

    p.steerAxisEdit = tbl["steering"]["axis_edit"].value_or(p.steerAxisEdit);
    read_vec3(tbl["steering"]["euler"], p.steerEuler);
    if (const toml::array* q = tbl["steering"]["quat"].as_array(); q && q->size() == 4) {
        p.steerQuat.x = static_cast<float>(q->get(0)->value_or(0.0));
        p.steerQuat.y = static_cast<float>(q->get(1)->value_or(0.0));
        p.steerQuat.z = static_cast<float>(q->get(2)->value_or(0.0));
        p.steerQuat.w = static_cast<float>(q->get(3)->value_or(1.0));
    }

    p.shadowBias       = tbl["shadow"]["bias"].value_or(p.shadowBias);
    p.shadowLightSize  = tbl["shadow"]["light_size"].value_or(p.shadowLightSize);
    p.shadowFloor      = tbl["shadow"]["floor"].value_or(p.shadowFloor);
    p.shadowHalfExtent = tbl["shadow"]["half_extent"].value_or(p.shadowHalfExtent);
    p.shadowDepthRange = tbl["shadow"]["depth_range"].value_or(p.shadowDepthRange);
    p.depthBiasConst   = tbl["shadow"]["depth_bias_const"].value_or(p.depthBiasConst);
    p.depthBiasSlope   = tbl["shadow"]["depth_bias_slope"].value_or(p.depthBiasSlope);
    p.csmShadowFar     = tbl["shadow"]["csm_far"].value_or(p.csmShadowFar);
    p.csmLambda        = tbl["shadow"]["csm_lambda"].value_or(p.csmLambda);

    p.rainIntensity = tbl["wet"]["rain_intensity"].value_or(p.rainIntensity);
    p.streakLen     = tbl["wet"]["streak_len"].value_or(p.streakLen);
    p.sprayEnabled  = tbl["wet"]["spray_enabled"].value_or(p.sprayEnabled);
    p.sprayDensity  = tbl["wet"]["spray_density"].value_or(p.sprayDensity);
    p.sprayLifetime = tbl["wet"]["spray_lifetime"].value_or(p.sprayLifetime);
    p.spraySize     = tbl["wet"]["spray_size"].value_or(p.spraySize);
    p.sprayOpacity  = tbl["wet"]["spray_opacity"].value_or(p.sprayOpacity);

    p.carOverride  = tbl["car"]["override"].value_or(p.carOverride);
    p.carMetalness = tbl["car"]["metalness"].value_or(p.carMetalness);
    read_vec3(tbl["car"]["paint"], p.carPaint);
    p.carRoughnessMul = tbl["car"]["roughness_mul"].value_or(p.carRoughnessMul);

    p.ssaaScale         = tbl["quality"]["ssaa_scale"].value_or(p.ssaaScale);
    p.taaEnabled        = tbl["quality"]["taa_enabled"].value_or(p.taaEnabled);
    p.taaHistoryBlend   = tbl["quality"]["taa_history_blend"].value_or(p.taaHistoryBlend);
    p.motionBlurEnabled = tbl["quality"]["motion_blur_enabled"].value_or(p.motionBlurEnabled);
    p.motionBlurScale   = tbl["quality"]["motion_blur_scale"].value_or(p.motionBlurScale);

    p.dofEnabled    = tbl["dof"]["enabled"].value_or(p.dofEnabled);
    p.dofFocusDist  = tbl["dof"]["focus_dist"].value_or(p.dofFocusDist);
    p.dofFocusRange = tbl["dof"]["focus_range"].value_or(p.dofFocusRange);
    p.dofMaxCoC     = tbl["dof"]["max_coc"].value_or(p.dofMaxCoC);
    return true;
}

std::vector<std::string> list_presets() {
    std::vector<std::string> names;
    collect_preset_names(presets_dir(), names);
    collect_preset_names(legacy_presets_dir(), names);  // fallback: pre-move presets stay listed
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

}  // namespace swish::debugio

#endif  // SWISH_DEBUG_UI
