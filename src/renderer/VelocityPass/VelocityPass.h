#pragma once

// VelocityPass — per-object velocity G-buffer for the car body + wheels (debug-only).
//
// tasks/todo.md "Per-object velocity buffer": TaaPass's reprojection today derives
// motion purely from depth + a single camera-level prevViewProj (Renderer::m_prevViewProjUnjit),
// so a MOVING object (the car itself, spinning wheels) reprojects as if it were static
// world geometry — visible ghosting on the car body/wheels under TAA + motion blur.
//
// This pass re-draws the SAME car-body + wheel draw calls the G-buffer pass already
// submitted (CarEntity::get_draw_calls()), a second time, into a small dedicated
// RGBA16F target: rg = this-pixel's UV-space motion vector (rigid transform diff,
// current vs previous frame's object matrix — see CarEntity::get_prev_draw_models()),
// a = 1.0 validity flag (0 elsewhere, via a full-clear each frame). TaaPass samples
// this as an extra input and uses it in place of its depth-only estimate wherever
// valid (taa.frag).
//
// Depth-tested (read-only, GREATER_OR_EQUAL — reverse-Z) against the existing scene
// depth so only the actually-visible car surface (matching what the G-buffer pass
// resolved) writes a velocity; occluded/self-occluded car geometry is discarded.
// GREATER_OR_EQUAL (not strict GREATER) tolerates this pass's vertex math not being
// bit-identical to basic.vert's camera-relative-rebase path — acceptable slop for a
// debug-only feature whose non-goal is per-pixel precision (see todo.md).
//
// SWISH_DEBUG_UI-gated exactly like TaaPass/DofPass: a release build never compiles
// or instantiates this pass, so release output stays byte-identical.

#ifdef SWISH_DEBUG_UI

#include "renderer/GpuResource/GpuResource.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/RendererConfig.h"
#include "scene/SceneTypes.h"
#include "utils/types/Types.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace swish {

struct RendererServices;

class VelocityPass final : public IRenderPass {
public:
    VelocityPass()           = default;
    ~VelocityPass() override = default;

    const char* name() const override { return "Velocity"; }
    void        declare(PassDeclaration& d) const override { d.read({Img::HdrDepth}).write({Img::Velocity}); }
    // Creates the velocity target and registers it as Img::Velocity (TaaPass samples it).
    void init(const PassInitCtx& ctx) override;
    void onResize(const PassInitCtx& ctx) override;
    // Cross-frame history: last frame's UN-jittered view-projection, private to this pass (TaaPass keeps its own
    // copy). Advanced only on frames TAA runs (tune.taaEnabled), exactly like TaaPass.
    void update(const FrameUpdate& u) override;
    // Re-draws `dynamic()`'s car body + wheel submeshes into the velocity target when tune.taaEnabled (only worth
    // the extra draws when TAA will consume it). Each draw pairs its CURRENT model matrix against CarEntity's
    // previous-frame matrix (GeometryProducer::dynamic_prev_models()); if the sizes mismatch (topology just changed)
    // every draw's velocity degrades to zero rather than mis-pairing indices. curViewProj is THIS frame's (possibly
    // jittered) camera matrix, matching the depth this pass tests against; prevViewProj is last frame's un-jittered.
    void record(const FrameCtx& ctx) override;
    void cleanup(VkDevice device) override;

    VkImageView get_view(uint32_t frameIndex) const { return m_views[frameIndex]; }

private:
    void create(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                VkExtent2D extent);
    void recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent, VkDevice device);
    void draw(VkCommandBuffer cmd, uint32_t frameIndex, VkBuffer carVBO, VkBuffer carIBO,
              const std::vector<DrawCall>& draws, const std::vector<Mat4>& prevModels, const Mat4& curViewProj,
              const Mat4& prevViewProj) const;
    void publish(ImageRegistry& images) const;

    // Push block (matches velocity.vert). 4 mat4s = 256 B, a 16-byte multiple.
    struct Push {
        Mat4 curViewProj;
        Mat4 prevViewProj;
        Mat4 curModel;
        Mat4 prevModel;
    };

    void createImages(const RendererServices& s);
    void createRenderPass(VkDevice device, VkFormat depthFormat);
    void createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews);
    void destroyImagesAndFramebuffers(VkDevice device);
    void createPipeline(VkDevice device, VkPipelineCache cache);

    VmaAllocator m_allocator = nullptr;
    VkDevice     m_device    = VK_NULL_HANDLE;

    std::array<GpuImage, MAX_FRAMES_IN_FLIGHT>      m_images{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>   m_views{};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_framebuffers{};

    VkRenderPass     m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;

    Mat4 m_prevViewProj = Mat4(1.0f);  // last frame's un-jittered VP as of THIS frame (see update)
    Mat4 m_lastUnjit    = Mat4(1.0f);  // this frame's un-jittered VP, becomes m_prevViewProj next update

    VkExtent2D m_extent      = {};
    VkFormat   m_format      = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat   m_depthFormat = VK_FORMAT_UNDEFINED;
};

}  // namespace swish

#endif  // SWISH_DEBUG_UI
