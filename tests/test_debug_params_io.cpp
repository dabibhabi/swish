// DebugParams save/load round-trip tests (src/debug/DebugParamsIO.*).
// Compiled only in the swish_debug_tests target (-DSWISH_DEBUG_UI=ON); the IO
// layer needs only toml++ and glm, no ImGui/Vulkan. Presets are written to a scratch dir
// (SWISH_PRESETS_DIR -> CONFIG_DIR/test_presets/, inside the build tree, never the user's real
// ~/.config/swish/presets) and removed afterwards.
#include "debug/DebugParamsIO.h"
#include "debug/Tunables.h"
#include "scene/RoadScene/RoadParams.h"

#include <toml++/toml.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace swish;
namespace fs = std::filesystem;

namespace {

// Isolate every test from the user's real preset directory: presets_dir() reads this env var on
// each call, so setting it once before main() is enough.
[[maybe_unused]] const bool kPresetsIsolated = [] {
    ::setenv("SWISH_PRESETS_DIR", CONFIG_DIR "test_presets/", 1);
    return true;
}();

// Sets/clears an environment variable for one scope and restores it after.
struct EnvGuard {
    std::string name;
    std::string old;
    bool        had;
    EnvGuard(const char* n, const char* value) : name(n) {
        const char* cur = std::getenv(n);
        had             = cur != nullptr;
        if (had)
            old = cur;
        if (value)
            ::setenv(n, value, 1);
        else
            ::unsetenv(n);
    }
    ~EnvGuard() {
        if (had)
            ::setenv(name.c_str(), old.c_str(), 1);
        else
            ::unsetenv(name.c_str());
    }
};

struct PresetFile {
    std::string name;
    explicit PresetFile(std::string n) : name(std::move(n)) {}
    ~PresetFile() { std::error_code ec; fs::remove(debugio::presets_dir() + name + ".toml", ec); }
    void write_raw(const std::string& body) const {
        fs::create_directories(debugio::presets_dir());
        std::ofstream(debugio::presets_dir() + name + ".toml", std::ios::trunc) << body;
    }
};

#define EQF(f) CHECK(a.f == b.f)
#define EQV3(f) do { CHECK(a.f.x == b.f.x); CHECK(a.f.y == b.f.y); CHECK(a.f.z == b.f.z); } while (0)

// Every field save() persists. Float compare is exact: floats go float->double->
// shortest-round-trip text->double->float, which is lossless.
void expect_persisted_equal(const DebugParams& a, const DebugParams& b) {
    EQF(exposure); EQF(bloomThreshold); EQF(bloomIntensity); EQF(brightness); EQF(contrast); EQF(saturation);
    EQF(temperature); EQF(tint); EQF(autoExposure); EQF(aeKey); EQF(aeSpeed); EQF(aeMin); EQF(aeMax);
    EQV3(skyHorizonOvercast); EQV3(skyHorizonClear); EQV3(skyZenithOvercast); EQV3(skyZenithClear);
    EQF(clarity); EQF(sunDiscExpMin); EQF(sunDiscExpMax); EQF(sunDiscStrMin); EQF(sunDiscStrMax);
    EQV3(sunColor); EQF(sunAmbient); EQF(sunAzimuth); EQF(sunElevation);
    EQV3(fogColor); EQF(fogDist63); EQF(fogMax);
    EQF(envGlossExp); EQF(iblDiffuse); EQF(iblSpecular);
    EQF(ssaoEnabled); EQF(ssaoRadius); EQF(ssaoBias); EQF(ssaoIntensity);
    EQF(ssrEnabled); EQF(ssrIntensity); EQF(ssrMaxDist); EQF(ssrThickness); EQF(ssrStride);
    EQF(steerAxisEdit); EQV3(steerEuler);
    CHECK(a.steerQuat.x == b.steerQuat.x); CHECK(a.steerQuat.y == b.steerQuat.y);
    CHECK(a.steerQuat.z == b.steerQuat.z); CHECK(a.steerQuat.w == b.steerQuat.w);
    EQF(shadowBias); EQF(shadowFloor); EQF(shadowLightSize); EQF(shadowHalfExtent); EQF(shadowDepthRange);
    EQF(depthBiasConst); EQF(depthBiasSlope); EQF(csmShadowFar); EQF(csmLambda);
    EQF(rainIntensity); EQF(wetPorosity); EQF(wetRoughness); EQF(streakLen); EQF(puddlesEnabled); EQF(rigidBodyChassis);
    EQF(puddleCoverage); EQF(sprayEnabled); EQF(sprayDensity); EQF(sprayLifetime); EQF(spraySize);
    EQF(sprayOpacity);
    EQF(carOverride); EQF(carMetalness); EQV3(carPaint); EQF(carRoughnessMul);
    EQF(ssaaScale); EQF(taaEnabled); EQF(taaHistoryBlend); EQF(motionBlurEnabled); EQF(motionBlurScale);
    EQF(dofEnabled); EQF(dofFocusDist); EQF(dofFocusRange); EQF(dofMaxCoC);
    EQF(clearcoatMul); EQF(flakeDensityMul); EQF(clearcoatRoughness); EQF(flakeRoughness); EQF(flakeScale);
    EQF(flakeJitter);
    for (uint32_t i = 0; i < MAT_COUNT; ++i) {
        INFO("material slot " << i);
        CHECK(a.matOverrides[i].enabled == b.matOverrides[i].enabled);
        CHECK(a.matOverrides[i].metalness == b.matOverrides[i].metalness);
        CHECK(a.matOverrides[i].roughnessMul == b.matOverrides[i].roughnessMul);
        CHECK(a.matOverrides[i].color.x == b.matOverrides[i].color.x);
        CHECK(a.matOverrides[i].color.y == b.matOverrides[i].color.y);
        CHECK(a.matOverrides[i].color.z == b.matOverrides[i].color.z);
        CHECK(a.matOverrides[i].clearcoat == b.matOverrides[i].clearcoat);
        CHECK(a.matOverrides[i].flakeDensity == b.matOverrides[i].flakeDensity);
    }
}

// Distinct, non-default, float-exact-ish value in every persisted field.
DebugParams modified() {
    DebugParams p;
    float       n = 0.1237f;
    auto        f = [&]() { n += 0.3171f; return n; };
    p.exposure = f(); p.bloomThreshold = f(); p.bloomIntensity = f(); p.brightness = f(); p.contrast = f();
    p.saturation = f(); p.temperature = f(); p.tint = f(); p.autoExposure = !p.autoExposure;
    p.aeKey = f(); p.aeSpeed = f(); p.aeMin = f(); p.aeMax = f();
    p.skyHorizonOvercast = {f(), f(), f()}; p.skyHorizonClear = {f(), f(), f()};
    p.skyZenithOvercast = {f(), f(), f()}; p.skyZenithClear = {f(), f(), f()};
    p.clarity = f(); p.sunDiscExpMin = f(); p.sunDiscExpMax = f(); p.sunDiscStrMin = f(); p.sunDiscStrMax = f();
    p.sunColor = {f(), f(), f()}; p.sunAmbient = f(); p.sunAzimuth = f(); p.sunElevation = f();
    p.fogColor = {f(), f(), f()}; p.fogDist63 = f(); p.fogMax = f();
    p.envGlossExp = f(); p.iblDiffuse = f(); p.iblSpecular = f();
    p.ssaoEnabled = !p.ssaoEnabled; p.ssaoRadius = f(); p.ssaoBias = f(); p.ssaoIntensity = f();
    p.ssrEnabled = !p.ssrEnabled; p.ssrIntensity = f(); p.ssrMaxDist = f(); p.ssrThickness = f(); p.ssrStride = f();
    p.steerAxisEdit = !p.steerAxisEdit; p.steerEuler = {f(), f(), f()}; p.steerQuat = {f(), f(), f(), f()};
    p.shadowBias = f(); p.shadowFloor = f(); p.shadowLightSize = f(); p.shadowHalfExtent = f();
    p.shadowDepthRange = f(); p.depthBiasConst = f(); p.depthBiasSlope = f(); p.csmShadowFar = f(); p.csmLambda = f();
    p.rainIntensity = f(); p.wetPorosity = f(); p.wetRoughness = f(); p.streakLen = f();
    p.puddlesEnabled = !p.puddlesEnabled; p.rigidBodyChassis = !p.rigidBodyChassis; p.puddleCoverage = f(); p.sprayEnabled = !p.sprayEnabled;
    p.sprayDensity = f(); p.sprayLifetime = f(); p.spraySize = f(); p.sprayOpacity = f();
    p.carOverride = !p.carOverride; p.carMetalness = f(); p.carPaint = {f(), f(), f()}; p.carRoughnessMul = f();
    p.ssaaScale = f(); p.taaEnabled = !p.taaEnabled; p.taaHistoryBlend = f();
    p.motionBlurEnabled = !p.motionBlurEnabled; p.motionBlurScale = f();
    p.dofEnabled = !p.dofEnabled; p.dofFocusDist = f(); p.dofFocusRange = f(); p.dofMaxCoC = f();
    p.clearcoatMul = f(); p.flakeDensityMul = f(); p.clearcoatRoughness = f(); p.flakeRoughness = f();
    p.flakeScale = f(); p.flakeJitter = f();
    for (uint32_t i = 0; i < MAT_COUNT; i += 2) {  // every other slot enabled
        auto& o = p.matOverrides[i];
        o.enabled = true; o.metalness = f(); o.roughnessMul = f(); o.color = {f(), f(), f()};
        o.clearcoat = f(); o.flakeDensity = f();
    }
    return p;
}

}  // namespace

