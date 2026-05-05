#include "DeferredLightingPipeline.h"

#include "../Pipeline/Pipeline.h"

#include <string>

namespace swish {

namespace {
// Push constants are invView + invProj (two mat4 = 128 bytes), fragment-only.
constexpr uint32_t kLightingPushConstSize = 128;

struct LightingPushConstants {
    Mat4 invView;
    Mat4 invProj;
};
static_assert(sizeof(LightingPushConstants) == kLightingPushConstSize,
              "LightingPushConstants size must match push-const range");
}  // namespace

void DeferredLightingPipeline::init(VkDevice device, const Config& cfg) {
    VkPushConstantRange lightPC{};
    lightPC.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightPC.offset     = 0;
    lightPC.size       = kLightingPushConstSize;

    m_layout = Pipeline::createLayout(
        device,
        {cfg.cameraSetLayout, cfg.gbufferSetLayout},
        {lightPC});

    buildPipeline(device, cfg.lightingRenderPass, cfg.extent);
}

void DeferredLightingPipeline::recreate(VkDevice device, VkRenderPass lightingRenderPass,
                                        VkExtent2D extent) {
    destroyPipelineOnly(device);
    buildPipeline(device, lightingRenderPass, extent);
}

void DeferredLightingPipeline::cleanup(VkDevice device) {
    destroyPipelineOnly(device);
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

void DeferredLightingPipeline::bind_and_record(VkCommandBuffer cmd,
                                               VkDescriptorSet cameraSet,
                                               VkDescriptorSet gbufferSet,
                                               const Mat4& invView,
                                               const Mat4& invProj,
                                               VkExtent2D extent) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport vp{0.0f, 0.0f,
                  static_cast<float>(extent.width), static_cast<float>(extent.height),
                  0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            0, 1, &cameraSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout,
                            1, 1, &gbufferSet, 0, nullptr);

    LightingPushConstants pc{invView, invProj};
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, kLightingPushConstSize, &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void DeferredLightingPipeline::buildPipeline(VkDevice device, VkRenderPass renderPass,
                                             VkExtent2D extent) {
    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "lighting.frag.spv";
    cfg.noVertexInput    = true;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.pipelineLayout   = m_layout;

    m_pipeline = Pipeline::create(device, cfg, renderPass, extent);
}

void DeferredLightingPipeline::destroyPipelineOnly(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

}  // namespace swish
