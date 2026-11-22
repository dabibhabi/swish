#pragma once
// FullscreenPass<Push>: the shared shape of the fullscreen post passes (bloom, god-rays, SSAO, SSR, composite):
// one render pass instance, a bound pipeline, 1..2 descriptor sets, an optional fragment push block and a single
// 3-vertex draw. A derived pass only says WHAT to draw (describe) and, where it needs them, the barriers around the
// draw (before / after). The recorded Vulkan calls are exactly the hand-written sequence they replace:
//
//   describe (CPU only)  ->  before()  ->  begin render pass, bind pipeline, viewport+scissor,
//   bind sets, push constants (skipped for NoPush), draw(3)  ->  end render pass  ->  after()
//
// The GPU objects (render pass, framebuffer, pipeline, layout, sets) are still owned by PostProcessManager: this
// class only removes the recording duplication. Moving that ownership into the pass is the next step (see
// src/renderer/README.md, "Remaining work").
//
// NOTE: the push structs used here are PostProcessParams (64 B: 13 shader floats + 12 B host padding), SsaoParams,
// SsrParams and GodRaysParams. The 16-byte rule (utils/vulkan/PushConstants.h) is asserted next to each struct and at
// each VkPushConstantRange (vkx::push_range).

#include "renderer/Passes/IRenderPass.h"
#include "utils/vulkan/RenderScope.h"

#include <vulkan/vulkan.h>

#include <array>
#include <type_traits>

namespace swish {

struct NoPush {};

// Everything one fullscreen draw needs; handles are owned by the PostProcessManager (or the pass).
struct FullscreenDraw {
    VkRenderPass                   renderPass  = VK_NULL_HANDLE;
    VkFramebuffer                  framebuffer = VK_NULL_HANDLE;
    VkExtent2D                     extent{};
    VkClearValue                   clear{};
    VkPipeline                     pipeline = VK_NULL_HANDLE;
    VkPipelineLayout               layout   = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> sets{};  // bound at set index 0..setCount-1
    uint32_t                       setCount = 1;
};

inline VkClearValue clear_color(float r, float g, float b, float a) {
    VkClearValue c{};
    c.color = {{r, g, b, a}};
    return c;
}

template <class Push = NoPush>
class FullscreenPass : public IRenderPass {
public:
    void init(const PassInitCtx&) override {}  // objects are owned by PostProcessManager
    void cleanup(VkDevice) override {}

    void record(const FrameCtx& c) final {
        FullscreenDraw d{};
        Push           p{};
        describe(c, d, p);
        before(c);
        {
            vkx::RenderScope scope(c.cmd, d.renderPass, d.framebuffer, d.extent, d.clear);
            vkCmdBindPipeline(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline);
            vkx::set_viewport_scissor(c.cmd, d.extent);
            vkCmdBindDescriptorSets(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout, 0, d.setCount, d.sets.data(), 0,
                                    nullptr);
            if constexpr (!std::is_empty_v<Push>)
                vkCmdPushConstants(c.cmd, d.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &p);
            vkCmdDraw(c.cmd, 3, 1, 0, 0);
        }
        after(c);
    }

protected:
    // Fill the draw and the push block. CPU only: must not record commands (it runs before before()).
    virtual void describe(const FrameCtx& c, FullscreenDraw& d, Push& p) const = 0;
    // Barriers recorded before the render pass begins / after it ends (eager, same place as before the registry).
    virtual void before(const FrameCtx& c) const { (void)c; }
    virtual void after(const FrameCtx& c) const { (void)c; }
};

}  // namespace swish
