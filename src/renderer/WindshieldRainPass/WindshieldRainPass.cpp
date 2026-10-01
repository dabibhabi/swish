#include "WindshieldRainPass.h"

#include "renderer/CameraUniforms/CameraUniforms.h"
#include "renderer/GeometryProducer/GeometryProducer.h"
#include "renderer/Pipeline/Pipeline.h"
#include "renderer/Renderer/RendererServices.h"
#include "renderer/RendererConfig.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "renderer/Vertex.h"
#include "scene/Camera/Camera.h"
#include "scene/Entity/CarEntity.h"  // CarEntity::kMaxSpeed for the windshield speed factor
#include "scene/SceneTypes.h"
#include "utils/units/Units.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/Descriptors.h"
#include "utils/vulkan/ImageView.h"
#include "utils/vulkan/Init.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/Samplers.h"

#include <array>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <memory>

namespace swish {

namespace {
// Push-constant block for the wetness-update fullscreen pass. Mirrors the
// `Params` block in windshield_wetness.frag (std430, 32 bytes).
struct WetnessPush {
    Vec2  flow;        // screen-space flow dir (down at rest → up at speed)
    float intensity;   // rain intensity [0,1]
    float dt;          // seconds
    float wiperAngle;  // rad
    float wiperOn;     // 0/1
    float aspect;      // width / height
    float advect;      // upstream sample distance (UV)
};
}  // namespace

void WindshieldRainPass::init(const PassInitCtx& ctx) {
    create(ctx.services, ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent,
           ctx.cameraSetLayout);
}

void WindshieldRainPass::onResize(const PassInitCtx& ctx) {
    recreate(ctx.images.views(Img::Hdr), ctx.images.views(Img::HdrDepth), ctx.renderExtent, ctx.services.device);
}

void WindshieldRainPass::update(const FrameUpdate& u) {
    // Project car forward direction to screen space for the flow direction.
    // carVelocity = carForward * speed; normalize to get direction.
    const float carSpeed = glm::length(u.carVelocity);
    Vec2        screenFwd(0.0f, -1.0f);  // default: straight up (driving forward)
    if (carSpeed > 0.001f && u.camera) {
        Vec3 fwdDir = u.carVelocity / carSpeed;
        Vec4 vsFwd  = u.camera->get_view_matrix() * Vec4(fwdDir, 0.0f);
        Vec2 raw(vsFwd.x, vsFwd.y);
        if (glm::length(raw) > 0.001f)
            screenFwd = glm::normalize(raw);
    }
    // Normalize over the car's actual top speed (kMaxSpeed, now 92000 WU/s)
    // so speedFactor still spans 0..1 across the full speed range.
    const float speedFactor = glm::clamp(carSpeed / CarEntity::kMaxSpeed, 0.0f, 1.0f);
    step(u.frameIndex, u.dt, screenFwd, speedFactor, u.state.wetness, u.state.rainIntensity, u.wiperEnabled);
}

void WindshieldRainPass::record(const FrameCtx& c) {
    const GeometryProducer& geo = *c.res.geometry;
    const SceneGeometry&    car = geo.dynamic();
    if (geo.windshield_draws().empty() || !car.has_geometry())
        return;

    // Step the persistent wetness map, then snapshot the post-glass HDR scene so the windshield rain can refract it
    // (drops act as lenses). Leaves HDR in COLOR_ATTACHMENT_OPTIMAL.
    record_wetness_update(c.cmd);
    record_scene_snapshot(c.cmd, c.frameIndex, c.images.image(Img::Hdr, c.frameIndex), c.renderExtent);

    // Refractive water drops on the front windshield.
    VkDescriptorSet camSet = c.res.cameraUniforms->get_set(c.frameIndex);
    vkCmdBindDescriptorSets(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &camSet, 0, nullptr);
    record_draws(c.cmd, c.frameIndex, car.get_vertex_buffer(), car.get_index_buffer(), geo.windshield_draws());
}

void WindshieldRainPass::create(const RendererServices&                              s,
                                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                                VkDescriptorSetLayout cameraSetLayout) {
    m_extent         = extent;
    m_allocator      = s.allocator;
    m_physicalDevice = s.physicalDevice;
    m_commandPool    = s.commandPool;
    m_queue          = s.graphicsQueue;
    m_depthFormat    = resource::findDepthFormat(s.physicalDevice);

    createRenderPass(s.device, m_depthFormat);
    createFramebuffers(s.device, hdrViews, depthViews);
    createUBOs(s);
    createRefractionResources(s.device, s.physicalDevice, extent);
    createWetnessResources(s.device, s.physicalDevice, extent);
    createDescriptors(s.device);
    createPipeline(s.device, s.pipelineCache, cameraSetLayout);
    createWetnessPipeline(s.device, s.pipelineCache);
}

void WindshieldRainPass::step(uint32_t frameIndex, float deltaTime, Vec2 screenFlowDir, float speedFactor,
                              float wetness, float intensity, bool wiperEnabled) {
    m_time += deltaTime;
    m_wiperEnabled = wiperEnabled;

    // Continuous wiper: advance the phase only while enabled; the blade angle
    // is a sinusoid so the blade sweeps across the windshield and back.
    static_assert(swish::util::units::kTwoPi == 6.2831853f, "must stay bit-identical to the previous literal");
    constexpr float kTwoPi          = swish::util::units::kTwoPi;
    constexpr float kWiperSpeed     = 3.2f;   // rad/s of the phase oscillator
    constexpr float kWiperHalfSweep = 1.15f;  // ~±66° of blade travel
    if (wiperEnabled) {
        m_wiperPhase += kWiperSpeed * deltaTime;
        if (m_wiperPhase > kTwoPi)
            m_wiperPhase -= kTwoPi;
    }
    const float bladeAngle = std::sin(m_wiperPhase) * kWiperHalfSweep;

    // Water flow direction for the SCREEN-SPACE wetness-map advection. Here +Y is
    // screen-down, so gravity = (0,+1) drifts the field DOWN at rest, and aero
    // lift = (0.15,-1) streams it UP (and slightly sideways) at high speed. (This
    // is screen space, so the sign is NOT flipped the way the mesh-UV shader is.)
    const Vec2 gravity = Vec2(0.0f, 1.0f);
    const Vec2 aero    = Vec2(0.15f, -1.0f);
    // Crossover lifted to a higher normalized band (matches the mesh-UV shader's
    // smoothstep(0.25,0.60)): water only streams UP at genuinely high speed, not a
    // gentle cruise. speedFactor now spans 0..1 over the full top speed.
    const float aeroBias = glm::smoothstep(0.25f, 0.60f, speedFactor);
    m_waterFlow          = glm::normalize(glm::mix(gravity, aero, aeroBias));
    m_curIntensity       = intensity;
    m_dt                 = deltaTime;
    m_wiperAngle         = bladeAngle;
    m_advect             = deltaTime * (0.05f + 0.30f * speedFactor);  // drift this frame (UV)

    WindshieldRainUBO ubo{};
    ubo.flowAndTime = Vec4(screenFlowDir.x, screenFlowDir.y, speedFactor, m_time);
    // density (reserved/unused in shader), refractStrength dialed back 0.135 → 0.095
    // so the colored-light bokeh still smears through the big DISCRETE beads (ref
    // img B) without over-displacing into a gray smear. fresnelGain (screenAndRefr.w)
    // back to 0.06 — bright bead rims, not a glowing sheet.
    ubo.params        = Vec4(wetness, intensity, 60.0f, 0.095f);
    ubo.screenAndRefr = Vec4(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 0.06f);
    ubo.wiperState    = Vec4(bladeAngle, m_wiperPhase, wiperEnabled ? 1.0f : 0.0f, kWiperSpeed);
    std::memcpy(m_ubos[frameIndex].mapped(), &ubo, sizeof(ubo));
}

void WindshieldRainPass::record_wetness_update(VkCommandBuffer cmd) {
    // Fullscreen pass: write the new wetness into m_wetImages[1] (B), reading the
    // history m_wetImages[0] (A) via m_wetDescSet. Render pass leaves B in
    // SHADER_READ_ONLY (its finalLayout).
    VkRenderPassBeginInfo bi = vkx::makeRenderPassBeginInfo();
    bi.renderPass            = m_wetRenderPass;
    bi.framebuffer           = m_wetFramebuffer;
    bi.renderArea.extent     = m_extent;
    bi.clearValueCount       = 0;
    bi.pClearValues          = nullptr;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_wetPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_wetPipeLayout, 0, 1, &m_wetDescSet, 0, nullptr);

