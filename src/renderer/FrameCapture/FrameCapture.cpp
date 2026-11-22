#ifdef SWISH_DEBUG_UI

#include "FrameCapture.h"

#include "renderer/FrameCapture/CapturePath.h"
#include "utils/vulkan/Barrier.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace swish {

namespace {

constexpr const char* kDefaultDir = "build-debug/captures";

std::string capture_dir() {
    const char* env = std::getenv("SWISH_CAPTURE_DIR");
    return (env && *env) ? std::string(env) : std::string(kDefaultDir);
}

bool write_file(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

// How a swapchain format is stored. The preferred swapchain format is R16G16B16A16_SFLOAT (extended sRGB linear), the
// 8-bit ones are the fallbacks.
enum class SwapKind { Bgra8, Rgba8, Rgba16F, Unsupported };

SwapKind swap_kind_for(VkFormat f) {
    switch (f) {
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return SwapKind::Bgra8;
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
            return SwapKind::Rgba8;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return SwapKind::Rgba16F;
        default:
            return SwapKind::Unsupported;
    }
}

VkDeviceSize bytes_per_pixel(SwapKind k) { return k == SwapKind::Rgba16F ? 8 : 4; }

}  // namespace

void FrameCapture::init(VmaAllocator allocator) { m_allocator = allocator; }

void FrameCapture::cleanup() {
    m_ldrBuffer.reset();
    m_hdrBuffer.reset();
    m_status.state = CaptureStatus::State::Idle;
}

void FrameCapture::request(bool hdr) {
    if (busy()) return;
    m_wantHdr        = hdr;
    m_status.state   = CaptureStatus::State::Requested;
    m_status.message = hdr ? "capture requested (LDR + HDR)" : "capture requested";
}

GpuBuffer FrameCapture::make_readback_buffer(VkDeviceSize size) const {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    return GpuBuffer(m_allocator, bi, ai);
}

void FrameCapture::record(VkCommandBuffer cmd, u32 frameIndex, VkImage swapImage, VkFormat swapFormat,
                          VkExtent2D swapExtent, bool swapHasTransferSrc, VkImage hdrImage, VkExtent2D hdrExtent) {
    if (m_status.state != CaptureStatus::State::Requested) return;

    const SwapKind kind = swap_kind_for(swapFormat);
    if (!swapHasTransferSrc || kind == SwapKind::Unsupported || swapExtent.width == 0 || swapExtent.height == 0) {
        m_status.state   = CaptureStatus::State::Failed;
        m_status.message = swapHasTransferSrc ? "unsupported swapchain format for capture"
                                              : "swapchain was created without TRANSFER_SRC usage";
        return;
    }

    m_ldrBuffer =
        make_readback_buffer(static_cast<VkDeviceSize>(swapExtent.width) * swapExtent.height * bytes_per_pixel(kind));
    m_hasHdr = m_wantHdr && hdrImage != VK_NULL_HANDLE && hdrExtent.width > 0 && hdrExtent.height > 0;
    if (m_hasHdr) m_hdrBuffer = make_readback_buffer(static_cast<VkDeviceSize>(hdrExtent.width) * hdrExtent.height * 8);
    if (!m_ldrBuffer || (m_hasHdr && !m_hdrBuffer) || !m_ldrBuffer.mapped()) {
        m_ldrBuffer.reset();
        m_hdrBuffer.reset();
        m_status.state   = CaptureStatus::State::Failed;
        m_status.message = "could not allocate the host-visible staging buffer";
        return;
    }

    constexpr VkImageAspectFlags kColor = VK_IMAGE_ASPECT_COLOR_BIT;

    // Swapchain: after the last pass it sits in PRESENT_SRC (the table has no PRESENT_SRC row, so explicit masks).
    vkx::BarrierMasks toSrc{true, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
    vkx::BarrierMasks toPresent{true, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                VK_ACCESS_2_TRANSFER_READ_BIT, 0};
    vkx::image_barrier_with(cmd, swapImage, kColor, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, toSrc);
    VkBufferImageCopy region{};  // bufferRowLength/ImageHeight 0 = tightly packed
    region.imageSubresource = {kColor, 0, 0, 1};
    region.imageExtent      = {swapExtent.width, swapExtent.height, 1};
    vkCmdCopyImageToBuffer(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_ldrBuffer.handle(), 1, &region);
    vkx::image_barrier_with(cmd, swapImage, kColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, toPresent);

    if (m_hasHdr) {
        // HdrFinalizePass leaves Img::Hdr in SHADER_READ_ONLY; both table rows exist.
        vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
            cmd, hdrImage, kColor);
        VkBufferImageCopy hr{};
        hr.imageSubresource = {kColor, 0, 0, 1};
        hr.imageExtent      = {hdrExtent.width, hdrExtent.height, 1};
        vkCmdCopyImageToBuffer(cmd, hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_hdrBuffer.handle(), 1, &hr);
        vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            cmd, hdrImage, kColor);
        m_hdrExtent = hdrExtent;
    }