TEST_CASE("DebugParamsIO: default params round-trip", "[debugio]") {
    PresetFile  pf("swish_test_default");
    DebugParams src;
    REQUIRE(debugio::save(src, pf.name));
    DebugParams dst;
    REQUIRE(debugio::load(dst, pf.name));
    expect_persisted_equal(src, dst);
}

TEST_CASE("DebugParamsIO: modified params round-trip", "[debugio]") {
    PresetFile  pf("swish_test_modified");
    DebugParams src = modified();
    REQUIRE(debugio::save(src, pf.name));
    DebugParams dst;  // defaults; every persisted field must be overwritten
    REQUIRE(debugio::load(dst, pf.name));
    expect_persisted_equal(src, dst);
    CHECK(dst.exposure != DebugParams{}.exposure);  // guards against a vacuous mutate()
}

TEST_CASE("DebugParamsIO: load listed via list_presets", "[debugio]") {
    PresetFile pf("swish_test_listed");
    REQUIRE(debugio::save(DebugParams{}, pf.name));
    const auto names = debugio::list_presets();
    CHECK(std::find(names.begin(), names.end(), pf.name) != names.end());
}

TEST_CASE("DebugParamsIO: unknown keys and tables are ignored", "[debugio]") {
    PresetFile pf("swish_test_unknown");
    pf.write_raw("[grade]\nexposure = 3.5\nbogus_key = 42\n[totally_unknown]\nx = 1\n");
    DebugParams p;
    const float contrastBefore = p.contrast;
    REQUIRE(debugio::load(p, pf.name));  // still succeeds
    CHECK(p.exposure == 3.5f);           // known key applied
    CHECK(p.contrast == contrastBefore); // absent key keeps in-app value (merge policy)
}

