// Dial needles of the rigged 993 (tasks/todo.md "Rigged Porsche" S1c): calibration, damped follower,
// startup sweep and draw pose in scene/Entity/GaugeNeedles.h, plus the [gauges] table of config/car.toml (CarConfig).

#include "scene/CarConfig/CarConfig.h"
#include "scene/Entity/GaugeNeedles.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

using namespace swish;
using Catch::Approx;

TEST_CASE("gauges: tach fit hits the painted numerals and clamps to the printed range", "[gauges]") {
    const GaugeParams p = kGaugeDefaults;
    // Measured numerals: 1k -109.0, 4k -0.4, 7k +105.5. The briefed fit (-145 + 36/krpm) is off by
    // 0.0 / 0.6 / 1.5 deg there (least squares would be -144.3 + 35.75/krpm, max 0.9 deg).
    CHECK(tach_angle_deg(p, 1000.f) == Approx(-109.f).margin(1.6f));
    CHECK(tach_angle_deg(p, 4000.f) == Approx(-0.4f).margin(1.6f));
    CHECK(tach_angle_deg(p, 7000.f) == Approx(105.5f).margin(1.6f));
    CHECK(tach_angle_deg(p, -500.f) == tach_angle_deg(p, p.tachMinRpm));
    CHECK(tach_angle_deg(p, 12000.f) == tach_angle_deg(p, p.tachMaxRpm));
    for (float r = 0.f; r < 7600.f; r += 50.f) CHECK(tach_angle_deg(p, r + 50.f) > tach_angle_deg(p, r));
}

TEST_CASE("gauges: speedo LUT maps every anchor exactly, is monotonic and clamps", "[gauges]") {
    const SpeedoLut& lut = kSpeedoLut993;
    REQUIRE(speedo_lut_valid(lut));
    for (size_t i = 0; i < static_cast<size_t>(lut.count); i++) CHECK(speedo_angle_deg(lut, lut.kmh[i]) == lut.deg[i]);
    float prev = speedo_angle_deg(lut, 0.f);
    for (float v = 0.5f; v <= 300.f; v += 0.5f) {
        const float a = speedo_angle_deg(lut, v);
        CHECK(a >= prev);
        prev = a;
    }
    CHECK(speedo_angle_deg(lut, -20.f) == lut.deg[0]);
    CHECK(speedo_angle_deg(lut, 400.f) == lut.deg[9]);
    CHECK(speedo_angle_deg(lut, 40.f) == Approx(0.5f * (-125.2f + -90.9f)));  // halfway 20..60
}

TEST_CASE("gauges: LUT validation rejects bad tables", "[gauges]") {
    SpeedoLut bad = kSpeedoLut993;
    bad.kmh[3]    = bad.kmh[2];  // not strictly increasing
    CHECK_FALSE(speedo_lut_valid(bad));
    SpeedoLut one = kSpeedoLut993;
    one.count     = 1;
    CHECK_FALSE(speedo_lut_valid(one));
    CHECK(gauge_params_valid(kGaugeDefaults));
}

TEST_CASE("gauges: follower converges without overshoot, frame-rate independent and stable", "[gauges]") {
    const float w = 1.f / kGaugeDefaults.needleTauS;
    for (const float dt : {1.f / 240.f, 1.f / 60.f, 1.f / 15.f, 0.5f}) {
        NeedleFollower s{-145.f, 0.f};
        float          t = 0.f;
        while (t < 1.0f) {
            s = needle_follow(s, 100.f, dt, w);
            t += dt;
            INFO("dt " << dt << " t " << t);
            CHECK(s.pos <= 100.f + 1e-3f);  // never past the target
            CHECK(s.vel >= -1e-3f);         // never turns back
        }
        CHECK(s.pos == Approx(100.f).margin(0.05f));
    }
    // The exact solution: the same wall-clock time gives the same position at any step size.
    NeedleFollower a{0.f, 0.f}, b{0.f, 0.f};
    for (int i = 0; i < 240; i++) a = needle_follow(a, 50.f, 1.f / 240.f, w);
    for (int i = 0; i < 4; i++) b = needle_follow(b, 50.f, 0.25f, w);
    CHECK(a.pos == Approx(b.pos).margin(1e-3f));
    // One time constant from rest: e = e0 (1 + 1) e^-1 = 0.7358 e0.
    const NeedleFollower c = needle_follow({0.f, 0.f}, 1.f, kGaugeDefaults.needleTauS, w);
    CHECK(c.pos == Approx(1.f - 2.f * std::exp(-1.f)).margin(1e-5f));
    // Degenerate inputs leave the state alone.
    CHECK(needle_follow({3.f, 1.f}, 9.f, 0.f, w).pos == 3.f);
}

