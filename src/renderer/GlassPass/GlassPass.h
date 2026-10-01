#pragma once

#include "renderer/Passes/IRenderPass.h"
#include "renderer/RendererConfig.h"
#include "scene/SceneTypes.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace swish {

struct RendererServices;

// ══════════════════════════════════════════════════════════════════════
// GlassPass — forward transparent pass that renders alphaMode=BLEND
// car glass (windows, windshield) onto the HDR buffer after the
// deferred lighting and world rain passes.
//
// The glass uses the same VBO/IBO as the dynamic (car) geometry but
// draws only the glass-flagged submesh index ranges using a dedicated
// pipeline with SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending and depth
// test (read-only — glass does not write depth).
//
// Shader: Fresnel-modulated tint + sun specular highlight.
// Push constants: PushConstantData { Mat4 model; Vec4 color; }
// Descriptor sets: set 0 = camera UBO (bound by Renderer before call)
// ══════════════════════════════════════════════════════════════════════
class GlassPass final : public IRenderPass {
public:
    GlassPass()           = default;
    ~GlassPass() override = default;

    const char* name() const override { return "Glass"; }
    void        init(const PassInitCtx& ctx) override;
    void        onResize(const PassInitCtx& ctx) override;
    // Draws the glass submeshes of the car (shared VBO/IBO) onto HDR; nothing without glass draws or car geometry.
    void record(const FrameCtx& ctx) override;
    void cleanup(VkDevice device) override;

private:
    void create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                VkDescriptorSetLayout cameraSetLayout);
    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);
    // Draw glass draw calls using the shared car VBO/IBO. Camera set 0 must already be bound on m_pipeLayout.
    void record_draws(VkCommandBuffer cmd, uint32_t frameIndex, VkBuffer carVBO, VkBuffer carIBO,
                      const std::vector<DrawCall>& glassDCs) const;

    void createRenderPass(VkDevice device, VkFormat depthFormat);
    void createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyFramebuffers(VkDevice device);
    void createPipeline(VkDevice device, VkPipelineCache cache, VkDescriptorSetLayout cameraSetLayout);

    VkRenderPass                                    m_renderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    VkExtent2D m_extent      = {};
    VkFormat   m_depthFormat = VK_FORMAT_UNDEFINED;
};

}  // namespace swish
