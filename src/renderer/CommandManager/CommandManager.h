#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Manages the command pool and per-frame command buffers.
// One pool tied to the graphics queue family.
// One command buffer per frame-in-flight (so you can record N+1 while GPU
// executes N).
class CommandManager {
public:
    CommandManager()  = default;
    ~CommandManager() = default;

    // Creates the command pool + allocate MAX_FRAMES_IN_FLIGHT command
    // buffers. The pool needs the graphics queue family index (from
    // Device::findQueueFamilies). Use
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT so buffers can be
    // re-recorded.
    void init(VkDevice device, uint32_t graphicsQueueFamily, uint32_t bufferCount);

    // Destroys command pool (buffers are freed automatically with the pool)
    void cleanup(VkDevice device);

    // Returns the command buffer for the given frame index
    VkCommandBuffer getBuffer(uint32_t frameIndex) const;

    // Resets a command buffer so it can be re-recorded
    void resetBuffer(uint32_t frameIndex);

    // Begins recording commands into a buffer
    void beginRecording(uint32_t frameIndex);

    // Ends recording
    void endRecording(uint32_t frameIndex);

    VkCommandPool getPool() const;

private:
    VkCommandPool                m_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_buffers;
};

}  // namespace swish
