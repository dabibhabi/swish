#pragma once

// Purpose:    GPU-free physics bench harness. Drives the REAL CarEntity::update() at a fixed dt through
//             the public InputFrame seam (CarEntity::apply_input_frame), measures standard figures of
//             merit, and prints them against 992 Turbo S reference targets.
// Units:      SI at this API (s, m, m/s, g, km/h for reporting). Conversion to WU is CarEntity's job
//             (get_speed_mps); constants come from utils/units/Units.h.
// Complexity: O(steps); a scenario is <= a few 10^4 steps of CarEntity::update().
// Example:    const BenchResult r = bench::zero_to_100();   // r.measured in seconds
// See also:   tests/test_bench.cpp, scene/Entity/CarEntity.h (apply_input_frame), tests/test_golden_car.cpp.
// Test:       tests/test_bench.cpp  ([bench])
//
// Reference values are DOCUMENTED TARGETS (not pass/fail gates): published 992 Turbo S figures
// (manufacturer ~2.7 s 0-100 km/h; ~31-32 m 100-0 km/h from road tests; ~1.1 g skidpad from road tests;
// 330 km/h top speed). "PASS" in the table only means "within the documented tolerance".

#include "scene/Entity/CarEntity.h"
#include "utils/input/InputFrame.h"
#include "utils/units/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace swish::bench {

inline constexpr float  kDt        = 1.0f / 120.0f;  // fixed bench step (< the 1/15 s update cap)
inline constexpr double kKmhPerMps = 3.6;            // exact (3600 s/h / 1000 m/km)
inline constexpr double kG         = 9.80665;        // standard gravity, m/s^2

// What a driver callback sees each step (pre-update state; SI).
struct BenchState {
    double t        = 0.0;  // elapsed seconds
    double v_mps    = 0.0;  // forward speed
    double dist_m   = 0.0;  // distance travelled (integral of |v| dt)
    double yaw_rate = 0.0;  // rad/s, right-positive (from heading change over the last step)
    int    step     = 0;
};

struct RunOutcome {
    BenchState final_state;
    bool       stopped = false;  // stop predicate fired (false => hit the time cap)
    bool       finite  = true;   // every sampled state stayed finite
};

// Core loop. drive(state, car) -> InputFrame; stop(state) -> bool; observe(state) is optional telemetry.
// Heading change is unwrapped to +/-180 deg per step; CarEntity heading obeys heading -= deg(r)*dt, so the
// right-positive yaw rate is r = -d(heading)/dt.
template <class Drive, class Stop, class Observe>
RunOutcome run_scenario(CarEntity& car, Drive&& drive, Stop&& stop, Observe&& observe, double max_seconds) {
    RunOutcome out;
    BenchState s;
    const int  max_steps = static_cast<int>(max_seconds / kDt);
    float      prev_head = car.get_rotation().y;
    for (int i = 0; i < max_steps; ++i) {
        s.step  = i;
        s.t     = static_cast<double>(i) * kDt;
        s.v_mps = car.get_speed_mps();
        if (stop(s)) {
            out.stopped = true;
            break;
        }
        const InputFrame f = drive(s, car);
        car.apply_input_frame(f, kDt);
        car.update(kDt);
        const float head = car.get_rotation().y;
        float       dh   = head - prev_head;
        while (dh > 180.f) dh -= 360.f;
        while (dh < -180.f) dh += 360.f;
        prev_head  = head;
        s.yaw_rate = -static_cast<double>(glm::radians(dh)) / kDt;
        s.v_mps    = car.get_speed_mps();
        s.dist_m += std::abs(s.v_mps) * kDt;
        if (!std::isfinite(s.v_mps) || !std::isfinite(s.yaw_rate)) out.finite = false;
        observe(s);
    }
    out.final_state = s;
    return out;
}

template <class Drive, class Stop>
RunOutcome run_scenario(CarEntity& car, Drive&& drive, Stop&& stop, double max_seconds) {
    return run_scenario(car, drive, stop, [](const BenchState&) {}, max_seconds);
}

struct BenchResult {
    std::string name;
    std::string unit;
    double      measured  = 0.0;
    double      reference = 0.0;
    double      tol_rel   = 0.0;  // documented relative tolerance (NOT enforced by tests)
    bool        finished  = false;
    bool        finite    = false;
    bool        within() const { return std::abs(measured - reference) <= tol_rel * std::abs(reference); }
};

// Throttle/brake speed-hold: P controller, throttle if below target, brake if well above.
inline InputFrame speed_hold(double v, double target) {
    InputFrame   f;
    const double e = target - v;
    if (e > 0.0)
        f.throttle = static_cast<float>(std::clamp(e * 0.5, 0.0, 1.0));
    else if (e < -0.5)
        f.brake = static_cast<float>(std::clamp(-e * 0.25, 0.0, 1.0));
    return f;
}

// 0 -> 100 km/h, full throttle from rest, straight. Time interpolated linearly across the crossing step.
inline BenchResult zero_to_100() {
    CarEntity    car;
    const double target = 100.0 / kKmhPerMps;
    double       prev_v = 0.0, t_cross = 0.0;
    bool         crossed = false;
    auto         drive   = [](const BenchState&, CarEntity&) {
        InputFrame f;
        f.throttle = 1.f;
        return f;
    };
    auto stop    = [&](const BenchState& s) { return crossed; };
    auto observe = [&](const BenchState& s) {
        if (!crossed && s.v_mps >= target) {
            const double dv = s.v_mps - prev_v;
            const double fr = dv > 0.0 ? (target - prev_v) / dv : 1.0;
            t_cross         = s.t + kDt * fr;  // s.t is the step START; step end = t + dt
            crossed         = true;
        }
        prev_v = s.v_mps;
    };
    const RunOutcome o = run_scenario(car, drive, stop, observe, 30.0);
    return {"0-100 km/h", "s", t_cross, 2.7, 0.25, crossed, o.finite};
}

