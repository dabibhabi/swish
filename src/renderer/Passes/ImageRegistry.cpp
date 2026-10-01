#include "ImageRegistry.h"

namespace swish {

void ImageRegistry::set_shared(Img id, VkImage image, VkImageView view) {
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        m_slots[idx(id)][f] = {image, view};
}

void ImageRegistry::set(Img id, uint32_t frame, VkImage image, VkImageView view) {
    m_slots[idx(id)][frame] = {image, view};
}

std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> ImageRegistry::views(Img id) const {
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> out{};
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        out[f] = m_slots[idx(id)][f].view;
    return out;
}

}  // namespace swish
