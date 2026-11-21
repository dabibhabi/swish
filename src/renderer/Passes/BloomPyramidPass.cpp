// BloomPyramid (lighting L4.4, debug builds): widens the bloom. The existing chain blurs once at quarter
// resolution, which gives a tight glow only. This pass blurs the result again at 1/8, 1/16 and 1/32 and mixes
// those levels back over the bloom image, so a bright lamp or panel gets a wide, soft halo around its tight
// core. The mix is a chain of alpha blends, so the bloom's total energy (and bloom_intensity) is unchanged.
//
// Self-contained like DofPass: it owns its level images, framebuffers, descriptor sets and two pipelines, and
// reads / writes only Img::BloomBlurV. Off (DebugParams::bloomPyramid) it records nothing.
#ifdef SWISH_DEBUG_UI

#include "debug/DebugParams.h"
#include "renderer/GpuResource/GpuResource.h"
#include "renderer/Passes/IRenderPass.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/PostProcessManager/PostProcessManager.h"
#include "renderer/Renderer/RendererServices.h"
#include "utils/io/AppPaths.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Descriptors.h"
#include "utils/vulkan/ImageView.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/RenderScope.h"
#include "utils/vulkan/Samplers.h"

#include <algorithm>
#include <array>
#include <memory>

namespace swish {

namespace {

constexpr uint32_t kLevels = 3;  // 1/8, 1/16 and 1/32 of the render extent (the bloom image is 1/4)
constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// Blend weight of each level as it is drawn over the result so far: mix(mix(mix(L0, L1, .5), L2, .4), L3, .3).
constexpr std::array<float, kLevels> kWeights{0.5f, 0.4f, 0.3f};

class BloomPyramidPass final : public IRenderPass {
public:
    const char* name() const override { return "BloomPyramid"; }
    void        declare(PassDeclaration& d) const override { d.read({Img::BloomBlurV}).write({Img::BloomBlurV}); }

    void init(const PassInitCtx& ctx) override {
        const VkDevice device = ctx.services.device;
        m_allocator           = ctx.services.allocator;

        // Level targets: every pixel is written, so the old contents do not matter.
        vkx::ColorPassDesc level;
        level.color = {.format = kFormat};
        m_levelPass = vkx::make_color_pass(device, level);
        // The combine draws over the bloom image, which arrives readable and must keep its contents.
        vkx::ColorPassDesc over;
        over.color = {.format  = kFormat,
                      .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .initial = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        m_overPass = vkx::make_color_pass(device, over);

        m_sampler   = vkx::make_sampler(device);  // linear, clamp-to-edge
        m_setLayout = vkx::make_sampler_layout(device, 1);
        m_pool      = vkx::make_pool(device, {.maxSets = 3 * kLevels, .combinedImageSamplers = 3 * kLevels});
        for (uint32_t k = 0; k < kLevels; ++k) {
            m_setH[k]  = vkx::allocate_set(device, m_pool, m_setLayout);
            m_setV[k]  = vkx::allocate_set(device, m_pool, m_setLayout);
            m_setUp[k] = vkx::allocate_set(device, m_pool, m_setLayout);
        }

        const VkPushConstantRange pc = vkx::push_range<PostProcessParams>(VK_SHADER_STAGE_FRAGMENT_BIT);
        m_layout                     = pipeline::create_layout(device, {m_setLayout}, {pc});
        PipelineConfig cfg{};
        cfg.vertShaderPath   = util::paths::shader_dir() + "fullscreen.vert.spv";
        cfg.fragShaderPath   = util::paths::shader_dir() + "bloom_blur.frag.spv";
        cfg.noVertexInput    = true;
        cfg.cullMode         = VK_CULL_MODE_NONE;
        cfg.enableDepthTest  = false;
        cfg.enableDepthWrite = false;
        cfg.pipelineLayout   = m_layout;
        const VkExtent2D any{16, 16};  // viewport and scissor are dynamic
        m_blurPipeline     = pipeline::create_graphics(device, ctx.services.pipelineCache, cfg, m_levelPass, any);
        cfg.fragShaderPath = util::paths::shader_dir() + "bloom_combine.frag.spv";
        cfg.enableBlending = true;  // src alpha / one minus src alpha
        m_combinePipeline  = pipeline::create_graphics(device, ctx.services.pipelineCache, cfg, m_overPass, any);

        build(ctx);
    }

    void onResize(const PassInitCtx& ctx) override {
        destroy_sized(ctx.services.device);
        build(ctx);
    }

    void record(const FrameCtx& c) override {
        if (!c.tune.bloomPyramid) return;
        const VkCommandBuffer cmd = c.cmd;
        const auto draw = [&](VkPipeline pipeline, VkDescriptorSet set, VkExtent2D ext, const PostProcessParams& pp) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkx::set_viewport_scissor(cmd, ext);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessParams), &pp);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        };
        const auto readable = [&](VkImage image) {
            vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
                cmd, image, VK_IMAGE_ASPECT_COLOR_BIT);
        };
        const VkClearValue clear{};

        // Down the pyramid: each level is a horizontal then a vertical blur of the level above, at half its size.
        for (uint32_t k = 0; k < kLevels; ++k) {
            PostProcessParams h{}, v{};
            h.texel_x = 1.0f / static_cast<float>(m_extent[k].width);
            v.texel_y = 1.0f / static_cast<float>(m_extent[k].height);
            {
                vkx::RenderScope scope(cmd, m_levelPass, m_fbA[k], m_extent[k], clear);
                draw(m_blurPipeline, m_setH[k], m_extent[k], h);
            }
            readable(m_a[k].handle());
            {
                vkx::RenderScope scope(cmd, m_levelPass, m_fbB[k], m_extent[k], clear);
                draw(m_blurPipeline, m_setV[k], m_extent[k], v);
            }
            readable(m_b[k].handle());
        }

