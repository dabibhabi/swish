#include "RainSystem.h"

#include "RainPhysics.h"

#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../Pipeline/Pipeline.h"
#include "../Renderer/RendererServices.h"
#include "../ResourceManager/ResourceManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace swish {

// ── Rain volume parameters ─────────────────────────────────────────────
static constexpr float kHalfExtent = 20000.0f;  // 20 m radius around camera
static constexpr float kDropSpeed  = 9000.0f;   // 9 m/s — reference speed for the wind cap only
static constexpr float kStreakLen  = 3200.0f;   // 3.2 m streak length — long, thin, motion-blurred
static constexpr float kMaxRainRate = 25.0f;    // mm/hr at intensity 1.0 (heavy rain)
static constexpr float kWetRate    = 0.08f;     // wetness accumulation rate (s⁻¹)
static constexpr float kDryRate    = 0.012f;    // wetness decay rate (s⁻¹)

// ── Far parallax layer scaling (relative to the near layer) ────────────
static constexpr float kFarHalfExtentScale = 2.0f;
static constexpr float kFarIntensityScale  = 0.55f;  // dimmer/farther (fragAlpha ∝ intensity)
static constexpr float kFarStreakScale     = 0.8f;
static constexpr float kFarTimePhase       = 317.0f;  // decorrelate far field from near (shared seeds)

void RainSystem::init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                      const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                      VkDescriptorSetLayout cameraSetLayout) {
    m_extent      = extent;
    m_depthFormat = ResourceManager::findDepthFormat(s.physicalDevice);

    createRenderPass(s.device);
    createFramebuffers(s.device, hdrViews, depthViews);
    createGeometry(s);
    createInstanceBuffer(s);
    createRainUBOs(s);
    createDescriptors(s.device, cameraSetLayout);
    createPipeline(s.device, cameraSetLayout);
}

void RainSystem::update(uint32_t frameIndex, float deltaTime, float intensity, Vec3 wind) {
    m_intensity = intensity;
    m_time += deltaTime;

    // Wetness accumulates quickly, drains slowly
    float rate = (m_wetness < m_intensity) ? kWetRate : kDryRate;
    m_wetness += (m_intensity - m_wetness) * rate * deltaTime;
    m_wetness = std::clamp(m_wetness, 0.0f, 1.0f);

    float streakLen = kStreakLen * (0.5f + 0.5f * m_intensity);

    // Keep the vertical fall dominant. The caller subtracts the car velocity
    // (up to kMaxForwardSpeed ≈ 30000 WU/s) from the wind, which can far exceed
    // kDropSpeed and tip the velocity nearly parallel to the view direction —
    // a streak aligned with the view foreshortens into a swinging dot. Capping
    // the horizontal drift to a fraction of the fall speed keeps streaks reading
    // as vertical-diagonal streaks while still leaning into the direction of travel.
    const float maxHoriz = 0.6f * kDropSpeed;
    float       hlen     = std::sqrt(wind.x * wind.x + wind.z * wind.z);
    if (hlen > maxHoriz && hlen > 1e-5f) {
        float scale = maxHoriz / hlen;
        wind.x *= scale;
        wind.z *= scale;
    }

    // One physical rain rate R (mm/hr) drives per-drop size + fall speed in the
    // shader (Marshall–Palmer + Gunn–Kinzer, see RainPhysics.h) and the wetness
    // target — replacing five independent 0–1 knobs.
    const float rainRate = intensity_to_rain_rate(m_intensity, kMaxRainRate);

    RainUBO ubo{};
    ubo.windAndTime = Vec4(wind.x, wind.y, wind.z, m_time);
    ubo.params      = Vec4(m_intensity, streakLen, rainRate, kHalfExtent);
    std::memcpy(m_rainUBOs[frameIndex].mapped(), &ubo, sizeof(ubo));

    // Far parallax layer: shares the same wind, seeds, and rain rate, but a larger
    // volume + lower intensity + time phase render it as dimmer, farther streaks.
    RainUBO farUbo{};
    farUbo.windAndTime = Vec4(wind.x, wind.y, wind.z, m_time + kFarTimePhase);
    farUbo.params = Vec4(m_intensity * kFarIntensityScale, streakLen * kFarStreakScale, rainRate,
                         kHalfExtent * kFarHalfExtentScale);
    std::memcpy(m_rainUBOsFar[frameIndex].mapped(), &farUbo, sizeof(farUbo));
}

