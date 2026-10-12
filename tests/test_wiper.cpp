// Windshield wipers (tasks/todo.md "Rigged Porsche" S1b): the pure model in
// scene/Entity/Wiper.h. Mode cycle, rate per mode, the intermittent dwell, the eased
// park-and-sweep profile, and the parking rule (Off never stops mid-glass).

#include "scene/Entity/Wiper.h"
#include "scene/Entity/WiperConfig.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace swish;
using Catch::Approx;

namespace {

constexpr float kDt = 1.f / 120.f;

// Step for `seconds` at kDt; returns the final state.
WiperState run(WiperState s, const WiperParams& p, float seconds) {
    for (int i = 0, n = static_cast<int>(std::lround(seconds / kDt)); i < n; i++)
        s = wiper_step(s, p, kDt);
    return s;
}

// Count completed wipes (phase wrapping back past park) over `seconds`.
int count_wipes(WiperState s, const WiperParams& p, float seconds) {
    int wipes = 0;
    for (int i = 0, n = static_cast<int>(std::lround(seconds / kDt)); i < n; i++) {
        const WiperState next = wiper_step(s, p, kDt);
        if (!wiper_parked(s) && (next.phase < s.phase))
            wipes++;
        s = next;
    }
    return wipes;
}

}  // namespace

TEST_CASE("V cycles Off -> Intermittent -> Low -> High -> Off", "[wiper]") {
    REQUIRE(wiper_next_mode(WiperMode::Off) == WiperMode::Intermittent);
    REQUIRE(wiper_next_mode(WiperMode::Intermittent) == WiperMode::Low);
    REQUIRE(wiper_next_mode(WiperMode::Low) == WiperMode::High);
    REQUIRE(wiper_next_mode(WiperMode::High) == WiperMode::Off);
}

TEST_CASE("rates: Low 45 and High 65 wipes per minute", "[wiper]") {
    const WiperParams p;
    REQUIRE(wiper_rate_hz(WiperMode::Low, p) == Approx(45.f / 60.f));
    REQUIRE(wiper_rate_hz(WiperMode::High, p) == Approx(65.f / 60.f));
    REQUIRE(wiper_rate_hz(WiperMode::Intermittent, p) == Approx(45.f / 60.f));  // a single Low wipe
    REQUIRE(wiper_rate_hz(WiperMode::Off, p) == Approx(45.f / 60.f));           // parks at Low speed

    const WiperState low  = wiper_set_mode({}, WiperMode::Low);
    const WiperState high = wiper_set_mode({}, WiperMode::High);
    REQUIRE(std::abs(count_wipes(low, p, 60.f) - 45) <= 1);
    REQUIRE(std::abs(count_wipes(high, p, 60.f) - 65) <= 1);
}

TEST_CASE("Off stays parked; parked means angle 0", "[wiper]") {
    const WiperParams p;
    const WiperState  s = run({}, p, 10.f);
    REQUIRE(wiper_parked(s));
    REQUIRE_FALSE(wiper_active(s));
    REQUIRE(wiper_angle(s, p) == 0.f);
}

TEST_CASE("the sweep is park -> top -> park with eased reversals", "[wiper]") {
    WiperParams p;
    REQUIRE(wiper_profile(0.f, p.ease) == Approx(0.f).margin(1e-6));
    REQUIRE(wiper_profile(0.5f, p.ease) == Approx(1.f));
    REQUIRE(wiper_profile(0.25f, p.ease) == Approx(0.5f));
    REQUIRE(wiper_angle(WiperState{WiperMode::Low, 0.5f, 0.f}, p) == Approx(glm::radians(105.f)));

    // Eased (cosine): angular speed ~0 at park and at the top; linear (ease 0): constant.
    const float h     = 1e-3f;
    const auto  speed = [&](float u, float e) { return (wiper_profile(u + h, e) - wiper_profile(u - h, e)) / (2 * h); };
    const float vMid  = speed(0.25f, 1.f);
    const float vTop  = std::abs(speed(0.5f, 1.f));
    const float vPark = speed(h, 1.f);
    const float vLinear = speed(0.1f, 0.f);
    REQUIRE(vTop < 0.01f * vMid);
    REQUIRE(vPark < 0.02f * vMid);
    REQUIRE(vLinear == Approx(2.f).margin(1e-2));

    // Symmetric: the return stroke retraces the up stroke.
    for (float u : {0.1f, 0.2f, 0.3f, 0.45f})
        REQUIRE(wiper_profile(u, 1.f) == Approx(wiper_profile(1.f - u, 1.f)).margin(1e-5));
    // Never past the top, never below park.
    for (int i = 0; i <= 100; i++) {
        const float s = wiper_profile(static_cast<float>(i) / 100.f, 0.6f);
        REQUIRE(s >= 0.f);
        REQUIRE(s <= 1.f + 1e-6f);
    }
}

