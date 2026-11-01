#pragma once
// ============================================================================
// vkx/ShaderModule.h
//
// Vulkan concept : Shader modules (VkShaderModule created from SPIR-V words).
// Spec           : https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
//                  chapter "Shaders", section "Shader Modules" (anchor #shaders-modules, verify)
// Guide          : https://docs.vulkan.org/guide/latest/ (Shader Modules page; verify)
// Vulkan-Hpp     : vk::Device::createShaderModuleUnique / vk::ShaderModuleCreateInfo
//                  https://github.com/KhronosGroup/Vulkan-Hpp
// MoltenVK notes : SPIR-V is translated to MSL by SPIRV-Cross at pipeline creation; the
//                  module itself is cheap to destroy right after vkCreate*Pipelines.
//
// Purpose        : One shared implementation of the shader-module creation that is
//                  duplicated byte-for-byte in Pipeline::createShaderModule
//                  (Pipeline.cpp) and the anonymous copy in DepthOnlyPipeline.cpp, and it
//                  returns an RAII owner so the private ScopedShaderModule in Pipeline.cpp
//                  can go away in the migration step.
// Ownership      : Returns a UniqueShaderModule (see DeviceHandle.h lifetime rule).
// Errors         : Throws std::runtime_error via VK_CHECK, or std::invalid_argument when
//                  the byte size is not a multiple of 4 (SPIR-V is 32-bit words).
// See also       : DeviceHandle.h, README.md in this directory.
// ============================================================================

#include "Check.h"
#include "DeviceHandle.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace swish::vkx {

// `code` is the raw .spv file (util::io::read_binary returns std::vector<char>). The
// vector's heap storage is at least 4-byte aligned, as the existing code already relies on.
template <HasDevice Services>
UniqueShaderModule<Services> make_shader_module(const Services& services, const std::vector<char>& code) {
    if (code.empty() || code.size() % 4 != 0)
        throw std::invalid_argument("vkx::make_shader_module: SPIR-V size must be a non-zero multiple of 4");
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(services.device, &info, nullptr, &module));
    return UniqueShaderModule<Services>(services, module);
}

}  // namespace swish::vkx
