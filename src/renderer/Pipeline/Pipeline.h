#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace swish {

// Configuration struct for pipeline creation — the core DRY pattern.
// Most pipelines override only 2-3 fields; the rest use sensible defaults.
struct PipelineConfig {
    std::string vertShaderPath;
    std::string fragShaderPath;

    std::vector<VkVertexInputBindingDescription>   vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    bool                                           noVertexInput = false;

    VkCullModeFlags     cullMode = VK_CULL_MODE_BACK_BIT;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    bool enableBlending   = false;
    bool additiveBlending = false;  // src=ONE dst=ONE; overrides enableBlending when true

    bool        enableDepthTest  = true;
    bool        enableDepthWrite = true;
    VkCompareOp depthCompareOp   = VK_COMPARE_OP_GREATER;  // reverse-Z (near→1, far→0); shadow pass overrides to LESS

    uint32_t colorAttachmentCount = 1;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // Dynamic rendering (Vulkan 1.3): when true, create() chains a VkPipelineRenderingCreateInfo built from
    // the fields below and IGNORES the VkRenderPass argument (pass VK_NULL_HANDLE). Only valid when the
    // device enabled dynamicRendering (RendererServices::dynamicRendering). Default false = classic path.
    bool                  dynamicRendering = false;
    std::vector<VkFormat> dynamicColorFormats;                       // one per colour attachment
    VkFormat              dynamicDepthFormat = VK_FORMAT_UNDEFINED;  // UNDEFINED = no depth attachment
};

// Free functions, no state (was an all-static `class Pipeline` with a hidden process-wide cache global).
//
// The VkPipelineCache is owned by `Device` (loaded from / saved to config/pipeline_cache.bin), published to the
// subsystems through `RendererServices::pipelineCache`, and passed to `create_graphics` / `create_compute`
// explicitly. VK_NULL_HANDLE is valid: the driver then compiles from scratch.
namespace pipeline {

// `renderPass` is ignored (may be VK_NULL_HANDLE) when config.dynamicRendering is set.
VkPipeline create_graphics(VkDevice device, VkPipelineCache cache, const PipelineConfig& config,
                           VkRenderPass renderPass, VkExtent2D extent);

VkPipelineLayout create_layout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts,
                               const std::vector<VkPushConstantRange>& pushConstants = {});

// Compute pipeline from a single .comp SPIR-V module + a prebuilt layout. Used by SpraySystem (the first, and so
// far only, compute pass in the renderer).
VkPipeline create_compute(VkDevice device, VkPipelineCache cache, const std::string& compShaderPath,
                          VkPipelineLayout layout);

}  // namespace pipeline

}  // namespace swish