TEST_CASE("DebugParamsIO: typos silently fall back to current values (documents CURRENT behaviour)",
          "[debugio]") {
    PresetFile pf("swish_test_typo");
    // Misspelled table ([grde]) and misspelled key (expsure): load() returns true and
    // changes nothing; there is no warning or error. A future validating loader will
    // intentionally break this test.
    pf.write_raw("[grde]\nexposure = 9.0\n[grade]\nexpsure = 9.0\n");
    DebugParams p, defaults;
    REQUIRE(debugio::load(p, pf.name));
    CHECK(p.exposure == defaults.exposure);
}

TEST_CASE("DebugParamsIO: wrong types and out-of-range values (documents CURRENT behaviour)", "[debugio]") {
    PresetFile pf("swish_test_badvalues");
    // - wrong type (string for float): silently keeps the current value.
    // - out-of-range: NOT clamped or validated; loaded verbatim.
    // - integer literal for a float key: accepted (toml++ converts).
    // - wrong-size vec3 array: ignored, keeps current value.
    pf.write_raw("[grade]\nexposure = \"bright\"\ncontrast = -1000000.0\nsaturation = 2\n"
                 "[sun]\ncolor = [1.0, 2.0]\n");
    DebugParams p, defaults;
    REQUIRE(debugio::load(p, pf.name));
    CHECK(p.exposure == defaults.exposure);
    CHECK(p.contrast == -1000000.0f);
    CHECK(p.saturation == 2.0f);
    CHECK(p.sunColor == defaults.sunColor);
}

