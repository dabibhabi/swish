#pragma once
// ============================================================================
// vkx/DebugLabel.h
//
// Vulkan concept : Debug utils command-buffer labels (VK_EXT_debug_utils):
//                  vkCmdBeginDebugUtilsLabelEXT / vkCmdEndDebugUtilsLabelEXT.
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  extension appendix "VK_EXT_debug_utils" (anchor #VK_EXT_debug_utils, verify)
// Guide          : https://docs.vulkan.org/guide/latest/debug_utils.html (verify)
// Vulkan-Hpp     : vk::CommandBuffer::beginDebugUtilsLabelEXT / vk::DebugUtilsLabelEXT
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : MoltenVK implements VK_EXT_debug_utils and forwards labels to Metal
//                  debug groups, so they show up in Xcode GPU capture. RenderDoc is not
//                  available on macOS. (behaviour: verify on target SDK)
//
// Purpose        : Name passes in GPU captures. Owner decision V3: labels exist ONLY in
//                  SWISH_DEBUG_UI builds. Without that macro ScopedLabel is an empty type
//                  with inline no-op members, so release code generation is unchanged
//                  and call sites need no #ifdef.
// Ownership      : ScopedLabel is a stack RAII object (begin in ctor, end in dtor) bound to
//                  one VkCommandBuffer that must be in the recording state for its whole
//                  lifetime. DebugLabelFns holds only function pointers (no ownership).
//                  It is passed explicitly; there is NO global singleton.
// Loading        : VK_EXT_debug_utils is an instance extension already enabled in debug
//                  builds of VulkanContext; load pointers once with load_debug_label_fns(instance)
//                  and keep the struct next to the VkInstance owner. If the pointers are
//                  null (extension absent) ScopedLabel is a no-op.
// See also       : Barrier.h, README.md in this directory.
// ============================================================================

#include <vulkan/vulkan.h>

namespace swish::vkx {

struct DebugLabelFns {
    PFN_vkCmdBeginDebugUtilsLabelEXT begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT   end   = nullptr;
    [[nodiscard]] bool available() const { return begin != nullptr && end != nullptr; }
};

#ifdef SWISH_DEBUG_UI

inline DebugLabelFns load_debug_label_fns(VkInstance instance) {
    DebugLabelFns f;
    f.begin = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
    f.end = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
    return f;
}

class ScopedLabel {
public:
    ScopedLabel(VkCommandBuffer cmd, const DebugLabelFns& fns, const char* name, float r = 1.f, float g = 1.f,
                float b = 1.f)
        : m_cmd(cmd), m_end(fns.available() ? fns.end : nullptr) {
        if (m_end) {
            VkDebugUtilsLabelEXT label{};
            label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = name;
            label.color[0]   = r;
            label.color[1]   = g;
            label.color[2]   = b;
            label.color[3]   = 1.f;
            fns.begin(cmd, &label);
        }
    }
    ~ScopedLabel() {
        if (m_end)
            m_end(m_cmd);
    }
    ScopedLabel(const ScopedLabel&)            = delete;
    ScopedLabel& operator=(const ScopedLabel&) = delete;

private:
    VkCommandBuffer                m_cmd;
    PFN_vkCmdEndDebugUtilsLabelEXT m_end;
};

#else  // release: no function-pointer lookup, no label calls, empty object

inline DebugLabelFns load_debug_label_fns(VkInstance) {
    return {};
}

class ScopedLabel {
public:
    ScopedLabel(VkCommandBuffer, const DebugLabelFns&, const char*, float = 1.f, float = 1.f, float = 1.f) {}
    ScopedLabel(const ScopedLabel&)            = delete;
    ScopedLabel& operator=(const ScopedLabel&) = delete;
};

#endif

}  // namespace swish::vkx