TEST_CASE("Intermittent: one Low wipe, then a dwell at park", "[wiper]") {
    const WiperParams p;  // 45/min = 1.333 s per wipe, 4 s dwell
    const float       wipeS = 60.f / p.lowWipesPerMin;
    WiperState        s     = wiper_set_mode({}, WiperMode::Intermittent);

    // It wipes at once (no initial dwell), at Low speed.
    s = wiper_step(s, p, kDt);
    REQUIRE_FALSE(wiper_parked(s));
    s = run(s, p, 0.5f * wipeS - kDt);
    REQUIRE(wiper_angle(s, p) == Approx(glm::radians(p.sweepDeg)).epsilon(0.01));

    // Back at park after one wipe, then rests for the dwell.
    s = run(s, p, 0.5f * wipeS + 2 * kDt);
    REQUIRE(wiper_parked(s));
    REQUIRE(s.dwell > 0.f);
    s = run(s, p, p.dwellS - 0.1f);
    REQUIRE(wiper_parked(s));
    REQUIRE(wiper_active(s));  // switched on, resting
    s = run(s, p, 0.2f);
    REQUIRE_FALSE(wiper_parked(s));  // next wipe started

    // Period = wipe + dwell: over 60 s, 60 / 5.333 ~ 11 wipes.
    const int n = count_wipes(wiper_set_mode({}, WiperMode::Intermittent), p, 60.f);
    REQUIRE(std::abs(n - static_cast<int>(60.f / (wipeS + p.dwellS))) <= 1);
}

TEST_CASE("Off mid-glass finishes the wipe and parks; it never freezes", "[wiper]") {
    const WiperParams p;
    WiperState        s = run(wiper_set_mode({}, WiperMode::High), p, 0.3f);  // somewhere up the glass
    REQUIRE(wiper_angle(s, p) > 0.1f);
    s                 = wiper_set_mode(s, WiperMode::Off);
    const float phase = s.phase;
    s                 = wiper_step(s, p, kDt);
    REQUIRE(s.phase > phase);  // still moving
    REQUIRE(wiper_active(s));
    s = run(s, p, 60.f / p.lowWipesPerMin);  // at most one Low wipe to finish
    REQUIRE(wiper_parked(s));
    REQUIRE(wiper_angle(s, p) == 0.f);
    REQUIRE_FALSE(wiper_active(s));
    REQUIRE(run(s, p, 5.f).phase == 0.f);  // and stays there
}

TEST_CASE("a mode switch never jumps the blade", "[wiper]") {
    const WiperParams p;
    WiperState        s      = run(wiper_set_mode({}, WiperMode::Low), p, 0.4f);
    const float       before = wiper_angle(s, p);
    s                        = wiper_set_mode(s, WiperMode::High);
    REQUIRE(wiper_angle(s, p) == before);
    // High -> Low continues at the lower rate from the same place.
    const float ph = s.phase;
    s              = wiper_step(wiper_set_mode(s, WiperMode::Low), p, kDt);
    REQUIRE(s.phase - ph == Approx(wiper_rate_hz(WiperMode::Low, p) * kDt));
}

TEST_CASE("WiperConfig maps the baked fields onto WiperParams; a missing bake falls back", "[wiper]") {
    WiperConfig c{};
    c.magic                = WiperConfig::MAGIC;
    c.version              = WiperConfig::VERSION;
    c.low_wipes_per_min    = 40.f;
    c.high_wipes_per_min   = 70.f;
    c.intermittent_dwell_s = 3.f;
    c.sweep_deg            = 100.f;
    c.ease                 = 0.5f;
    const WiperParams p    = wiper_params_from_config(c);
    REQUIRE(p.lowWipesPerMin == 40.f);
    REQUIRE(p.highWipesPerMin == 70.f);
    REQUIRE(p.dwellS == 3.f);
    REQUIRE(p.sweepDeg == 100.f);
    REQUIRE(p.ease == 0.5f);

    const WiperParams d = load_wiper_params("/nonexistent/wiper.bin");
    REQUIRE(d.lowWipesPerMin == WiperParams{}.lowWipesPerMin);
    REQUIRE(d.sweepDeg == WiperParams{}.sweepDeg);
    REQUIRE(WiperParams{}.sweepDeg < 120.f);  // the rigged blades leave the glass past ~120
}

TEST_CASE("config/wiper.toml (baked) matches the WiperParams defaults", "[wiper]") {
    // swish_tests depends on bake_configs, so wiper.bin is the real bake of config/wiper.toml.
    const WiperParams baked = load_wiper_params(CONFIG_DIR "wiper.bin");
    const WiperParams d;
    REQUIRE(baked.lowWipesPerMin == d.lowWipesPerMin);
    REQUIRE(baked.highWipesPerMin == d.highWipesPerMin);
    REQUIRE(baked.dwellS == d.dwellS);
    REQUIRE(baked.sweepDeg == d.sweepDeg);
    REQUIRE(baked.ease == d.ease);
}