void RainSystem::record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (m_intensity < 0.001f)
        return;

    // Two attachments (color + depth), both LOAD_OP_LOAD — clear values are ignored
    // but the count must match the render pass attachment count.
    VkClearValue clearVals[2]{};

    auto beginInfo              = vk::makeRenderPassBeginInfo();
    beginInfo.renderPass        = m_renderPass;
    beginInfo.framebuffer       = m_framebuffers[frameIndex];
    beginInfo.renderArea.extent = m_extent;
    beginInfo.clearValueCount   = 2;
    beginInfo.pClearValues      = clearVals;

    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamic viewport + scissor (matches Renderer pattern)
    VkViewport vp{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Binding 0: per-vertex quad corners
    VkBuffer     vertBufs[] = {m_quadVBO.handle(), m_instanceBuffer.handle()};
    VkDeviceSize offsets[]  = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertBufs, offsets);
    vkCmdBindIndexBuffer(cmd, m_quadIBO.handle(), 0, VK_INDEX_TYPE_UINT16);

    // Set 1: rain UBO (set 0 = camera, already bound by Renderer before this call).
    // Two draws reuse the same pipeline/geometry/instance buffer; additive blending +
    // depthWrite=false make the layer order irrelevant (no sorting needed).
    //
    // Scale the visible drop count with intensity (≈ rain rate): light rain shows
    // fewer drops rather than the same 8192 at a lower alpha.
    uint32_t drawCount = static_cast<uint32_t>(static_cast<float>(kRainMaxDrops) * m_intensity + 0.5f);
    drawCount          = std::clamp(drawCount, 1u, kRainMaxDrops);

    // Far parallax layer first (dimmer, farther).
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 1, 1, &m_descSetsFar[frameIndex], 0,
                            nullptr);
    vkCmdDrawIndexed(cmd, 6, drawCount, 0, 0, 0);

    // Near layer.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 1, 1, &m_descSets[frameIndex], 0,
                            nullptr);
    vkCmdDrawIndexed(cmd, 6, drawCount, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void RainSystem::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                          const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                          VkDevice device) {
    m_extent = extent;
    destroyFramebuffers(device);
    createFramebuffers(device, hdrViews, depthViews);
}

void RainSystem::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipeLayout, nullptr);
        m_pipeLayout = VK_NULL_HANDLE;
    }

    if (m_descPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_rainLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_rainLayout, nullptr);
        m_rainLayout = VK_NULL_HANDLE;
    }

    // Far descriptor sets need no explicit free — they are released when m_descPool
    // is destroyed above; m_rainLayout is shared with the near sets.
    // RAII (VMA): reset frees each buffer + its sub-allocation (persistent maps
    // are released by vmaDestroyBuffer — no manual vkUnmapMemory).
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_rainUBOs[i].reset();
        m_rainUBOsFar[i].reset();
    }

    destroyFramebuffers(device);

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_instanceBuffer.reset();
    m_quadIBO.reset();
    m_quadVBO.reset();
}

// ── Private helpers ────────────────────────────────────────────────────

void RainSystem::createRenderPass(VkDevice device) {
    // Attachment 0: HDR color — LOAD to preserve deferred lighting output.
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Attachment 1: depth — read-only; scene depth is in DEPTH_STENCIL_READ_ONLY_OPTIMAL
    // after transitionGBufferForLighting() transitions it from ATTACHMENT_OPTIMAL.
    // The lighting fullscreen pass samples depth from this layout; we inherit that state.
    VkAttachmentDescription depthAtt{};
    depthAtt.format         = m_depthFormat;
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // After lighting pass: color was written (COLOR_ATTACHMENT_OUTPUT) and depth was
    // sampled by the lighting shader (FRAGMENT_SHADER). Both must be visible to rain.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkAttachmentDescription atts[] = {colorAtt, depthAtt};

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments    = atts;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass));
}

void RainSystem::createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                                    const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkImageView atts[] = {hdrViews[i], depthViews[i]};

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