    WetnessPush pc{};
    pc.flow       = m_waterFlow;
    pc.intensity  = m_curIntensity;
    pc.dt         = m_dt;
    pc.wiperAngle = m_wiperAngle;
    pc.wiperOn    = m_wiperEnabled ? 1.0f : 0.0f;
    pc.aspect     = static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height);
    pc.advect     = m_advect;
    vkCmdPushConstants(cmd, m_wetPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);  // fullscreen triangle
    vkCmdEndRenderPass(cmd);

    // Copy B → A so next frame's history = this result (fixed bindings: the rain
    // pass always samples B, this pass always reads A).
    vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
        cmd, m_wetImages[1].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL>(
        cmd, m_wetImages[0].handle(), VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImage(cmd, m_wetImages[1].handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_wetImages[0].handle(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Both back to SHADER_READ — A for next frame's read, B for this frame's draw.
    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_wetImages[0].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_wetImages[1].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
}

void WindshieldRainPass::record_scene_snapshot(VkCommandBuffer cmd, uint32_t frameIndex, VkImage hdrImage,
                                               VkExtent2D extent) {
    // Snapshot the (post-glass) HDR scene into a sampleable image so the
    // fragment shader can refract it without a read/write feedback loop on HDR.
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL>(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL>(
        cmd, m_refrImages[frameIndex].handle(), VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent         = {extent.width, extent.height, 1};
    vkCmdCopyImage(cmd, hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_refrImages[frameIndex].handle(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, m_refrImages[frameIndex].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
    vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL>(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT);
}

void WindshieldRainPass::record_draws(VkCommandBuffer cmd, uint32_t frameIndex, VkBuffer carVBO, VkBuffer carIBO,
                                      const std::vector<DrawCall>& windshieldDCs) const {
    if (windshieldDCs.empty() || carVBO == VK_NULL_HANDLE)
        return;

    VkClearValue clearVals[2]{};
    auto         beginInfo      = vkx::makeRenderPassBeginInfo();
    beginInfo.renderPass        = m_renderPass;
    beginInfo.framebuffer       = m_framebuffers[frameIndex];
    beginInfo.renderArea.extent = m_extent;
    beginInfo.clearValueCount   = 2;
    beginInfo.pClearValues      = clearVals;

    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &carVBO, &offset);
    vkCmdBindIndexBuffer(cmd, carIBO, 0, VK_INDEX_TYPE_UINT32);

    // Set 1: rain UBO + refraction sampler + wetness sampler (set 0 = camera).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 1, 1, &m_descSets[frameIndex], 0,
                            nullptr);

    for (const auto& dc : windshieldDCs) {
        PushConstantData push{};
        push.model = dc.model;
        push.color = dc.color;
        vkCmdPushConstants(cmd, m_pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           kPushConstantModelColorSize, &push);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void WindshieldRainPass::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                  const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                                  VkDevice device) {
    m_extent = extent;
    destroyFramebuffers(device);
    createFramebuffers(device, hdrViews, depthViews);
    destroyRefractionResources(device);
    createRefractionResources(device, m_physicalDevice, extent);
    destroyWetnessResources(device);
    createWetnessResources(device, m_physicalDevice, extent);
    writeDescriptors(device);  // refraction + wetness views changed → rebind
}

void WindshieldRainPass::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_wetPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_wetPipeline, nullptr);
        m_wetPipeline = VK_NULL_HANDLE;
    }
    if (m_pipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
        m_pipeLayout = VK_NULL_HANDLE;
    }
    if (m_wetPipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_wetPipeLayout, nullptr);
        m_wetPipeLayout = VK_NULL_HANDLE;
    }
    if (m_descPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_ownLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_ownLayout, nullptr);
        m_ownLayout = VK_NULL_HANDLE;
    }
    if (m_wetSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_wetSetLayout, nullptr);
        m_wetSetLayout = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_ubos[i].reset();  // VMA-backed: frees buffer + sub-allocation (was persistently mapped)
    }
    destroyRefractionResources(device);
    if (m_refrSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_refrSampler, nullptr);
        m_refrSampler = VK_NULL_HANDLE;
    }
    destroyWetnessResources(device);
    if (m_wetRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_wetRenderPass, nullptr);
        m_wetRenderPass = VK_NULL_HANDLE;
    }
    destroyFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// ── Private helpers ────────────────────────────────────────────────────

