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
    VkCompareOp depthCompareOp   = VK_COMPARE_OP_LESS;

    uint32_t colorAttachmentCount = 1;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

// Static factory — creates pipelines from a config.
class Pipeline {
public:
    static VkPipeline create(VkDevice device, const PipelineConfig& config, VkRenderPass renderPass, VkExtent2D extent);

    static VkPipelineLayout createLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts,
                                         const std::vector<VkPushConstantRange>& pushConstants = {});

private:
    static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
};

}  // namespace swish
