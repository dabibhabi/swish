#include "SpraySystem.h"

#include "../../utils/VulkanCheck.h"
#include "../../utils/VulkanInit.h"
#include "../Pipeline/Pipeline.h"
#include "../Renderer/RendererServices.h"
#include "../ResourceManager/ResourceManager.h"

#include <array>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

namespace swish {

// ── Spray tuning constants (WU; 1 m = 1000 WU) ─────────────────────────
static constexpr float kSprayRefSpeed  = 20000.0f;  // speed (WU/s ≈ 45 mph) at which spray reaches full
static constexpr float kSprayGravity   = -9810.0f;  // gravity (WU/s²) — real 9.81 m/s²
static constexpr float kSprayDrag      = 0.85f;     // air drag (1/s) — lower so the mist lofts + thins
static constexpr float kSprayUpSpeed   = 4500.0f;   // launch up speed (WU/s) — taller plume
static constexpr float kSprayBackSpeed = 3000.0f;   // backward kick relative to travel (WU/s)
static constexpr float kSpraySpread    = 1200.0f;   // lateral spread at the wheels (WU ≈ 1.2 m)

void SpraySystem::init(const RendererServices& s, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                       const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                       VkDescriptorSetLayout cameraSetLayout) {
    m_extent      = extent;
    m_depthFormat = ResourceManager::findDepthFormat(s.physicalDevice);

    createParticleBuffer(s);
    createSimUBOs(s);
    createComputeDescriptors(s.device);
    createComputePipeline(s.device);
    createRenderPass(s.device);
    createFramebuffers(s.device, hdrViews, depthViews);
    createDrawDescriptors(s.device);
    createDrawPipeline(s.device, cameraSetLayout);
}

void SpraySystem::update(uint32_t frameIndex, float deltaTime, Vec3 spawnCentre, Vec3 carVelocity, float wetness,
                         const SprayParams& params) {
    // Frame seed decorrelates respawns/directions; keep it < 2²⁴ (shader uses uint()).
    m_frameSeed = std::fmod(m_frameSeed + 1.0f, 16777216.0f);

    const float speed       = glm::length(carVelocity);
    const float speedFactor = glm::clamp(speed / kSprayRefSpeed, 0.0f, 1.0f);
    // Emission gated by wetness × speed — dry road (release default) emits nothing.
    const float emit = params.enabled ? glm::clamp(params.density * wetness * speedFactor, 0.0f, 1.0f) : 0.0f;

    m_active  = params.enabled && wetness > 0.02f && speedFactor > 0.01f;
    m_opacity = params.opacity;

    SpraySimUBO ubo{};
    ubo.spawnPosDt = Vec4(spawnCentre, deltaTime);
    ubo.carVel     = Vec4(carVelocity, emit);
    ubo.params     = Vec4(params.lifetime, params.size, kSprayGravity, kSprayDrag);
    ubo.params2    = Vec4(kSpraySpread, m_frameSeed, kSprayUpSpeed, kSprayBackSpeed);
    std::memcpy(m_simUBOs[frameIndex].mapped(), &ubo, sizeof(ubo));
}

void SpraySystem::record_compute(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (!m_active)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeLayout, 0, 1, &m_computeSets[frameIndex],
                            0, nullptr);
    vkCmdDispatch(cmd, kSprayMaxParticles / 64, 1, 1);

    // Compute wrote the particle SSBO; the billboard vertex shader reads it. Order
    // the write before the read (a plain buffer barrier — the buffer never changes
    // layout). Recorded OUTSIDE any render pass, before the spray draw.
    VkBufferMemoryBarrier bmb{};
    bmb.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bmb.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    bmb.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.buffer              = m_particleBuffer.handle();
    bmb.offset              = 0;
    bmb.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                         1, &bmb, 0, nullptr);
}

void SpraySystem::record_draws(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (!m_active)
        return;

    VkClearValue clearVals[2]{};  // LOAD_OP_LOAD — ignored, but count must match

    auto beginInfo              = vk::makeRenderPassBeginInfo();
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawPipeline);
    // Set 0 (camera) is bound by the caller (Renderer::recordSprayPass). Set 1 = SSBO.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_drawPipeLayout, 1, 1, &m_drawSet, 0, nullptr);

    Vec4 tune(m_opacity, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(cmd, m_drawPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4), &tune);

    // 6 verts/quad × kSprayMaxParticles instances; dead particles cull in the VS.
    vkCmdDraw(cmd, 6, kSprayMaxParticles, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void SpraySystem::recreate(const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
                           const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& depthViews, VkExtent2D extent,
                           VkDevice device) {
    m_extent = extent;
    destroyFramebuffers(device);
    createFramebuffers(device, hdrViews, depthViews);
}

void SpraySystem::cleanup(VkDevice device) {
    if (m_drawPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_drawPipeline, nullptr);
        m_drawPipeline = VK_NULL_HANDLE;
    }
    if (m_drawPipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_drawPipeLayout, nullptr);
        m_drawPipeLayout = VK_NULL_HANDLE;
    }
    if (m_drawPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_drawPool, nullptr);
        m_drawPool = VK_NULL_HANDLE;
    }
    if (m_drawSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_drawSetLayout, nullptr);
        m_drawSetLayout = VK_NULL_HANDLE;
    }

    destroyFramebuffers(device);
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    if (m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    if (m_computePipeLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_computePipeLayout, nullptr);
        m_computePipeLayout = VK_NULL_HANDLE;
    }
    if (m_computePool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_computePool, nullptr);
        m_computePool = VK_NULL_HANDLE;
    }
    if (m_computeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_computeSetLayout, nullptr);
        m_computeSetLayout = VK_NULL_HANDLE;
    }

    // RAII (VMA): reset frees each buffer + its sub-allocation.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        m_simUBOs[i].reset();
    m_particleBuffer.reset();
}