TEST_CASE("DebugParamsIO: missing or malformed file returns false and leaves params untouched", "[debugio]") {
    DebugParams p, defaults;
    CHECK_FALSE(debugio::load(p, "swish_test_does_not_exist"));
    PresetFile pf("swish_test_malformed");
    pf.write_raw("[grade\nexposure = = 1\n");
    CHECK_FALSE(debugio::load(p, pf.name));
    CHECK(p.exposure == defaults.exposure);
}

TEST_CASE("DebugParamsIO: fields that are NOT persisted (documents CURRENT behaviour)", "[debugio]") {
    // Surprising finding: haze, god-rays, culling and wheel-tuning knobs live in
    // DebugParams but save() never writes them, so a preset cannot restore them.
    PresetFile  pf("swish_test_unpersisted");
    DebugParams src;
    src.hazeDist        = 12345.f;
    src.godrayDensity   = 0.777f;
    src.cullMainViewDist = 4321.f;
    src.wheelSpinMul    = 3.f;
    REQUIRE(debugio::save(src, pf.name));
    DebugParams dst;
    REQUIRE(debugio::load(dst, pf.name));
    CHECK(dst.hazeDist == DebugParams{}.hazeDist);
    CHECK(dst.godrayDensity == DebugParams{}.godrayDensity);
    CHECK(dst.cullMainViewDist == DebugParams{}.cullMainViewDist);
    CHECK(dst.wheelSpinMul == DebugParams{}.wheelSpinMul);
}

// ── Tunable registry (src/debug/Tunables.def), ssr group ────────────────────
// ssr uses a push-constant block (ssr.frag), so there is no GLSL SP_* release macro to diff
// against the registry; instead the defaults are pinned to the literals shipped today
// (release DebugParams{} feeds SsrParams, so these ARE the release values).
TEST_CASE("Tunables: registry defaults equal the shipped literals (release-neutral)", "[tunables]") {
    const DebugParams d{};
    CHECK(d.ssrEnabled == true);
    CHECK(d.ssrIntensity == 0.714f);
    CHECK(d.ssrMaxDist == 36018.0f);
    CHECK(d.ssrThickness == 1835.0f);
    CHECK(d.ssrStride == 8063.0f);
    // reflection + wet material: the pre-registry struct defaults (== lighting.frag release literals).
    CHECK(d.iblDiffuse == 1.30f);
    CHECK(d.iblSpecular == 1.0f);
    CHECK(d.envGlossExp == 3.0f);
    CHECK(d.wetPorosity == 0.631f);
    CHECK(d.wetRoughness == 1.0f);
    // Puddles (owner decision C2, 2026-09-29): now registry entries; same values as the old struct defaults.
    CHECK(d.puddlesEnabled == true);
    CHECK(d.puddleCoverage == 0.501f);
    // Physics model stub: default is the bicycle model (identity).
    CHECK(d.rigidBodyChassis == false);
    // Single C++ source of truth: the registry default IS the scene road constant WetGrip.h physics reads.
    CHECK(d.puddleCoverage == swish::road::kPuddleCoverage);
    // No `.release =` override on any current entry: release default == debug default.
    for (const auto& t : tunables::all()) {
        INFO(t.name);
        CHECK(t.release_default() == t.def);
        CHECK_FALSE(t.release.has_value());
    }
}

