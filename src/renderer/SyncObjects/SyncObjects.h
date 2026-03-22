#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Per-frame synchronization objects.
// Each frame-in-flight needs its own set so CPU and GPU don't step on each
// other.
//
// The three primitives:
//   imageAvailableSemaphore — GPU→GPU: "swap image is ready to render to"
//   renderFinishedSemaphore — GPU→GPU: "rendering is done, safe to present"
//   inFlightFence           — GPU→CPU: "safe to reuse this frame's command
//   buffer"
class SyncObjects {
public:
    SyncObjects()  = default;
    ~SyncObjects() = default;

    // Creates MAX_FRAMES_IN_FLIGHT sets of (2 semaphores + 1 fence).
    // Fences should start SIGNALED (so the first frame doesn't wait forever).
    void init(VkDevice device, uint32_t framesInFlight);

    // Destroys all semaphores and fences
    void cleanup(VkDevice device);

    // Waits for the given frame's fence (blocks CPU until GPU finishes that
    // frame)
    void waitForFence(VkDevice device, uint32_t frameIndex);

    // Resets the fence after waiting (so it can be signaled again)
    void resetFence(VkDevice device, uint32_t frameIndex);

    void recreateRenderFinishedSemaphore(VkDevice device, uint32_t frameIndex);

    VkSemaphore getImageAvailable(uint32_t frameIndex) const;
    VkSemaphore getRenderFinished(uint32_t frameIndex) const;
    VkFence     getInFlightFence(uint32_t frameIndex) const;

private:
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence>     m_inFlightFences;
};

}  // namespace swish
