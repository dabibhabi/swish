#include "scene/Entity/CarPhysics.h"
#include "scene/Entity/WheelKinematics.h"
#include "utils/units/Units.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

using namespace swish;
using Catch::Matchers::WithinAbs;

// Exercises the longitudinal force model (P0 #5), the engine torque curve +
// PDK-style automatic gearbox (Wave 2 step 3b), and the Phase-1 911 Turbo S
// retarget: AWD slip-responsive PTU split, per-axle friction circle with load
// transfer, and the parametric camber/toe + anti-roll-bar suspension curves
// (archive/docs/phase-1-physics-speed-accuracy.md). All pure math — no GPU. rWheel
// below is the loader-measured REAR rolling radius (~0.363 m — see
// WheelFrame::radius / ModelManager::load_car and
// CarEntity::rear_wheel_radius's fallback of the same value).
namespace {
constexpr float kRearWheelRadiusM = 0.363f;
constexpr float kSixtyMph         = 26.8224f;  // m/s

// Integrates full throttle from v=0 using the REAL stateful gearbox (not the
// idealized gear_for_speed) — the same two-call-per-frame pattern as
// CarEntity::update(): gearbox_shift_step, then longitudinal_accel. Stops at
// vTarget or tMax, whichever comes first.
struct AccelRun {
    float            v = 0.0f;
    float            t = 0.0f;
    GearState        gear{};
    std::vector<int> gearHistory;
};

AccelRun simulate_full_throttle(const CarParams& p, const TireParams& tp, const GearboxParams& g, float rWheel,
                                float vTarget, float dt = 1.0f / 240.0f, float tMax = 40.0f) {
    AccelRun run;
    while (run.v < vTarget && run.t < tMax) {
        run.gear = gearbox_shift_step(run.gear, run.v, g, rWheel, dt);
        run.v += longitudinal_accel(run.v, 1.0f, 0.0f, run.gear.gear, p, tp, g, rWheel) * dt;
        run.t += dt;
        run.gearHistory.push_back(run.gear.gear);
    }
    return run;
}

// Holds v FIXED (a steady cruise) and steps ONLY the shift state machine —
// gearbox_shift_step depends on (v, gear, dt), not throttle, so this isolates
// the anti-chatter property from any speed-holding controller.
int count_shifts_holding_speed(GearState start, float vHold, const GearboxParams& g, float rWheel,
                               float duration = 5.0f, float dt = 1.0f / 240.0f) {
    GearState state  = start;
    int       shifts = 0;
    for (float t = 0.0f; t < duration; t += dt) {
        const int before = state.gear;
        state            = gearbox_shift_step(state, vHold, g, rWheel, dt);
        if (state.gear != before)
            ++shifts;
    }
    return shifts;
}
}  // namespace

TEST_CASE("Engine torque curve hits both anchors and is rev-limited past redline", "[car][engine]") {
    // Anchors are exact table nodes (0% error by construction). Turbo S:
    // 800 N.m (590 lb-ft) plateau 2500-4000 rpm, 478 kW (641 hp) at 6750.
    REQUIRE_THAT(engine_torque(2500.0f), WithinAbs(800.0f, 800.0f * 0.02f));  // plateau start
    REQUIRE_THAT(engine_torque(4000.0f), WithinAbs(800.0f, 800.0f * 0.02f));  // plateau end
    REQUIRE_THAT(engine_torque(3200.0f), WithinAbs(800.0f, 1e-3f));           // FLAT in between
    REQUIRE_THAT(engine_torque(6750.0f), WithinAbs(676.2f, 676.2f * 0.02f));  // peak power rpm
    // P = T*omega at the power anchor reproduces the 478 kW spec to a few percent.
    const float omega  = 6750.0f * 2.0f * kEnginePi / 60.0f;
    const float powerW = engine_torque(6750.0f) * omega;
    REQUIRE_THAT(powerW, WithinAbs(478000.0f, 478000.0f * 0.03f));
    // The peak-POWER rpm really is where power peaks (it isn't just a label):
    // no lower or higher sample on the curve makes more.
    for (float rpm : {2500.0f, 4000.0f, 5000.0f, 6000.0f, 7000.0f}) {
        const float p = engine_torque(rpm) * rpm * 2.0f * kEnginePi / 60.0f;
        REQUIRE(p <= powerW);
    }
    // Turbo lag is REAL and load-bearing (it is why the launch is not a flat
    // mu*g): torque at 1400 rpm is well under the plateau.
    REQUIRE(engine_torque(1400.0f) < 0.7f * 800.0f);
    REQUIRE(engine_torque(1400.0f) > 0.3f * 800.0f);  // but not a dead engine either
    // Sane at idle: positive, no extrapolation below idle.
    REQUIRE(engine_torque(900.0f) > 0.0f);
    REQUIRE(engine_torque(0.0f) > 0.0f);
    // Zero AT and past redline (rev limiter genuinely limits revs) — and
    // CONTINUOUS there (no jump): approaching 7200 the curve is already ~0.
    REQUIRE_THAT(engine_torque(7199.9f), WithinAbs(0.0f, 5.0f));
    REQUIRE(engine_torque(7200.0f) <= 0.0f);
    REQUIRE(engine_torque(7500.0f) <= 0.0f);
    REQUIRE(engine_torque(12000.0f) <= 0.0f);
}

