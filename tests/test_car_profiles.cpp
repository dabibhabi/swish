// Car profiles (CarProfile.h): the 993 Carrera RS profile's figures of merit, the gearCount
// generalisation of the Ch01 gearbox, and that the 992 Turbo S default is unchanged bit for bit.
#include "bench/Bench.h"
#include "scene/Entity/CarEntity.h"
#include "scene/Entity/CarPhysics.h"
#include "scene/Entity/CarProfile.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace swish;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
constexpr CarProfile kRs = car_profiles::rs_993();

// Compile-time: the 993 gearbox passes every Ch01 validator.
static_assert(gear_count_in_range(kRs.gearbox));
static_assert(ratios_positive(kRs.gearbox));
static_assert(ratios_strictly_descending(kRs.gearbox));
static_assert(shift_band_ordered(kRs.gearbox));
static_assert(dwell_non_negative(kRs.gearbox));
static_assert(upshift_lands_above_downshift(kRs.gearbox));
static_assert(downshift_lands_below_upshift(kRs.gearbox));
static_assert(gearbox_params_valid(kRs.gearbox));
static_assert(torque_curve_valid(kRs.torque));
static_assert(car_profile_valid(kRs));
// The unused 7th slot is not a gear: the same box with gearCount 7 must fail (a zero ratio).
static_assert(!gearbox_params_valid(GearboxParams{.ratios = kRs.gearbox.ratios, .gearCount = 7}));
static_assert(!gear_count_in_range(GearboxParams{.gearCount = 0}));
static_assert(!gear_count_in_range(GearboxParams{.gearCount = 8}));

// ── Frozen copies of the pre-profile 992 code (Powertrain.h before 2026-10-12) ──
namespace legacy {
float engine_torque(float rpm) {
    struct Pt {
        float rpm, nm;
    };
    static constexpr Pt kCurve[] = {
        {900.f, 250.f},  {1500.f, 480.f}, {2000.f, 700.f},  {2500.f, 800.f}, {4000.f, 800.f},
        {5000.f, 760.f}, {6000.f, 715.f}, {6750.f, 676.2f}, {7000.f, 620.f}, {7200.f, 0.f},
    };
    constexpr float  kRedline = 7200.f;
    constexpr size_t kCurveN  = sizeof(kCurve) / sizeof(kCurve[0]);
    if (rpm >= kRedline)
        return 0.f;
    if (rpm <= kCurve[0].rpm)
        return kCurve[0].nm;
    for (size_t i = 0; i + 1 < kCurveN; ++i) {
        if (rpm >= kCurve[i].rpm && rpm <= kCurve[i + 1].rpm) {
            const float f = (rpm - kCurve[i].rpm) / (kCurve[i + 1].rpm - kCurve[i].rpm);
            return kCurve[i].nm + f * (kCurve[i + 1].nm - kCurve[i].nm);
        }
    }
    return 0.f;
}
int gear_for_speed(float v, const GearboxParams& g, float rWheel) {
    for (int i = 0; i < 6; ++i) {
        if (engine_rpm(v, i, g, rWheel) < g.upshiftRpm)
            return i;
    }
    return 6;
}
GearState gearbox_shift_step(GearState state, float v, const GearboxParams& g, float rWheel, float dt) {
    state.sinceShift += dt;
    const float rpm = engine_rpm(v, state.gear, g, rWheel);
    if (state.sinceShift >= g.shiftDwell) {
        if (rpm >= g.upshiftRpm && state.gear < 6) {
            state.gear += 1;
            state.sinceShift = 0.0f;
        } else if (rpm <= g.downshiftRpm && state.gear > 0) {
            state.gear -= 1;
            state.sinceShift = 0.0f;
        }
    }
    return state;
}
}  // namespace legacy

float rs_accel(float v, int gear, float throttle = 1.f) {
    return longitudinal_accel(v, throttle, 0.f, gear, kRs.car, kRs.tire, kRs.gearbox, kRs.wheelRadiusM, kRs.drivetrain,
                              AxleLateral{}, nullptr, kRs.torque);
}
}  // namespace

