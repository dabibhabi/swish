#pragma once
// ImageRegistry: the named images passes exchange. `enum class Img` is the typo-safe name, the registry maps it to
// this frame-in-flight's VkImage + VkImageView. Owners (PostProcessManager, VelocityPass) REGISTER what they create
// (again after every swapchain recreate); consumers look up by name instead of taking 4 different view arrays.
//
// Not a render graph: no lifetimes, no automatic barriers. Barriers stay eager and hand-placed in the pass that
// owns the transition (see IRenderPass.h).

#include "renderer/RendererConfig.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace swish {

// Add a name here when a second pass needs an image another pass owns.
enum class Img : uint8_t {
    Hdr,       // R16G16B16A16F scene colour, per frame in flight
    HdrDepth,  // scene depth (D32F), per frame in flight
    GbAlbedo,  // G-buffer, per frame
    GbNormal,
    GbMaterial,
    GbClearcoat,  // clearcoat + flake MRT
    Ao,           // raw SSAO (half res, debug builds write it; release keeps it primed white)
    AoBlur,       // blurred AO the composite samples
    BloomExtract,
    BloomBlurH,
    BloomBlurV,
    Ssr,       // screen-space reflections the composite adds
    Godrays,   // god-ray shafts the composite adds
    Lum,       // auto-exposure luminance pyramid (image only, no view)
    Velocity,  // per-object velocity (VelocityPass, debug builds)
    kCount
};

inline constexpr std::size_t kImgCount = static_cast<std::size_t>(Img::kCount);

class ImageRegistry {
public:
    // Same image in every frame slot (bloom/AO/SSR/god-rays are single images shared by the frames in flight).
    void set_shared(Img id, VkImage image, VkImageView view = VK_NULL_HANDLE);
    void set(Img id, uint32_t frame, VkImage image, VkImageView view);

    VkImage     image(Img id, uint32_t frame) const { return m_slots[idx(id)][frame].image; }
    VkImageView view(Img id, uint32_t frame) const { return m_slots[idx(id)][frame].view; }

    // The per-frame views of `id` (what descriptor writes and framebuffers take).
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> views(Img id) const;

    bool has(Img id) const {
        return m_slots[idx(id)][0].image != VK_NULL_HANDLE || m_slots[idx(id)][0].view != VK_NULL_HANDLE;
    }

private:
    struct Slot {
        VkImage     image = VK_NULL_HANDLE;
        VkImageView view  = VK_NULL_HANDLE;
    };
    static std::size_t idx(Img id) { return static_cast<std::size_t>(id); }

    std::array<std::array<Slot, MAX_FRAMES_IN_FLIGHT>, kImgCount> m_slots{};
};

}  // namespace swish
