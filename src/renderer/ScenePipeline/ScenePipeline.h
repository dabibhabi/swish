#pragma once

#include <vulkan/vulkan.h>

namespace swish {


class ScenePipeline {
public:
    struct Config {
        VkDescriptorSetLayout cameraSetLayout   = VK_NULL_HANDLE;  // set 0
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;  // set 1
        VkRenderPass          targetRenderPass  = VK_NULL_HANDLE;  // PostProcessManager::get_gbuffer_render_pass()
        VkExtent2D            extent            = {0, 0};
    };

    ScenePipeline() = default;

    void init(VkDevice device, const Config& cfg);
    void cleanup(VkDevice device);
    void bind(VkCommandBuffer cmd, VkExtent2D extent, VkDescriptorSet cameraSet) const;

    VkPipelineLayout get_layout() const { return m_layout; }

private:
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

}  // namespace swish
