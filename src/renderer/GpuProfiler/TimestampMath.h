#pragma once

// ── TimestampMath.h ──────────────────────────────────────────────────
// Purpose   : the Vulkan-free arithmetic behind the GPU timestamp profiler (GpuProfiler.h): tick masking and
//             wraparound, ticks -> milliseconds, query-slot indexing for the per-frame ring, the readback latency,
//             per-pass time extraction and smoothing. Header-only so it is unit-tested without a GPU.
// Units     : timestamps are raw GPU ticks; `periodNs` is VkPhysicalDeviceLimits::timestampPeriod (nanoseconds
//             PER TICK); results are milliseconds. Smoothing rate k is 1/seconds, dt seconds.
// Layout    : BOUNDARY scheme, N passes -> N+1 timestamps per frame. Boundary i is written just before pass i runs
//             (boundary 0 = frame start) and boundary N after the last pass. Pass i costs ts[i+1] - ts[i]; the
//             total is ts[N] - ts[0] and equals the sum of the passes exactly (no gaps). A pass that records
//             nothing yields ~0 ms for free. Cost: N+1 queries instead of 2N, and no per-pass begin/end pair.
//             Caveat: GPU work from adjacent passes may overlap, so a time is "attributed to the pass whose
//             boundaries bracket it", not an isolated cost.
// Ring      : the profiler keeps one slice of N+1 queries per frame in flight (ring size = MAX_FRAMES_IN_FLIGHT).
//             Slot s = frame % ring. The CPU waits on that slot's fence at the top of drawFrame, so by then the
//             slot's previous frame (frame - ring, i.e. N-2 with 2 frames in flight) has finished and its results
//             are read just before the slot is reset and re-recorded. Latency is therefore exactly `ring` frames.
// Precision : tick deltas are exact in u64; conversion is done in double and narrowed to float at the end.
// See also  : GpuProfiler.h, utils/math/Smoothing.h
// Test      : tests/utils/test_renderer_TimestampMath.cpp

#include "utils/math/Smoothing.h"
#include "utils/types/StdTypes.h"

#include <vector>

namespace swish::gpuprof {

// Mask selecting the valid low bits of a timestamp (VkQueueFamilyProperties::timestampValidBits, 0 = unsupported).
// 64 or more bits means every bit is valid.
constexpr u64 valid_mask(u32 validBits) {
    if (validBits == 0) return 0;
    if (validBits >= 64) return ~u64{0};
    return (u64{1} << validBits) - 1;
}

// Elapsed ticks between two timestamps of a counter that is only `validBits` wide. The counter wraps, so the
// difference is taken modulo 2^validBits: a begin near the top and an end after the wrap still gives the true span.
constexpr u64 elapsed_ticks(u64 begin, u64 end, u32 validBits) {
    const u64 m = valid_mask(validBits);
    return ((end & m) - (begin & m)) & m;
}

// Ticks -> milliseconds. periodNs is nanoseconds per tick (timestampPeriod).
constexpr double ticks_to_ms(u64 ticks, float periodNs) {
    return static_cast<double>(ticks) * static_cast<double>(periodNs) * 1.0e-6;
}

// ── slot indexing ─────────────────────────────────────────────────────
constexpr u32 boundary_count(u32 passCount) { return passCount + 1; }
constexpr u32 ring_slot(u64 frame, u32 ringSize) { return ringSize == 0 ? 0 : static_cast<u32>(frame % ringSize); }
// First query index of `slot`'s slice in the pool (pool size = ringSize * boundary_count).
constexpr u32 slot_base(u32 slot, u32 passCount) { return slot * boundary_count(passCount); }
// Query index of boundary `b` (0..passCount) of `slot`.
constexpr u32 query_index(u32 slot, u32 passCount, u32 b) { return slot_base(slot, passCount) + b; }
constexpr u32 pool_query_count(u32 ringSize, u32 passCount) { return ringSize * boundary_count(passCount); }
// Frames between recording a slot and reading it back (see "Ring" above).
constexpr u32 readback_latency_frames(u32 ringSize) { return ringSize; }

// Per-pass milliseconds from N+1 boundary timestamps. `ms` is resized to passCount; returns the total in ms
// (boundary N minus boundary 0). `ts` must hold passCount + 1 values.
inline double pass_times_ms(const u64* ts, u32 passCount, u32 validBits, float periodNs, std::vector<float>& ms) {
    ms.assign(passCount, 0.0f);
    for (u32 i = 0; i < passCount; ++i)
        ms[i] = static_cast<float>(ticks_to_ms(elapsed_ticks(ts[i], ts[i + 1], validBits), periodNs));
    return ticks_to_ms(elapsed_ticks(ts[0], ts[passCount], validBits), periodNs);
}

// Exponential smoothing of one displayed value. The first sample seeds the filter (no ramp from zero).
inline float smooth_ms(float previous, float sample, bool first, float k, float dt) {
    if (first) return sample;
    return util::math::exp_smooth(previous, sample, k, dt);
}

}  // namespace swish::gpuprof
