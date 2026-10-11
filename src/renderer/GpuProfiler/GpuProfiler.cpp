#ifdef SWISH_DEBUG_UI

#include "GpuProfiler.h"

#include "renderer/GpuProfiler/TimestampMath.h"
#include "utils/vulkan/Check.h"

#include <iostream>

namespace swish {

namespace {
constexpr float kSmoothRate = 4.0f;  // 1/s: ~250 ms time constant for the displayed numbers
}

void GpuProfiler::init(const RendererServices& svc, u32 graphicsQueueFamily, const PassRegistry& passes) {
    cleanup();
    m_device    = svc.device;
    m_sync2     = svc.synchronization2;
    m_passCount = static_cast<u32>(passes.size());

    m_snap = {};
    for (std::string_view n : passes.names())
        m_snap.names.emplace_back(n);
    m_snap.ms.assign(m_passCount, 0.0f);
    m_snap.smoothedMs.assign(m_passCount, 0.0f);

    // Support check: the graphics queue family must have valid timestamp bits (timestampComputeAndGraphics == true
    // guarantees it for every graphics/compute family; when false only a non-zero timestampValidBits counts).
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(svc.physicalDevice, &props);
    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(svc.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(svc.physicalDevice, &familyCount, families.data());

    m_validBits = graphicsQueueFamily < familyCount ? families[graphicsQueueFamily].timestampValidBits : 0;
    m_periodNs  = props.limits.timestampPeriod;

    if (m_validBits == 0 || m_periodNs <= 0.0f || m_passCount == 0) {
        std::cerr << "[GpuProfiler] GPU timestamps unavailable (validBits=" << m_validBits << " period=" << m_periodNs
                  << " ns, timestampComputeAndGraphics=" << (props.limits.timestampComputeAndGraphics ? "yes" : "no")
                  << "); per-pass timing disabled\n";
        return;
    }

    VkQueryPoolCreateInfo qi{};
    qi.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = gpuprof::pool_query_count(MAX_FRAMES_IN_FLIGHT, m_passCount);
    VK_CHECK(vkCreateQueryPool(m_device, &qi, nullptr, &m_pool));

    m_raw.assign(gpuprof::boundary_count(m_passCount), 0);
    m_pending.fill(false);
    m_snap.supported = true;
    std::cerr << "[GpuProfiler] " << m_passCount << " passes, " << qi.queryCount << " timestamp queries, " << m_periodNs
              << " ns/tick, " << m_validBits << " valid bits, "
              << (m_sync2 ? "vkCmdWriteTimestamp2" : "vkCmdWriteTimestamp") << "\n";
}

void GpuProfiler::cleanup() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_snap.supported = false;
}

void GpuProfiler::write_boundary(VkCommandBuffer cmd, u32 slot, u32 boundary) const {
    const u32 q = gpuprof::query_index(slot, m_passCount, boundary);
    if (m_sync2)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_pool, q);
    else
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, q);
}

void GpuProfiler::on_record_begin(VkCommandBuffer cmd, u32 frameIndex) {
    if (m_pool == VK_NULL_HANDLE)
        return;
    const u32 slot = gpuprof::ring_slot(frameIndex, MAX_FRAMES_IN_FLIGHT);
    vkCmdResetQueryPool(cmd, m_pool, gpuprof::slot_base(slot, m_passCount), gpuprof::boundary_count(m_passCount));
    write_boundary(cmd, slot, 0);
    m_pending[slot] = true;
}

void GpuProfiler::on_pass_recorded(VkCommandBuffer cmd, u32 frameIndex, usize passIndex) {
    if (m_pool == VK_NULL_HANDLE || passIndex >= m_passCount)
        return;
    write_boundary(cmd, gpuprof::ring_slot(frameIndex, MAX_FRAMES_IN_FLIGHT), static_cast<u32>(passIndex) + 1);
}

void GpuProfiler::collect(u32 frameSlot, float dt) {
    if (m_pool == VK_NULL_HANDLE)
        return;
    const u32 slot = gpuprof::ring_slot(frameSlot, MAX_FRAMES_IN_FLIGHT);
    if (!m_pending[slot])
        return;  // first frames, or the slot was not recorded (early-out frame)

    const u32 count = gpuprof::boundary_count(m_passCount);
    // No VK_QUERY_RESULT_WAIT_BIT: the slot's fence is already signalled, so this normally succeeds; if the driver
    // still reports NOT_READY we skip this read-back instead of stalling and try again next time.
    const VkResult r = vkGetQueryPoolResults(m_device, m_pool, gpuprof::slot_base(slot, m_passCount), count,
                                             count * sizeof(u64), m_raw.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT);
    if (r == VK_NOT_READY)
        return;
    VK_CHECK(r);
    m_pending[slot] = false;

    std::vector<float> ms;
    const double       total = gpuprof::pass_times_ms(m_raw.data(), m_passCount, m_validBits, m_periodNs, ms);
    const bool         first = m_snap.frames == 0;
    for (u32 i = 0; i < m_passCount; ++i) {
        m_snap.smoothedMs[i] = gpuprof::smooth_ms(m_snap.smoothedMs[i], ms[i], first, kSmoothRate, dt);
        m_snap.ms[i]         = ms[i];
    }
    m_snap.totalMs         = static_cast<float>(total);
    m_snap.smoothedTotalMs = gpuprof::smooth_ms(m_snap.smoothedTotalMs, m_snap.totalMs, first, kSmoothRate, dt);
    ++m_snap.frames;
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
