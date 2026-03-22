#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace swish {

class Device;
struct SwapChainSupportDetails;

// Manages the swap chain, image views, and provides format/extent info.
// When the window resizes, this entire class gets destroyed and recreated.
// That's why it's its own class — clean destroy + recreate cycle.
class Swapchain {
public:
    Swapchain()  = default;
    ~Swapchain() = default;

    // Creates the swap chain and image views.
    // Needs Device (for logical device + physical device) and surface.
    //
    // Steps:
    //   1. Query swap chain support (via Device::querySwapChainSupport)
    //   2. Choose surface format — prefer VK_FORMAT_B8G8R8A8_SRGB
    //   3. Choose present mode — prefer MAILBOX (triple buffer), fallback FIFO
    //   4. Choose extent — match the framebuffer size (careful on Retina!)
    //   5. Choose image count — min + 1, clamped to max
    //   6. vkCreateSwapchainKHR
    //   7. Retrieve swap images with vkGetSwapchainImagesKHR
    //   8. Create one VkImageView per swap image
    void init(Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height);

    // Destroys image views and the swap chain
    void cleanup(VkDevice device);

    // ── Getters ────────────────────────────────────────────────────
    VkSwapchainKHR                  getSwapchain() const;
    VkFormat                        getImageFormat() const;
    VkExtent2D                      getExtent() const;
    const std::vector<VkImage>&     getImages() const;
    const std::vector<VkImageView>& getImageViews() const;
    uint32_t                        getImageCount() const;

private:
    VkSwapchainKHR           m_swapchain   = VK_NULL_HANDLE;
    VkFormat                 m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent      = {0, 0};
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;

    // Picks the best surface format from available options
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) const;

    // Picks the best present mode (MAILBOX > FIFO)
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& available) const;

    // Determines swap extent (uses framebuffer size, not window size)
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) const;
};

}  // namespace swish
