#pragma once

// Windshield wipers (tasks/todo.md "Rigged Porsche" S1b): the V-key mode cycle, the
// park-and-sweep motion and the blade geometry. Header-only, GPU/GLFW-free and pure, so
// tests/test_wiper.cpp pins the rates, the intermittent dwell and the parking rule.
//
// One angle drives everything: App steps a WiperState, CarEntity turns the rigged blades
// about their spindles by wiper_angle(), and WindshieldRainPass clears the rain where those
// blades are on screen (the same angle, the same pivot frames, the same arm tip; the blade
// geometry is in WiperBlade.h). std + GLM only.
//
// Motion. One wipe is phase u in [0,1): park -> top -> park, angle 0 -> sweep -> 0.
//   theta(u) = sweep * s(u),   s(u) = mix(1 - |2u - 1|, (1 - cos 2 pi u) / 2, ease)
// ease = 1 is the cosine profile: zero angular speed at park and at the top, the way a
// real linkage decelerates into each reversal. du/dt = wipes per minute / 60.

#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace swish {

enum class WiperMode : uint8_t { Off, Intermittent, Low, High };

// Defaults mirror config/car.toml [wipers] (CarConfig); release reads that file, debug builds
// tune them live (Tunables.def group "wiper"). A test pins file == defaults.
struct WiperParams {
    float lowWipesPerMin  = 45.f;   // full wipes (park -> top -> park) per minute on Low
    float highWipesPerMin = 65.f;   // ... on High
    float dwellS          = 4.f;    // Intermittent: pause at park between single Low wipes
    float sweepDeg        = 105.f;  // park -> top; the rigged blades leave the glass past ~120
    float ease            = 1.f;    // 0 = constant angular speed, 1 = cosine (eased reversals)
};

struct WiperState {
    WiperMode mode  = WiperMode::Off;
    float     phase = 0.f;  // [0,1) through the current wipe; exactly 0 = parked
    float     dwell = 0.f;  // s left resting at park (Intermittent only)
};

// V: Off -> Intermittent -> Low -> High -> Off.
[[nodiscard]] constexpr WiperMode wiper_next_mode(WiperMode m) {
    switch (m) {
        case WiperMode::Off:
            return WiperMode::Intermittent;
        case WiperMode::Intermittent:
            return WiperMode::Low;
        case WiperMode::Low:
            return WiperMode::High;
        case WiperMode::High:
            return WiperMode::Off;
    }
    return WiperMode::Off;
}

[[nodiscard]] constexpr const char* wiper_mode_name(WiperMode m) {
    switch (m) {
        case WiperMode::Off:
            return "Off";
        case WiperMode::Intermittent:
            return "Intermittent";
        case WiperMode::Low:
            return "Low";
        case WiperMode::High:
            return "High";
    }
    return "?";
}

// Wipes per second. Intermittent wipes at Low speed; Off finishes its wipe at Low speed too
// (the park switch runs the motor on its low winding).
[[nodiscard]] constexpr float wiper_rate_hz(WiperMode m, const WiperParams& p) {
    return (m == WiperMode::High ? p.highWipesPerMin : p.lowWipesPerMin) / 60.f;
}

// A mode switch keeps the blade where it is (no jump); it only clears a pending dwell, so a
// switch to Intermittent wipes at once, as on a real stalk.
[[nodiscard]] constexpr WiperState wiper_set_mode(WiperState s, WiperMode m) {
    s.mode  = m;
    s.dwell = 0.f;
    return s;
}

[[nodiscard]] constexpr bool wiper_parked(const WiperState& s) {
    return s.phase <= 0.f;
}

// Moving, or switched on (an Intermittent blade dwelling at park is active).
[[nodiscard]] constexpr bool wiper_active(const WiperState& s) {
    return s.mode != WiperMode::Off || !wiper_parked(s);
}

// Advance by dt seconds. Off never stops mid-glass: it finishes the wipe, then parks.
[[nodiscard]] inline WiperState wiper_step(WiperState s, const WiperParams& p, float dt) {
    if (wiper_parked(s)) {
        if (s.mode == WiperMode::Off)
            return s;
        if (s.mode == WiperMode::Intermittent && s.dwell > 0.f) {
            s.dwell = std::max(0.f, s.dwell - dt);
            return s;
        }
    }
    s.phase += wiper_rate_hz(s.mode, p) * std::max(dt, 0.f);
    if (s.phase >= 1.f) {
        if (s.mode == WiperMode::Low || s.mode == WiperMode::High) {
            s.phase -= std::floor(s.phase);  // continuous: straight into the next wipe
        } else {
            s.phase = 0.f;  // Off / Intermittent: rest at park
            s.dwell = s.mode == WiperMode::Intermittent ? p.dwellS : 0.f;
        }
    }
    return s;
}

// Normalized sweep s(u) in [0,1] (see the header comment).
[[nodiscard]] inline float wiper_profile(float phase, float ease) {
    const float u      = phase - std::floor(phase);
    const float tri    = 1.f - std::abs(2.f * u - 1.f);
    const float cosine = 0.5f - 0.5f * std::cos(util::units::kTwoPi * u);
    const float e      = std::clamp(ease, 0.f, 1.f);
    return tri + (cosine - tri) * e;
}

// Blade angle (rad) off park: 0 parked, sweepDeg at the top.
[[nodiscard]] inline float wiper_angle(const WiperState& s, const WiperParams& p) {
    return glm::radians(p.sweepDeg) * wiper_profile(s.phase, p.ease);
}

}  // namespace swish
