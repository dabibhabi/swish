// Phase C slip/skid/spray-boost visual hooks (SlipVisuals.h). GPU-free.

#include "scene/Entity/CarEntity.h"
#include "scene/Entity/SlipVisuals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace swish;

namespace {
const CarParams  kCar{};
const TireParams kTire{};

CarSlipVisuals eval(const SlipInputs& in) {
    return compute_slip_visuals(in, kCar, kTire);
}
}  // namespace

TEST_CASE("slip visuals: identity at rest and in pure rolling", "[slip]") {
    SECTION("rest") {
        const CarSlipVisuals v = eval(SlipInputs{});
        for (const WheelSlipVisual& w : v.wheel) {
            REQUIRE(w.sprayBoost == 1.0f);
            REQUIRE_FALSE(w.skid);
        }
        REQUIRE(v.agg.maxSprayBoost == 1.0f);
        REQUIRE_FALSE(v.agg.anySkid);
        REQUIRE(v.agg.skidCount == 0);
    }
    SECTION("pure rolling at highway speed") {
        SlipInputs in;
        in.vx                  = 30.0f;
        const CarSlipVisuals v = eval(in);
        for (const WheelSlipVisual& w : v.wheel) {
            REQUIRE(w.sprayBoost == 1.0f);
            REQUIRE_FALSE(w.skid);
            REQUIRE(w.slipAngle == 0.0f);
            REQUIRE(w.slipRatio == 0.0f);
        }
    }
    SECTION("small slip below ramp start stays identity") {
        SlipInputs in;
        in.vx                  = 30.0f;
        in.vl                  = 30.0f * 0.02f;  // ~1.1 deg
        const CarSlipVisuals v = eval(in);
        REQUIRE(v.agg.maxSprayBoost == 1.0f);
        REQUIRE_FALSE(v.agg.anySkid);
    }
}

TEST_CASE("slip visuals: thresholds trigger skid", "[slip]") {
    const SlipVisualParams sp{};
    SECTION("slip angle") {
        REQUIRE_FALSE(make_wheel_slip_visual(0.f, 7.0f * SlipVisualParams::kDegToRad, 20.f, sp).skid);
        REQUIRE(make_wheel_slip_visual(0.f, 9.0f * SlipVisualParams::kDegToRad, 20.f, sp).skid);
    }
    SECTION("slip ratio") {
        REQUIRE_FALSE(make_wheel_slip_visual(0.15f, 0.f, 20.f, sp).skid);
        REQUIRE(make_wheel_slip_visual(0.25f, 0.f, 20.f, sp).skid);
    }
    SECTION("below minimum speed never skids or boosts") {
        const WheelSlipVisual w = make_wheel_slip_visual(0.5f, 0.5f, 1.0f, sp);
        REQUIRE_FALSE(w.skid);
        REQUIRE(w.sprayBoost == 1.0f);
    }
    SECTION("from raw state: hard sideslip skids the rear") {
        SlipInputs in;
        in.vx                  = 25.0f;
        in.vl                  = 25.0f * 0.30f;  // ~17 deg rear slip
        const CarSlipVisuals v = eval(in);
        REQUIRE(v.wheel[2].skid);
        REQUIRE(v.wheel[3].skid);
        REQUIRE(v.agg.anySkid);
        REQUIRE(v.agg.skidCount >= 2);
    }
    SECTION("from raw state: hard braking at the grip limit skids") {
        SlipInputs in;
        in.vx                  = 25.0f;
        const float fz         = kCar.mass * kCar.gravity;
        in.fx.front            = -0.6f * kCar.tireMu * fz;
        in.fx.rear             = -0.4f * kCar.tireMu * fz;
        const CarSlipVisuals v = eval(in);
        REQUIRE(v.agg.anySkid);
        REQUIRE(v.wheel[0].slipRatio < 0.f);
    }
}

TEST_CASE("slip visuals: spray boost is monotonic in slip and bounded", "[slip]") {
    const SlipVisualParams sp{};
    float                  prev = 1.0f;
    for (int i = 0; i <= 60; ++i) {
        const float a = static_cast<float>(i) * 0.5f * SlipVisualParams::kDegToRad;  // 0..30 deg
        const float b = make_wheel_slip_visual(0.f, a, 20.f, sp).sprayBoost;
        REQUIRE(b >= prev);
        REQUIRE(b <= sp.sprayBoostMax);
        prev = b;
    }
    REQUIRE_THAT(prev, Catch::Matchers::WithinAbs(sp.sprayBoostMax, 1e-5));

    prev = 1.0f;
    for (int i = 0; i <= 50; ++i) {
        const float k = static_cast<float>(i) * 0.01f;
        const float b = make_wheel_slip_visual(k, 0.f, 20.f, sp).sprayBoost;
        REQUIRE(b >= prev);
        prev = b;
    }
}

TEST_CASE("slip visuals: symmetric in slip sign", "[slip]") {
    for (float a : {0.02f, 0.1f, 0.2f, 0.4f}) {
        const WheelSlipVisual p = make_wheel_slip_visual(0.f, a, 20.f);
        const WheelSlipVisual n = make_wheel_slip_visual(0.f, -a, 20.f);
        REQUIRE(p.sprayBoost == n.sprayBoost);
        REQUIRE(p.skid == n.skid);
    }
    for (float k : {0.05f, 0.15f, 0.25f, 0.5f}) {
        const WheelSlipVisual p = make_wheel_slip_visual(k, 0.f, 20.f);
        const WheelSlipVisual n = make_wheel_slip_visual(-k, 0.f, 20.f);
        REQUIRE(p.sprayBoost == n.sprayBoost);
        REQUIRE(p.skid == n.skid);
    }
    SlipInputs l, r;
    l.vx = r.vx = 25.0f;
    l.vl        = 6.0f;
    r.vl        = -6.0f;
    REQUIRE(eval(l).agg.maxSprayBoost == eval(r).agg.maxSprayBoost);
    REQUIRE(eval(l).agg.skidCount == eval(r).agg.skidCount);
}

TEST_CASE("CarEntity::get_wheel_slip_visuals is identity for a fresh car", "[slip]") {
    CarEntity            car;
    const CarSlipVisuals v = car.get_wheel_slip_visuals();
    REQUIRE(v.agg.maxSprayBoost == 1.0f);
    REQUIRE_FALSE(v.agg.anySkid);
}
