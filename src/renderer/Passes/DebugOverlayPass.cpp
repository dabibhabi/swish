// Debug overlay (SWISH_DEBUG_UI only): the Dear ImGui panel drawn over the composited frame. DebugUI owns its own
// LOAD render pass over the swapchain image (composite left it in PRESENT_SRC), so the panel sits on top. The panel
// itself is built earlier in the frame by Renderer (DebugUI::begin_frame mutates DebugParams).
#ifdef SWISH_DEBUG_UI

#include "debug/DebugUI.h"
#include "renderer/Passes/IRenderPass.h"

#include <memory>

namespace swish {

namespace {

class DebugOverlayPass final : public IRenderPass {
public:
    const char* name() const override { return "DebugOverlay"; }
    void        init(const PassInitCtx&) override {}
    void        record(const FrameCtx& c) override { c.res.debugUI->record(c.cmd, c.imageIndex, c.swapExtent); }
    void        cleanup(VkDevice) override {}
};

}  // namespace

std::unique_ptr<IRenderPass> make_debug_overlay_pass() {
    return std::make_unique<DebugOverlayPass>();
}

}  // namespace swish

#endif  // SWISH_DEBUG_UI