        // Back up: blend every level over the bloom image (one render pass, three draws), then hand it on
        // readable, the layout the composite expects.
        {
            vkx::RenderScope scope(cmd, m_overPass, m_fbTarget, m_target, clear);
            for (uint32_t k = 0; k < kLevels; ++k) {
                PostProcessParams pp{};
                pp.bloom_intensity = kWeights[k];
                draw(m_combinePipeline, m_setUp[k], m_target, pp);
            }
        }
        readable(c.images.image(Img::BloomBlurV, c.frameIndex));
    }

    void cleanup(VkDevice device) override {
        destroy_sized(device);
        if (m_blurPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_blurPipeline, nullptr);
        if (m_combinePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_combinePipeline, nullptr);
        if (m_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_layout, nullptr);
        if (m_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_pool, nullptr);
        if (m_setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
        if (m_levelPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_levelPass, nullptr);
        if (m_overPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_overPass, nullptr);
        m_blurPipeline = m_combinePipeline = VK_NULL_HANDLE;
        m_layout                           = VK_NULL_HANDLE;
        m_pool                             = VK_NULL_HANDLE;
        m_setLayout                        = VK_NULL_HANDLE;
        m_sampler                          = VK_NULL_HANDLE;
        m_levelPass = m_overPass = VK_NULL_HANDLE;
    }

private:
    // Everything that depends on the bloom image's size or view: level images, framebuffers, descriptor writes.
    void build(const PassInitCtx& ctx) {
        const VkDevice    device    = ctx.services.device;
        const VkImageView bloomView = ctx.images.view(Img::BloomBlurV, 0);
        m_target                    = ctx.res.pp->get_bloom_extent();

        const auto make_fb = [&](VkRenderPass pass, VkImageView view, VkExtent2D ext) {
            VkFramebufferCreateInfo fb{};
            fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass      = pass;
            fb.attachmentCount = 1;
            fb.pAttachments    = &view;
            fb.width           = ext.width;
            fb.height          = ext.height;
            fb.layers          = 1;
            VkFramebuffer out  = VK_NULL_HANDLE;
            VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &out));
            return out;
        };

        for (uint32_t k = 0; k < kLevels; ++k) {
            m_extent[k] = {std::max(1u, m_target.width >> (k + 1)), std::max(1u, m_target.height >> (k + 1))};
            for (GpuImage* img : {&m_a[k], &m_b[k]})
                *img = gpu::deviceLocalImage(m_allocator, m_extent[k].width, m_extent[k].height, kFormat,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            m_viewA[k] = vkx::make_image_view2d(device, m_a[k].handle(), kFormat);
            m_viewB[k] = vkx::make_image_view2d(device, m_b[k].handle(), kFormat);
            m_fbA[k]   = make_fb(m_levelPass, m_viewA[k], m_extent[k]);
            m_fbB[k]   = make_fb(m_levelPass, m_viewB[k], m_extent[k]);
        }
        m_fbTarget = make_fb(m_overPass, bloomView, m_target);

        const auto write = [&](VkDescriptorSet set, VkImageView view) {
            vkx::write_sampler(device, set, 0, m_sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        for (uint32_t k = 0; k < kLevels; ++k) {
            write(m_setH[k], k == 0 ? bloomView : m_viewB[k - 1]);  // the level above
            write(m_setV[k], m_viewA[k]);
            write(m_setUp[k], m_viewB[k]);
        }
    }

    void destroy_sized(VkDevice device) {
        const auto drop_fb = [&](VkFramebuffer& fb) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        };
        const auto drop_view = [&](VkImageView& v) {
            if (v != VK_NULL_HANDLE) vkDestroyImageView(device, v, nullptr);
            v = VK_NULL_HANDLE;
        };
        drop_fb(m_fbTarget);
        for (uint32_t k = 0; k < kLevels; ++k) {
            drop_fb(m_fbA[k]);
            drop_fb(m_fbB[k]);
            drop_view(m_viewA[k]);
            drop_view(m_viewB[k]);
            m_a[k].reset();
            m_b[k].reset();
        }
    }

    VmaAllocator          m_allocator       = VK_NULL_HANDLE;
    VkRenderPass          m_levelPass       = VK_NULL_HANDLE;
    VkRenderPass          m_overPass        = VK_NULL_HANDLE;
    VkSampler             m_sampler         = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout       = VK_NULL_HANDLE;
    VkDescriptorPool      m_pool            = VK_NULL_HANDLE;
    VkPipelineLayout      m_layout          = VK_NULL_HANDLE;
    VkPipeline            m_blurPipeline    = VK_NULL_HANDLE;
    VkPipeline            m_combinePipeline = VK_NULL_HANDLE;

    VkExtent2D                           m_target{};
    VkFramebuffer                        m_fbTarget = VK_NULL_HANDLE;
    std::array<VkExtent2D, kLevels>      m_extent{};
    std::array<GpuImage, kLevels>        m_a, m_b;  // a = after the horizontal blur, b = the finished level
    std::array<VkImageView, kLevels>     m_viewA{}, m_viewB{};
    std::array<VkFramebuffer, kLevels>   m_fbA{}, m_fbB{};
    std::array<VkDescriptorSet, kLevels> m_setH{}, m_setV{}, m_setUp{};
};

}  // namespace

std::unique_ptr<IRenderPass> make_bloom_pyramid_pass() { return std::make_unique<BloomPyramidPass>(); }

}  // namespace swish

#endif  // SWISH_DEBUG_UI