TEST_CASE("Tunables: descriptor table is consistent", "[tunables]") {
    const DebugParams d{};
    REQUIRE(tunables::all().size() == 29);  // every TUNE_B / TUNE_F row in Tunables.def (groups: ssr, reflection, wet, car_lights, wiper, car)
    for (const auto& t : tunables::all()) {
        INFO(t.name);
        CHECK(t.in_range(t.def));
        const std::string g = t.group;
        CHECK((g == "ssr" || g == "reflection" || g == "wet" || g == "car_lights" || g == "wiper" ||
               g == "car"));
        if (t.kind == tunables::Kind::Float)
            CHECK(t.get_float(d) == t.def);  // struct default generated from the same line
        else
            CHECK(t.get_bool(d) == (t.def != 0.0f));
    }
    // (group, key) pairs and field names unique
    for (size_t i = 0; i < tunables::all().size(); ++i)
        for (size_t j = i + 1; j < tunables::all().size(); ++j) {
            const auto& a = tunables::all()[i];
            const auto& b = tunables::all()[j];
            CHECK((std::string(a.group) != b.group || std::string(a.key) != b.key));
            CHECK(std::string(a.name) != b.name);
        }
    // UI order pinned: enabled, intensity, max_dist, thickness, stride (TOML keys unchanged).
    const char* keys[] = {"enabled", "intensity", "max_dist", "thickness", "stride"};
    for (size_t i = 0; i < 5; ++i)
        CHECK(std::string(tunables::all()[i].key) == keys[i]);
}

TEST_CASE("Tunables: registry-driven round trip for every descriptor", "[tunables][debugio]") {
    PresetFile  pf("swish_test_registry_rt");
    DebugParams src;
    float       n = 0.0f;
    for (const auto& t : tunables::all()) {  // distinct in-range values via descriptors
        if (t.kind == tunables::Kind::Bool) {
            src.*t.b = !(t.def != 0.0f);
        } else {
            n += 0.137f;
            src.*t.f = t.lo + (t.hi - t.lo) * (0.25f + n * 0.3f);
        }
    }
    REQUIRE(debugio::save(src, pf.name));
    DebugParams dst;
    REQUIRE(debugio::load(dst, pf.name));
    for (const auto& t : tunables::all()) {
        INFO(t.name);
        if (t.kind == tunables::Kind::Bool)
            CHECK(t.get_bool(dst) == t.get_bool(src));
        else
            CHECK(t.get_float(dst) == t.get_float(src));
    }
    CHECK(dst.ssrEnabled != DebugParams{}.ssrEnabled);  // non-vacuous
}

TEST_CASE("Tunables: rigid-body chassis toggle persists under [car] and defaults to bicycle", "[tunables][debugio]") {
    PresetFile  pf("swish_test_rigid_body");
    DebugParams src;
    src.rigidBodyChassis = true;
    REQUIRE(debugio::save(src, pf.name));
    {
        std::ifstream      in(debugio::presets_dir() + pf.name + ".toml");
        std::ostringstream ss;
        ss << in.rdbuf();
        const auto tbl = toml::parse(ss.str());
        CHECK(tbl["car"]["rigid_body_chassis"].value<bool>() == true);
    }
    DebugParams dst;
    REQUIRE(debugio::load(dst, pf.name));
    CHECK(dst.rigidBodyChassis == true);
    // A preset written before this key existed (no [car] table) leaves the bicycle default untouched.
    pf.write_raw("[ssr]\nenabled = false\n");
    DebugParams old;
    REQUIRE(debugio::load(old, pf.name));
    CHECK(old.rigidBodyChassis == false);
}

