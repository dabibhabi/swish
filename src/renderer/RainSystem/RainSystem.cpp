#include "RainSystem.h"

#include "../../utils/Types.h"
#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../Pipeline/Pipeline.h"
#include "../Renderer/RendererServices.h"
#include "../ResourceManager/ResourceManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace swish {

// ── Rain volume parameters ─────────────────────────────────────────────
static constexpr float kHalfExtent  = 20000.0f;  // 20 m radius around camera
static constexpr float kDropSpeed   = 9000.0f;   // 9 m/s base fall speed
static constexpr float kStreakLen   = 1200.0f;   // 1.2 m streak length (heavy rain)
static constexpr float kWetRate     = 0.08f;     // wetness accumulation rate (s⁻¹)
static constexpr float kDryRate     = 0.012f;    // wetness decay rate (s⁻¹)

void RainSystem::init(const RendererServices& s,
                      const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                      const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                      VkExtent2D extent,
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
    m_time     += deltaTime;

    // Wetness accumulates quickly, drains slowly
    float rate  = (m_wetness < m_intensity) ? kWetRate : kDryRate;
    m_wetness  += (m_intensity - m_wetness) * rate * deltaTime;
    m_wetness   = std::clamp(m_wetness, 0.0f, 1.0f);

    float streakLen = kStreakLen * (0.5f + 0.5f * m_intensity);

    RainUBO ubo{};
    ubo.windAndTime = Vec4(wind.x, wind.y, wind.z, m_time);
    ubo.params      = Vec4(m_intensity, streakLen, kDropSpeed, kHalfExtent);
    std::memcpy(m_rainUBOMapped[frameIndex], &ubo, sizeof(ubo));
}

void RainSystem::record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (m_intensity < 0.001f) return;

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
    VkViewport vp{0.0f, 0.0f,
                  static_cast<float>(m_extent.width),
                  static_cast<float>(m_extent.height),
                  0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, m_extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Binding 0: per-vertex quad corners
    VkBuffer     vertBufs[] = {m_quadVBO, m_instanceBuffer};
    VkDeviceSize offsets[]  = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertBufs, offsets);
    vkCmdBindIndexBuffer(cmd, m_quadIBO, 0, VK_INDEX_TYPE_UINT16);

    // Set 1: rain UBO (set 0 = camera, already bound by Renderer before this call)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeLayout, 1, 1, &m_descSets[frameIndex], 0, nullptr);

    vkCmdDrawIndexed(cmd, 6, kRainMaxDrops, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void RainSystem::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                           const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews,
                           VkExtent2D extent,
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

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_rainUBOs[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, m_rainUBOMemories[i]);
            vkDestroyBuffer(device, m_rainUBOs[i], nullptr);
            vkFreeMemory(device, m_rainUBOMemories[i], nullptr);
            m_rainUBOs[i]         = VK_NULL_HANDLE;
            m_rainUBOMemories[i]  = VK_NULL_HANDLE;
            m_rainUBOMapped[i]    = nullptr;
        }
    }

    destroyFramebuffers(device);

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    if (m_instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_instanceBuffer, nullptr);
        vkFreeMemory(device, m_instanceBufferMemory, nullptr);
        m_instanceBuffer       = VK_NULL_HANDLE;
        m_instanceBufferMemory = VK_NULL_HANDLE;
    }
    if (m_quadIBO != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_quadIBO, nullptr);
        vkFreeMemory(device, m_quadIBOMemory, nullptr);
        m_quadIBO       = VK_NULL_HANDLE;
        m_quadIBOMemory = VK_NULL_HANDLE;
    }
    if (m_quadVBO != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_quadVBO, nullptr);
        vkFreeMemory(device, m_quadVBOMemory, nullptr);
        m_quadVBO       = VK_NULL_HANDLE;
        m_quadVBOMemory = VK_NULL_HANDLE;
    }
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
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
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

void RainSystem::createFramebuffers(VkDevice device,
                                    const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
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
        {{ 0.5f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 1.0f}, {0.0f, 1.0f}},
    }};
    const std::array<uint16_t, 6> indices{0, 1, 2, 2, 3, 0};

    const VkDeviceSize vSize = sizeof(verts);
    const VkDeviceSize iSize = sizeof(indices);
    const VkMemoryPropertyFlags hostFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // VBO
    ResourceManager::createBuffer(s.device, s.physicalDevice, vSize,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostFlags,
                                  m_quadVBO, m_quadVBOMemory);
    void* vMap;
    VK_CHECK(vkMapMemory(s.device, m_quadVBOMemory, 0, vSize, 0, &vMap));
    std::memcpy(vMap, verts.data(), vSize);
    vkUnmapMemory(s.device, m_quadVBOMemory);

    // IBO
    ResourceManager::createBuffer(s.device, s.physicalDevice, iSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostFlags,
                                  m_quadIBO, m_quadIBOMemory);
    void* iMap;
    VK_CHECK(vkMapMemory(s.device, m_quadIBOMemory, 0, iSize, 0, &iMap));
    std::memcpy(iMap, indices.data(), iSize);
    vkUnmapMemory(s.device, m_quadIBOMemory);
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
    const VkMemoryPropertyFlags hostFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    ResourceManager::createBuffer(s.device, s.physicalDevice, size,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostFlags,
                                  m_instanceBuffer, m_instanceBufferMemory);
    void* mapped;
    VK_CHECK(vkMapMemory(s.device, m_instanceBufferMemory, 0, size, 0, &mapped));
    std::memcpy(mapped, seeds.data(), size);
    vkUnmapMemory(s.device, m_instanceBufferMemory);
}

void RainSystem::createRainUBOs(const RendererServices& s) {
    const VkDeviceSize          uboSize   = sizeof(RainUBO);
    const VkMemoryPropertyFlags hostFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ResourceManager::createBuffer(s.device, s.physicalDevice, uboSize,
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostFlags,
                                      m_rainUBOs[i], m_rainUBOMemories[i]);
        VK_CHECK(vkMapMemory(s.device, m_rainUBOMemories[i], 0, uboSize, 0, &m_rainUBOMapped[i]));
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

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descPool));

    // Allocate sets
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_rainLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descSets.data()));

    // Write rain UBO into each set
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufInfo{m_rainUBOs[i], 0, sizeof(RainUBO)};
        VkWriteDescriptorSet   write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_descSets[i];
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
    cfg.vertShaderPath = std::string(SHADER_DIR) + "rain.vert.spv";
    cfg.fragShaderPath = std::string(SHADER_DIR) + "rain.frag.spv";
    cfg.vertexBindings  = {bindings[0], bindings[1]};
    cfg.vertexAttributes = {attrs[0], attrs[1], attrs[2]};
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = true;   // scene depth occludes rain behind car surfaces
    cfg.enableDepthWrite = false;
    cfg.additiveBlending = true;
    cfg.pipelineLayout   = m_pipeLayout;

    m_pipeline = Pipeline::create(device, cfg, m_renderPass, m_extent);
}

}  // namespace swish