void WindshieldRainPass::createRenderPass(VkDevice device, VkFormat depthFormat) {
    // Forward pass over HDR (LOAD) with read-only depth, like Rain/Glass, but the dependency also covers the
    // scene-snapshot copy (TRANSFER writes) and the fragment shader sampling the refraction image.
    const std::array<VkSubpassDependency, 1> deps = {vkx::subpass_dependency(
        VK_SUBPASS_EXTERNAL, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)};

    vkx::ColorPassDesc d;
    d.color        = {.format  = VK_FORMAT_R16G16B16A16_SFLOAT,
                      .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .final   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    d.depth        = vkx::AttachmentDesc{.format  = depthFormat,
                                         .load    = VK_ATTACHMENT_LOAD_OP_LOAD,
                                         .store   = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                         .initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                         .final   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    d.dependencies = deps;
    m_renderPass   = vkx::make_color_pass(device, d);
}

void WindshieldRainPass::createFramebuffers(VkDevice                                             device,
                                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                            const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView             atts[] = {hdrViews[i], depthViews[i]};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments    = atts;
        fbInfo.width           = m_extent.width;
        fbInfo.height          = m_extent.height;
        fbInfo.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]));
    }
}

void WindshieldRainPass::destroyFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void WindshieldRainPass::createUBOs(const RendererServices& s) {
    (void)s;  // allocator captured into m_allocator during init()
    const VkDeviceSize uboSize = sizeof(WindshieldRainUBO);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Host-visible, persistently mapped (VMA) — written every frame via .mapped().
        m_ubos[i] = gpu::hostVisibleBuffer(m_allocator, uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
}

void WindshieldRainPass::createRefractionResources(VkDevice device, VkPhysicalDevice physicalDevice,
                                                   VkExtent2D extent) {
    (void)physicalDevice;  // VMA holds the physical device; kept for signature symmetry
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_refrImages[i] =
            gpu::deviceLocalImage(m_allocator, extent.width, extent.height, m_refrFormat, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        m_refrViews[i] = vkx::make_image_view2d(device, m_refrImages[i].handle(), m_refrFormat);
    }

    if (m_refrSampler == VK_NULL_HANDLE) {
        m_refrSampler = vkx::make_sampler(
            device, {.mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR, .border = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK});
    }
}

void WindshieldRainPass::destroyRefractionResources(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_refrViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_refrViews[i], nullptr);
            m_refrViews[i] = VK_NULL_HANDLE;
        }
        m_refrImages[i].reset();  // VMA-backed: frees image + sub-allocation
    }
}

