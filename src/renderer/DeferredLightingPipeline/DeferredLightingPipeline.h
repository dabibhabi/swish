#pragma once

#include "../../utils/Types.h"

#include <vulkan/vulkan.h>

namespace swish {

// Owns the deferred-lighting pipeline + its layout. Replaces the previous
// cross-ownership where Renderer built the layout but PostProcessManager
// destroyed it on swapchain recreate (claude_docs/07-status-and-debt.md
// item #6). Now both layout and pipeline live here together; the layout
// survives recreate (depends only on stable descriptor-set layouts), and
// the pipeline rebuilds for the new render pass / extent.
class DeferredLightingPipeline {
public:
    struct Config {
        VkDescriptorSetLayout cameraSetLayout  = VK_NULL_HANDLE;  // set 0 — from CameraUniforms
        VkDescriptorSetLayout gbufferSetLayout = VK_NULL_HANDLE;  // set 1 — from PostProcessManager (lighting tex layout)
        VkRenderPass          lightingRenderPass = VK_NULL_HANDLE;
        VkExtent2D            extent             = {0, 0};
    };

    DeferredLightingPipeline() = default;

    void init(VkDevice device, const Config& cfg);

    // Rebuild the pipeline at a new render-pass / extent. Layout is
    // preserved (stable across swapchain recreate).
    void recreate(VkDevice device, VkRenderPass lightingRenderPass, VkExtent2D extent);

    void cleanup(VkDevice device);

    // Bind pipeline + viewport/scissor, bind both descriptor sets, push
    // invView/invProj, and issue the fullscreen draw. One call per frame.
    void bind_and_record(VkCommandBuffer cmd,
                         VkDescriptorSet cameraSet,
                         VkDescriptorSet gbufferSet,
                         const Mat4& invView,
                         const Mat4& invProj,
                         VkExtent2D extent) const;

private:
    void buildPipeline(VkDevice device, VkRenderPass renderPass, VkExtent2D extent);
    void destroyPipelineOnly(VkDevice device);

    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

}  // namespace swish