// ── Private helpers ────────────────────────────────────────────────────

void SpraySystem::createParticleBuffer(const RendererServices& s) {
    const VkDeviceSize size = sizeof(SprayParticle) * kSprayMaxParticles;
    m_particleBuffer        = gpu::deviceLocalBuffer(s.allocator, size,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    // Zero the buffer once so every particle starts dead (life = 0). One-time submit
    // (mirrors SceneGeometry's staging pattern); the compute pass takes over after.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = s.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(s.device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    vkCmdFillBuffer(cmd, m_particleBuffer.handle(), 0, VK_WHOLE_SIZE, 0);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(s.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(s.graphicsQueue);

    vkFreeCommandBuffers(s.device, s.commandPool, 1, &cmd);
}

void SpraySystem::createSimUBOs(const RendererServices& s) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        m_simUBOs[i] = gpu::hostVisibleBuffer(s.allocator, sizeof(SpraySimUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

void SpraySystem::createComputeDescriptors(VkDevice device) {
    // Set 0: binding 0 = particle SSBO (RW), binding 1 = sim UBO. Both compute-stage.
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings    = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_computeSetLayout));

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = MAX_FRAMES_IN_FLIGHT;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_computePool));

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(m_computeSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_computePool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts        = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_computeSets.data()));

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo ssboInfo{m_particleBuffer.handle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo uboInfo{m_simUBOs[i].handle(), 0, sizeof(SpraySimUBO)};

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_computeSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &ssboInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_computeSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &uboInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

void SpraySystem::createComputePipeline(VkDevice device) {
    m_computePipeLayout = Pipeline::createLayout(device, {m_computeSetLayout});
    m_computePipeline =
        Pipeline::createCompute(device, std::string(SHADER_DIR) + "spray_sim.comp.spv", m_computePipeLayout);
}

void SpraySystem::createRenderPass(VkDevice device) {
    // Identical to RainSystem: HDR color LOAD/STORE + read-only depth LOAD.
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

void SpraySystem::createFramebuffers(VkDevice device, const std::array<VkImageView, MAX_FRAMES_IN_FLIGHT>& hdrViews,
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

void SpraySystem::destroyFramebuffers(VkDevice device) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffers[i], nullptr);
            m_framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void SpraySystem::createDrawDescriptors(VkDevice device) {
    // Set 1: binding 0 = particle SSBO, read in the vertex shader. A single set —
    // the SSBO is shared, so it doesn't need per-frame duplication.
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding         = 0;
    ssboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount = 1;
    ssboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &ssboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_drawSetLayout));

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = 1;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_drawPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_drawPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_drawSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &m_drawSet));

    VkDescriptorBufferInfo ssboInfo{m_particleBuffer.handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet   write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_drawSet;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo     = &ssboInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void SpraySystem::createDrawPipeline(VkDevice device, VkDescriptorSetLayout cameraSetLayout) {
    // set 0 = camera (shared), set 1 = particle SSBO (owned); push = opacity (vec4).
    VkPushConstantRange pc{};
    pc.stageFlags    = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset        = 0;
    pc.size          = sizeof(Vec4);
    m_drawPipeLayout = Pipeline::createLayout(device, {cameraSetLayout, m_drawSetLayout}, {pc});

    PipelineConfig cfg{};
    cfg.vertShaderPath   = std::string(SHADER_DIR) + "spray.vert.spv";
    cfg.fragShaderPath   = std::string(SHADER_DIR) + "spray.frag.spv";
    cfg.noVertexInput    = true;  // gl_VertexIndex quad + gl_InstanceIndex SSBO lookup
    cfg.cullMode         = VK_CULL_MODE_NONE;
    cfg.enableDepthTest  = true;  // scene depth occludes spray behind the car body
    cfg.enableDepthWrite = false;
    cfg.additiveBlending = true;
    cfg.pipelineLayout   = m_drawPipeLayout;

    m_drawPipeline = Pipeline::create(device, cfg, m_renderPass, m_extent);
}

}  // namespace swish