void WindshieldRainPass::createWetnessResources(VkDevice device, VkPhysicalDevice physicalDevice, VkExtent2D extent) {
    (void)physicalDevice;  // VMA holds the physical device; kept for signature symmetry
    for (int i = 0; i < 2; i++) {
        m_wetImages[i] =
            gpu::deviceLocalImage(m_allocator, extent.width, extent.height, m_wetFormat, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_wetViews[i] = vkx::make_image_view2d(device, m_wetImages[i].handle(), m_wetFormat);
    }

    if (m_wetRenderPass == VK_NULL_HANDLE) {
        // Fullscreen pass that overwrites every texel (DONT_CARE load), left in SHADER_READ_ONLY; the two
        // dependencies order it against the previous frame's reads/copies and the next copy / sampling.
        const std::array<VkSubpassDependency, 2> deps = {
            vkx::subpass_dependency(
                VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
            vkx::subpass_dependency(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT)};
        vkx::ColorPassDesc d;
        d.color         = {.format = m_wetFormat, .final = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        d.dependencies  = deps;
        m_wetRenderPass = vkx::make_color_pass(device, d);
    }

    // Framebuffer targets B (m_wetImages[1]) — the update always writes B.
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_wetRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &m_wetViews[1];
    fbInfo.width           = extent.width;
    fbInfo.height          = extent.height;
    fbInfo.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_wetFramebuffer));

    clearWetnessImages(device);
}

void WindshieldRainPass::destroyWetnessResources(VkDevice device) {
    if (m_wetFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_wetFramebuffer, nullptr);
        m_wetFramebuffer = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; i++) {
        if (m_wetViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_wetViews[i], nullptr);
            m_wetViews[i] = VK_NULL_HANDLE;
        }
        m_wetImages[i].reset();  // VMA-backed: frees image + sub-allocation
    }
}

