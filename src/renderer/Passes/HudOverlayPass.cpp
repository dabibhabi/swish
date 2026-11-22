// HUD overlay (C3b, SWISH_DEBUG_UI only): the speed / district / next-exit strip and route minimap, drawn by
// HudOverlay with its own LOAD render pass on the composite framebuffer (swapchain image, PRESENT_SRC in and out).
#ifdef SWISH_DEBUG_UI

#include "debug/HudOverlay.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/PostProcessManager/PostProcessManager.h"

#include <memory>

namespace swish {

namespace {

class HudOverlayPass final : public IRenderPass {
public:
    const char* name() const override { return "HudOverlay"; }
    void        init(const PassInitCtx&) override {}
    // Composite's framebuffer is reused: a LOAD pass with the same format is render-pass compatible.
    void record(const FrameCtx& c) override {
        if (c.res.hud != nullptr)
            c.res.hud->record(c.cmd, c.res.pp->get_composite_framebuffer(c.imageIndex), c.swapExtent, c.tune);
    }
    void cleanup(VkDevice) override {}
};

}  // namespace

std::unique_ptr<IRenderPass> make_hud_overlay_pass() { return std::make_unique<HudOverlayPass>(); }

}  // namespace swish

#endif  // SWISH_DEBUG_UI
