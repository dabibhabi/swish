#include "renderer/SpraySystem/WheelSpray.h"

#include <catch2/catch_test_macros.hpp>

using namespace swish;

// GPU-free tests of the per-wheel spray emitter stub.

namespace {
WheelSprayInput wheel(float speedMps, float slip = 1.0f) {
    WheelSprayInput w;
    w.pos          = Vec3(0.0f);
    w.vel          = Vec3(0.0f, 0.0f, -util::units::m(speedMps));
    w.slipSprayMul = slip;
    return w;
}
std::array<WheelSprayInput, 4> all4(float speedMps) {
    return {wheel(speedMps), wheel(speedMps), wheel(speedMps), wheel(speedMps)};
}
}  // namespace

TEST_CASE("Wheel spray: zero when dry", "[rain][spray]") {
    REQUIRE(wheel_spray_rate(wheel(30.0f), 0.0f, 0.0f) == 0.0f);
    REQUIRE(wheel_spray_rate(wheel(30.0f), 0.0f, 1.0f) == 0.0f);
    REQUIRE(compute_wheel_spray(all4(30.0f), 0.0f, 1.0f).count == 0);
}

TEST_CASE("Wheel spray: zero when stationary", "[rain][spray]") {
    REQUIRE(wheel_spray_rate(wheel(0.0f), 1.0f, 1.0f) == 0.0f);
    REQUIRE(compute_wheel_spray(all4(0.0f), 1.0f, 1.0f).count == 0);
}

TEST_CASE("Wheel spray: monotonic in wetness, standing water, speed, slip", "[rain][spray]") {
    float prev = -1.0f;
    for (float w = 0.0f; w <= 1.0001f; w += 0.1f) {
        float r = wheel_spray_rate(wheel(5.0f), w, 0.3f);
        REQUIRE(r >= prev);
        prev = r;
    }
    prev = -1.0f;
    for (float s = 0.0f; s <= 1.0001f; s += 0.1f) {
        float r = wheel_spray_rate(wheel(5.0f), 0.8f, s);
        REQUIRE(r >= prev);
        prev = r;
    }
    prev = -1.0f;
    for (float v = 0.0f; v <= 30.0f; v += 1.0f) {
        float r = wheel_spray_rate(wheel(v), 0.8f, 0.5f);
        REQUIRE(r >= prev);
        prev = r;
    }
    REQUIRE(wheel_spray_rate(wheel(10.0f, 2.0f), 0.3f, 0.2f) > wheel_spray_rate(wheel(10.0f, 1.0f), 0.3f, 0.2f));
    REQUIRE(wheel_spray_rate(wheel(10.0f, 0.0f), 1.0f, 1.0f) == 0.0f);
    // Strictly increasing somewhere in the interior.
    REQUIRE(wheel_spray_rate(wheel(10.0f), 0.8f, 0.5f) > wheel_spray_rate(wheel(10.0f), 0.4f, 0.5f));
    REQUIRE(wheel_spray_rate(wheel(10.0f), 0.4f, 0.8f) > wheel_spray_rate(wheel(10.0f), 0.4f, 0.1f));
}

TEST_CASE("Wheel spray: emitters compacted, bounded, carry wheel state", "[rain][spray]") {
    auto wheels   = all4(25.0f);
    wheels[1]     = wheel(0.0f);  // one stationary wheel
    wheels[2].pos = Vec3(1.0f, 2.0f, 3.0f);
    auto r        = compute_wheel_spray(wheels, 1.0f, 1.0f);
    REQUIRE(r.count == 3);
    REQUIRE(r.emitters[1].pos == Vec3(1.0f, 2.0f, 3.0f));
    for (uint32_t i = 0; i < r.count; ++i) {
        REQUIRE(r.emitters[i].emitRate > 0.0f);
        REQUIRE(r.emitters[i].emitRate <= 1.0f);
    }
    // Standing water widens the cone.
    REQUIRE(compute_wheel_spray(all4(25.0f), 1.0f, 1.0f).emitters[0].coneHalfAngle >
            compute_wheel_spray(all4(25.0f), 1.0f, 0.0f).emitters[0].coneHalfAngle);
}
