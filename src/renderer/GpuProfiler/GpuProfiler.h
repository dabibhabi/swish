#pragma once
// GpuProfiler: per-pass GPU timing from timestamp queries (debug builds only, SWISH_DEBUG_UI).
//
// How it works (arithmetic and layout rationale: TimestampMath.h):
//  - One VkQueryPool of MAX_FRAMES_IN_FLIGHT * (passCount + 1) timestamps. PassRegistry::record_all notifies this
//    class (IPassObserver) before the first pass and after every pass; boundary i is written before pass i, so pass
//    i costs ts[i+1] - ts[i] and an empty pass costs ~0.
//  - Boundary writes use vkCmdWriteTimestamp2 (ALL_COMMANDS) when synchronization2 is enabled, else vkCmdWriteTimestamp
//    (BOTTOM_OF_PIPE). The frame's slice is reset with vkCmdResetQueryPool at record start.
//  - collect() runs right after the frame-slot fence wait and reads the slot's PREVIOUS frame (N-2 with 2 frames in
//    flight) with vkGetQueryPoolResults and NO wait flag; VK_NOT_READY just skips (keeps the slot pending).
//  - Degrades gracefully: if the graphics queue family reports timestampValidBits == 0 (or timestampPeriod <= 0)
//    `supported` stays false, no pool is created, no queries are issued, and one line is logged.
//
// UI contract: read snapshot() (plain struct, updated in collect()); the debug-ui agent draws it.
//
// TODO(stub): the full version must (a) split a pass that records several sub-workloads (G-buffer vs shadow atlas)
// with nested labels, (b) report GPU-vs-CPU frame time and flag when the GPU is the bottleneck, (c) keep a rolling
// min/max/p95 history for a graph, (d) optionally add pipeline-statistics queries, (e) validate the MoltenVK
// timestamp counter against a CPU clock (Metal may return coarse, command-buffer granular values).

#ifdef SWISH_DEBUG_UI

#include "renderer/Passes/PassRegistry.h"
#include "renderer/Renderer/RendererServices.h"
#include "renderer/RendererConfig.h"
#include "utils/types/StdTypes.h"

#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <vector>

namespace swish {

// What the UI reads. Index i of names / ms / smoothedMs is registry pass i (PassList.def order).
struct GpuProfileSnapshot {
    bool                     supported = false;  // false: timestamps unusable on this device/queue, ms stay empty
    std::vector<std::string> names;
    std::vector<float>       ms;          // most recent frame that has been read back
    std::vector<float>       smoothedMs;  // exponentially smoothed
    float                    totalMs         = 0.0f;
    float                    smoothedTotalMs = 0.0f;
    u32                      frames          = 0;  // read-backs completed so far (0 = nothing to show yet)
};

class GpuProfiler final : public IPassObserver {
public:
    GpuProfiler() = default;
    ~GpuProfiler() override { cleanup(); }
    GpuProfiler(const GpuProfiler&)            = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    // `passes` must already be fully built (size() fixes the pool size). Safe to call when timestamps are
    // unsupported (then supported() is false).
    void init(const RendererServices& svc, u32 graphicsQueueFamily, const PassRegistry& passes);
    void cleanup();

    // Call after the frame slot's fence wait, before that slot is re-recorded. dt = frame delta seconds.
    void collect(u32 frameSlot, float dt);

    const GpuProfileSnapshot& snapshot() const { return m_snap; }
    bool                      supported() const { return m_snap.supported; }

    // IPassObserver
    void on_record_begin(VkCommandBuffer cmd, u32 frameIndex) override;
    void on_pass_recorded(VkCommandBuffer cmd, u32 frameIndex, usize passIndex) override;

private:
    void write_boundary(VkCommandBuffer cmd, u32 slot, u32 boundary) const;

    VkDevice    m_device    = VK_NULL_HANDLE;
    VkQueryPool m_pool      = VK_NULL_HANDLE;
    u32         m_passCount = 0;
    u32         m_validBits = 0;
    float       m_periodNs  = 0.0f;  // nanoseconds per tick
    bool        m_sync2     = false;

    std::array<bool, MAX_FRAMES_IN_FLIGHT> m_pending{};  // slot has recorded queries not yet read back
    std::vector<u64>                       m_raw;        // scratch for vkGetQueryPoolResults

    GpuProfileSnapshot m_snap;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
