#include "ScenePipeline.h"

#include "../../scene/SceneTypes.h"
#include "../Pipeline/Pipeline.h"
#include "../Vertex.h"

#include <string>

namespace swish {

void ScenePipeline::init(VkDevice device, const Config& cfg) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(PushConstantData);

    m_layout = Pipeline::createLayout(device, {cfg.cameraSetLayout, cfg.materialSetLayout}, {pushConstantRange});

    auto binding    = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();

    PipelineConfig config{};
    config.vertShaderPath       = std::string(SHADER_DIR) + "basic.vert.spv";
    config.fragShaderPath       = std::string(SHADER_DIR) + "gbuffer.frag.spv";
    config.cullMode             = VK_CULL_MODE_BACK_BIT;
    config.colorAttachmentCount = 3;
    config.vertexBindings.push_back(binding);
    config.vertexAttributes.assign(attributes.begin(), attributes.end());
    config.pipelineLayout = m_layout;

    m_pipeline = Pipeline::create(device, config, cfg.targetRenderPass, cfg.extent);
}

void ScenePipeline::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

void ScenePipeline::bind(VkCommandBuffer cmd, VkExtent2D extent, VkDescriptorSet cameraSet) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &cameraSet, 0, nullptr);
}

}  // namespace swish