TEST_CASE("Tunables: saved file keeps the [ssr] table and TOML key names", "[tunables][debugio]") {
    PresetFile pf("swish_test_registry_keys");
    REQUIRE(debugio::save(DebugParams{}, pf.name));
    const toml::table tbl = toml::parse_file(debugio::presets_dir() + pf.name + ".toml");
    const toml::table* ssr = tbl["ssr"].as_table();
    REQUIRE(ssr != nullptr);
    CHECK(ssr->size() == 5);
    CHECK(tbl["ssr"]["enabled"].value<bool>() == true);
    CHECK(tbl["ssr"]["intensity"].value<double>().has_value());
    CHECK(tbl["ssr"]["max_dist"].value<double>().has_value());
    CHECK(tbl["ssr"]["thickness"].value<double>().has_value());
    CHECK(tbl["ssr"]["stride"].value<double>().has_value());
}

TEST_CASE("Tunables: saved file keeps the [reflection] / [wet] TOML key names", "[tunables][debugio]") {
    PresetFile pf("swish_test_registry_keys2");
    REQUIRE(debugio::save(DebugParams{}, pf.name));
    const toml::table tbl = toml::parse_file(debugio::presets_dir() + pf.name + ".toml");
    const toml::table* refl = tbl["reflection"].as_table();
    REQUIRE(refl != nullptr);
    CHECK(refl->size() == 3);
    CHECK(tbl["reflection"]["env_gloss_exp"].value<double>().has_value());
    CHECK(tbl["reflection"]["ibl_diffuse"].value<double>().has_value());
    CHECK(tbl["reflection"]["ibl_specular"].value<double>().has_value());
    // [wet] mixes registry keys (porosity, roughness, puddles_enabled, puddle_coverage) with the hand-written
    // rain / streak / spray keys (size stays 11: the two puddle keys moved from hand-written to the registry).
    const toml::table* wet = tbl["wet"].as_table();
    REQUIRE(wet != nullptr);
    CHECK(wet->size() == 11);
    CHECK(tbl["wet"]["porosity"].value<double>().has_value());
    CHECK(tbl["wet"]["roughness"].value<double>().has_value());
    CHECK(tbl["wet"]["rain_intensity"].value<double>().has_value());
    CHECK(tbl["wet"]["puddle_coverage"].value<double>().has_value());
    CHECK(tbl["wet"]["puddles_enabled"].value<bool>().has_value());
}

// C5: the hand-written GLSL release literals in shaders/lighting.frag must equal the registry
// defaults (the release build ignores DebugParams for these UBO-only tunables, so the shader `#else`
// block IS the release value). Reads the shader via __FILE__; skipped if the source tree is absent.
TEST_CASE("Tunables: lighting.frag release SP_* literals match the registry defaults", "[tunables]") {
    const fs::path shader = fs::path(__FILE__).parent_path().parent_path() / "shaders" / "lighting.frag";
    std::ifstream  in(shader);
    if (!in) {
        WARN("shaders/lighting.frag not found next to the test sources; skipping");
        return;
    }
    struct Map { const char* field; const char* macro; };
    const Map maps[] = {{"iblDiffuse", "SP_IBL_DIFFUSE"},   {"iblSpecular", "SP_IBL_SPECULAR"},
                        {"envGlossExp", "SP_ENV_GLOSS_EXP"}, {"wetPorosity", "SP_WET_POROSITY"},
                        {"wetRoughness", "SP_WET_ROUGHNESS"},
                        // C2: the lit pass's release puddle coverage IS the registry default that SSR now packs too.
                        {"puddleCoverage", "SP_PUDDLE_COVERAGE"}};
    // Numeric `#define SP_X <number>` lines (the debug block defines them as `sp.<member>` instead).
    std::vector<std::pair<std::string, float>> literals;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string        define, name, value;
        if (!(ls >> define >> name >> value) || define != "#define" || name.rfind("SP_", 0) != 0)
            continue;
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end != value.c_str() && *end == '\0')
            literals.emplace_back(name, v);
    }
    for (const Map& m : maps) {
        INFO(m.macro);
        const auto lit = std::find_if(literals.begin(), literals.end(), [&](const auto& l) { return l.first == m.macro; });
        REQUIRE(lit != literals.end());
        const auto t = std::find_if(tunables::all().begin(), tunables::all().end(),
                                    [&](const tunables::Tunable& x) { return std::string(x.name) == m.field; });
        REQUIRE(t != tunables::all().end());
        CHECK(lit->second == t->release_default());
    }
}