void RainSystem::destroyFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void RainSystem::createGeometry(const RendererServices& s) {
    // Four billboard corners: (localPos.x, localPos.y, uv.x, uv.y)
    // localPos.x in [-0.5, +0.5] (perpendicular to fall, screen width)
    // localPos.y in [0, 1]       (along fall direction, streak length)
    const std::array<RainQuadVertex, 4> verts{{
        {{-0.5f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 1.0f}, {0.0f, 1.0f}},
    }};
    const std::array<uint16_t, 6>       indices{0, 1, 2, 2, 3, 0};

    const VkDeviceSize vSize = sizeof(verts);
    const VkDeviceSize iSize = sizeof(indices);

    // Host-visible, persistently mapped (VMA) — written once here.
    m_quadVBO = gpu::hostVisibleBuffer(s.allocator, vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(m_quadVBO.mapped(), verts.data(), vSize);

    m_quadIBO = gpu::hostVisibleBuffer(s.allocator, iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(m_quadIBO.mapped(), indices.data(), iSize);
}

void RainSystem::createInstanceBuffer(const RendererServices& s) {
    // Seed the instance buffer with random positions in [0,1]³ + size variation.
    // These are never changed after upload — all animation is in the vertex shader.
    std::mt19937                          rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    std::vector<RainInstance> seeds(kRainMaxDrops);
    for (auto& inst : seeds) {
        inst.seed = Vec4(dist(rng), dist(rng), dist(rng), dist(rng));
    }

    const VkDeviceSize size = sizeof(RainInstance) * kRainMaxDrops;

    m_instanceBuffer = gpu::hostVisibleBuffer(s.allocator, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(m_instanceBuffer.mapped(), seeds.data(), size);
}

void RainSystem::createRainUBOs(const RendererServices& s) {
    const VkDeviceSize uboSize = sizeof(RainUBO);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Host-visible, persistently mapped (VMA) — written every frame via .mapped().
        m_rainUBOs[i]    = gpu::hostVisibleBuffer(s.allocator, uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_rainUBOsFar[i] = gpu::hostVisibleBuffer(s.allocator, uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
}

void RainSystem::createDescriptors(VkDevice device, VkDescriptorSetLayout cameraSetLayout) {
    // Set 1 layout: single uniform buffer (rain UBO)
    VkDescriptorSetLayoutBinding rainBinding{};
    rainBinding.binding         = 0;
    rainBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rainBinding.descriptorCount = 1;
    rainBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &rainBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_rainLayout));

    // Descriptor pool — sized for BOTH the near and far per-frame sets
    // (2 * MAX_FRAMES_IN_FLIGHT), or vkAllocateDescriptorSets for the far sets
    // would fail with VK_ERROR_OUT_OF_POOL_MEMORY.
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = 2 * MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool));

    // Allocate sets — near and far share the same set-1 layout (m_rainLayout).
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_rainLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descSets.data()));
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descSetsFar.data()));

    // Write the near + far rain UBO into their respective sets. Each
    // VkDescriptorBufferInfo must outlive its vkUpdateDescriptorSets call, so
    // keep both alive in this iteration and submit them as a 2-element batch.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufInfo{m_rainUBOs[i].handle(), 0, sizeof(RainUBO)};
        VkDescriptorBufferInfo bufInfoFar{m_rainUBOsFar[i].handle(), 0, sizeof(RainUBO)};

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_descSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bufInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_descSetsFar[i];
        writes[1].dstBinding      = 0;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &bufInfoFar;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    (void)cameraSetLayout;  // used only to build the pipeline layout below
}

void RainSystem::createPipeline(VkDevice device, VkDescriptorSetLayout cameraSetLayout) {
    // Layout: set 0 = camera UBO (shared), set 1 = rain UBO (owned)
    m_pipeLayout = Pipeline::createLayout(device, {cameraSetLayout, m_rainLayout});

    // Vertex input: binding 0 = per-vertex quad corner, binding 1 = per-instance seed
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(RainQuadVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(RainInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[3]{};
    // localPos (binding 0, location 0): vec2
    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(RainQuadVertex, localPos);
    // uv (binding 0, location 1): vec2
    attrs[1].binding  = 0;
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset   = offsetof(RainQuadVertex, uv);
    // seed (binding 1, location 2): vec4
    attrs[2].binding  = 1;
    attrs[2].location = 2;
    attrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset   = offsetof(RainInstance, seed);

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "rain.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "rain.frag.spv";
    cfg.vertexBindings   = {bindings[0], bindings[1]};
    cfg.vertexAttributes = {attrs[0], attrs[1], attrs[2]};
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = true;  // scene depth occludes rain behind car surfaces
    cfg.enableDepthWrite = false;
    cfg.additiveBlending = true;
    cfg.pipelineLayout   = m_pipeLayout;

    m_pipeline = Pipeline::create(device, cfg, m_renderPass, m_extent);
}

}  // namespace swish
