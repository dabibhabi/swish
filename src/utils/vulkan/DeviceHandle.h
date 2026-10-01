#pragma once
// ============================================================================
// vkx/DeviceHandle.h
//
// Vulkan concept : Object lifetime and destruction of device-child objects
//                  (VkPipeline, VkShaderModule, VkRenderPass, VkFramebuffer, VkSampler,
//                  VkImageView, VkDescriptor{SetLayout,Pool}, VkPipelineLayout ...).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  section "Object Lifetime" (anchor #fundamentals-objectmodel-lifetime, verify)
// Guide          : https://docs.vulkan.org/guide/latest/object_lifetime.html (verify)
// Vulkan-Hpp     : vk::UniqueHandle<T, Dispatch> / vk::raii::* (we deliberately do NOT use
//                  vk::raii, see archive/local/review_2026-09-29/docs/vulkan-dispatch-and-utils.md option C)
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : none specific; MoltenVK destroys Metal objects when the Vk object is
//                  destroyed, so leaking one keeps a Metal pipeline/texture alive.
//
// Purpose        : Move-only owner of ONE raw Vulkan handle; destructor calls the matching
//                  vkDestroy*(device, handle, nullptr). Raw handles stay in every signature
//                  (get()); there is no second handle vocabulary. Same idea as
//                  GpuBuffer/GpuImage (VMA) for non-VMA objects.
//
// LIFETIME RULE (owner decision V6)
//                  A DeviceHandle stores a POINTER to the services object (constructed from
//                  a reference) and reads `services.device` when it is destroyed. The
//                  services object (RendererServices) MUST OUTLIVE EVERY HANDLE, and the
//                  VkDevice it names must still be alive: handles must die BEFORE
//                  Device::cleanup(), the same rule as docs/vma-memory.md. The destructor
//                  asserts that services are non-null and `services.device != VK_NULL_HANDLE`;
//                  if the assert is compiled out (NDEBUG) and the device is already null it
//                  skips the destroy call rather than invoking UB.
//                  Because the device is read at destruction (not copied at construction),
//                  a Device::cleanup() that nulls `services.device` before handles die is
//                  detected by the assert.
// Layering       : `vkx` must not include renderer headers. `Services` is therefore a
//                  template parameter constrained by a concept: any type with a
//                  `VkDevice device` member (RendererServices qualifies structurally, no
//                  include needed). The default is the tiny DeviceRef below for tests and
//                  code without RendererServices.
// Ownership      : Owns the handle; move-only; release() gives up ownership.
// See also       : ShaderModule.h (first user), README.md in this directory.
// ============================================================================

#include <vulkan/vulkan.h>

#include <cassert>
#include <concepts>
#include <utility>

namespace swish::vkx {

// Minimal stand-in for RendererServices (same member name).
struct DeviceRef {
    VkDevice device = VK_NULL_HANDLE;
};

template <class S>
concept HasDevice = requires(const S& s) {
    { s.device } -> std::convertible_to<VkDevice>;
};

template <class H, auto Destroy, HasDevice Services = DeviceRef>
class DeviceHandle {
public:
    DeviceHandle() = default;
    DeviceHandle(const Services& services, H handle) : m_services(&services), m_handle(handle) {}
    ~DeviceHandle() { reset(); }

    DeviceHandle(const DeviceHandle&)            = delete;
    DeviceHandle& operator=(const DeviceHandle&) = delete;

    DeviceHandle(DeviceHandle&& o) noexcept
        : m_services(std::exchange(o.m_services, nullptr)), m_handle(std::exchange(o.m_handle, H{})) {}
    DeviceHandle& operator=(DeviceHandle&& o) noexcept {
        if (this != &o) {
            reset();
            m_services = std::exchange(o.m_services, nullptr);
            m_handle   = std::exchange(o.m_handle, H{});
        }
        return *this;
    }

    void reset() noexcept {
        if (m_handle != H{}) {
            assert(m_services != nullptr && "vkx::DeviceHandle: services destroyed before handle");
            assert(m_services->device != VK_NULL_HANDLE && "vkx::DeviceHandle: handle outlived the VkDevice");
            if (m_services != nullptr && m_services->device != VK_NULL_HANDLE)
                Destroy(static_cast<VkDevice>(m_services->device), m_handle, nullptr);
        }
        m_handle   = H{};
        m_services = nullptr;
    }

    [[nodiscard]] H get() const { return m_handle; }
    [[nodiscard]] explicit operator bool() const { return m_handle != H{}; }

    // Gives up ownership; the caller must destroy the handle.
    [[nodiscard]] H release() {
        m_services = nullptr;
        return std::exchange(m_handle, H{});
    }

private:
    const Services* m_services = nullptr;
    H               m_handle{};
};

template <HasDevice S = DeviceRef>
using UniqueShaderModule = DeviceHandle<VkShaderModule, vkDestroyShaderModule, S>;
template <HasDevice S = DeviceRef>
using UniquePipeline = DeviceHandle<VkPipeline, vkDestroyPipeline, S>;
template <HasDevice S = DeviceRef>
using UniquePipelineLayout = DeviceHandle<VkPipelineLayout, vkDestroyPipelineLayout, S>;
template <HasDevice S = DeviceRef>
using UniqueDescriptorSetLayout = DeviceHandle<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout, S>;
template <HasDevice S = DeviceRef>
using UniqueDescriptorPool = DeviceHandle<VkDescriptorPool, vkDestroyDescriptorPool, S>;
template <HasDevice S = DeviceRef>
using UniqueRenderPass = DeviceHandle<VkRenderPass, vkDestroyRenderPass, S>;
template <HasDevice S = DeviceRef>
using UniqueFramebuffer = DeviceHandle<VkFramebuffer, vkDestroyFramebuffer, S>;
template <HasDevice S = DeviceRef>
using UniqueImageView = DeviceHandle<VkImageView, vkDestroyImageView, S>;
template <HasDevice S = DeviceRef>
using UniqueSampler = DeviceHandle<VkSampler, vkDestroySampler, S>;

}  // namespace swish::vkx
