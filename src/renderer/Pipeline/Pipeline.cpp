#include "Pipeline.h"

#include "utils/io/File.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Rendering.h"
#include "utils/vulkan/ShaderModule.h"

#include <array>

namespace swish::pipeline {

VkPipeline create_graphics(VkDevice device, VkPipelineCache cache, const PipelineConfig& config,
                           VkRenderPass renderPass, VkExtent2D extent) {
    // 1. Load SPIR-V
    auto vertCode = util::io::read_binary(config.vertShaderPath);
    auto fragCode = util::io::read_binary(config.fragShaderPath);

    // 2. Create shader modules (RAII: destroyed when this function returns, after the pipeline exists)
    const vkx::DeviceRef deviceRef{device};  // must outlive the module handles below
    const auto           vertModuleOwner = vkx::make_shader_module(deviceRef, vertCode);
    const auto           fragModuleOwner = vkx::make_shader_module(deviceRef, fragCode);
    const VkShaderModule vertModule      = vertModuleOwner.get();
    const VkShaderModule fragModule      = fragModuleOwner.get();

    // 3. Shader stage infos
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName  = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName  = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

    // 4. Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config.noVertexInput) {
        vertexInputInfo.vertexBindingDescriptionCount   = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
    } else {
        vertexInputInfo.vertexBindingDescriptionCount   = static_cast<uint32_t>(config.vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions      = config.vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions    = config.vertexAttributes.data();
    }

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = config.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Dynamic viewport + scissor (no pipeline recreation on resize)
    std::array<VkDynamicState, 2>    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = config.cullMode;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    // 8. Multisampling (disabled)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = config.enableDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable      = config.enableDepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp        = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // 10. Color blending
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(config.colorAttachmentCount);
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (config.additiveBlending) {
            attachment.blendEnable         = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.colorBlendOp        = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        } else if (config.enableBlending) {
            attachment.blendEnable         = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp        = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        } else {
            attachment.blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments    = colorBlendAttachments.data();

    // 11. Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages             = shaderStages.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = config.pipelineLayout;
    pipelineInfo.subpass             = 0;

    // Dynamic rendering: attachment formats replace the render pass (renderPass must be null then).
    VkPipelineRenderingCreateInfo renderingInfo{};
    if (config.dynamicRendering) {
        renderingInfo      = vkx::make_pipeline_rendering_info(config.dynamicColorFormats.data(),
                                                               static_cast<uint32_t>(config.dynamicColorFormats.size()),
                                                               config.dynamicDepthFormat);
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
    } else {
        pipelineInfo.renderPass = renderPass;
    }

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &pipeline));

    return pipeline;
}

VkPipelineLayout create_layout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts,
                               const std::vector<VkPushConstantRange>& pushConstants) {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts            = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    layoutInfo.pPushConstantRanges    = pushConstants.data();

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout));

    return layout;
}

VkPipeline create_compute(VkDevice device, VkPipelineCache cache, const std::string& compShaderPath,
                          VkPipelineLayout layout) {
    auto                 code = util::io::read_binary(compShaderPath);
    const vkx::DeviceRef deviceRef{device};                                    // must outlive the module handle below
    const auto           modOwner = vkx::make_shader_module(deviceRef, code);  // destroyed on return
    const VkShaderModule mod      = modOwner.get();

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName  = "main";

    VkComputePipelineCreateInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage  = stage;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, cache, 1, &info, nullptr, &pipeline));

    return pipeline;
}

}  // namespace swish::pipeline