TEST_CASE("993 RS: top speed is 277 km/h within 3%, drag-limited in 6th below the cut", "[car][profile]") {
    const float vTop =
        steady_state_top_speed(kRs.car, kRs.tire, kRs.gearbox, kRs.wheelRadiusM, kRs.drivetrain, kRs.torque);
    REQUIRE_THAT(vTop * 3.6f, WithinRel(277.0f, 0.03f));
    const float rpmTop = engine_rpm(vTop, 5, kRs.gearbox, kRs.wheelRadiusM);
    REQUIRE(rpmTop < kRs.gearbox.upshiftRpm);  // no rev-limiter top speed, and no attempt to shift past 6th
    for (int gear = 0; gear < 5; ++gear)
        REQUIRE(top_speed_in_gear(gear, kRs.car, kRs.tire, kRs.gearbox, kRs.wheelRadiusM, kRs.drivetrain, kRs.torque) <
                vTop);
}

TEST_CASE("993 RS: speeds per gear at the 6800 rpm cut match the G50/31 table", "[car][profile]") {
    // v = rpm·2πr / (60·i·FD), r = 0.320 m → 75.5 / 119.1 / 156.5 / 191.8 / 232.6 / 290.5 km/h.
    const float expectKmh[6] = {75.5f, 119.1f, 156.5f, 191.8f, 232.6f, 290.5f};
    for (int g = 0; g < 6; ++g) {
        const float v = 6800.f * 2.f * kEnginePi * kRs.wheelRadiusM /
                        (60.f * kRs.gearbox.ratios[static_cast<size_t>(g)] * kRs.gearbox.finalDrive);
        CHECK_THAT(v * 3.6f, WithinAbs(expectKmh[g], 0.1f));
        CHECK_THAT(engine_rpm(v, g, kRs.gearbox, kRs.wheelRadiusM), WithinRel(6800.f, 1e-4f));
    }
}

TEST_CASE("993 RS: 0-100 km/h through the real CarEntity is 4.8-5.6 s", "[car][profile]") {
    CarEntity car;
    car.apply_profile(kRs);
    const double target = 100.0 / bench::kKmhPerMps;
    double       prev_v = 0.0, t_cross = 0.0;
    bool         crossed = false;
    int          maxGear = 0;
    auto         drive   = [](const bench::BenchState&, CarEntity&) {
        InputFrame f;
        f.throttle = 1.f;
        return f;
    };
    auto stop    = [&](const bench::BenchState&) { return crossed; };
    auto observe = [&](const bench::BenchState& s) {
        maxGear = std::max(maxGear, car.get_gear());
        if (!crossed && s.v_mps >= target) {
            const double dv = s.v_mps - prev_v;
            t_cross         = s.t + bench::kDt * (dv > 0.0 ? (target - prev_v) / dv : 1.0);
            crossed         = true;
        }
        prev_v = s.v_mps;
    };
    const bench::RunOutcome o = bench::run_scenario(car, drive, stop, observe, 30.0);
    REQUIRE(crossed);
    REQUIRE(o.finite);
    CHECK(t_cross >= 4.8);
    CHECK(t_cross <= 5.6);
    CHECK(maxGear == 1);  // 100 km/h is in 2nd (1st runs out at 75 km/h)
}