TEST_CASE("Launch acceleration is AWD four-wheel-limited (~mu*g), not rear-axle-limited", "[car][longitudinal][awd]") {
    CarParams        p;
    TireParams       t;
    GearboxParams    g;
    DrivetrainParams d;
    // 1st gear: engine_torque at the (vEff-floored, i.e. clutch-slipping)
    // launch rpm, multiplied through 1st*finalDrive, clears the four-wheel
    // traction ceiling — by design, see the GearboxParams comment.
    float a0 = longitudinal_accel(0.0f, /*throttle=*/1.0f, /*brake=*/0.0f, /*gear=*/0, p, t, g, kRearWheelRadiusM);

    // The Turbo S is AWD, and at launch the rear axle is demanded far past its
    // own limit, so the PTU clutch locks (lambda -> 1) and the axles are tied:
    // torque flows to grip, and the ceiling is the SUM of both axles' circles.
    // The axle-load sum is invariant under longitudinal transfer, so that sum
    // is exactly mu*m*g -> a_x = mu*g. This is EMERGENT (from the friction
    // circle + PTU lock law), not a scripted launch number.
    const float axFourWheel = p.tireMu * p.gravity;  // 10.30 m/s^2 = 1.05 g
    REQUIRE_THAT(a0, WithinAbs(axFourWheel, 0.05f));

    // ...and it is genuinely ABOVE what the rear axle alone could do — i.e.
    // the AWD model is doing real work, not reproducing the old RWD cap.
    const float axRearOnly = rear_traction_cap_force(p, t) / p.mass;  // 7.66 m/s^2
    REQUIRE(a0 > axRearOnly * 1.25f);

    // The PTU really is locked here, and the front share really did rise off
    // its base value.
    const float fDemand = gear_drive_force(0.0f, /*gear=*/0, g, p, kRearWheelRadiusM);
    const float lambda  = ptu_lock_fraction(fDemand, p, t, d);
    REQUIRE_THAT(lambda, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(ptu_front_share(d, lambda), WithinAbs(d.frontShareMax, 1e-6f));

    // Pinned band around the concrete Turbo S number (~10.30), independent of
    // re-deriving the formula.
    REQUIRE(a0 > 9.8f);
    REQUIRE(a0 < 10.8f);
}

TEST_CASE("AWD model degenerates EXACTLY to the legacy RWD closed form", "[car][longitudinal][awd]") {
    CarParams  p;
    TireParams t;
    // frontShareBase = 0 and an open clutch (lambda = 0) makes the AWD model a
    // rear-axle-only cap, which rear_traction_cap_force solves in closed form.
    // This is the bridge that proves the generalization did not change the
    // physics, only widen it.
    DrivetrainParams rwd;
    rwd.frontShareBase = 0.0f;
    rwd.frontShareMax  = 0.0f;

    const float legacy = rear_traction_cap_force(p, t);
    const float awd    = awd_deliverable_force(/*fDemand=*/1e7f, p, t, rwd, /*lambda=*/0.0f);
    // kLoadIters = 3 fixed-point passes from a cold start; each contracts the
    // residual by mu*h_cg/L = 0.193, so 0.193^3 = 0.72% is the bound (see the
    // comment on kLoadIters). The closed form is the exact fixed point.
    REQUIRE_THAT(awd, WithinAbs(legacy, legacy * 0.01f));
    REQUIRE(awd < legacy);  // approaching from below, as a cold start must
}

TEST_CASE("PTU engages with demand, not on a timer or a state machine", "[car][awd]") {
    CarParams        p;
    TireParams       t;
    GearboxParams    g;
    DrivetrainParams d;
    const float      v = 10.0f;

    // Sweep throttle in 1st. Light throttle: the rear axle is nowhere near its
    // limit, so the clutch stays open and the car is rear-driven (the real
    // Turbo S's steady-state behaviour). Hard throttle: the rear is
    // over-demanded, the clutch closes, and the front share rises to its
    // ceiling. Monotone in between — no hysteresis, no latched state.
    float prevLambda = -1.0f;
    for (float th : {0.1f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float fDemand = th * gear_drive_force(v, /*gear=*/0, g, p, kRearWheelRadiusM);
        const float lambda  = ptu_lock_fraction(fDemand, p, t, d);
        REQUIRE(lambda >= prevLambda);  // monotone non-decreasing in demand
        REQUIRE(lambda >= 0.0f);
        REQUIRE(lambda <= 1.0f);
        prevLambda = lambda;
    }
    // The two ends really are the two extremes.
    REQUIRE_THAT(ptu_lock_fraction(0.1f * gear_drive_force(v, 0, g, p, kRearWheelRadiusM), p, t, d),
                 WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(ptu_lock_fraction(1.0f * gear_drive_force(v, 0, g, p, kRearWheelRadiusM), p, t, d),
                 WithinAbs(1.0f, 1e-6f));
    // Open clutch => the front takes only its base share.
    REQUIRE_THAT(ptu_front_share(d, 0.0f), WithinAbs(d.frontShareBase, 1e-6f));
    // A DEMAND below the rear's own grip is delivered in full (nothing is lost
    // to a split that the tires could have used).
    const float easy = 0.1f * gear_drive_force(v, 0, g, p, kRearWheelRadiusM);
    REQUIRE_THAT(awd_deliverable_force(easy, p, t, d, 0.0f), WithinAbs(easy, 1.0f));
}

TEST_CASE("Axle load transfer: sum is invariant, rear squats under power, front dives under braking",
          "[car][longitudinal][awd]") {
    CarParams   p;
    TireParams  t;
    const float L      = t.a + t.b;
    const float weight = p.mass * p.gravity;

    const AxleLoad stat = axle_loads_long(p, t, 0.0f);
    REQUIRE_THAT(stat.front, WithinAbs(weight * t.b / L, 1.0f));  // 40% front (Turbo S)
    REQUIRE_THAT(stat.rear, WithinAbs(weight * t.a / L, 1.0f));   // 60% rear
    REQUIRE_THAT(stat.front / weight, WithinAbs(0.40f, 0.005f));

    // The invariant the locked-coupling ceiling depends on: F_zf + F_zr = m*g
    // for ANY a_x (until an axle unloads completely).
    for (float ax : {-10.0f, -5.0f, 0.0f, 5.0f, 10.0f}) {
        const AxleLoad l = axle_loads_long(p, t, ax);
        REQUIRE_THAT(l.front + l.rear, WithinAbs(weight, 1.0f));
    }
    // Direction: power squats the rear, braking dives the front.
    REQUIRE(axle_loads_long(p, t, 10.0f).rear > stat.rear);
    REQUIRE(axle_loads_long(p, t, 10.0f).front < stat.front);
    REQUIRE(axle_loads_long(p, t, -10.0f).front > stat.front);
    // Never negative (an axle cannot pull the road down).
    REQUIRE(axle_loads_long(p, t, 100.0f).front >= 0.0f);
    REQUIRE(axle_loads_long(p, t, -100.0f).rear >= 0.0f);
}

TEST_CASE("Friction circle: lateral demand eats longitudinal capacity (F_x^2 + F_y^2 <= (mu F_z)^2)",
          "[car][longitudinal][tire]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;

    // The primitive itself, against the closed form.
    const float mu = 1.05f, fz = 5000.0f;
    REQUIRE_THAT(friction_circle_fx_max(mu, fz, 0.0f), WithinAbs(mu * fz, 1e-3f));
    REQUIRE_THAT(friction_circle_fx_max(mu, fz, mu * fz), WithinAbs(0.0f, 1e-3f));
    REQUIRE_THAT(friction_circle_fx_max(mu, fz, 0.6f * mu * fz), WithinAbs(0.8f * mu * fz, 1.0f));  // 3-4-5
    REQUIRE(friction_circle_fx_max(mu, fz, 2.0f * mu * fz) == 0.0f);  // saturated, not NaN

    // ...and through the whole stack: a full-throttle launch loses accel
    // monotonically as more of the circle goes to cornering.
    float prev = 1e9f;
    for (float frac : {0.0f, 0.3f, 0.6f, 0.9f}) {
        const AxleLoad    l = axle_loads_long(p, t, 0.0f);
        const AxleLateral lat{frac * p.tireMu * l.front, frac * p.tireMu * l.rear};
        const float a = longitudinal_accel(0.0f, 1.0f, 0.0f, 0, p, t, g, kRearWheelRadiusM, DrivetrainParams{}, lat);
        REQUIRE(a < prev);
        REQUIRE(a > 0.0f);
        prev = a;
    }
    // Zero lateral demand reproduces the straight-line launch EXACTLY (the
    // default argument and an explicit zero must agree — guards a sign slip).
    REQUIRE(longitudinal_accel(0.0f, 1.0f, 0.0f, 0, p, t, g, kRearWheelRadiusM, DrivetrainParams{}, AxleLateral{}) ==
            longitudinal_accel(0.0f, 1.0f, 0.0f, 0, p, t, g, kRearWheelRadiusM));
}

TEST_CASE("Acceleration falls with speed (power-limited)", "[car][longitudinal]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    // Each speed uses the gear WOT full-throttle would actually be in there
    // (gear_for_speed — the idealized, hysteresis-free sequential pick).
    auto accel_at = [&](float v) {
        return longitudinal_accel(v, 1.0f, 0.0f, gear_for_speed(v, g, kRearWheelRadiusM), p, t, g, kRearWheelRadiusM);
    };
    float a0  = accel_at(0.0f);
    float a40 = accel_at(40.0f);
    float a85 = accel_at(85.0f);
    REQUIRE(a0 > a40);
    REQUIRE(a40 > a85);
    REQUIRE(a85 > 0.0f);  // still accelerating below top speed
}

TEST_CASE(
    "Top speed emerges from the force balance (~205 mph, Turbo S) and is DRAG-limited, "
    "not rev-limited",
    "[car][longitudinal]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    float         vTop = steady_state_top_speed(p, t, g, kRearWheelRadiusM);
    // 205 mph = 91.7 m/s: drag (0.5*1.225*0.379*2.14*91.7^2 ≈ 4.18 kN) +
    // rolling (≈213 N) balances the engine force available in top gear there.
    REQUIRE_THAT(vTop, WithinAbs(91.7f, 2.0f));
    REQUIRE_THAT(vTop * 2.23694f, WithinAbs(205.0f, 4.0f));  // stated in the spec's own units
    // Emergent: accelerating just below vTop, decelerating just above.
    REQUIRE(longitudinal_accel(vTop - 5.0f, 1.0f, 0.0f, 6, p, t, g, kRearWheelRadiusM) > 0.0f);
    REQUIRE(longitudinal_accel(vTop + 5.0f, 1.0f, 0.0f, 6, p, t, g, kRearWheelRadiusM) < 0.0f);
    // DRAG-limited, not REV-limited: top gear's rpm at Vmax must sit below the
    // 7200 rpm redline with real headroom, and land essentially ON the 6750 rpm
    // power peak (which is what "the gearing is right" means — see the
    // GearboxParams comment).
    const float rpmAtTop = engine_rpm(vTop, /*gear=*/6, g, kRearWheelRadiusM);
    REQUIRE(rpmAtTop < 7200.0f);
    REQUIRE(rpmAtTop > 6400.0f);  // genuinely near the power peak, not a token margin
    // The LAST gear really is the Vmax gear (steady_state_top_speed scans all
    // of them, so this is a fact about the ratio set, not an assumption).
    for (int gear = 0; gear < 6; ++gear)
        REQUIRE(top_speed_in_gear(gear, p, t, g, kRearWheelRadiusM) < vTop);
    // And the shorter gears are rev-limited, which is what makes them shorter:
    // each one runs into the redline rather than into drag.
    REQUIRE(engine_rpm(top_speed_in_gear(3, p, t, g, kRearWheelRadiusM), 3, g, kRearWheelRadiusM) > 7000.0f);
}

TEST_CASE("0-60 mph lands on the Turbo S's 2.8 s, EMERGENTLY", "[car][longitudinal][gearbox][awd]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    AccelRun      run = simulate_full_throttle(p, t, g, kRearWheelRadiusM, kSixtyMph);

    // MEASURED: 2.758 s (dt = 1/240). The reference figure is 2.8 s (the
    // independent-test consensus; Porsche's own launch-control claim is 2.6 s).
    // Nothing in the model targets this number — there is no launch-control
    // rpm hold, no PSM/PTM layer and no 0-60 fudge factor anywhere. It falls
    // out of three things that were each set for their own reasons:
    //   1) the AWD four-wheel friction ceiling, mu*m*g = 17.2 kN (1.05 g),
    //      which binds from a standstill to 23.4 m/s;
    //   2) the turbo torque curve, which is what stops the launch being an
    //      instant 1.05 g (446 N.m at the 1412 rpm launch point in 1st) and
    //      what ends the traction-limited phase when 2nd falls under the
    //      ceiling at 23.4 m/s;
    //   3) the PDK ratio set, which puts 60 mph in 3rd.
    // Being 1.5% quicker than the reference is honest slack: this model has no
    // torque interruption across shifts (a real PDK loses ~50 ms per shift,
    // and there are two shifts inside this sprint).
    REQUIRE(run.t > 2.6f);
    REQUIRE(run.t < 2.95f);

    // The sprint is TIRE-limited, not gearing-limited, for most of its length:
    // the first two gears both hold the four-wheel ceiling.
    const float ceiling = p.tireMu * p.mass * p.gravity;
    for (float v : {0.0f, 5.0f, 10.0f, 14.0f, 18.0f, 22.0f}) {
        const int gear = (v < 14.66f) ? 0 : 1;
        REQUIRE(gear_drive_force(v, gear, g, p, kRearWheelRadiusM) > ceiling);
    }
    // Two upshifts inside 0-60, ending in 3rd.
    REQUIRE(run.gear.gear == 2);

    // And it is MUCH quicker than the outgoing RWD-limited model would be with
    // the same engine — i.e. the AWD traction model is what bought the time,
    // not a power/gearing bump. A rear-only ceiling of 7.66 m/s^2 cannot reach
    // 60 mph inside 3.4 s no matter how the gearing is arranged.
    REQUIRE(run.t < kSixtyMph / (rear_traction_cap_force(p, t) / p.mass));
}

TEST_CASE("Gear never skips down while accelerating (monotonic under WOT)", "[car][gearbox]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    // Run all the way up near top speed — every upshift threshold gets exercised.
    const float vTop = steady_state_top_speed(p, t, g, kRearWheelRadiusM);
    AccelRun    run  = simulate_full_throttle(p, t, g, kRearWheelRadiusM, vTop - 1.0f, 1.0f / 240.0f, 40.0f);
    REQUIRE(run.gearHistory.size() > 100);  // sanity: the run actually happened
    for (size_t i = 1; i < run.gearHistory.size(); ++i)
        REQUIRE(run.gearHistory[i] >= run.gearHistory[i - 1]);
    REQUIRE(run.gear.gear == 6);  // reaches top gear before running out of speed
}

TEST_CASE("Braking loses stopping power while cornering (four-wheel friction circle)", "[car][longitudinal][tire]") {
    CarParams  p;
    TireParams t;
    // Straight-line: exactly mu*m*g, because the axle-load SUM is invariant
    // under load transfer — unchanged in kind from the pre-AWD brake model.
    REQUIRE_THAT(brake_cap_force(p, t), WithinAbs(p.tireMu * p.mass * p.gravity, 1.0f));
    // Cornering hard: part of every tire's circle is spent laterally, so the
    // brakes cannot deliver mu*m*g any more.
    const AxleLoad    l = axle_loads_long(p, t, 0.0f);
    const AxleLateral lat{0.7f * p.tireMu * l.front, 0.7f * p.tireMu * l.rear};
    REQUIRE(brake_cap_force(p, t, lat) < brake_cap_force(p, t));
    REQUIRE(brake_cap_force(p, t, lat) > 0.0f);

    // The closed form sqrt(1 - 0.7^2) = 0.714 of straight-line capacity only
    // holds if the axle loads DON'T move while braking — which they do, so
    // isolate the circle geometry with a hypothetical zero-height CG. (With
    // the real 0.45 m CG the answer is ~0.66, lower, because load piles onto
    // the front axle whose lateral demand was set from its lighter static
    // load: the same absolute F_y then eats a bigger share of the rear's
    // now-smaller circle than it frees on the front. That coupling is the
    // point of solving per-axle rather than per-car.)
    TireParams flat = t;
    flat.hCg        = 0.0f;
    const AxleLateral flatLat{0.7f * p.tireMu * axle_loads_long(p, flat, 0.0f).front,
                              0.7f * p.tireMu * axle_loads_long(p, flat, 0.0f).rear};
    REQUIRE_THAT(brake_cap_force(p, flat, flatLat),
                 WithinAbs(0.714f * brake_cap_force(p, flat), 0.005f * brake_cap_force(p, flat)));
    // And with the real CG height the load-transfer coupling makes it lower
    // still — a stricter, not looser, limit while cornering under brakes.
    REQUIRE(brake_cap_force(p, t, lat) < brake_cap_force(p, flat, flatLat));
}

TEST_CASE("Auto-shift does not hunt at a steady speed (hysteresis + dwell, lessons-06 §3)", "[car][gearbox]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    // The 1st->2nd upshift boundary speed (rpm(v, 1st) == upshiftRpm).
    const float ratio1  = g.ratios[0] * g.finalDrive;
    const float vShift1 = g.upshiftRpm * 2.0f * kEnginePi * kRearWheelRadiusM / (60.0f * ratio1);

    for (float vHold : {vShift1 - 0.2f, vShift1 - 0.02f, vShift1, vShift1 + 0.02f, vShift1 + 0.2f}) {
        // Ramp up via the REAL state machine so the starting gear is whatever
        // a normal launch would actually be in at vHold (not a hand-picked gear).
        AccelRun run    = simulate_full_throttle(p, t, g, kRearWheelRadiusM, vHold);
        int      shifts = count_shifts_holding_speed(run.gear, vHold, g, kRearWheelRadiusM);
        // At most the ONE shift that crossing the boundary legitimately
        // triggers — never more (which would be hunting back and forth).
        REQUIRE(shifts <= 1);
    }
}

TEST_CASE("Braking is tire-limited on all four wheels (peak decel ~ mu*g, <= 1.3 g), unaffected by AWD",
          "[car][longitudinal]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    // Brakes are NOT drive-limited (all four corners brake regardless of which
    // axle is driven), so this is unchanged by the RWD → AWD retarget and by
    // the gearbox (throttle=0 ⇒ gear is irrelevant). At low speed (negligible
    // aero drag) braking decel ≈ mu*g.
    float aBrake = longitudinal_accel(5.0f, /*throttle=*/0.0f, /*brake=*/1.0f, /*gear=*/0, p, t, g, kRearWheelRadiusM);
    REQUIRE(aBrake < 0.0f);  // decelerating
    float g_ = p.gravity;
    REQUIRE(std::fabs(aBrake) <= 1.3f * g_);  // not physically impossible
    REQUIRE(std::fabs(aBrake) >= 0.9f * g_);  // strong performance brakes
}

TEST_CASE("Coasting decelerates (drag + rolling resistance)", "[car][longitudinal]") {
    CarParams     p;
    TireParams    t;
    GearboxParams g;
    REQUIRE(longitudinal_accel(30.0f, 0.0f, 0.0f, /*gear=*/0, p, t, g, kRearWheelRadiusM) < 0.0f);
}

// ── Lateral / tire model (P0 #4) ──────────────────────────────────────

// Integrate the dynamic bicycle to steady state for a fixed (vx, delta).
// Mirrors CarEntity::update's integration: dynamic bicycle + the friction-circle
// yaw cap + the sideslip bound that the game actually applies each step.
static void integrate_lateral(float vx, float delta, const CarParams& p, const TireParams& t, float& vlOut, float& rOut,
                              int steps = 4000, float dt = 1.0f / 240.0f) {
    float vl = 0.0f, r = 0.0f;
    for (int i = 0; i < steps; ++i) {
        BicycleDeriv d = dynamic_bicycle_deriv(vx, vl, r, delta, p, t);
        vl += d.vlDot * dt;
        r += d.rDot * dt;
        const float rMax = max_yaw_rate(vx, p);
        r                = std::clamp(r, -rMax, rMax);
        vl               = std::clamp(vl, -std::fabs(vx), std::fabs(vx));
    }
    vlOut = vl;
    rOut  = r;
}

TEST_CASE("Dynamic yaw matches kinematic sign and ~magnitude in the linear regime", "[car][tire]") {
    CarParams   p;
    TireParams  t;
    const float vx = 8.0f, delta = 0.03f;  // gentle steer, moderate speed
    float       vl, r;
    integrate_lateral(vx, delta, p, t, vl, r);
    const float L     = t.a + t.b;
    const float r_kin = (vx / L) * std::tan(delta);  // right-positive
    REQUIRE(r > 0.0f);                               // right steer → right yaw (same sign as kinematic)
    REQUIRE(r < r_kin * 1.05f);                      // understeer: at/below kinematic
    REQUIRE(r > r_kin * 0.55f);                      // but same order of magnitude
}

TEST_CASE("Lateral acceleration saturates near mu*g (grip limit)", "[car][tire]") {
    CarParams  p;
    TireParams t;
    // Aggressive steer at speed: kinematic would demand far more than the tires give.
    float vl, r;
    integrate_lateral(30.0f, 0.4f, p, t, vl, r);
    float ay = 30.0f * r;                                    // lateral acceleration (m/s^2)
    REQUIRE(std::fabs(ay) <= 1.15f * p.tireMu * p.gravity);  // capped at ~mu*g
    REQUIRE(std::fabs(ay) > 0.5f * p.tireMu * p.gravity);    // but genuinely cornering hard
}

TEST_CASE("Dynamic bicycle stays numerically stable at high speed", "[car][tire]") {
    CarParams   p;
    TireParams  t;
    float       vl = 0.0f, r = 0.0f;
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 3000; ++i) {  // 25 s of a 0.1 rad step input at 40 m/s
        BicycleDeriv d = dynamic_bicycle_deriv(40.0f, vl, r, 0.1f, p, t);
        vl += d.vlDot * dt;
        r += d.rDot * dt;
        const float rMax = max_yaw_rate(40.0f, p);
        r                = std::clamp(r, -rMax, rMax);
        vl               = std::clamp(vl, -40.0f, 40.0f);
    }
    REQUIRE(std::isfinite(vl));
    REQUIRE(std::isfinite(r));
    REQUIRE(std::fabs(r) < 2.0f);  // bounded, no divergence
    REQUIRE(std::fabs(vl) <= 40.0f);
}

// ── Road-wheel kinematics (wheel articulation, Phase A) ───────────────

TEST_CASE("Wheel spin follows rolling without slip (omega = v/r)", "[wheel][kinematic]") {
    // 10 m/s on the measured 0.345 m front radius for one 60 Hz frame.
    const float dt    = 1.0f / 60.0f;
    const float theta = wheel_spin_step(0.0f, 10.0f, 0.345f, dt);
    REQUIRE_THAT(theta, WithinAbs(10.0f * dt / 0.345f, 1e-5f));
    // The larger rear radius (0.362 m) spins slower at the same speed.
    REQUIRE(wheel_spin_step(0.0f, 10.0f, 0.362f, dt) < theta);
}

TEST_CASE("Reverse runs the wheel backward and wraps into range", "[wheel][kinematic]") {
    const float dt    = 1.0f / 60.0f;
    const float theta = wheel_spin_step(0.1f, -5.0f, 0.345f, dt);
    // 0.1 - 5*dt/0.345 < 0, so the step must wrap back up into [0, 2π).
    REQUIRE(theta >= 0.0f);
    REQUIRE(theta < kTwoPi);
    REQUIRE_THAT(theta, WithinAbs(0.1f - 5.0f * dt / 0.345f + kTwoPi, 1e-4f));
}

TEST_CASE("Zero speed leaves the wheel angle unchanged", "[wheel][kinematic]") {
    REQUIRE_THAT(wheel_spin_step(1.2f, 0.0f, 0.345f, 1.0f / 60.0f), WithinAbs(1.2f, 1e-6f));
}

TEST_CASE("Wrapped theta stays bounded and accurate over a km-scale drive", "[wheel][kinematic]") {
    // 4.2 km at 30 m/s in 60 Hz steps: ~8,400 steps / ~12,170 rad unwrapped —
    // far past where an unwrapped float32 theta quantizes visibly (ULP ≈ 1e-3
    // rad up there). The wrapped float must stay inside [0, 2π) and keep
    // tracking a double-precision reference.
    const float  r = 0.345f, v = 30.0f, dt = 1.0f / 60.0f;
    const int    steps = static_cast<int>(4200.0f / (v * dt));
    const double step  = static_cast<double>(v) * dt / r;
    float        theta = 0.0f, thetaMin = 0.0f, thetaMax = 0.0f;
    double       ref = 0.0;
    for (int i = 0; i < steps; ++i) {
        theta    = wheel_spin_step(theta, v, r, dt);
        ref      = std::fmod(ref + step, 6.283185307179586);
        thetaMin = std::min(thetaMin, theta);
        thetaMax = std::max(thetaMax, theta);
    }
    REQUIRE(thetaMin >= 0.0f);
    REQUIRE(thetaMax < kTwoPi);
    REQUIRE_THAT(theta, WithinAbs(static_cast<float>(ref), 5e-3f));
}

// ── Quarter-car suspension (Phase B) ──────────────────────────────────
// archive/docs/wheels/phase-b-spec.md §10. k/c/mass are derived exactly the way
// CarEntity::update_wheel_visuals() derives them at runtime — from
// CarParams::mass and the TireParams a/b axle-load levers — never
// hardcoded, so these tests tracked the GT3 ride-rate spec and now track the
// Phase-1 Turbo S one (WheelKinematics.h::SuspensionParams) without editing,
// which is the whole point of specifying fn/zeta rather than k/c.
namespace {
struct CornerRates {
    float massFront, massRear;
    float kFront, cFront;
    float kRear, cRear;
    float travelMax;
};

CornerRates corner_rates() {
    const CarParams        p;
    const TireParams       t;
    const SuspensionParams susp;
    const float            L           = t.a + t.b;
    const float            sprungTotal = p.mass - 4.f * susp.unsprungMassKg;

    CornerRates r{};
    r.massFront = sprungTotal * (t.b / L) * 0.5f;
    r.massRear  = sprungTotal * (t.a / L) * 0.5f;
    r.travelMax = susp.travelMaxM;
    spring_from_ride_freq(r.massFront, susp.fnFrontHz, susp.zetaFront, r.kFront, r.cFront);
    spring_from_ride_freq(r.massRear, susp.fnRearHz, susp.zetaRear, r.kRear, r.cRear);
    return r;
}
}  // namespace

TEST_CASE("Turbo S ride rates satisfy the semi-implicit stability bound at 1/240 s", "[wheel][suspension]") {
    // phase-b-spec.md §4: h < (2/omega_n)*(sqrt(1+zeta^2) - zeta). The bound
    // depends only on (fn, zeta) — NOT on mass — which is exactly why the
    // Phase-1 mass retarget could not destabilise it. This test is the guard
    // for any future ride-rate edit: change fn/zeta and it tells you whether
    // the 1/240 s substep is still legal.
    const SuspensionParams s;
    auto                   hMax = [](float fn, float zeta) {
        const float w = kTwoPi * fn;
        return (2.f / w) * (std::sqrt(1.f + zeta * zeta) - zeta);
    };
    constexpr float h = 1.f / 240.f;
    REQUIRE(hMax(s.fnFrontHz, s.zetaFront) > 10.f * h);  // measured margin 27x
    REQUIRE(hMax(s.fnRearHz, s.zetaRear) > 10.f * h);    // measured margin 24x
    REQUIRE(hMax(6.f, 1.5f) > 3.f * h);                  // debug-slider ceiling (spec §11)
    // Anti-roll bar coupling stiffens the ROLL mode by (1 + 2*ratio), i.e.
    // omega_n by sqrt(1 + 2*ratio), so the bound must still hold there.
    const AntiRollParams arb;
    const float          fnRollFront = s.fnFrontHz * std::sqrt(1.f + 2.f * arb.frontRatio);
    REQUIRE(hMax(fnRollFront, s.zetaFront / std::sqrt(1.f + 2.f * arb.frontRatio)) > 10.f * h);

    // Sanity on the derived rates: a heavy rear-engine sports car, softer than
    // the GT3 numbers this replaced (which were 51 / 95 N/mm at GT3 masses).
    const CornerRates r = corner_rates();
    REQUIRE(r.kFront / 1000.f > 30.f);  // N/mm
    REQUIRE(r.kFront / 1000.f < 46.f);
    REQUIRE(r.kRear / 1000.f > 60.f);
    REQUIRE(r.kRear / 1000.f < 82.f);
    REQUIRE(r.kRear > r.kFront);  // flat-ride convention, rear-engine car
}

TEST_CASE("Suspension settles to equilibrium from a displaced start", "[wheel][suspension]") {
    const CornerRates r = corner_rates();
    CornerSpring      s{0.03f, 0.f};
    for (int i = 0; i < 2400; ++i)
        s = suspension_step(s, 0.f, r.kFront, r.cFront, r.massFront, 1.f / 240.f, r.travelMax);
    REQUIRE(std::abs(s.x_m) < 1e-3f);
    REQUIRE(std::abs(s.v_mps) < 1e-3f);
}

TEST_CASE("No amplitude growth — the property forward Euler fails", "[wheel][suspension]") {
    const CornerRates r = corner_rates();
    CornerSpring      s{0.03f, 0.f};
    float             late = 0.f;
    for (int i = 0; i < 240; ++i)
        s = suspension_step(s, 0.f, r.kRear, r.cRear, r.massRear, 1.f / 240.f, r.travelMax);
    for (int i = 0; i < 240; ++i) {
        s    = suspension_step(s, 0.f, r.kRear, r.cRear, r.massRear, 1.f / 240.f, r.travelMax);
        late = std::max(late, std::abs(s.x_m));
    }
    REQUIRE(late < 0.03f);
}

TEST_CASE("Travel is clamped under extreme input", "[wheel][suspension]") {
    const CornerRates r = corner_rates();
    CornerSpring      s{};
    for (int i = 0; i < 600; ++i)
        s = suspension_step(s, 10.f, r.kFront, r.cFront, r.massFront, 1.f / 240.f, r.travelMax);
    REQUIRE(s.x_m <= r.travelMax + 1e-6f);
    REQUIRE(s.x_m >= -r.travelMax - 1e-6f);
}

TEST_CASE("Symmetry — equal input gives equal corners", "[wheel][suspension]") {
    // The two front corners share identical mass/k/c (spec §2); driven with
    // the same target from the same initial state they must respond
    // identically — guards against any accidental per-corner asymmetry.
    const CornerRates r = corner_rates();
    CornerSpring      left{}, right{};
    for (int i = 0; i < 500; ++i) {
        left  = suspension_step(left, 0.02f, r.kFront, r.cFront, r.massFront, 1.f / 240.f, r.travelMax);
        right = suspension_step(right, 0.02f, r.kFront, r.cFront, r.massFront, 1.f / 240.f, r.travelMax);
    }
    REQUIRE_THAT(left.x_m, WithinAbs(right.x_m, 1e-9f));
}

TEST_CASE("Stability at the debug-slider ceiling (fn=6Hz, zeta=1.5) stays finite and bounded", "[wheel][suspension]") {
    const CornerRates r = corner_rates();
    float             kCeil, cCeil;
    spring_from_ride_freq(r.massFront, 6.f, 1.5f, kCeil, cCeil);
    CornerSpring s{0.03f, 0.f};
    for (int i = 0; i < 2400; ++i)
        s = suspension_step(s, 0.f, kCeil, cCeil, r.massFront, 1.f / 240.f, r.travelMax);
    REQUIRE(std::isfinite(s.x_m));
    REQUIRE(std::abs(s.x_m) <= r.travelMax + 1e-6f);
}

TEST_CASE("Rest is exactly rest — guards the unchanged-at-rest property of §6/§7", "[wheel][suspension]") {
    const CornerRates r = corner_rates();
    CornerSpring      s{};
    s = suspension_step(s, 0.f, r.kFront, r.cFront, r.massFront, 1.f / 240.f, r.travelMax);
    REQUIRE(s.x_m == 0.f);
    REQUIRE(s.v_mps == 0.f);
}

TEST_CASE("Adaptive substepping never exceeds the h_target stability bound", "[wheel][suspension]") {
    constexpr float hTarget     = 1.f / 240.f;
    constexpr int   maxSubsteps = 32;

    // Sweep representative frame times: a normal frame, the App's global
    // 1/15s cap (App.cpp:292 — N=16 per spec §5), and a hitch well beyond
    // maxSubsteps*hTarget that must be clamped by the local guard (§5).
    for (float dtRaw : {1.f / 240.f, 1.f / 60.f, 1.f / 15.f, 5.f}) {
        const float dt_used = std::min(dtRaw, maxSubsteps * hTarget);
        const int   N       = std::max(1, static_cast<int>(std::ceil(dt_used / hTarget)));
        REQUIRE(dt_used / N <= hTarget + 1e-9f);
        REQUIRE(N <= maxSubsteps);
    }

    // And the real function stays finite/bounded through an extreme hitch.
    const CornerRates r = corner_rates();
    CornerSpring      s{};
    s = suspension_advance(s, 0.02f, r.kFront, r.cFront, r.massFront, 5.f, hTarget, r.travelMax);
    REQUIRE(std::isfinite(s.x_m));
    REQUIRE(std::abs(s.x_m) <= r.travelMax + 1e-6f);
}

// Regression guard for the Phase-B unit bug (archive/docs/wheels/phase-b-spec.md §7).
// The draw-matrix translation is composed AFTER car_model, which already carries
// the entity scale (1000). Converting x_m to world units there scaled by 1000
// TWICE, lifting each wheel ~14 m off the car on ordinary road crown — the
// symptom was empty wheel arches and wheels detached from the car while driving.
TEST_CASE("Suspension draw offset stays in meters (no double unit conversion)", "[wheels][suspension]") {
    constexpr float kWorldScale = swish::util::units::kWuPerMetre;  // 1 m = 1000 WU (Units.h)
    const float     x_m         = 0.0145f;  // measured crown residual across the track

    // The helper must be the identity in meters — NOT a world-unit conversion.
    REQUIRE_THAT(suspension_draw_offset_m(x_m), WithinAbs(x_m, 1e-9f));

    // After car_model's scale is applied, the world displacement must be
    // centimeters. The bug produced 14 500 WU here instead of 14.5.
    const float worldDisplacementWU = suspension_draw_offset_m(x_m) * kWorldScale;
    REQUIRE(worldDisplacementWU < 100.0f);

    // Same bound at the travel clamp: 45 mm of travel is 45 WU, never 45 000.
    SuspensionParams susp;
    REQUIRE(suspension_draw_offset_m(susp.travelMaxM) * kWorldScale < 100.0f);
}

// ── Anti-roll bar (Phase 1 §4.3) ──────────────────────────────────────

TEST_CASE("Anti-roll bar resists roll only — nothing at all in pure heave", "[wheel][suspension][arb]") {
    const AntiRollParams arb;
    // Both wheels up together (heave / a crest): the bar is untwisted, so it
    // must contribute EXACTLY zero. This is the defining property; a bar that
    // pushed back in heave would just be a stiffer spring.
    for (float x : {-0.04f, -0.01f, 0.f, 0.01f, 0.04f})
        REQUIRE(arb_target_shift_m(x, x, arb.frontRatio) == 0.f);

    // One wheel up, the other down (roll): equal and opposite corrections, and
    // each OPPOSES its own corner's displacement (that is the "anti" part).
    const float left = 0.02f, right = -0.02f;
    const float sL = arb_target_shift_m(left, right, arb.frontRatio);
    const float sR = arb_target_shift_m(right, left, arb.frontRatio);
    REQUIRE_THAT(sL, WithinAbs(-sR, 1e-9f));
    REQUIRE(sL < 0.f);  // left corner is high -> bar pulls its target down
    REQUIRE(sR > 0.f);
    // Magnitude is the ratio times the roll travel difference.
    REQUIRE_THAT(sL, WithinAbs(-arb.frontRatio * (left - right), 1e-9f));

    // Front bar stiffer than rear (balance on a rear-engine car), and both are
    // corrections, not overrides — well under 1.0.
    REQUIRE(arb.frontRatio > arb.rearRatio);
    REQUIRE(arb.frontRatio < 1.0f);
    REQUIRE(arb.rearRatio > 0.0f);
}

TEST_CASE("Anti-roll bar reduces the steady-state roll angle it is given", "[wheel][suspension][arb]") {
    // Integrate a rolled pair (one corner asked up, the other down) with and
    // without the bar, exactly the way CarEntity::update_wheel_visuals() does:
    // the partner's PREVIOUS travel feeds this step's target.
    const CornerRates    r = corner_rates();
    const AntiRollParams arb;
    auto                 settle = [&](float ratio) {
        CornerSpring a{}, b{};
        for (int i = 0; i < 4000; ++i) {
            const CornerSpring pa = a, pb = b;
            a = suspension_step(a, 0.02f + arb_target_shift_m(pa.x_m, pb.x_m, ratio), r.kFront, r.cFront, r.massFront,
                                1.f / 240.f, r.travelMax);
            b = suspension_step(b, -0.02f + arb_target_shift_m(pb.x_m, pa.x_m, ratio), r.kFront, r.cFront, r.massFront,
                                1.f / 240.f, r.travelMax);
        }
        return a.x_m - b.x_m;  // roll travel difference
    };
    const float rollNoBar   = settle(0.f);
    const float rollWithBar = settle(arb.frontRatio);
    REQUIRE(rollNoBar > 0.f);
    REQUIRE(rollWithBar < rollNoBar);  // the bar actually resists roll
    REQUIRE(rollWithBar > 0.f);        // ...without inverting it
    // Closed form: the roll mode sees k*(1 + 2*ratio), so the settled roll
    // travel scales by 1/(1 + 2*ratio) = 0.588 at ratio 0.35.
    REQUIRE_THAT(rollWithBar, WithinAbs(rollNoBar / (1.f + 2.f * arb.frontRatio), 1e-3f));
    // And it stays STABLE (bounded, finite) — the coupling must not make the
    // pair diverge, which is the risk with any cross-corner feedback term.
    REQUIRE(std::isfinite(rollWithBar));
    REQUIRE(std::abs(rollWithBar) <= r.travelMax + 1e-6f);
}

// ── Camber / toe gain curves (Phase 1 §4.3) ───────────────────────────

TEST_CASE("Camber/toe at rest is EXACTLY the static alignment, and the delta is exactly zero",
          "[wheel][suspension][camber]") {
    const SuspensionGeometry g;
    // Absolute form reproduces the static alignment at zero travel.
    REQUIRE(camber_toe_from_travel(0.f, /*front=*/true, g).camberDeg == g.camberFrontDeg);
    REQUIRE(camber_toe_from_travel(0.f, /*front=*/true, g).toeDeg == g.toeFrontDeg);
    REQUIRE(camber_toe_from_travel(0.f, /*front=*/false, g).camberDeg == g.camberRearDeg);
    REQUIRE(camber_toe_from_travel(0.f, /*front=*/false, g).toeDeg == g.toeRearDeg);

    // HARD INVARIANT for the draw path: at rest the DELTA is exactly 0, so the
    // added draw-matrix rotation is the identity and the rest pose is
    // bit-identical to pre-Phase-1 (the asset already bakes in static camber,
    // so the delta — not the absolute — is what may be applied there).
    for (bool front : {true, false}) {
        REQUIRE(camber_toe_delta_from_travel(0.f, front, g).camberDeg == 0.f);
        REQUIRE(camber_toe_delta_from_travel(0.f, front, g).toeDeg == 0.f);
    }

    // Static alignment matches the published 992 Turbo S direction and rough
    // magnitude: negative camber, more at the rear; a touch of toe-IN, more at
    // the rear.
    REQUIRE(g.camberFrontDeg < 0.f);
    REQUIRE(g.camberRearDeg < g.camberFrontDeg);
    REQUIRE(g.toeFrontDeg > 0.f);
    REQUIRE(g.toeRearDeg > g.toeFrontDeg);
}

TEST_CASE("Camber gains negatively in bump, positively in droop, and is progressive", "[wheel][suspension][camber]") {
    const SuspensionGeometry g;
    const SuspensionParams   s;

    for (bool front : {true, false}) {
        const float bump  = camber_toe_delta_from_travel(s.travelMaxM, front, g).camberDeg;
        const float droop = camber_toe_delta_from_travel(-s.travelMaxM, front, g).camberDeg;
        REQUIRE(bump < 0.f);   // compression leans the wheel top IN
        REQUIRE(droop > 0.f);  // extension leans it back OUT
        // Monotone across the whole travel range (no fold-back inside it).
        float prev = 1e9f;
        for (int i = -10; i <= 10; ++i) {
            const float x  = s.travelMaxM * static_cast<float>(i) / 10.f;
            const float cd = camber_toe_delta_from_travel(x, front, g).camberDeg;
            REQUIRE(cd < prev);
            prev = cd;
        }
        // PROGRESSIVE: the delta-squared term makes bump gain MORE camber than
        // droop loses (|bump| > |droop|), which is the shape every real
        // camber curve has. A purely linear curve would make these equal.
        REQUIRE(std::abs(bump) > std::abs(droop) * 1.05f);
        // Magnitude is visible but not cartoonish at full travel: 0.5-2 deg.
        REQUIRE(std::abs(bump) > 0.5f);
        REQUIRE(std::abs(bump) < 2.0f);
    }
    // The rear multi-link gains more camber than the double-wishbone front.
    REQUIRE(std::abs(camber_toe_delta_from_travel(s.travelMaxM, false, g).camberDeg) >
            std::abs(camber_toe_delta_from_travel(s.travelMaxM, true, g).camberDeg));
}

TEST_CASE("Toe gain (bump steer) is small, linear, and toe-IN in bump", "[wheel][suspension][camber]") {
    const SuspensionGeometry g;
    const SuspensionParams   s;

    for (bool front : {true, false}) {
        const float bump  = camber_toe_delta_from_travel(s.travelMaxM, front, g).toeDeg;
        const float droop = camber_toe_delta_from_travel(-s.travelMaxM, front, g).toeDeg;
        REQUIRE(bump > 0.f);  // toe-IN in bump (the stabilising direction)
        REQUIRE(droop < 0.f);
        // LINEAR (spec §4.3 writes toe with no second-order term), so bump and
        // droop are exact mirrors.
        REQUIRE_THAT(bump, WithinAbs(-droop, 1e-6f));
        // Bump steer must stay SMALL — it is a stability nuisance in real cars
        // and a visible wheel-waggle artifact here if it gets large.
        REQUIRE(std::abs(bump) < 0.4f);
    }
    // Multi-link rear is designed with more bump-steer toe-in than the front.
    REQUIRE(camber_toe_delta_from_travel(s.travelMaxM, false, g).toeDeg >
            camber_toe_delta_from_travel(s.travelMaxM, true, g).toeDeg);
}

// ── Wet-road grip + hydroplaning (Wave 2 step 4) ──────────────────────
// docs/lessons-from-chrono/07-wet-roads-and-rain.md §4. All pure math on
// CarPhysics.h's effective_mu()/puddle_amount()/film_over_tread() — no GPU,
// no CarEntity (which needs a GLFWwindow + loaded model).

TEST_CASE("effective_mu at wetness<=0 returns muDry EXACTLY — dry handling is unchanged", "[car][wet]") {
    WetGripParams g;
    for (float v : {0.0f, 1.0f, -5.0f, 30.0f, 100.0f}) {
        for (float film : {0.0f, 0.5f, 1.0f, 2.0f}) {
            REQUIRE(effective_mu(0.0f, v, film, g) == g.muDry);
            REQUIRE(effective_mu(-1.0f, v, film, g) == g.muDry);  // guards a sign flip too
        }
    }
    // muDry matches CarParams' own dry default EXACTLY (same literal, same
    // compiler) — so `params.tireMu = effective_mu(m_wetness, ...)` in
    // CarEntity::update() writes back the identical bit pattern a
    // default-constructed CarParams already has at wetness 0. Every OTHER
    // test in this file exercises longitudinal_accel/dynamic_bicycle_deriv/
    // max_yaw_rate with a plain default CarParams and never calls
    // effective_mu — that unmodified, still-green suite IS the dry
    // bit-identical regression guard the task asks for.
    CarParams p;
    REQUIRE(g.muDry == p.tireMu);
}

TEST_CASE("Wet grip degrades monotonically with wetness (no standing water)", "[car][wet]") {
    WetGripParams g;
    const float   v    = 20.0f;  // below vPlane, no hydroplaning
    float         prev = effective_mu(0.0f, v, 0.0f, g);
    for (float w : {0.1f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float mu = effective_mu(w, v, 0.0f, g);
        REQUIRE(mu < prev);
        prev = mu;
    }
    REQUIRE(prev < g.muDry);
    REQUIRE(prev > 0.0f);
}

TEST_CASE("Speed fade: at fixed wetness, mu falls with speed up to vPlane, then holds flat", "[car][wet]") {
    WetGripParams g;
    const float   w    = 0.8f;
    float         prev = effective_mu(w, 0.0f, 0.0f, g);
    for (float v : {5.0f, 10.0f, 15.0f, 20.0f, g.vPlane}) {
        const float mu = effective_mu(w, v, 0.0f, g);
        REQUIRE(mu < prev);
        prev = mu;
    }
    // Beyond vPlane with no standing water (filmOverTread<1): the fade term
    // is a function of min(vAbs, vPlane), so it saturates — grip stops
    // falling further with speed alone (the NASA/Horne law only kicks in
    // once there's actually water to plane on; see the hydroplane test).
    const float atPlane = effective_mu(w, g.vPlane, 0.0f, g);
    const float beyond  = effective_mu(w, g.vPlane + 20.0f, 0.0f, g);
    REQUIRE_THAT(beyond, WithinAbs(atPlane, 1e-6f));
}

TEST_CASE("Hydroplane onset is smooth (no step) across [vPlane, vPlane+planeBand]", "[car][wet]") {
    WetGripParams g;
    const float   w = 1.0f, film = 1.5f;  // standing water present (filmOverTread >= 1)

    // Continuous with the non-planing branch AT vPlane — the two branches
    // share the same value at their boundary (t clamps to 0 there).
    const float atPlaneNoFilm = effective_mu(w, g.vPlane, 0.0f, g);
    const float atPlaneFilm   = effective_mu(w, g.vPlane, film, g);
    REQUIRE_THAT(atPlaneFilm, WithinAbs(atPlaneNoFilm, 1e-6f));

    // Fine sweep across and past the band: monotonically non-increasing, and
    // no single sub-step swallows a large fraction of the total drop (i.e.
    // no hidden discontinuity — a real step would show up as one dv-sized
    // interval eating most of the range).
    constexpr int N       = 200;
    const float   v0      = g.vPlane - 1.0f;
    const float   v1      = g.vPlane + g.planeBand + 1.0f;
    const float   dv      = (v1 - v0) / N;
    float         prevMu  = effective_mu(w, v0, film, g);
    float         maxStep = 0.0f;
    for (int i = 1; i <= N; ++i) {
        const float v  = v0 + dv * i;
        const float mu = effective_mu(w, v, film, g);
        REQUIRE(mu <= prevMu + 1e-5f);
        maxStep = std::max(maxStep, prevMu - mu);
        prevMu  = mu;
    }
    const float totalDrop = effective_mu(w, v0, film, g) - effective_mu(w, v1, film, g);
    REQUIRE(maxStep < totalDrop * 0.1f);

    // Ends land exactly where the model promises: fully in the plateau at
    // and past vPlane+planeBand, grip is muPlane.
    REQUIRE_THAT(effective_mu(w, g.vPlane + g.planeBand, film, g), WithinAbs(g.muPlane, 1e-4f));
    REQUIRE_THAT(effective_mu(w, g.vPlane + g.planeBand + 5.0f, film, g), WithinAbs(g.muPlane, 1e-4f));
}

TEST_CASE("Recovery: mu returns as speed drops back below vPlane", "[car][wet]") {
    WetGripParams g;
    const float   w = 1.0f, film = 2.0f;
    const float   highSpeed = effective_mu(w, g.vPlane + 10.0f, film, g);  // deep in the plateau
    const float   lowSpeed  = effective_mu(w, g.vPlane - 5.0f, film, g);   // back below vPlane
    REQUIRE(lowSpeed > highSpeed);
    // Recovers to essentially the SAME "faded" grip a spot with no standing
    // water has at the same (wetness, speed) — no lingering penalty once
    // speed alone leaves the hydroplane gate.
    const float lowSpeedNoFilm = effective_mu(w, g.vPlane - 5.0f, 0.0f, g);
    REQUIRE_THAT(lowSpeed, WithinAbs(lowSpeedNoFilm, 1e-6f));
}

TEST_CASE("puddle_amount is deterministic, in [0,1], and zero at coverage 0", "[car][wet][puddle]") {
    REQUIRE(puddle_amount(12345.0f, -6789.0f, 0.0f) == 0.0f);
    for (float x : {-500000.0f, 0.0f, 12345.6f, 987654.0f}) {
        for (float z : {-999999.0f, 0.0f, 42424.0f}) {
            const float a = puddle_amount(x, z, kPuddleCoveragePhysics);
            const float b = puddle_amount(x, z, kPuddleCoveragePhysics);  // determinism
            REQUIRE(a == b);
            REQUIRE(a >= 0.0f);
            REQUIRE(a <= 1.0f);
        }
    }
}

TEST_CASE(
    "puddle_amount reaches near-full depth somewhere, and film_over_tread crosses the "
    "hydroplane gate there at high wetness but not at low wetness",
    "[car][wet][puddle]") {
    // Coverage ~0.501 is designed so roughly half the road reads as a full
    // puddle (lighting.frag: "deep puddles ... resolve as a coherent puddle
    // mirror") — a 100 m x 100 m sweep at sub-cell resolution (~7.7 m cells)
    // should find one.
    float maxPuddle = 0.0f, bestX = 0.0f, bestZ = 0.0f;
    for (float x = 0.0f; x < 100000.0f; x += 733.0f) {
        for (float z = 0.0f; z < 100000.0f; z += 733.0f) {
            const float a = puddle_amount(x, z, kPuddleCoveragePhysics);
            if (a > maxPuddle) {
                maxPuddle = a;
                bestX     = x;
                bestZ     = z;
            }
        }
    }
    REQUIRE(maxPuddle > 0.9f);

    // At that deepest-found spot: full wetness crosses the hydroplane gate...
    REQUIRE(film_over_tread(bestX, bestZ, 1.0f) >= 1.0f);
    // ...but early/light rain (low wetness) never does, even there.
    REQUIRE(film_over_tread(bestX, bestZ, 0.3f) < 1.0f);
}

TEST_CASE(
    "kFilmDepthScale: damp (non-flooded) patches stay under the hydroplane gate even at "
    "full wetness",
    "[car][wet][puddle]") {
    // Direct arithmetic check on the scale constant's promise (see its
    // comment in CarPhysics.h): a shoreline / partially-formed puddle
    // (puddle_amount up to ~0.5) never reaches filmOverTread >= 1, however
    // wet the road gets — it fades grip via speedFade like any wet tarmac,
    // but never planes. A fully-formed puddle at full wetness DOES cross.
    REQUIRE(kFilmDepthScale * 0.5f * 1.0f < 1.0f);
    REQUIRE(kFilmDepthScale * 1.0f * 1.0f >= 1.0f);
}

TEST_CASE("Steering authority: yaw-rate cap is materially lower when hydroplaning", "[car][wet][steer]") {
    WetGripParams g;
    CarParams     dry;  // tireMu = 1.10 (default, dry)
    CarParams     hydroplaning;
    hydroplaning.tireMu = g.muPlane;  // 0.08 — what effective_mu returns mid-hydroplane

    const float vx        = 25.0f;  // m/s, in the hydroplaning speed range
    const float rMaxDry   = max_yaw_rate(vx, dry);
    const float rMaxPlane = max_yaw_rate(vx, hydroplaning);

    // max_yaw_rate = tireMu*gravity/vx is directly proportional to tireMu —
    // the SAME m_effective_mu that feeds longitudinal_accel also collapses
    // the yaw-rate (steering authority) cap, confirming issue 6's "reduced
    // steering authority" (not just reduced forward traction) falls out of
    // the one-line CarParams::tireMu patch with no separate wiring needed.
    // Concretely at vx=25 m/s: rMaxDry ~= 0.432 rad/s (~24.7 deg/s) vs
    // rMaxPlane ~= 0.031 rad/s (~1.8 deg/s) — ~7.3% of the dry cap.
    REQUIRE(rMaxPlane < rMaxDry * 0.1f);
    REQUIRE_THAT(rMaxDry, WithinAbs(dry.tireMu * dry.gravity / vx, 1e-5f));
    REQUIRE_THAT(rMaxPlane, WithinAbs(hydroplaning.tireMu * hydroplaning.gravity / vx, 1e-5f));
}

// ══════════════════════════════════════════════════════════════════════
// Phase 2 — hydroplane spin-out (archive/docs/phase-2-hydroplane-spinout.md)
//
// Ordered the way the spec asks for it (§5/§6): the negative-v_x validation
// FIRST, because a NaN or a runaway yaw there is indistinguishable from "the
// physics is just violent" once you are looking at pixels.
// ══════════════════════════════════════════════════════════════════════

namespace {

// One step of the FULL vehicle state, mirroring CarEntity::update()'s exact
// call sequence and ordering (same two longitudinal calls, same one-step lag on
// the per-axle longitudinal force, same kinematic blend below 5 m/s, same
// clamps). Kept here rather than in the harness above because Phase 2's
// behaviour is a property of the whole loop, not of any single function.
struct SpinState {
    float            vx = 0.f;   // m/s, body forward
    float            vl = 0.f;   // m/s, body rightward
    float            r  = 0.f;   // rad/s, right-positive
    float            headingDeg = 0.f;
    float            worldX = 0.f, worldZ = 0.f;  // WU
    GearState        gear{};
    AxleLongitudinal lastLong{};
    float            planeFrac = 0.f;
    float            mu        = 0.f;
};

struct SpinInput {
    float throttle = 0.f, brake = 0.f, steerRad = 0.f;
    float wetness = 0.f;
    // When >= 0, overrides the puddle-field query (a "lake" — sustained
    // standing water). Negative means "sample the real puddle field".
    float filmOverride = -1.f;
};

constexpr float kBlendSpeedM  = 5.0f;
constexpr float kWorldPerM    = 1000.0f;
constexpr float kRestLatM     = 0.05f;
constexpr float kRestYawRadS  = 0.02f;

void spin_step(SpinState& s, const SpinInput& in, const CarParams& base, const TireParams& t, const GearboxParams& g,
               const DrivetrainParams& d, const WetGripParams& wg, float dt) {
    const float film =
        (in.filmOverride >= 0.f) ? in.filmOverride : film_over_tread(s.worldX, s.worldZ, in.wetness);
    const float vBody = std::sqrt(s.vx * s.vx + s.vl * s.vl);
    s.mu              = effective_mu(in.wetness, vBody, film, wg);
    s.planeFrac       = hydroplane_fraction(vBody, film, wg);

    CarParams p = base;
    p.tireMu    = s.mu;

    AxleLateral latUse{};
    if (vBody >= kBlendSpeedM)
        latUse = axle_lateral_use(
            axle_lateral_forces(s.vx, s.vl, s.r, in.steerRad, p, t, s.lastLong, s.planeFrac));

    s.gear           = gearbox_shift_step(s.gear, s.vx, g, kRearWheelRadiusM, dt);
    const float prev = s.vx;
    s.vx += longitudinal_accel(s.vx, in.throttle, in.brake, s.gear.gear, p, t, g, kRearWheelRadiusM, d, latUse,
                               &s.lastLong) *
            dt;
    s.vx += body_transport_accel(s.vl, s.r, s.planeFrac) * dt;
    if (in.brake > 0.f && prev > 0.f && s.vx < 0.f && s.planeFrac <= 0.f)
        s.vx = 0.f;

    if (std::fabs(s.vx) < 0.0005f && in.throttle == 0.f && std::fabs(s.vl) < kRestLatM &&
        std::fabs(s.r) < kRestYawRadS) {
        s.vx = 0.f;
        s.vl = 0.f;
        s.r  = 0.f;
    }
    if (s.vx == 0.f && s.vl == 0.f && s.r == 0.f)
        return;

    const float L       = t.a + t.b;
    const float rKin    = (s.vx / L) * std::tan(in.steerRad);
    const float vBodyLat = std::sqrt(s.vx * s.vx + s.vl * s.vl);
    if (vBodyLat < kBlendSpeedM) {
        s.r  = rKin;
        s.vl = 0.f;
    } else {
        const float        vlPrev = std::fabs(s.vl);
        const float        rPrev  = std::fabs(s.r);
        const BicycleDeriv bd =
            dynamic_bicycle_deriv(s.vx, s.vl, s.r, in.steerRad, p, t, s.lastLong, s.planeFrac);
        s.vl += bd.vlDot * dt;
        s.r += bd.rDot * dt;
        const float blend = std::clamp((vBodyLat - kBlendSpeedM) / 3.0f, 0.f, 1.f);
        s.r               = rKin + (s.r - rKin) * blend;
        const float vB    = std::sqrt(s.vx * s.vx + s.vl * s.vl);
        const float rMax  = rate_limited_bound(yaw_rate_limit(s.vx, vB, p, t, s.planeFrac), rPrev,
                                               max_yaw_decel(p, t), dt);
        const float vlMax = rate_limited_bound(sideslip_limit(s.vx, vB, s.planeFrac), vlPrev,
                                               p.tireMu * p.gravity, dt);
        s.r               = std::clamp(s.r, -rMax, rMax);
        s.vl              = std::clamp(s.vl, -vlMax, vlMax);
    }

    s.headingDeg -= s.r * 180.0f / kEnginePi * dt;
    const float yaw = s.headingDeg * kEnginePi / 180.0f;
    const float fx = std::cos(yaw), fz = -std::sin(yaw);
    s.worldX += (fx * s.vx + (-fz) * s.vl) * kWorldPerM * dt;
    s.worldZ += (fz * s.vx + (fx)*s.vl) * kWorldPerM * dt;
}

struct SpinSummary {
    float maxAbsYawRateDeg = 0.f;  // only sampled while genuinely moving
    float cumYawDeg        = 0.f;
    float maxAbsBetaDeg    = 0.f;
    float minVx            = 1e9f;
    float minPlaneMu       = 9.f;
    float maxPlaneFrac     = 0.f;
    bool  finite           = true;
};

SpinSummary spin_run(SpinState& s, SpinInput in, float secs, float dt = 1.0f / 120.0f) {
    const CarParams        base;
    const TireParams       t;
    const GearboxParams    g;
    const DrivetrainParams d;
    const WetGripParams    wg;
    SpinSummary            out;
    const int              steps = static_cast<int>(secs / dt);
    for (int i = 0; i < steps; ++i) {
        spin_step(s, in, base, t, g, d, wg, dt);
        if (!std::isfinite(s.vx) || !std::isfinite(s.vl) || !std::isfinite(s.r) ||
            !std::isfinite(s.headingDeg) || !std::isfinite(s.worldX) || !std::isfinite(s.worldZ)) {
            out.finite = false;
            break;
        }
        const float V = std::sqrt(s.vx * s.vx + s.vl * s.vl);
        if (V > 2.0f) {  // beta/yaw-rate are meaningless at a standstill
            out.maxAbsYawRateDeg = std::max(out.maxAbsYawRateDeg, std::fabs(s.r) * 180.f / kEnginePi);
            out.maxAbsBetaDeg =
                std::max(out.maxAbsBetaDeg, std::fabs(std::atan2(s.vl, s.vx) * 180.f / kEnginePi));
        }
        out.cumYawDeg -= s.r * 180.f / kEnginePi * dt;
        out.minVx        = std::min(out.minVx, s.vx);
        out.minPlaneMu   = std::min(out.minPlaneMu, s.mu);
        out.maxPlaneFrac = std::max(out.maxPlaneFrac, s.planeFrac);
    }
    return out;
}

// Drive on GRIP for `secs` at a held steer angle, so the car enters the water
// already in a steady corner — which is the only scenario in which a hydroplane
// has anything to do. A car hydroplaning in a dead straight line, correctly,
// does nothing at all: there is no yaw rate for the lost grip to stop damping
// and no lateral velocity for it to stop cancelling. Partial throttle holds the
// speed up so the entry speed is the one asked for.
void spin_warmup(SpinState& s, float steerRad, float secs, float dt = 1.0f / 120.0f) {
    SpinInput in;
    in.steerRad = steerRad;
    in.throttle = 0.35f;
    in.wetness  = 0.0f;
    const CarParams        base;
    const TireParams       t;
    const GearboxParams    g;
    const DrivetrainParams d;
    const WetGripParams    wg;
    for (int i = 0; i < static_cast<int>(secs / dt); ++i)
        spin_step(s, in, base, t, g, d, wg, dt);
}

// The deepest puddle cell in a 100 m x 100 m sweep — where a hydroplane can
// actually start (same search the existing puddle test uses).
[[maybe_unused]] void deepest_puddle(float& x, float& z) {
    float best = 0.f;
    x = z = 0.f;
    for (float px = 0.f; px < 100000.f; px += 500.f)
        for (float pz = 0.f; pz < 100000.f; pz += 500.f) {
            const float a = puddle_amount(px, pz, kPuddleCoveragePhysics);
            if (a > best) {
                best = a;
                x    = px;
                z    = pz;
            }
        }
}

}  // namespace

// ── §5.1 negative v_x: max_yaw_rate ───────────────────────────────────

TEST_CASE("max_yaw_rate is sign-symmetric in vx (the spec's max(vx,1) floor bug is NOT present)",
          "[car][tire][negative-vx]") {
    CarParams p;
    // The spec (archive/docs/phase-2-hydroplane-spinout.md §5.1) predicted the floor was
    // std::max(vx, 1.0f), which at vx = -25 would silently return the
    // FORWARD-SPEED-1 cap (mu*g/1 = 10.3 rad/s) instead of mu*g/25. Pin both the
    // symmetry and the actual value so a future edit can't reintroduce it.
    for (float v : {1.5f, 5.0f, 12.0f, 25.0f, 40.0f, 91.0f}) {
        REQUIRE_THAT(max_yaw_rate(-v, p), WithinAbs(max_yaw_rate(v, p), 1e-6f));
        REQUIRE_THAT(max_yaw_rate(-v, p), WithinAbs(p.tireMu * p.gravity / v, 1e-6f));
        REQUIRE(max_yaw_rate(-v, p) < p.tireMu * p.gravity);  // i.e. NOT the vx=1 cap
    }
    // Inside the |vx| < 1 floor both signs collapse to the same finite value —
    // no 1/0 blow-up as vx passes through zero.
    for (float v : {-0.9f, -0.001f, 0.0f, 0.001f, 0.9f}) {
        const float rMax = max_yaw_rate(v, p);
        REQUIRE(std::isfinite(rMax));
        REQUIRE_THAT(rMax, WithinAbs(p.tireMu * p.gravity, 1e-6f));
    }
    // A symmetric clamp on |r| carries no sign, so there is no steering-sign
    // convention to match here (the spec asked for that to be re-checked).
    // Verified structurally: the function does not take delta at all.
}

// ── §5.2 negative v_x: slip angles ────────────────────────────────────

TEST_CASE("Slip angles are sign-correct at negative vx: steering while moving backwards yaws the car the RIGHT way",
          "[car][tire][negative-vx]") {
    CarParams   p;
    TireParams  t;
    const float L = t.a + t.b;

    // The kinematic reference: r = vx*tan(delta)/L, which flips sign with vx.
    // The dynamic model's INITIAL yaw acceleration must agree with it in sign
    // for every combination of motion direction and steer direction.
    for (float vx : {-25.0f, -12.0f, -8.0f, 8.0f, 12.0f, 25.0f}) {
        for (float delta : {-0.20f, -0.05f, 0.05f, 0.20f}) {
            const float       rKin = (vx / L) * std::tan(delta);
            const BicycleDeriv d  = dynamic_bicycle_deriv(vx, 0.0f, 0.0f, delta, p, t);
            REQUIRE(std::isfinite(d.rDot));
            REQUIRE(std::isfinite(d.vlDot));
            REQUIRE(d.rDot * rKin > 0.0f);  // same sign as the kinematic model
        }
    }

    // Magnitude symmetry: the front slip angle's steer term is sgn(vx)*delta, so
    // reversing BOTH the direction of travel and the steer must give the mirror
    // image of the forward case.
    for (float vx : {8.0f, 25.0f}) {
        for (float delta : {0.05f, 0.20f}) {
            const BicycleDeriv fwd = dynamic_bicycle_deriv(vx, 0.0f, 0.0f, delta, p, t);
            const BicycleDeriv rev = dynamic_bicycle_deriv(-vx, 0.0f, 0.0f, -delta, p, t);
            REQUIRE_THAT(rev.rDot, WithinAbs(fwd.rDot, 1e-4f));
        }
    }
}

TEST_CASE("Reversing above the 5 m/s dynamic threshold steers the correct way (pre-Phase-2 live bug)",
          "[car][tire][negative-vx]") {
    // This is not hypothetical: CarEntity's reverse creep tops out at
    // kMaxReverseSpeed = 12 m/s, WELL above kDynamicBlendSpeed = 5 m/s, so the
    // dynamic bicycle model has always been the one running in reverse. Before
    // the sgn(vx) fix the integrated heading went 42.8 deg/s the WRONG way here.
    CarParams   p;
    TireParams  t;
    const float L = t.a + t.b, dt = 1.0f / 120.0f, delta = 0.20f;
    float       vx = -10.0f, vl = 0.0f, r = 0.0f, headingDeg = 0.0f;
    for (int i = 0; i < 120; ++i) {  // 1 s
        const BicycleDeriv d = dynamic_bicycle_deriv(vx, vl, r, delta, p, t);
        vl += d.vlDot * dt;
        r += d.rDot * dt;
        const float rMax = max_yaw_rate(vx, p);
        r                = std::clamp(r, -rMax, rMax);
        vl               = std::clamp(vl, -std::fabs(vx), std::fabs(vx));
        headingDeg -= r * 180.0f / kEnginePi * dt;
    }
    const float rKin = (vx / L) * std::tan(delta);
    REQUIRE(rKin < 0.0f);  // reversing with right steer ⇒ negative (right-positive) yaw rate
    REQUIRE(r < 0.0f);     // ...and the dynamic model must agree
    REQUIRE(headingDeg > 0.0f);  // heading -= deg(r)*dt, so a negative r RAISES heading
}

TEST_CASE("Bicycle model stays finite and bounded across a vx sweep straddling zero, at every grip level",
          "[car][tire][negative-vx]") {
    TireParams          t;
    const WetGripParams wg;
    for (float mu : {1.05f, 0.60f, 0.30f, wg.muPlane, 0.01f}) {
        CarParams p;
        p.tireMu = mu;
        for (float vx = -40.0f; vx <= 40.0f; vx += 0.25f) {
            for (float vl : {-30.0f, -1.0f, 0.0f, 1.0f, 30.0f}) {
                for (float r : {-3.0f, 0.0f, 3.0f}) {
                    const BicycleDeriv d = dynamic_bicycle_deriv(vx, vl, r, 0.20f, p, t);
                    REQUIRE(std::isfinite(d.vlDot));
                    REQUIRE(std::isfinite(d.rDot));
                    // Lateral accel from tires cannot exceed mu*g (both axle
                    // forces are clamped at mu*Fz and Fzf + Fzr = m*g), so the
                    // only other term in vlDot is the -vx*r transport term.
                    REQUIRE(std::fabs(d.vlDot + vx * r) <= mu * p.gravity + 1e-3f);
                    // Yaw accel cannot exceed the fully-asymmetric saturated
                    // moment 2*mu*m*g*a*b/(L*Iz).
                    const float L      = t.a + t.b;
                    const float rDotCap = 2.0f * mu * p.mass * p.gravity * t.a * t.b / (L * t.iz);
                    REQUIRE(std::fabs(d.rDot) <= rDotCap + 1e-3f);
                }
            }
        }
    }
}

// ── The hydroplane gate itself ────────────────────────────────────────

TEST_CASE("hydroplane_fraction gates on BOTH standing water and speed, and agrees with effective_mu",
          "[car][wet][spin]") {
    WetGripParams g;
    // No standing water ⇒ never planing, at any speed.
    for (float v : {0.0f, 20.0f, g.vPlane + 50.0f, 100.0f})
        for (float film : {0.0f, 0.5f, 0.999f})
            REQUIRE(hydroplane_fraction(v, film, g) == 0.0f);
    // Standing water but at or below vPlane ⇒ still not planing.
    for (float v : {0.0f, 10.0f, g.vPlane})
        REQUIRE(hydroplane_fraction(v, 1.5f, g) == 0.0f);
    // Fully planing at and past vPlane + planeBand, monotone in between.
    REQUIRE_THAT(hydroplane_fraction(g.vPlane + g.planeBand, 1.5f, g), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(hydroplane_fraction(g.vPlane + g.planeBand + 30.0f, 1.5f, g), WithinAbs(1.0f, 1e-6f));
    float prev = 0.0f;
    for (int i = 0; i <= 40; ++i) {
        const float v = g.vPlane + g.planeBand * i / 40.0f;
        const float s = hydroplane_fraction(v, 1.5f, g);
        REQUIRE(s >= prev - 1e-6f);
        REQUIRE(s >= 0.0f);
        REQUIRE(s <= 1.0f);
        prev = s;
    }
    // Sign-agnostic: a car sliding backwards at 40 m/s planes just as hard.
    REQUIRE_THAT(hydroplane_fraction(-40.0f, 1.5f, g), WithinAbs(hydroplane_fraction(40.0f, 1.5f, g), 1e-6f));

    // ONE definition of "is it planing": effective_mu must be exactly the
    // fraction-weighted blend, since it now calls this function internally.
    for (float w : {0.5f, 1.0f}) {
        for (float v : {20.0f, 28.0f, 30.0f, 34.0f, 45.0f}) {
            const float muLow = g.muDry + w * (g.muWet - g.muDry);
            const float faded = muLow * (1.0f - w * g.speedFade * std::min(v, g.vPlane));
            const float s     = hydroplane_fraction(v, 1.5f, g);
            REQUIRE_THAT(effective_mu(w, v, 1.5f, g), WithinAbs(faded + s * (g.muPlane - faded), 1e-6f));
        }
    }
}

// ── Why the spec's "just relax the clamp" cannot work ─────────────────

TEST_CASE("Static-load saturation gives IDENTICALLY zero yaw moment — the saturated model is yaw-neutral, not unstable",
          "[car][tire][spin]") {
    // This is the Phase-2 finding that contradicts the spec's §2/§3.1 premise.
    // With STATIC axle loads, Fzf ∝ b and Fzr ∝ a, so when both axles saturate
    // at the same sign the yaw moment a*mu*Fzf - b*mu*Fzr is exactly
    //   mu*(a*m*g*b/L - b*m*g*a/L) = 0.
    // No amount of relaxing max_yaw_rate() can make a yaw-neutral model diverge.
    CarParams  p;
    TireParams t;
    p.tireMu   = 0.08f;  // hydroplane grip
    const float L   = t.a + t.b;
    const float FzF = p.mass * p.gravity * t.b / L;
    const float FzR = p.mass * p.gravity * t.a / L;
    REQUIRE_THAT(t.a * p.tireMu * FzF - t.b * p.tireMu * FzR, WithinAbs(0.0f, 1e-2f));

    // Observable consequence: drive the model deep into saturation and the yaw
    // ACCELERATION vanishes, so r just freezes wherever it was.
    const BicycleDeriv d = dynamic_bicycle_deriv(30.0f, 12.0f, 1.0f, 0.30f, p, t);
    REQUIRE_THAT(d.rDot, WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("Longitudinal load transfer BREAKS that degeneracy: the saturated yaw moment becomes -mu*m*ax*hCg",
          "[car][tire][spin]") {
    // The mechanism the spin actually comes from. With transfer,
    //   a*mu*Fzf(ax) - b*mu*Fzr(ax) = -mu*m*ax*hCg
    // which is non-zero whenever the car is accelerating or braking — and under
    // braking it is an OVERSTEER moment.
    CarParams  p;
    TireParams t;
    p.tireMu = 1.05f;
    for (float ax : {-8.0f, -3.0f, 3.0f, 8.0f}) {
        const AxleLoad ld = axle_loads_long(p, t, ax);
        const float    m  = t.a * p.tireMu * ld.front - t.b * p.tireMu * ld.rear;
        REQUIRE_THAT(m, WithinAbs(-p.tireMu * p.mass * ax * t.hCg, 1e-1f));
        REQUIRE(std::fabs(m) > 100.0f);  // materially non-zero
    }
}

TEST_CASE("axle_lateral_forces with default arguments is bit-identical to the Phase-1 static-load model",
          "[car][tire][spin]") {
    // The Phase-2 circle closure is weighted by planeFrac, so every existing
    // caller (which passes neither optional argument) is untouched. Checked
    // against the closed form rather than a golden number.
    TireParams t;
    for (float mu : {1.05f, 0.52f, 0.08f}) {
        CarParams p;
        p.tireMu        = mu;
        const float L   = t.a + t.b;
        const float FzF = p.mass * p.gravity * t.b / L;
        const float FzR = p.mass * p.gravity * t.a / L;
        for (float vx : {8.0f, 30.0f, -30.0f}) {
            for (float vl : {-5.0f, 0.0f, 5.0f}) {
                for (float r : {-0.5f, 0.0f, 0.5f}) {
                    const float sgn    = (vx >= 0.0f) ? 1.0f : -1.0f;
                    const float delta  = 0.12f;
                    const float vxs    = std::max(std::fabs(vx), 1.0f);
                    const float alphaF = (vl + t.a * r) / vxs - sgn * delta;
                    const float alphaR = (vl - t.b * r) / vxs;
                    const AxleLateralForces fy = axle_lateral_forces(vx, vl, r, delta, p, t);
                    REQUIRE(fy.front == std::clamp(-t.cAlphaFront * alphaF, -mu * FzF, mu * FzF));
                    REQUIRE(fy.rear == std::clamp(-t.cAlphaRear * alphaR, -mu * FzR, mu * FzR));
                    // planeFrac = 0 must also be exactly the default path.
                    const AxleLateralForces zero =
                        axle_lateral_forces(vx, vl, r, delta, p, t, AxleLongitudinal{-5000.f, -5000.f}, 0.0f);
                    REQUIRE(zero.front == fy.front);
                    REQUIRE(zero.rear == fy.rear);
                }
            }
        }
    }
}

TEST_CASE("Circle closure: a braking longitudinal force shrinks lateral capacity, most at the unloaded rear",
          "[car][tire][spin]") {
    CarParams  p;
    TireParams t;
    p.tireMu = 0.52f;
    // Use the model's OWN brake split (per-axle friction-circle caps under
    // load transfer) rather than hand-picked numbers, so the test exercises the
    // real pathway. 70% pedal: hard, but not a four-wheel lock (at full pedal
    // every axle spends its whole circle longitudinally and lateral capacity
    // correctly goes to zero everywhere, which has no front/rear bias to check).
    AxleLongitudinal caps{};
    brake_cap_force(p, t, AxleLateral{}, &caps);
    const float            pedal = 0.7f;
    const AxleLongitudinal brake{-pedal * caps.front, -pedal * caps.rear};
    REQUIRE(std::fabs(brake.front) > std::fabs(brake.rear) * 0.5f);  // sane split
    // Demand a huge slip angle so both axles are pinned at their caps — then the
    // returned force magnitude IS the cap. (vl > 0 ⇒ both forces point LEFT, i.e.
    // negative, so compare magnitudes, not signed values.)
    const AxleLateralForces open   = axle_lateral_forces(30.f, 25.f, 0.f, 0.f, p, t);
    const AxleLateralForces closed = axle_lateral_forces(30.f, 25.f, 0.f, 0.f, p, t, brake, 1.0f);
    REQUIRE(std::fabs(closed.front) < std::fabs(open.front));
    REQUIRE(std::fabs(closed.rear) < std::fabs(open.rear));
    // Braking transfers load FORWARD, so the rear loses proportionally more of
    // its cornering capacity than the front — the oversteer bias a spin needs.
    REQUIRE(std::fabs(closed.rear / open.rear) < std::fabs(closed.front / open.front));
    // And it never goes negative or NaN even when the longitudinal demand alone
    // exceeds the circle.
    const AxleLateralForces crushed =
        axle_lateral_forces(30.f, 25.f, 0.f, 0.f, p, t, AxleLongitudinal{-99999.f, -99999.f}, 1.0f);
    REQUIRE(std::isfinite(crushed.front));
    REQUIRE(std::isfinite(crushed.rear));
    // Cap floored at 0 rather than NaN, so the force collapses to exactly 0.
    REQUIRE(crushed.front == 0.0f);
    REQUIRE(crushed.rear == 0.0f);
}

// ── The relaxed clamps ────────────────────────────────────────────────

TEST_CASE("yaw_rate_limit / sideslip_limit are exactly the old clamps at planeFrac 0 and released at 1",
          "[car][tire][spin]") {
    CarParams  p;
    TireParams t;
    p.tireMu = 0.08f;
    for (float vx : {-40.0f, -8.0f, 8.0f, 40.0f}) {
        const float vBody = std::fabs(vx) + 12.0f;
        REQUIRE(yaw_rate_limit(vx, vBody, p, t, 0.0f) == max_yaw_rate(vx, p));
        REQUIRE(sideslip_limit(vx, vBody, 0.0f) == std::fabs(vx));
        // Fully released: the pivot rail / body speed.
        REQUIRE_THAT(yaw_rate_limit(vx, vBody, p, t, 1.0f), WithinAbs(yaw_pivot_rail(vBody, t), 1e-4f));
        REQUIRE_THAT(sideslip_limit(vx, vBody, 1.0f), WithinAbs(vBody, 1e-4f));
        // Monotone in planeFrac, and the release is BIG enough to permit a spin:
        // the quasi-steady cap at hydroplane grip is ~1.8 deg/s, which no spin
        // can live inside.
        float prev = yaw_rate_limit(vx, vBody, p, t, 0.0f);
        for (int i = 1; i <= 10; ++i) {
            const float v = yaw_rate_limit(vx, vBody, p, t, i / 10.0f);
            REQUIRE(v >= prev - 1e-6f);
            prev = v;
        }
        REQUIRE(yaw_rate_limit(vx, vBody, p, t, 1.0f) > 40.0f * max_yaw_rate(vx, p));
        // Out-of-range planeFrac is clamped, not extrapolated.
        REQUIRE(yaw_rate_limit(vx, vBody, p, t, -5.0f) == max_yaw_rate(vx, p));
        REQUIRE_THAT(yaw_rate_limit(vx, vBody, p, t, 7.0f), WithinAbs(yaw_pivot_rail(vBody, t), 1e-4f));
    }
    // The rail is a numerical guard, not a behaviour: even a violent spin
    // (300 deg/s) sits far inside it at highway speed.
    REQUIRE(yaw_pivot_rail(34.0f, t) > 20.0f);
}

TEST_CASE("body_transport_accel makes pure rotation conserve speed (and is inert when not planing)",
          "[car][tire][spin]") {
    REQUIRE(body_transport_accel(15.0f, 2.0f, 0.0f) == 0.0f);
    REQUIRE(body_transport_accel(0.0f, 2.0f, 1.0f) == 0.0f);
    REQUIRE(body_transport_accel(15.0f, 0.0f, 1.0f) == 0.0f);
    REQUIRE_THAT(body_transport_accel(15.0f, 2.0f, 1.0f), WithinAbs(30.0f, 1e-5f));
    REQUIRE_THAT(body_transport_accel(15.0f, 2.0f, 0.5f), WithinAbs(15.0f, 1e-5f));

    // Force-free pure rotation: with no tire or resistance forces at all, the
    // body-frame velocity must simply rotate, so |V| is invariant. WITH the
    // term the error is tiny (explicit-Euler only); WITHOUT it, |V| grows
    // without bound — the spurious energy injection the term exists to remove.
    const float r = 2.0f, dt = 1.0f / 240.0f;
    for (int withTerm = 0; withTerm < 2; ++withTerm) {
        float vx = 30.0f, vl = 0.0f;
        for (int i = 0; i < 240; ++i) {  // 1 s, ~115 deg of rotation
            const float ax = withTerm ? body_transport_accel(vl, r, 1.0f) : 0.0f;
            const float al = -vx * r;
            vx += ax * dt;
            vl += al * dt;
        }
        const float V = std::sqrt(vx * vx + vl * vl);
        if (withTerm)
            REQUIRE_THAT(V, WithinAbs(30.0f, 0.5f));
        else
            REQUIRE(V > 45.0f);  // >50% of energy conjured out of nothing
    }
}

// ── Whole-loop behaviour: the spin, and dry safety ────────────────────

TEST_CASE("Hydroplane spin-out emerges: a car mid-corner on standing water comes round to ~80 deg of sideslip",
          "[car][wet][spin]") {
    // The realistic manoeuvre, and the one the whole phase is about: 3 s of
    // ordinary cornering on grip, then the tires lift onto standing water.
    // Nothing else changes — same held steer, same throttle-off coast.
    SpinState s;
    s.vx        = 40.0f;  // 90 mph, well past vPlane + planeBand
    s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
    const float steer = 6.0f * kEnginePi / 180.0f;
    spin_warmup(s, steer, 3.0f);

    const float rEntry    = s.r;
    const float betaEntry = std::atan2(s.vl, s.vx) * 180.0f / kEnginePi;
    const float vEntry    = std::sqrt(s.vx * s.vx + s.vl * s.vl);
    REQUIRE(rEntry > 0.1f);                    // genuinely cornering on entry
    REQUIRE(std::fabs(betaEntry) < 5.0f);      // ...and tracking properly

    SpinInput in;
    in.wetness            = 1.0f;
    in.filmOverride       = 1.5f;  // a sustained flooded stretch
    in.steerRad           = steer;
    const SpinSummary out = spin_run(s, in, 8.0f);

    REQUIRE(out.finite);
    REQUIRE(out.maxPlaneFrac > 0.99f);  // it really did plane
    REQUIRE(out.minPlaneMu < 0.1f);     // ...down to muPlane
    // The signature of the spin: the nose keeps rotating (there is no grip left
    // to damp the yaw rate) while momentum carries the car straight on, so
    // sideslip runs away far past the 45 deg the old clamp allowed.
    REQUIRE(out.maxAbsBetaDeg > 60.0f);
    REQUIRE(std::fabs(out.cumYawDeg) > 60.0f);
    // Forward speed is traded for lateral speed rather than destroyed: the
    // body-speed magnitude is nearly conserved even as vx collapses.
    const float vExit = std::sqrt(s.vx * s.vx + s.vl * s.vl);
    REQUIRE(s.vx < 0.25f * vEntry);          // forward component has collapsed...
    REQUIRE(vExit > 0.80f * vEntry);         // ...but the car is still travelling
    REQUIRE(std::fabs(s.vl) > 0.5f * vEntry);  // and most of it is now sideways
    // Bounded, not exploding: nowhere near the kinematic pivot rail.
    REQUIRE(out.maxAbsYawRateDeg < 1000.0f);
}

TEST_CASE("The SAME manoeuvre on a dry road just corners — Phase 2 is inert without standing water",
          "[car][wet][spin]") {
    SpinState s;
    s.vx        = 40.0f;
    s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
    const float steer = 6.0f * kEnginePi / 180.0f;
    spin_warmup(s, steer, 3.0f);

    SpinInput in;
    in.wetness            = 0.0f;  // dry
    in.filmOverride       = 0.0f;  // no standing water
    in.steerRad           = steer;
    const SpinSummary out = spin_run(s, in, 8.0f);

    REQUIRE(out.finite);
    REQUIRE(out.maxPlaneFrac == 0.0f);  // the gate never opens
    REQUIRE_THAT(out.minPlaneMu, WithinAbs(CarParams{}.tireMu, 1e-6f));
    // A normal steady corner: a few degrees of sideslip, speed barely off.
    REQUIRE(out.maxAbsBetaDeg < 6.0f);
    REQUIRE(s.vx > 30.0f);
    REQUIRE(out.minVx > 30.0f);
}

TEST_CASE("A straight-line hydroplane correctly does nothing — losing grip is not the same as losing control",
          "[car][wet][spin]") {
    // Guards against the feature degenerating into "low mu ⇒ random rotation".
    // With no yaw rate and no sideslip to begin with, there is nothing for the
    // lost grip to stop doing, so the car tracks dead straight.
    SpinState s;
    s.vx        = 40.0f;
    s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
    spin_warmup(s, 0.0f, 3.0f);

    SpinInput in;
    in.wetness            = 1.0f;
    in.filmOverride       = 1.5f;
    in.steerRad           = 0.0f;
    const SpinSummary out = spin_run(s, in, 8.0f);

    REQUIRE(out.finite);
    REQUIRE(out.maxPlaneFrac > 0.99f);  // fully planing the whole time...
    REQUIRE(out.maxAbsBetaDeg < 1.0f);  // ...and dead straight anyway
    REQUIRE(std::fabs(out.cumYawDeg) < 1.0f);
}

TEST_CASE("Skill-based recovery: countersteer + lift arrests a hydroplane slide; holding the input does not",
          "[car][wet][spin]") {
    // Spec decision 3 — recovery has no code of its own. The clamps re-engage
    // as grip returns, and countersteering aligns the nose with the velocity
    // vector so sideslip stops growing. Same opening for both runs.
    const auto simulate = [](bool recover) {
        SpinState s;
        s.vx        = 40.0f;
        s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
        const float steer = 6.0f * kEnginePi / 180.0f;
        spin_warmup(s, steer, 3.0f);

        const CarParams        base;
        const TireParams       t;
        const GearboxParams    g;
        const DrivetrainParams d;
        const WetGripParams    wg;
        const float            dt      = 1.0f / 120.0f;
        float                  maxBeta = 0.0f;
        for (int i = 0; i < 120 * 8; ++i) {
            SpinInput in;
            in.wetness      = 1.0f;
            in.filmOverride = 1.5f;
            const float beta = std::atan2(s.vl, s.vx) * 180.0f / kEnginePi;
            const float V    = std::sqrt(s.vx * s.vx + s.vl * s.vl);
            if (recover && std::fabs(beta) > 8.0f)
                in.steerRad = (beta > 0.0f) ? 0.61f : -0.61f;  // steer INTO the slide, to the lock
            else
                in.steerRad = steer;
            spin_step(s, in, base, t, g, d, wg, dt);
            if (V > 2.0f)
                maxBeta = std::max(maxBeta, std::fabs(beta));
        }
        return maxBeta;
    };
    const float held      = simulate(false);
    const float recovered = simulate(true);
    REQUIRE(held > 60.0f);          // left alone, the car comes right round
    REQUIRE(recovered < held);      // countersteer genuinely helps...
    REQUIRE(recovered < 0.7f * held);  // ...and by a wide margin
}

TEST_CASE("A hydroplane slide drives the forward speed through zero and negative, with no NaN anywhere",
          "[car][wet][spin][negative-vx]") {
    // The regime the whole §5 validation exists for. Once sideslip passes 90 deg
    // the body-frame forward speed is NEGATIVE; assert we get there and that
    // nothing in the loop breaks when we do.
    SpinState s;
    s.vx        = 45.0f;
    s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
    const float steer = 12.0f * kEnginePi / 180.0f;
    spin_warmup(s, steer, 3.0f);

    SpinInput in;
    in.wetness            = 1.0f;
    in.filmOverride       = 1.5f;
    in.steerRad           = steer;
    in.brake              = 0.4f;  // partial brake: keeps the axles asymmetrically loaded
    const SpinSummary out = spin_run(s, in, 12.0f);

    REQUIRE(out.finite);
    REQUIRE(out.minVx < 0.0f);  // the forward component really did reverse
    REQUIRE(std::isfinite(s.worldX));
    REQUIRE(std::isfinite(s.worldZ));
    REQUIRE(std::isfinite(s.headingDeg));
    REQUIRE(std::isfinite(s.vl));
    REQUIRE(std::isfinite(s.r));
    // No runaway: the car sheds speed, never gains it.
    REQUIRE(std::sqrt(s.vx * s.vx + s.vl * s.vl) < 46.0f);
    // And the reverse-CREEP cap (kMaxReverseSpeed = 12 m/s) must NOT have
    // clipped a real slide — it now lives on the driver-reverse branch only.
    REQUIRE(out.minVx > -400.0f);
}

// ── The per-axle longitudinal report ──────────────────────────────────

TEST_CASE("longitudinal_accel's per-axle split sums to the force it returned, and is inert when unused",
          "[car][longitudinal][spin]") {
    CarParams              p;
    TireParams             t;
    GearboxParams          g;
    DrivetrainParams       d;
    for (float v : {5.0f, 20.0f, 45.0f, 80.0f}) {
        for (int gear : {0, 2, 6}) {
            for (float thr : {0.0f, 0.5f, 1.0f}) {
                for (float brk : {0.0f, 1.0f}) {
                    AxleLongitudinal split{};
                    const float withPtr = longitudinal_accel(v, thr, brk, gear, p, t, g, kRearWheelRadiusM, d,
                                                             AxleLateral{}, &split);
                    const float noPtr =
                        longitudinal_accel(v, thr, brk, gear, p, t, g, kRearWheelRadiusM, d, AxleLateral{});
                    REQUIRE(withPtr == noPtr);  // the out-param cannot change the result
                    REQUIRE(std::isfinite(split.front));
                    REQUIRE(std::isfinite(split.rear));
                    // The split is the CONTACT-PATCH force, so it is m*a_x plus
                    // the aero drag that a_x also carries but the tires do not.
                    // Pinning this exactly is the regression guard for the
                    // aero-drag exclusion (charging drag to the circle zeroed
                    // the lateral capacity outright at speed).
                    const float fDrag = 0.5f * p.airDensity * p.dragCoeff * p.frontalArea * v * v;
                    REQUIRE_THAT((split.front + split.rear - fDrag) / p.mass, WithinAbs(withPtr, 1e-2f));
                }
            }
        }
    }
    // Signs: full throttle drives both axles forward (AWD); full brake with no
    // throttle pulls both back.
    AxleLongitudinal drive{}, brake{};
    longitudinal_accel(20.0f, 1.0f, 0.0f, 1, p, t, g, kRearWheelRadiusM, d, AxleLateral{}, &drive);
    longitudinal_accel(20.0f, 0.0f, 1.0f, 1, p, t, g, kRearWheelRadiusM, d, AxleLateral{}, &brake);
    REQUIRE(drive.front > 0.0f);
    REQUIRE(drive.rear > 0.0f);
    REQUIRE(drive.rear > drive.front);  // rear-biased AWD
    REQUIRE(brake.front < 0.0f);
    REQUIRE(brake.rear < 0.0f);
    REQUIRE(std::fabs(brake.front) > std::fabs(brake.rear));  // braking loads the front
}

// ── Clamp re-engagement must not delete momentum ──────────────────────

TEST_CASE("rate_limited_bound never lets a clamp brake harder than friction can", "[car][tire][spin]") {
    const float dt = 1.0f / 120.0f;
    // Not already violated ⇒ the bound is returned untouched (no-op).
    REQUIRE(rate_limited_bound(30.0f, 5.0f, 10.0f, dt) == 30.0f);
    REQUIRE(rate_limited_bound(30.0f, 30.0f, 10.0f, dt) == 30.0f);
    // Violated ⇒ the bound may only tighten by maxRate*dt per step.
    const float b = rate_limited_bound(12.0f, 29.4f, 5.6f, dt);
    REQUIRE(b > 29.3f);
    REQUIRE(b < 29.4f);
    REQUIRE_THAT(b, WithinAbs(29.4f - 5.6f * dt, 1e-5f));
    // Never negative even for absurd rates, and sign-agnostic in prevMag.
    REQUIRE(rate_limited_bound(0.0f, 1.0f, 1e9f, dt) == 0.0f);
    REQUIRE(rate_limited_bound(4.0f, -29.4f, 5.6f, dt) == rate_limited_bound(4.0f, 29.4f, 5.6f, dt));
    // Repeated application converges to the bound rather than sticking above it.
    float cur = 29.4f;
    for (int i = 0; i < 120 * 12; ++i)
        cur = std::min(cur, rate_limited_bound(12.0f, cur, 5.6f, dt));
    REQUIRE_THAT(cur, WithinAbs(12.0f, 1e-3f));
}

TEST_CASE("max_yaw_decel is the fully-asymmetric saturated yaw moment over Iz, and scales with mu",
          "[car][tire][spin]") {
    CarParams  p;
    TireParams t;
    const float L = t.a + t.b;
    REQUIRE_THAT(max_yaw_decel(p, t),
                 WithinAbs(2.0f * p.tireMu * p.mass * p.gravity * t.a * t.b / (L * t.iz), 1e-6f));
    CarParams plane = p;
    plane.tireMu    = 0.08f;
    REQUIRE(max_yaw_decel(plane, t) < max_yaw_decel(p, t));
    REQUIRE_THAT(max_yaw_decel(plane, t), WithinAbs(max_yaw_decel(p, t) * 0.08f / p.tireMu, 1e-5f));
}

TEST_CASE("Grip returning after a hydroplane slide does not teleport momentum away", "[car][wet][spin]") {
    // The defect this rate limit exists for, exercised end to end: corner on
    // grip, hydroplane until the car is well past 45 deg of sideslip, then take
    // the water away in one step. The body speed must decay at a tire-plausible
    // rate, not collapse.
    SpinState s;
    s.vx        = 40.0f;
    s.gear.gear = gear_for_speed(s.vx, GearboxParams{}, kRearWheelRadiusM);
    const float steer = 6.0f * kEnginePi / 180.0f;
    spin_warmup(s, steer, 3.0f);

    SpinInput wet;
    wet.wetness      = 1.0f;
    wet.filmOverride = 1.5f;
    wet.steerRad     = steer;
    spin_run(s, wet, 5.0f);
    const float betaMid = std::fabs(std::atan2(s.vl, s.vx) * 180.0f / kEnginePi);
    REQUIRE(betaMid > 50.0f);  // genuinely outside the quasi-steady 45 deg bound
    const float vBefore = std::sqrt(s.vx * s.vx + s.vl * s.vl);

    // Water gone, everything else identical. Step ONE frame and measure.
    SpinInput dry = wet;
    dry.filmOverride = 0.0f;
    const CarParams        base;
    const TireParams       t;
    const GearboxParams    g;
    const DrivetrainParams d;
    const WetGripParams    wg;
    const float            dt = 1.0f / 120.0f;
    spin_step(s, dry, base, t, g, d, wg, dt);
    const float vAfter = std::sqrt(s.vx * s.vx + s.vl * s.vl);
    // One frame may not shed more than ~mu*g*dt of speed (allow 3x for the
    // combined longitudinal + lateral budget and Euler slop). Before the rate
    // limit this single step removed ~19 m/s.
    REQUIRE(vBefore - vAfter < 3.0f * base.tireMu * base.gravity * dt);

    // And over the following second the slide unwinds smoothly rather than in a
    // single jump: every step stays inside the same per-step budget.
    for (int i = 0; i < 120; ++i) {
        const float v0 = std::sqrt(s.vx * s.vx + s.vl * s.vl);
        spin_step(s, dry, base, t, g, d, wg, dt);
        const float v1 = std::sqrt(s.vx * s.vx + s.vl * s.vl);
        REQUIRE(v0 - v1 < 3.0f * base.tireMu * base.gravity * dt);
        REQUIRE(std::isfinite(v1));
    }
    // Grip really did come back and really is pulling the car straight.
    REQUIRE(std::fabs(std::atan2(s.vl, s.vx) * 180.0f / kEnginePi) < betaMid);
}
