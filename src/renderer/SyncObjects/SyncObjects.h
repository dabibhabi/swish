#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Per-frame CPU/GPU synchronization primitives.
//
// Two sizing domains, on purpose:
//   imageAvailable, inFlightFences  — one per frame-in-flight slot. They track
//                                     CPU-side command-buffer reuse and the
//                                     acquire→submit handoff that happens
//                                     before we know the swapchain image.
//   renderFinished                  — one per swapchain image, indexed by the
//                                     imageIndex returned from
//                                     vkAcquireNextImageKHR.
//
// renderFinished must be indexed by image, not by frame-in-flight slot, to
// satisfy VUID-vkQueueSubmit-pSignalSemaphores-00067. A binary semaphore
// waited on by vkQueuePresentKHR cannot be re-signaled until the presentation
// engine has consumed it, and the engine can hold an image across more than
// one CPU frame (MAILBOX in particular). Indexing by image ties the
// semaphore's lifetime to the resource being waited on.
// See https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
class SyncObjects {
public:
    SyncObjects()  = default;
    ~SyncObjects() = default;

    // Creates framesInFlight (imageAvailable semaphore + inFlight fence) pairs
    // and swapchainImageCount renderFinished semaphores. Fences start signaled
    // so the first waitForFence returns immediately.
    void init(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount);

    // Destroys all semaphores and fences. Caller must have synchronized GPU
    // work (vkDeviceWaitIdle) first.
    void cleanup(VkDevice device);

    // Rebuilds the per-image renderFinished semaphore pool. Call from the
    // swapchain-recreate path: the image count may differ, and the Vulkan spec
    // allows binary semaphores to remain signaled when vkQueuePresentKHR
    // returns an error, so a recreate (not just a resize) is what guarantees
    // unsignaled state. Caller must vkDeviceWaitIdle first.
    void recreateRenderFinishedSemaphores(VkDevice device, uint32_t swapchainImageCount);

    void waitForFence(VkDevice device, uint32_t frameIndex);
    void resetFence(VkDevice device, uint32_t frameIndex);

    VkSemaphore getImageAvailable(uint32_t frameIndex) const;
    VkSemaphore getRenderFinished(uint32_t imageIndex) const;
    VkFence     getInFlightFence(uint32_t frameIndex) const;

private:
    std::vector<VkSemaphore> m_imageAvailable;  // indexed by frame-in-flight slot
    std::vector<VkSemaphore> m_renderFinished;  // indexed by swapchain image index
    std::vector<VkFence>     m_inFlightFences;  // indexed by frame-in-flight slot
};

}  // namespace swish
