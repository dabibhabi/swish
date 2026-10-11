#pragma once
// FrameCapture: dump the finished frame to disk (debug builds only, SWISH_DEBUG_UI).
//
// Flow (one capture at a time):
//   request(hdr)            UI / key sets a flag (cheap, any time).
//   record(...)             Renderer, after PassRegistry::record_all in frame F: the LDR swapchain image
//                           (PRESENT_SRC -> TRANSFER_SRC -> PRESENT_SRC) is copied with vkCmdCopyImageToBuffer into a
//                           host-visible VMA staging buffer; with hdr==true the scene-colour Img::Hdr (RGBA16F,
//                           SHADER_READ_ONLY at end of frame) is copied too. The swapchain image already contains
//                           the debug overlay (it is the last pass), so the PPM shows exactly what is on screen.
//   collect(frameSlot)      Renderer, right after the fence wait of that slot (frame F + MAX_FRAMES_IN_FLIGHT): the
//                           copy is guaranteed complete, so the pixels are converted and written to
//                           build-debug/captures/ (override: env SWISH_CAPTURE_DIR). Latency is 2 frames, and the
//                           file write is synchronous on the render thread (a one-frame hitch is expected).
//   status() / last_path()  plain accessors for the UI.
// The swapchain is normally R16G16B16A16_SFLOAT (extended sRGB linear); the PPM is clamped to [0,1] and sRGB-encoded
// from it (8-bit swapchain formats are copied as they are).
// Files: swish_YYYYMMDD_HHMMSS_mmm.ppm (8-bit LDR, sRGB-encoded as shown) and, for an HDR request, a .pfm with the same
// stem (linear float32 RGB at the SSAA render extent). Names and encoders are the pure code in CapturePath.h.
// The swapchain needs TRANSFER_SRC usage, which Swapchain adds in debug builds only (release is unchanged).
//
// TODO(stub): the full version must (a) write PNG (needs a deflate encoder) and a tone-mapped HDR preview, (b) move
// the file write to a worker thread, (c) support burst / timed capture and an offscreen "no overlay" capture taken
// before DebugOverlayPass, (d) capture other named images (G-buffer, AO) by Img, (e) re-capture the same frame at a
// frozen camera for regression diffs.

#ifdef SWISH_DEBUG_UI

#include "renderer/GpuResource/GpuResource.h"
#include "renderer/RendererConfig.h"
#include "utils/types/StdTypes.h"

#include <vulkan/vulkan.h>

#include <string>

namespace swish {

struct CaptureStatus {
    enum class State { Idle, Requested, InFlight, Saved, Failed };
    State       state = State::Idle;
    std::string lastPath;     // newest LDR file written ("" until the first save)
    std::string lastHdrPath;  // newest HDR file written
    std::string message;      // human-readable detail (error text when Failed)
    u32         savedCount = 0;
};

class FrameCapture {
public:
    void init(VmaAllocator allocator);
    void cleanup();  // before the allocator is destroyed

    // Ask for a capture of the next recorded frame. Ignored while one is already requested / in flight.
    void request(bool hdr);
    bool busy() const {
        return m_status.state == CaptureStatus::State::Requested || m_status.state == CaptureStatus::State::InFlight;
    }

    const CaptureStatus& status() const { return m_status; }
    const std::string&   last_path() const { return m_status.lastPath; }

    // Renderer hooks. record(): no-op unless a capture is Requested. hdrImage may be VK_NULL_HANDLE.
    void record(VkCommandBuffer cmd, u32 frameIndex, VkImage swapImage, VkFormat swapFormat, VkExtent2D swapExtent,
                bool swapHasTransferSrc, VkImage hdrImage, VkExtent2D hdrExtent);
    void collect(u32 frameIndex);

private:
    GpuBuffer make_readback_buffer(VkDeviceSize size) const;

    VmaAllocator m_allocator = nullptr;

    bool       m_wantHdr    = false;
    bool       m_hasHdr     = false;  // the in-flight capture includes an HDR copy
    u32        m_slot       = 0;      // frame slot that recorded the copy
    VkFormat   m_swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapExtent = {0, 0};
    VkExtent2D m_hdrExtent  = {0, 0};
    GpuBuffer  m_ldrBuffer;
    GpuBuffer  m_hdrBuffer;
    i64        m_lastStampMs = -1;  // same-millisecond collisions get a counter suffix
    u32        m_sameMsCount = 0;

    CaptureStatus m_status;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
