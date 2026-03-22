#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

// Describes WHAT you render to: which attachments (color, depth),
// how they're loaded/stored, and their layout transitions.
// For the boilerplate you need ONE render pass with 1 color + 1 depth
// attachment.
class RenderPass {
public:
    RenderPass()  = default;
    ~RenderPass() = default;

    // Creates the render pass.
    // Needs: color attachment format (from Swapchain::getImageFormat())
    //        depth attachment format (find via Device — typically D32_SFLOAT)
    //
    // The render pass describes:
    //   - Color attachment: loadOp=CLEAR, storeOp=STORE,
    //     initialLayout=UNDEFINED, finalLayout=PRESENT_SRC_KHR
    //   - Depth attachment: loadOp=CLEAR, storeOp=DONT_CARE,
    //     initialLayout=UNDEFINED, finalLayout=DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    //   - One subpass referencing both attachments
    //   - Subpass dependency for synchronization
    void init(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);

    // Creates one framebuffer per swap chain image.
    // Each framebuffer binds: swap image view + shared depth view → this render
    // pass.
    void createFramebuffers(VkDevice device, const std::vector<VkImageView>& swapImageViews, VkImageView depthImageView,
                            VkExtent2D extent);

    // Destroys framebuffers and render pass
    void cleanup(VkDevice device);

    VkRenderPass                      getRenderPass() const;
    const std::vector<VkFramebuffer>& getFramebuffers() const;

private:
    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};

}  // namespace swish