// 100 -> 0 km/h full brake, straight. Full throttle to 100 km/h, then full brake the instant the speed
// is reached (overshoot < one step of acceleration); distance integrated from the brake-on instant.
inline BenchResult braking_100_0() {
    CarEntity    car;
    const double target               = 100.0 / kKmhPerMps;
    enum class Ph { Accel, Brake } ph = Ph::Accel;
    double brake_d0 = 0.0, brake_dist = 0.0;
    bool   done  = false;
    auto   drive = [&](const BenchState& s, CarEntity&) {
        if (ph == Ph::Accel && s.v_mps >= target) {
            ph       = Ph::Brake;
            brake_d0 = s.dist_m;
        }
        InputFrame f;
        if (ph == Ph::Brake)
            f.brake = 1.f;
        else
            f.throttle = 1.f;
        return f;
    };
    auto stop = [&](const BenchState& s) {
        if (ph == Ph::Brake && s.v_mps <= 0.01) {
            brake_dist = s.dist_m - brake_d0;
            done       = true;
        }
        return done;
    };
    const RunOutcome o = run_scenario(car, drive, stop, 60.0);
    return {"100-0 km/h", "m", brake_dist, 31.5, 0.15, done, o.finite};
}

// Top speed: full throttle until mean acceleration over a 2 s window < 0.01 m/s^2, or the time cap.
// Reports km/h. Cap is 600 s; hitting it counts as NOT finished.
inline BenchResult top_speed() {
    CarEntity car;
    double    win_v = 0.0, win_t = 0.0, vtop = 0.0;
    bool      conv  = false;
    auto      drive = [](const BenchState&, CarEntity&) {
        InputFrame f;
        f.throttle = 1.f;
        return f;
    };
    auto stop    = [&](const BenchState&) { return conv; };
    auto observe = [&](const BenchState& s) {
        vtop = s.v_mps;
        if (s.t - win_t >= 2.0) {
            conv  = std::abs(s.v_mps - win_v) / (s.t - win_t) < 0.01;
            win_v = s.v_mps;
            win_t = s.t;
        }
    };
    const RunOutcome o = run_scenario(car, drive, stop, observe, 600.0);
    return {"top speed", "km/h", vtop * kKmhPerMps, 330.0, 0.05, conv, o.finite};
}

// Skidpad, R = 40 m. Method (documented): speed target ramps 6 -> 60 m/s at 0.5 m/s^2 (P throttle/brake
// hold) while an integrator steers to hold curvature kappa = r/v at 1/R (feed-forward kinematic
// atan(L/R)). Lateral acceleration a_y = v * r (r = yaw rate from heading change; ignores body sideslip,
// which is small and not publicly readable), low-passed with a 0.25 s EMA. The reported value is the
// peak EMA a_y / g reached while the radius is still within +/-10 % of 40 m (i.e. the quasi-steady
// grip limit). The run stops when the radius has been >25 % off for 1 s (limit exceeded), at 60 m/s, or at
// the cap.
inline BenchResult skidpad_40m() {
    constexpr double R       = 40.0;
    constexpr double kappa_t = 1.0 / R;
    CarEntity        car;
    const double     L_m    = 2.45;                // default (992) CarProfile wheelbase, car.wheelbase
    double           delta  = std::atan(L_m / R);  // rad, right-positive
    double           ay_ema = 0.0, peak_g = 0.0, off_t = 0.0;
    bool             limit = false, reached = false;
    auto             drive = [&](const BenchState& s, CarEntity&) {
        const double vt = std::min(6.0 + 0.5 * s.t, 60.0);
        InputFrame   f  = speed_hold(s.v_mps, vt);
        f.steer         = static_cast<float>(std::clamp(delta / glm::radians(car.steer_max()), -1.0, 1.0));
        f.steer_mode    = SteerMode::Target;
        return f;
    };
    auto stop    = [&](const BenchState& s) { return limit || s.v_mps >= 60.0; };
    auto observe = [&](const BenchState& s) {
        if (s.v_mps < 8.0) return;
        const double kappa = s.yaw_rate / s.v_mps;
        delta += 0.5 * (kappa_t - kappa) * kDt;
        delta = std::clamp(delta, 0.0, static_cast<double>(glm::radians(car.steer_max())));
        ay_ema += (s.v_mps * s.yaw_rate - ay_ema) * (kDt / 0.25);
        const double radius_err = std::abs(kappa - kappa_t) / kappa_t;
        if (radius_err < 0.10) {
            peak_g  = std::max(peak_g, ay_ema / kG);
            reached = true;
        }
        off_t = radius_err > 0.25 ? off_t + kDt : 0.0;
        if (off_t > 1.0) limit = true;
    };
    const RunOutcome o = run_scenario(car, drive, stop, observe, 120.0);
    return {"skidpad 40 m", "g", peak_g, 1.1, 0.15, reached, o.finite};
}

inline void print_table(const std::vector<BenchResult>& rs) {
    std::printf("\n%-14s %10s %10s %6s  %s\n", "scenario", "measured", "reference", "tol", "verdict");
    for (const auto& r : rs)
        std::printf("%-14s %8.2f %-3s %8.2f %5.0f%%  %s%s\n", r.name.c_str(), r.measured, r.unit.c_str(), r.reference,
                    r.tol_rel * 100.0, r.within() ? "PASS" : "OUT", r.finished ? "" : " (cap hit / not finished)");
    std::fflush(stdout);
}

}  // namespace swish::bench
