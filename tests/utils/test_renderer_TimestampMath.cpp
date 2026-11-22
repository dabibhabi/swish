#include "renderer/GpuProfiler/TimestampMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

namespace gp = swish::gpuprof;
using Catch::Matchers::WithinAbs;
using swish::u32;
using swish::u64;

TEST_CASE("valid_mask covers 0, partial and full widths", "[gpuprof]") {
    REQUIRE(gp::valid_mask(0) == 0);
    REQUIRE(gp::valid_mask(1) == 1);
    REQUIRE(gp::valid_mask(36) == (u64{1} << 36) - 1);
    REQUIRE(gp::valid_mask(64) == ~u64{0});
    REQUIRE(gp::valid_mask(80) == ~u64{0});
}

TEST_CASE("elapsed_ticks is a plain difference without wrap", "[gpuprof]") {
    REQUIRE(gp::elapsed_ticks(100, 350, 64) == 250);
    REQUIRE(gp::elapsed_ticks(7, 7, 36) == 0);
}

TEST_CASE("elapsed_ticks survives counter wraparound at the valid width", "[gpuprof]") {
    // 36-bit counter: begin 10 ticks before the wrap, end 20 ticks after => 30 ticks.
    const u64 top = (u64{1} << 36) - 10;
    REQUIRE(gp::elapsed_ticks(top, 20, 36) == 30);
    // 64-bit counter wraps through unsigned overflow.
    REQUIRE(gp::elapsed_ticks(~u64{0} - 4, 5, 64) == 10);
}

TEST_CASE("elapsed_ticks ignores garbage above the valid bits", "[gpuprof]") {
    const u64 junk = u64{0xABC} << 40;
    REQUIRE(gp::elapsed_ticks(junk | 100, junk | 160, 36) == 60);
}

TEST_CASE("elapsed_ticks with 0 valid bits is 0 (unsupported queue)", "[gpuprof]") {
    REQUIRE(gp::elapsed_ticks(1, 999, 0) == 0);
}

TEST_CASE("ticks_to_ms uses nanoseconds per tick", "[gpuprof]") {
    // 1 ns/tick: 1,000,000 ticks = 1 ms.
    REQUIRE_THAT(gp::ticks_to_ms(1'000'000, 1.0f), WithinAbs(1.0, 1e-9));
    // Apple-style 41.667 ns/tick (24 MHz): 24,000 ticks = 1 ms.
    REQUIRE_THAT(gp::ticks_to_ms(24'000, 1000.0f / 24.0f), WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(gp::ticks_to_ms(0, 83.3f), WithinAbs(0.0, 1e-12));
}

TEST_CASE("slot indexing partitions the pool without overlap", "[gpuprof]") {
    constexpr u32 passes = 14, ring = 2;
    REQUIRE(gp::boundary_count(passes) == 15);
    REQUIRE(gp::pool_query_count(ring, passes) == 30);
    REQUIRE(gp::slot_base(0, passes) == 0);
    REQUIRE(gp::slot_base(1, passes) == 15);
    REQUIRE(gp::query_index(1, passes, passes) == 29);  // last query of the pool
    REQUIRE(gp::query_index(0, passes, passes) + 1 == gp::slot_base(1, passes));
}

TEST_CASE("ring slot cycles and latency equals the ring size", "[gpuprof]") {
    REQUIRE(gp::ring_slot(0, 2) == 0);
    REQUIRE(gp::ring_slot(1, 2) == 1);
    REQUIRE(gp::ring_slot(2, 2) == 0);
    REQUIRE(gp::ring_slot(1001, 2) == 1);
    REQUIRE(gp::ring_slot(5, 0) == 0);  // degenerate ring never divides by zero
    // Frame N reuses the slot of frame N-ring, whose results are read just before: latency == ring.
    REQUIRE(gp::readback_latency_frames(2) == 2);
    REQUIRE(gp::ring_slot(10, 2) == gp::ring_slot(10 - gp::readback_latency_frames(2), 2));
}

TEST_CASE("pass_times_ms brackets passes by boundaries and the total is their sum", "[gpuprof]") {
    // 3 passes, 4 boundaries, 1 us per tick (period 1000 ns): 0 -> 2000 -> 2000 -> 5000 ticks.
    const u64          ts[] = {1000, 3000, 3000, 6000};  // pass 1 recorded nothing
    std::vector<float> ms;
    const double       total = gp::pass_times_ms(ts, 3, 64, 1000.0f, ms);
    REQUIRE(ms.size() == 3);
    REQUIRE_THAT(ms[0], WithinAbs(2.0, 1e-6));
    REQUIRE_THAT(ms[1], WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(ms[2], WithinAbs(3.0, 1e-6));
    REQUIRE_THAT(total, WithinAbs(5.0, 1e-6));
    REQUIRE_THAT(static_cast<double>(ms[0] + ms[1] + ms[2]), WithinAbs(total, 1e-5));
}

TEST_CASE("pass_times_ms handles a wrap in the middle of a frame", "[gpuprof]") {
    const u64          wrap = u64{1} << 36;
    const u64          ts[] = {wrap - 500, wrap - 100, 300};  // 400 ticks, then 400 ticks
    std::vector<float> ms;
    const double       total = gp::pass_times_ms(ts, 2, 36, 1000.0f, ms);  // 1 us/tick
    REQUIRE_THAT(ms[0], WithinAbs(0.4, 1e-6));
    REQUIRE_THAT(ms[1], WithinAbs(0.4, 1e-6));
    REQUIRE_THAT(total, WithinAbs(0.8, 1e-6));
}

TEST_CASE("smooth_ms seeds with the first sample then eases toward new ones", "[gpuprof]") {
    REQUIRE(gp::smooth_ms(0.0f, 4.0f, true, 6.0f, 0.016f) == 4.0f);
    const float next = gp::smooth_ms(4.0f, 8.0f, false, 6.0f, 0.016f);
    REQUIRE(next > 4.0f);
    REQUIRE(next < 8.0f);
    // Converges: many steps land on the target.
    float v = 4.0f;
    for (int i = 0; i < 600; ++i) v = gp::smooth_ms(v, 8.0f, false, 6.0f, 0.016f);
    REQUIRE_THAT(v, WithinAbs(8.0, 1e-3));
}