TEST_CASE("Tunables: reflection / wet-material load semantics match the old hand-written loader",
          "[tunables][debugio]") {
    PresetFile pf("swish_test_registry_load2");
    pf.write_raw("[reflection]\nibl_diffuse = 2\nibl_specular = \"x\"\nenv_gloss = 5.0\n"
                 "[wet]\nporosity = 9.5\nroughness = 0.25\n");
    DebugParams p, defaults;
    REQUIRE(debugio::load(p, pf.name));
    CHECK(p.iblDiffuse == 2.0f);                     // integer literal accepted
    CHECK(p.iblSpecular == defaults.iblSpecular);    // wrong type: ignored
    CHECK(p.envGlossExp == defaults.envGlossExp);    // typo'd key: ignored
    CHECK(p.wetPorosity == 9.5f);                    // above slider max: applied verbatim
    CHECK(p.wetRoughness == 0.25f);
}

TEST_CASE("Tunables: ssr load semantics unchanged (typo ignored, out-of-range unclamped)", "[tunables][debugio]") {
    PresetFile pf("swish_test_registry_load");
    pf.write_raw("[ssr]\nmax_dst = 1.0\nintensity = 50.0\nstride = \"x\"\nthickness = 7\n");
    DebugParams p, defaults;
    REQUIRE(debugio::load(p, pf.name));
    CHECK(p.ssrMaxDist == defaults.ssrMaxDist);  // typo: ignored
    CHECK(p.ssrIntensity == 50.0f);              // above slider max (2.0): applied verbatim
    CHECK(p.ssrStride == defaults.ssrStride);    // wrong type: ignored
    CHECK(p.ssrThickness == 7.0f);               // integer literal accepted, below lo (200)
    CHECK(tunables::count_out_of_range(p) == 2); // opt-in check reports them; load() never clamps
    CHECK(tunables::count_out_of_range(defaults) == 0);
}

TEST_CASE("Tunables: `.release =` override rule", "[tunables]") {
    // Pure rule: override wins in release, debug default otherwise.
    static_assert(release_value<float>(1.0f, std::nullopt) == 1.0f);
    static_assert(release_value<float>(1.0f, 2.0f) == 2.0f);
    static_assert(release_value<bool>(true, false) == false);
    static_assert(release_value<bool>(true, std::nullopt) == true);
    // This build config (debug-UI ON) always takes the debug default.
    static_assert(build_default<float>(1.0f, 2.0f) == 1.0f);
    // Descriptor with an override models it separately from `def`.
    constexpr tunables::Tunable t{tunables::Kind::Float, nullptr, &DebugParams::ssrStride, "x", "g", "k", "l", "%.0f",
                                  0, 10.0f, 0.0f, 100.0f, std::optional<float>(20.0f), nullptr};
    static_assert(t.release_default() == 20.0f && t.def == 10.0f && t.in_range(t.release_default()));
    constexpr tunables::Tunable u{tunables::Kind::Float, nullptr, &DebugParams::ssrStride, "x", "g", "k", "l", "%.0f",
                                  0, 10.0f, 0.0f, 100.0f, std::nullopt, nullptr};
    static_assert(u.release_default() == 10.0f);
    SUCCEED();
}