    m_slot           = frameIndex;
    m_swapFormat     = swapFormat;
    m_swapExtent     = swapExtent;
    m_status.state   = CaptureStatus::State::InFlight;
    m_status.message = "capture copied, waiting for the GPU";
}

void FrameCapture::collect(u32 frameIndex) {
    // The fence of `frameIndex` was just waited: if that slot recorded the copy, it is complete and visible.
    if (m_status.state != CaptureStatus::State::InFlight || frameIndex != m_slot) return;

    auto fail = [&](const std::string& why) {
        m_status.state   = CaptureStatus::State::Failed;
        m_status.message = why;
        std::cerr << "[FrameCapture] " << why << "\n";
        m_ldrBuffer.reset();
        m_hdrBuffer.reset();
    };

    const SwapKind kind = swap_kind_for(m_swapFormat);
    if (kind == SwapKind::Unsupported) return fail("unsupported swapchain format");

    // Name from the wall clock (local time); same-millisecond requests get a counter suffix.
    const i64 nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::time_t secs = static_cast<std::time_t>(nowMs / 1000);
    std::tm     lt{};
    localtime_r(&secs, &lt);
    const capture::Stamp stamp = capture::stamp_from_unix_ms(nowMs, static_cast<i32>(lt.tm_gmtoff / 60));
    m_sameMsCount              = (nowMs == m_lastStampMs) ? m_sameMsCount + 1 : 0;
    m_lastStampMs              = nowMs;

    const std::string dir = capture_dir();
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return fail("cannot create " + dir + ": " + ec.message());

    vmaInvalidateAllocation(m_allocator, m_ldrBuffer.allocation(), 0, VK_WHOLE_SIZE);
    const std::size_t pixels = static_cast<std::size_t>(m_swapExtent.width) * m_swapExtent.height;
    std::vector<u8>   ppm;
    if (kind == SwapKind::Rgba16F) {
        const auto rgba8 = capture::rgba16f_linear_to_rgba8_srgb(static_cast<const u16*>(m_ldrBuffer.mapped()), pixels);
        ppm = capture::encode_ppm(rgba8.data(), m_swapExtent.width, m_swapExtent.height, capture::PixelOrder::Rgba);
    } else {
        ppm = capture::encode_ppm(static_cast<const u8*>(m_ldrBuffer.mapped()), m_swapExtent.width, m_swapExtent.height,
                                  kind == SwapKind::Bgra8 ? capture::PixelOrder::Bgra : capture::PixelOrder::Rgba);
    }
    const std::string ppmPath = capture::full_path(dir, stamp, m_sameMsCount, capture::FileKind::Ppm);
    if (!write_file(ppmPath, ppm)) return fail("cannot write " + ppmPath);

    if (m_hasHdr) {
        vmaInvalidateAllocation(m_allocator, m_hdrBuffer.allocation(), 0, VK_WHOLE_SIZE);
        const auto texels = capture::rgba16f_to_float(static_cast<const u16*>(m_hdrBuffer.mapped()),
                                                      static_cast<std::size_t>(m_hdrExtent.width) * m_hdrExtent.height);
        const auto pfm    = capture::encode_pfm(texels.data(), m_hdrExtent.width, m_hdrExtent.height, 4);
        const std::string pfmPath = capture::full_path(dir, stamp, m_sameMsCount, capture::FileKind::Pfm);
        if (!write_file(pfmPath, pfm)) return fail("cannot write " + pfmPath);
        m_status.lastHdrPath = pfmPath;
    }

    m_ldrBuffer.reset();
    m_hdrBuffer.reset();
    m_status.lastPath = ppmPath;
    m_status.state    = CaptureStatus::State::Saved;
    ++m_status.savedCount;
    m_status.message = "saved " + ppmPath;
    std::cerr << "[FrameCapture] " << m_status.message << "\n";
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