TEST_CASE("gauges: startup sweep reaches full scale and returns, then follows live", "[gauges]") {
    const GaugeParams p = kGaugeDefaults;
    CHECK(gauge_sweep_fraction(0.f, p.sweepDurationS) == 0.f);
    CHECK(gauge_sweep_fraction(0.5f * p.sweepDurationS, p.sweepDurationS) == Approx(1.f));
    CHECK(gauge_sweep_fraction(p.sweepDurationS, p.sweepDurationS) == 0.f);

    GaugeState  s;
    const float dt    = 1.f / 120.f;
    float       peakR = -1e9f, peakS = -1e9f;
    float       t = 0.f;
    while (t < p.sweepDurationS - 0.5f * dt) {
        s     = gauges_step(s, p, 3000.f, 80.f, dt);
        peakR = std::max(peakR, s.rpm.pos);
        peakS = std::max(peakS, s.speed.pos);
        t += dt;
    }
    CHECK(peakR == Approx(tach_angle_deg(p, p.tachMaxRpm)).margin(0.05f));
    CHECK(peakS == Approx(kSpeedoLut993.deg[9]).margin(0.05f));
    s = gauges_step(s, p, 3000.f, 80.f, dt);  // the sweep ends this frame at zero
    CHECK(s.rpm.pos == Approx(tach_angle_deg(p, 0.f)));
    CHECK(s.speed.pos == Approx(kSpeedoLut993.deg[0]));
    for (int i = 0; i < 240; i++) s = gauges_step(s, p, 3000.f, 80.f, dt);
    CHECK(s.rpm.pos == Approx(tach_angle_deg(p, 3000.f)).margin(0.05f));
    CHECK(s.speed.pos == Approx(speedo_angle_deg(p.speedo, 80.f)).margin(0.05f));

    // A zero-length sweep follows from the first frame.
    GaugeParams q    = p;
    q.sweepDurationS = 0.f;
    GaugeState z     = gauges_step(GaugeState{}, q, 7000.f, 0.f, dt);
    CHECK(z.rpm.pos > tach_angle_deg(q, 0.f));
}

TEST_CASE("gauges: a +90 deg needle points at 3 o'clock for the driver", "[gauges]") {
    // A proper pivot frame like the rig's: local -Z (the sweep axis) points back at the driver (-X car),
    // local +Y up. Car space: nose +X, up +Y, right +Z, so 3 o'clock for the driver is +Z.
    Mat4 F(1.f);
    F[0] = Vec4(0.f, 0.f, -1.f, 0.f);
    F[1] = Vec4(0.f, 1.f, 0.f, 0.f);
    F[2] = Vec4(1.f, 0.f, 0.f, 0.f);
    F[3] = Vec4(1.f, 1.f, -0.4f, 1.f);
    REQUIRE(glm::determinant(glm::mat3(F)) == Approx(1.f));
    const Vec3 hub(F[3]);
    const Vec3 tip12 = hub + Vec3(0.f, 0.05f, 0.f);  // rest pose authored at 12 o'clock (rest = 0)
    const Vec3 tip   = Vec3(needle_pose(F, RigPart::NeedleRpm, 90.f, 0.f) * Vec4(tip12, 1.f));
    CHECK(tip.z - hub.z == Approx(0.05f).margin(1e-5f));
    CHECK(tip.y - hub.y == Approx(0.f).margin(1e-5f));
    // Theta == rest is the identity.
    const Mat4 I = needle_pose(F, RigPart::NeedleSpeed, -151.f, -151.f);
    CHECK(Vec3(I * Vec4(tip12, 1.f)).y == Approx(tip12.y));
}

TEST_CASE("gauges: config/car.toml [gauges] equals the C++ defaults; the reader falls back safely",
          "[gauges][config]") {
    const GaugeParams f = CarConfig::load(std::string(CONFIG_DIR) + "car.toml").gauges;
    const GaugeParams d = kGaugeDefaults;
    CHECK(f.tachOffsetDeg == d.tachOffsetDeg);
    CHECK(f.tachDegPerKrpm == d.tachDegPerKrpm);
    CHECK(f.tachMinRpm == d.tachMinRpm);
    CHECK(f.tachMaxRpm == d.tachMaxRpm);
    CHECK(f.rpmRestDeg == d.rpmRestDeg);
    CHECK(f.speedRestDeg == d.speedRestDeg);
    CHECK(f.needleTauS == Approx(d.needleTauS));
    CHECK(f.sweepDurationS == d.sweepDurationS);
    REQUIRE(f.speedo.count == d.speedo.count);
    for (size_t i = 0; i < static_cast<size_t>(d.speedo.count); i++) {
        CHECK(f.speedo.kmh[i] == d.speedo.kmh[i]);
        CHECK(f.speedo.deg[i] == d.speedo.deg[i]);
    }

    const GaugeParams partial = CarConfig::parse("[gauges]\ntach_offset_deg = -140.0\nneedle_tau_ms = 50\n").gauges;
    CHECK(partial.tachOffsetDeg == -140.f);
    CHECK(partial.needleTauS == Approx(0.05f));
    CHECK(partial.tachDegPerKrpm == d.tachDegPerKrpm);
    // An unsorted table is ignored, the rest still applies.
    const GaugeParams unsorted =
        CarConfig::parse("[gauges]\nspeedo_kmh = [0, 50, 20]\nspeedo_deg = [0, 1, 2]\ntach_max_rpm = 8000\n").gauges;
    CHECK(unsorted.speedo.count == d.speedo.count);
    CHECK(unsorted.tachMaxRpm == 8000.f);
    CHECK(CarConfig::parse("not = [valid").gauges.tachOffsetDeg == d.tachOffsetDeg);
    CHECK(CarConfig::load("/nonexistent/car.toml").gauges.sweepDurationS == d.sweepDurationS);
}