TEST_CASE("993 RS: redline 6800 rpm, zero torque at and above it, spec anchors hit", "[car][profile]") {
    REQUIRE(kRs.torque.redline == 6800.f);
    for (float rpm = 6800.f; rpm <= 12000.f; rpm += 50.f)
        REQUIRE(engine_torque(rpm, kRs.torque) == 0.f);
    REQUIRE(engine_torque(6799.f, kRs.torque) > 0.f);
    REQUIRE_THAT(engine_torque(5400.f, kRs.torque), WithinAbs(355.f, 1e-3f));  // peak torque [spec]
    const float omega6500 = 6500.f * 2.f * kEnginePi / 60.f;
    REQUIRE_THAT(engine_torque(6500.f, kRs.torque) * omega6500, WithinRel(221000.f, 0.005f));  // 300 PS [spec]
    // 5400 is the torque peak and 6500 the power peak over the whole band.
    float bestT = 0.f, bestTrpm = 0.f, bestP = 0.f, bestPrpm = 0.f;
    for (float rpm = 900.f; rpm < 6800.f; rpm += 10.f) {
        const float t = engine_torque(rpm, kRs.torque);
        const float p = t * rpm * 2.f * kEnginePi / 60.f;
        if (t > bestT) {
            bestT    = t;
            bestTrpm = rpm;
        }
        if (p > bestP) {
            bestP    = p;
            bestPrpm = rpm;
        }
    }
    CHECK(bestTrpm == 5400.f);
    CHECK(bestPrpm == 6500.f);
    CHECK(kRs.gearbox.upshiftRpm < kRs.torque.redline);
}

TEST_CASE("993 RS: gearCount 6, the shifter never selects gear index 6", "[car][profile][gearbox]") {
    REQUIRE(kRs.gearbox.gearCount == 6);
    REQUIRE(gear_for_speed(150.f, kRs.gearbox, kRs.wheelRadiusM) == 5);
    // Full throttle from rest to top speed with the stateful box, then cruise flat out far past it.
    GearState   gs{};
    float       v       = 0.f;
    const float dt      = 1.f / 240.f;
    int         maxGear = 0;
    for (float t = 0.f; t < 120.f; t += dt) {
        gs = gearbox_shift_step(gs, v, kRs.gearbox, kRs.wheelRadiusM, dt);
        v += rs_accel(v, gs.gear) * dt;
        maxGear = std::max(maxGear, gs.gear);
    }
    REQUIRE(maxGear == 5);
    for (float vHold = 0.f; vHold < 150.f; vHold += 0.5f) {
        GearState s{.gear = 5};
        s = gearbox_shift_step(s, vHold, kRs.gearbox, kRs.wheelRadiusM, 1.f);
        REQUIRE(s.gear <= 5);
    }
}

TEST_CASE("993 RS is rear-wheel drive: no front drive force, rear-only traction cap", "[car][profile]") {
    REQUIRE(ptu_lock_fraction(1e6f, kRs.car, kRs.tire, kRs.drivetrain) == 0.f);
    AxleLongitudinal split{};
    const float      f =
        awd_deliverable_force(1e6f, kRs.car, kRs.tire, kRs.drivetrain,
                              ptu_lock_fraction(1e6f, kRs.car, kRs.tire, kRs.drivetrain), AxleLateral{}, &split);
    REQUIRE(split.front == 0.f);
    REQUIRE_THAT(f, WithinRel(rear_traction_cap_force(kRs.car, kRs.tire), 0.01f));
}

TEST_CASE("993 RS: measured rig geometry overrides wheelbase and tyre radius", "[car][profile]") {
    const CarRigGeometry geo{.wheelbase_m         = 2.259f,
                             .track_front_m       = 1.40f,
                             .track_rear_m        = 1.44f,
                             .tyre_radius_front_m = 0.318f,
                             .tyre_radius_rear_m  = 0.320f,
                             .named_rig           = true};
    const CarProfile     p = car_profiles::rs_993(&geo);
    CHECK(p.car.wheelbase == 2.259f);
    CHECK_THAT(p.tire.a + p.tire.b, WithinAbs(2.259f, 1e-5f));
    CHECK_THAT(p.tire.b / (p.tire.a + p.tire.b), WithinAbs(0.40f, 1e-5f));  // 40 % front load kept
    CHECK(p.wheelRadiusM == 0.320f);
    CHECK(car_profile_valid(p));
    // Implausible (unset) measurements fall back to the spec numbers.
    const CarRigGeometry none{0.f, 0.f, 0.f, 0.f, 0.f, false};
    const CarProfile     q = car_profiles::rs_993(&none);
    CHECK(q.car.wheelbase == 2.272f);
    CHECK(q.wheelRadiusM == 0.320f);
}

