// Golden-TRAJECTORY tests for the REAL CarEntity::update() (not the hand-copied
// spin_step in test_car_physics.cpp). Fixture: tests/golden/car_trajectory.csv.
//
// CarEntity::update() is headless-safe (pure math + RoadGeometry/puddle field);
// only handle_input() needs a GLFWwindow. Since throttle/brake have no public
// setter (they are set by handle_input from key state, which cannot be
// injected without a real window), this test opens the private section of
// CarEntity via `#define private public` around ONE include. Production code
// is untouched; layout is identical (same access-agnostic members on clang).
//
// Toolchain-bound: values were generated with Apple clang + libm on arm64.
// Tolerance is a relative/absolute 1e-3 so benign libm ulp changes pass while
// any real physics change (gear logic, tire model, wet grip) fails. Regenerate
// deliberately with SWISH_UPDATE_GOLDENS=1 and review the diff.
#include "golden_util.h"
#include "scene/Entity/Entity.h"
#include "scene/Entity/CarPhysics.h"
#include "scene/Entity/WheelKinematics.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"  // deliberate: reach CarEntity's private inputs (no public setter)
#endif
#define private public
#include "scene/Entity/CarEntity.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

using namespace swish;

namespace {

struct Scenario {
    const char* name;
    float       throttle;
    float       brake;
    float       steerDeg;
    float       wetness;
    float       seconds;
};

// One line per sample: scenario,step,x,y,z,yawDeg,speedWU,latMps,yawRate,gear,mu,plane
constexpr Scenario kScenarios[] = {
    {"launch_straight", 1.f, 0.f, 0.f, 0.f, 12.f},   // 0->fast: gear shifts + drag balance
    {"throttle_then_brake", 1.f, 0.f, 0.f, 0.f, 8.f},  // brake phase handled below
    {"steady_turn_dry", 1.f, 0.f, 12.f, 0.f, 12.f},  // dynamic bicycle model engaged
    {"steady_turn_wet", 1.f, 0.f, 12.f, 1.f, 12.f},  // wet grip + hydroplane fraction
};

constexpr float kDt = 1.0f / 60.0f;

std::vector<std::string> run(const Scenario& sc) {
    CarEntity car;
    car.set_position(Vec3(0.f, 0.f, 0.f));
    car.set_rotation(Vec3(0.f, 0.f, 0.f));
    car.set_wetness(sc.wetness);
    car.set_steering_angle(sc.steerDeg);
    car.m_throttle = sc.throttle;
    car.m_brake    = sc.brake;

    const int steps = static_cast<int>(sc.seconds / kDt);
    std::vector<std::string> rows;
    for (int i = 1; i <= steps; ++i) {
        if (std::string(sc.name) == "throttle_then_brake" && i == steps / 2) {
            car.m_throttle = 0.f;
            car.m_brake    = 1.f;
        }
        car.update(kDt);
        if (i % 30 == 0) {  // every 0.5 s
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s,%d,%.4f,%.4f,%.4f,%.5f,%.4f,%.5f,%.6f,%d,%.5f,%.5f", sc.name, i,
                          car.get_position().x, car.get_position().y, car.get_position().z, car.get_rotation().y,
                          car.get_speed(), car.m_lateral_velocity, car.m_yaw_rate, car.get_gear(),
                          car.get_effective_mu(), car.m_plane_frac);
            rows.emplace_back(buf);
        }
    }
    return rows;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream        ss(s);
    std::string              t;
    while (std::getline(ss, t, ','))
        out.push_back(t);
    return out;
}

const std::string kFile = "car_trajectory.csv";

}  // namespace

TEST_CASE("Golden trajectory: real CarEntity::update", "[golden][car]") {
    std::vector<std::string> all;
    for (const auto& sc : kScenarios) {
        auto r = run(sc);
        all.insert(all.end(), r.begin(), r.end());
    }

    if (golden::update_mode()) {
        std::ofstream f(golden::path(kFile), std::ios::trunc);
        f << "# CarEntity::update golden trajectory (tests/test_golden_car.cpp), dt=1/60, sampled every 0.5 s\n"
          << "# scenario,step,x,y,z,yawDeg,speedWU,latMps,yawRate,gear,mu,planeFrac\n"
          << "# Regenerate: SWISH_UPDATE_GOLDENS=1 build/tests/swish_tests \"[golden]\"\n";
        for (const auto& l : all)
            f << l << '\n';
        SUCCEED("golden rewritten");
        return;
    }

    std::ifstream            f(golden::path(kFile));
    std::vector<std::string> expected;
    std::string              line;
    while (std::getline(f, line))
        if (!line.empty() && line[0] != '#')
            expected.push_back(line);

    REQUIRE(expected.size() == all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        INFO("row " << i << "\n  actual:   " << all[i] << "\n  expected: " << expected[i]);
        const auto a = split(all[i]);
        const auto e = split(expected[i]);
        REQUIRE(a.size() == e.size());
        CHECK(a[0] == e[0]);
        CHECK(a[1] == e[1]);
        CHECK(a[9] == e[9]);  // gear must match exactly
        for (size_t c = 2; c < a.size(); ++c) {
            if (c == 9)
                continue;
            const double av = std::stod(a[c]), ev = std::stod(e[c]);
            CHECK(std::abs(av - ev) <= 1e-3 + 1e-3 * std::abs(ev));
        }
    }
}

TEST_CASE("Golden trajectory: sanity invariants of the real update()", "[golden][car]") {
    // Independent of the golden file: guards against a corrupted fixture.
    CarEntity car;
    car.m_throttle = 1.f;
    for (int i = 0; i < 600; ++i)
        car.update(kDt);
    CHECK(car.get_speed() > 0.f);
    CHECK(std::isfinite(car.get_position().x));
    CHECK(car.get_gear() >= 1);  // shifted out of 1st within 10 s of full throttle
}
