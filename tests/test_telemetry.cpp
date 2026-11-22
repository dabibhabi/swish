// Telemetry ring, CSV round-trip, and deterministic replay.
#include "debug/TelemetryRecorder.h"
#include "scene/Entity/CarEntity.h"
#include "utils/telemetry/ReplayDriver.h"
#include "utils/telemetry/TelemetryRing.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sstream>
#include <vector>

using namespace swish;

namespace {
TelemetrySample make_sample(float t) {
    TelemetrySample s;
    s.t         = t;
    s.x         = t * 1234.5678f + 0.1f;
    s.y         = -t / 3.f;
    s.z         = 1.f / 7.f * t;
    s.yaw_deg   = 12.3456789f * t;
    s.speed_mps = 33.333333f;
    s.gear      = static_cast<int>(t) % 7;
    s.rpm       = 6543.21f;
    s.steer_deg = -17.5f;
    s.throttle  = 0.37f;
    s.brake     = 0.f;
    s.mu        = 1.05f;
    for (int i = 0; i < 4; ++i) {
        s.wheel_spin[static_cast<std::size_t>(i)] = 0.1f * static_cast<float>(i) + t;
        s.susp[static_cast<std::size_t>(i)]       = -0.003f * static_cast<float>(i + 1);
    }
    s.input.steer      = 0.123456789f;
    s.input.throttle   = 0.7f;
    s.input.brake      = 0.01f;
    s.input.steer_mode = SteerMode::Rate;
    return s;
}

bool bits_equal(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::vector<InputFrame> scripted_run(int n) {
    std::vector<InputFrame> v;
    for (int i = 0; i < n; ++i) {
        InputFrame f;
        f.steer_mode = (i / 300) % 2 == 0 ? SteerMode::Rate : SteerMode::Target;
        f.steer      = ((i / 50) % 3 == 0) ? -1.f : (((i / 50) % 3 == 1) ? 0.6f : 0.f);
        f.throttle   = (i / 120) % 4 == 3 ? 0.f : 1.f;
        f.brake      = (i / 120) % 4 == 3 ? 0.8f : 0.f;
        v.push_back(f);
    }
    return v;
}
}  // namespace

TEST_CASE("ring wraparound and ordering", "[telemetry]") {
    TelemetryRing r(4);
    CHECK(r.capacity() == 4);
    CHECK(r.size() == 0);
    CHECK(r.empty());
    for (int i = 0; i < 3; ++i) r.push(make_sample(static_cast<float>(i)));
    CHECK(r.size() == 3);
    CHECK(r.oldest().t == 0.f);
    CHECK(r.newest().t == 2.f);
    for (int i = 3; i < 10; ++i) r.push(make_sample(static_cast<float>(i)));
    CHECK(r.size() == 4);  // capped at capacity
    for (std::size_t i = 0; i < r.size(); ++i)
        CHECK(r.at(i).t == static_cast<float>(6 + i));  // oldest -> newest: 6,7,8,9
    r.clear();
    CHECK(r.size() == 0);
    CHECK(r.capacity() == 4);
    r.push(make_sample(42.f));
    CHECK(r.oldest().t == 42.f);
}

TEST_CASE("CSV round-trip is exact", "[telemetry]") {
    TelemetryRing r(8);
    for (int i = 0; i < 11; ++i)  // wraps: keeps 3..10
        r.push(make_sample(static_cast<float>(i)));
    std::stringstream ss;
    write_csv(r, ss);
    CHECK(ss.str().rfind("t,x,y,z,", 0) == 0);  // header row first

    TelemetryRing back(8);
    REQUIRE(read_csv(ss, back));
    REQUIRE(back.size() == r.size());
    for (std::size_t i = 0; i < r.size(); ++i) {
        const TelemetrySample &a = r.at(i), &b = back.at(i);
        CHECK(bits_equal(a.t, b.t));
        CHECK(bits_equal(a.x, b.x));
        CHECK(bits_equal(a.y, b.y));
        CHECK(bits_equal(a.z, b.z));
        CHECK(bits_equal(a.yaw_deg, b.yaw_deg));
        CHECK(bits_equal(a.speed_mps, b.speed_mps));
        CHECK(a.gear == b.gear);
        CHECK(bits_equal(a.rpm, b.rpm));
        CHECK(bits_equal(a.steer_deg, b.steer_deg));
        CHECK(bits_equal(a.mu, b.mu));
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(bits_equal(a.wheel_spin[k], b.wheel_spin[k]));
            CHECK(bits_equal(a.susp[k], b.susp[k]));
        }
        CHECK(bits_equal(a.input.steer, b.input.steer));
        CHECK(bits_equal(a.input.throttle, b.input.throttle));
        CHECK(a.input.steer_mode == b.input.steer_mode);
    }

    std::stringstream bad("not,a,header\n1,2,3\n");
    TelemetryRing     sink(2);
    CHECK_FALSE(read_csv(bad, sink));
}

TEST_CASE("replay is deterministic on fresh cars", "[telemetry][replay]") {
    const float   dt     = 1.f / 60.f;
    const auto    frames = scripted_run(1500);
    CarEntity     rec_car;
    TelemetryRing ring(frames.size());
    float         t = 0.f;
    for (const InputFrame& f : frames) {
        rec_car.apply_input_frame(f, dt);
        rec_car.update(dt);
        t += dt;
        ring.push(sample_from(rec_car, t, f));
    }
    REQUIRE(ring.size() == frames.size());
    CHECK(rec_car.get_speed_mps() > 1.f);  // the script actually drove

    // Round-trip the recording through CSV, then replay from the loaded copy.
    std::stringstream ss;
    write_csv(ring, ss);
    TelemetryRing loaded(frames.size());
    REQUIRE(read_csv(ss, loaded));

    ReplayDriver replay = ReplayDriver::from_ring(loaded, dt);
    REQUIRE(replay.size() == frames.size());
    CarEntity   car2;
    std::size_t i = 0;
    while (!replay.done()) {
        const InputFrame f = replay.next(dt);
        car2.apply_input_frame(f, dt);
        car2.update(dt);
        const TelemetrySample  s = sample_from(car2, 0.f, f);
        const TelemetrySample& r = ring.at(i);
        REQUIRE(bits_equal(s.x, r.x));
        REQUIRE(bits_equal(s.y, r.y));
        REQUIRE(bits_equal(s.z, r.z));
        REQUIRE(bits_equal(s.yaw_deg, r.yaw_deg));
        REQUIRE(bits_equal(s.speed_mps, r.speed_mps));
        ++i;
    }
    CHECK(i == frames.size());
    CHECK(replay.done());
    // Past the end: neutral frame, stays done.
    const InputFrame tail = replay.next(dt);
    CHECK(tail.throttle == 0.f);
    CHECK(replay.done());
}

#ifdef SWISH_DEBUG_UI
TEST_CASE("TelemetryRecorder records and replays", "[telemetry][debug]") {
    TelemetryRecorder rec(64);
    CarEntity         car;
    rec.set_recording(true);
    InputFrame f;
    f.throttle = 1.f;
    for (int i = 0; i < 10; ++i) {
        car.apply_input_frame(f, 0.01f);
        car.update(0.01f);
        rec.on_step(car, f, 0.01f);
    }
    CHECK(rec.ring().size() == 10);
    rec.start_replay(0.01f);
    CHECK(rec.replay_active());
    CHECK_FALSE(rec.recording());
    for (int i = 0; i < 10; ++i) CHECK(rec.next_replay_frame(0.01f).throttle == 1.f);
    CHECK_FALSE(rec.replay_active());
}
#endif