TEST_CASE("Tunables: visit dispatches Bool to tag::flag and Float to tag::real (const too)", "[tunables]") {
    DebugParams p;
    int         flags = 0, reals = 0;
    for (const auto& t : tunables::all()) {
        tunables::visit(t, p, [&](auto kind, auto& v) {
            using K = decltype(kind);
            if constexpr (std::is_same_v<K, swish::tag::flag_t>) {
                static_assert(std::is_same_v<std::remove_reference_t<decltype(v)>, bool>);
                CHECK(t.kind == tunables::Kind::Bool);
                ++flags;
            } else {
                static_assert(std::is_same_v<std::remove_reference_t<decltype(v)>, float>);
                CHECK(t.kind == tunables::Kind::Float);
                ++reals;
            }
        });
    }
    CHECK(flags == 7);  // TUNE_B rows
    CHECK(reals == 22);  // TUNE_F rows
    // The visitor sees the live field: writing through it changes DebugParams (what load/widget rely on).
    const auto& ssr = tunables::all()[1];  // ssrIntensity
    tunables::visit(ssr, p, [](auto, auto& v) { v = std::remove_reference_t<decltype(v)>(1.25f); });
    CHECK(p.ssrIntensity == 1.25f);
    // A const DebugParams yields const references (the save / dump path cannot mutate).
    const DebugParams& cp = p;
    tunables::visit(ssr, cp, [](auto, auto& v) { static_assert(std::is_const_v<std::remove_reference_t<decltype(v)>>); });
}

// ── Preset location (owner decision C3): per-user, outside build/, legacy build/ fallback ──
TEST_CASE("Presets: presets_dir() is per-user and outside the build tree", "[debugio][presets]") {
    const std::string legacy = std::string(CONFIG_DIR) + "presets/";
    {
        EnvGuard a("SWISH_PRESETS_DIR", "/explicit/dir");
        CHECK(debugio::presets_dir() == "/explicit/dir/");
    }
    {
        EnvGuard a("SWISH_PRESETS_DIR", nullptr), b("XDG_CONFIG_HOME", "/xdg"), c("HOME", "/home/u");
        CHECK(debugio::presets_dir() == "/xdg/swish/presets/");
    }
    {
        EnvGuard a("SWISH_PRESETS_DIR", nullptr), b("XDG_CONFIG_HOME", nullptr), c("HOME", "/home/u/");
        CHECK(debugio::presets_dir() == "/home/u/.config/swish/presets/");
        CHECK(debugio::presets_dir() != legacy);
    }
    {
        EnvGuard a("SWISH_PRESETS_DIR", nullptr), b("XDG_CONFIG_HOME", nullptr), c("HOME", nullptr);
        CHECK(debugio::presets_dir() == legacy);  // no home dir known: last-resort legacy location
    }
}

TEST_CASE("Presets: legacy build/ presets stay loadable and listed; the new dir wins", "[debugio][presets]") {
    const std::string legacyDir = std::string(CONFIG_DIR) + "presets/";
    const std::string name      = "swish_test_legacy_fallback";
    const std::string legacyFile = legacyDir + name + ".toml";
    fs::create_directories(legacyDir);
    std::ofstream(legacyFile, std::ios::trunc) << "[grade]\nexposure = 1.25\n";
    struct Cleanup {
        std::string f, g;
        ~Cleanup() { std::error_code ec; fs::remove(f, ec); fs::remove(g, ec); }
    } cleanup{legacyFile, debugio::presets_dir() + name + ".toml"};
    REQUIRE(debugio::presets_dir() != legacyDir);  // isolated from the legacy dir by the test env

    // Only in the legacy dir: loadable and listed.
    DebugParams p;
    REQUIRE(debugio::load(p, name));
    CHECK(p.exposure == 1.25f);
    const auto names = debugio::list_presets();
    CHECK(std::count(names.begin(), names.end(), name) == 1);

    // Same name saved to the new dir: the new copy wins on load, and the name is listed once.
    DebugParams src;
    src.exposure = 0.5f;
    REQUIRE(debugio::save(src, name));
    DebugParams q;
    REQUIRE(debugio::load(q, name));
    CHECK(q.exposure == 0.5f);
    const auto names2 = debugio::list_presets();
    CHECK(std::count(names2.begin(), names2.end(), name) == 1);
    CHECK(fs::exists(debugio::presets_dir() + name + ".toml"));
    CHECK(fs::exists(legacyFile));  // save() never touches the legacy copy
}