TEST_CASE("CarEntity::apply_profile carries steering, grip and redline", "[car][profile]") {
    CarEntity car;
    car.set_steering_angle(35.f);
    car.apply_profile(kRs);
    CHECK(car.steer_max() == 29.5f);
    CHECK(car.steer_ratio() == 16.5f);
    CHECK(car.get_steering_angle() == 29.5f);  // re-clamped to the new lock
    CHECK(car.get_redline_rpm() == 6800.f);
    car.set_wetness(0.f);
    car.update(1.f / 60.f);
    CHECK(car.get_effective_mu() == kRs.car.tireMu);  // muDry tracks tireMu exactly on a dry road
    // Lock to lock: 2 · 29.5° · 16.5 = 973.5° ≈ 2.7 turns.
    CHECK_THAT(2.f * car.steer_max() * car.steer_ratio() / 360.f, WithinAbs(2.7f, 0.01f));

    // A sustained full-lock run at speed stays finite and bounded (understeer-stable chassis).
    InputFrame f;
    f.throttle   = 1.f;
    f.steer      = 1.f;
    f.steer_mode = SteerMode::Target;
    for (int i = 0; i < 1200; ++i) {
        car.apply_input_frame(f, bench::kDt);
        car.update(bench::kDt);
    }
    CHECK(std::isfinite(car.get_speed_mps()));
    CHECK(std::isfinite(car.get_yaw_rate()));
    CHECK(std::abs(car.get_yaw_rate()) < 3.f);
}

TEST_CASE("992 Turbo S default profile reproduces the pre-profile code exactly", "[car][profile]") {
    const CarProfile p = car_profiles::turbo_s_992();
    REQUIRE(p.gearbox.gearCount == 7);
    // The constants CarEntity used to hard-code.
    CHECK(p.steerLockDeg == 35.f);
    CHECK(p.steerRatio == 450.f / 35.f);
    CHECK(p.wheelRadiusM == 0.363f);
    CHECK(p.car.wheelbase == 2'450.f / util::units::kWuPerMetre);
    CHECK(p.torque.redline == 7200.f);
    CHECK(WetGripParams{}.muDry == p.car.tireMu);
    CarEntity car;
    CHECK(car.steer_max() == 35.f);
    CHECK(car.get_redline_rpm() == 7200.f);

    // Torque: bit-identical over a fine sweep (both overloads).
    for (float rpm = 0.f; rpm < 8000.f; rpm += 0.37f) {
        REQUIRE(engine_torque(rpm) == legacy::engine_torque(rpm));
        REQUIRE(engine_torque(rpm, p.torque) == legacy::engine_torque(rpm));
    }
    // Gear selection and the shift machine: identical results with gearCount = 7.
    const float r = p.wheelRadiusM;
    for (float v = 0.f; v < 110.f; v += 0.25f)
        REQUIRE(gear_for_speed(v, p.gearbox, r) == legacy::gear_for_speed(v, p.gearbox, r));
    GearState a{}, b{};
    float     v = 0.f;
    for (int i = 0; i < 240 * 60; ++i) {  // 60 s: launch, all seven gears, then a coast-down
        const float throttle = i < 240 * 45 ? 1.f : 0.f;
        a                    = gearbox_shift_step(a, v, p.gearbox, r, 1.f / 240.f);
        b                    = legacy::gearbox_shift_step(b, v, p.gearbox, r, 1.f / 240.f);
        REQUIRE(a.gear == b.gear);
        REQUIRE(a.sinceShift == b.sinceShift);
        v += longitudinal_accel(v, throttle, 0.f, a.gear, p.car, p.tire, p.gearbox, r) * (1.f / 240.f);
    }
    // The explicit-profile path and the defaulted path agree to the bit.
    for (float vv = 0.f; vv < 95.f; vv += 0.5f)
        for (int g = 0; g < 7; ++g)
            REQUIRE(longitudinal_accel(vv, 1.f, 0.f, g, p.car, p.tire, p.gearbox, r, p.drivetrain, AxleLateral{},
                                       nullptr, p.torque) ==
                    longitudinal_accel(vv, 1.f, 0.f, g, CarParams{}, TireParams{}, GearboxParams{}, r));
}
