#pragma once
// ============================================================================
// vkx/PushConstants.h
//
// Vulkan concept : Push constants (VkPushConstantRange, vkCmdPushConstants).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  section "Push Constant Interface" / "Push Constant Updates"
//                  (anchor #descriptorsets-push-constants, verify);
//                  limit VkPhysicalDeviceLimits::maxPushConstantsSize (spec minimum 128 B)
// Guide          : https://docs.vulkan.org/guide/latest/push_constants.html (verify)
// Vulkan-Hpp     : vk::PushConstantRange, vk::CommandBuffer::pushConstants<T>
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : Swish rule: push-constant blocks must be 16-byte multiples (vec4
//                  aligned); a non-multiple size silently misbehaves on MoltenVK. The
//                  reported maxPushConstantsSize on the target machine has NOT been
//                  verified (SsrParams is 224 B, above the guaranteed 128 B).
//
// Purpose        : push_range<T>() builds a VkPushConstantRange and static_asserts the
//                  16-byte rule at compile time (five push blocks are unchecked today).
//                  check_push_limit() compares against the device limit, which is passed
//                  IN as a parameter: vkx never includes Device/renderer headers.
// Ownership      : Stateless.
// See also       : README.md in this directory.
// ============================================================================

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace swish::vkx {

inline constexpr uint32_t kPushConstantAlign = 16;

template <class P>
constexpr VkPushConstantRange push_range(VkShaderStageFlags stages, uint32_t offset = 0) {
    static_assert(std::is_trivially_copyable_v<P>, "push constant block must be trivially copyable");
    static_assert(sizeof(P) % kPushConstantAlign == 0,
                  "push constant block size must be a multiple of 16 bytes (MoltenVK); pad the struct");
    return VkPushConstantRange{stages, offset, static_cast<uint32_t>(sizeof(P))};
}

// `maxPushConstantsSize` comes from VkPhysicalDeviceLimits (caller reads it once at device
// selection). Throws if offset+size exceeds it.
template <class P>
inline void check_push_limit(uint32_t maxPushConstantsSize, uint32_t offset = 0) {
    if (offset + sizeof(P) > maxPushConstantsSize)
        throw std::runtime_error("vkx::check_push_limit: push block " + std::to_string(sizeof(P)) + " B at offset " +
                                 std::to_string(offset) + " exceeds maxPushConstantsSize " +
                                 std::to_string(maxPushConstantsSize));
}

}  // namespace swish::vkx