void WindshieldRainPass::clearWetnessImages(VkDevice device) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = m_commandPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cb));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &bi));

    VkClearColorValue       cc{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (int i = 0; i < 2; i++) {
        vkx::image_barrier<VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL>(cb, m_wetImages[i].handle(),
                                                                                            VK_IMAGE_ASPECT_COLOR_BIT);
        vkCmdClearColorImage(cb, m_wetImages[i].handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &range);
        vkx::image_barrier<VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
            cb, m_wetImages[i].handle(), VK_IMAGE_ASPECT_COLOR_BIT);
    }

    VK_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    VK_CHECK(vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_queue));
    vkFreeCommandBuffers(device, m_commandPool, 1, &cb);
}

void WindshieldRainPass::createDescriptors(VkDevice device) {
    // Rain set (set 1): UBO + refraction sampler + wetness sampler.
    VkDescriptorSetLayoutBinding rainB[3]{};
    rainB[0].binding         = 0;
    rainB[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rainB[0].descriptorCount = 1;
    rainB[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    rainB[1].binding         = 1;
    rainB[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rainB[1].descriptorCount = 1;
    rainB[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    rainB[2].binding         = 2;
    rainB[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rainB[2].descriptorCount = 1;
    rainB[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo rainLI{};
    rainLI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rainLI.bindingCount = 3;
    rainLI.pBindings    = rainB;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &rainLI, nullptr, &m_ownLayout));

    // Wetness-update set (set 0): previous wetness sampler.
    VkDescriptorSetLayoutBinding wetB{};
    wetB.binding         = 0;
    wetB.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wetB.descriptorCount = 1;
    wetB.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo wetLI{};
    wetLI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    wetLI.bindingCount = 1;
    wetLI.pBindings    = &wetB;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &wetLI, nullptr, &m_wetSetLayout));

    m_descPool = vkx::make_pool(device, {.maxSets               = MAX_FRAMES_IN_FLIGHT + 1,
                                         .combinedImageSamplers = 2 * MAX_FRAMES_IN_FLIGHT + 1,
                                         .uniformBuffers        = MAX_FRAMES_IN_FLIGHT});

    vkx::allocate_sets(device, m_descPool, m_ownLayout, m_descSets);
    m_wetDescSet = vkx::allocate_set(device, m_descPool, m_wetSetLayout);

    writeDescriptors(device);
}

void WindshieldRainPass::writeDescriptors(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufInfo{m_ubos[i].handle(), 0, sizeof(WindshieldRainUBO)};
        VkDescriptorImageInfo  refrInfo{m_refrSampler, m_refrViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo  wetInfo{m_refrSampler, m_wetViews[1],
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};  // B = current

        VkWriteDescriptorSet w[3]{};
        w[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet          = m_descSets[i];
        w[0].dstBinding      = 0;
        w[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].descriptorCount = 1;
        w[0].pBufferInfo     = &bufInfo;
        w[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet          = m_descSets[i];
        w[1].dstBinding      = 1;
        w[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1;
        w[1].pImageInfo      = &refrInfo;
        w[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet          = m_descSets[i];
        w[2].dstBinding      = 2;
        w[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].descriptorCount = 1;
        w[2].pImageInfo      = &wetInfo;
        vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
    }

    // Wetness-update set reads A (m_wetImages[0]) = history.
    VkDescriptorImageInfo histInfo{m_refrSampler, m_wetViews[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet  ww{};
    ww.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ww.dstSet          = m_wetDescSet;
    ww.dstBinding      = 0;
    ww.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ww.descriptorCount = 1;
    ww.pImageInfo      = &histInfo;
    vkUpdateDescriptorSets(device, 1, &ww, 0, nullptr);
}

void WindshieldRainPass::createPipeline(VkDevice device, VkPipelineCache cache, VkDescriptorSetLayout cameraSetLayout) {
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = kPushConstantModelColorSize;  // shaders use model+color only

    m_pipeLayout = pipeline::create_layout(device, {cameraSetLayout, m_ownLayout}, {pcRange});

    auto binding = Vertex::getBindingDescription();
    auto attrs   = Vertex::getAttributeDescriptions();

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "windshield_rain.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "windshield_rain.frag.spv";
    cfg.vertexBindings   = {binding};
    cfg.vertexAttributes = {attrs[0], attrs[1], attrs[2], attrs[3]};
    // The cockpit camera looks at the windshield's cabin-facing face, whose
    // outward normal points away from the viewer (a back face under CCW winding),
    // so cull FRONT to shade exactly that surface (verified visually).
    cfg.cullMode         = VK_CULL_MODE_FRONT_BIT;
    cfg.enableDepthTest  = true;
    cfg.enableDepthWrite = false;
    cfg.additiveBlending = false;
    cfg.enableBlending   = true;  // refractive water alpha-blends over the glass
    cfg.pipelineLayout   = m_pipeLayout;

    m_pipeline = pipeline::create_graphics(device, cache, cfg, m_renderPass, m_extent);
}

void WindshieldRainPass::createWetnessPipeline(VkDevice device, VkPipelineCache cache) {
    const VkPushConstantRange pcRange = vkx::push_range<WetnessPush>(VK_SHADER_STAGE_FRAGMENT_BIT);

    m_wetPipeLayout = pipeline::create_layout(device, {m_wetSetLayout}, {pcRange});

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "windshield_wetness.frag.spv";
    cfg.noVertexInput    = true;
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = false;
    cfg.enableDepthWrite = false;
    cfg.additiveBlending = false;
    cfg.enableBlending   = false;  // overwrite the wetness map
    cfg.pipelineLayout   = m_wetPipeLayout;

    m_wetPipeline = pipeline::create_graphics(device, cache, cfg, m_wetRenderPass, m_extent);
}

std::unique_ptr<IRenderPass> make_windshield_rain_pass() {
    return std::make_unique<WindshieldRainPass>();
}

}  // namespace swish
