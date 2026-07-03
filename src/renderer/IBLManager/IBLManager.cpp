#include "IBLManager.h"

#include "../../utils/VulkanCheck.h"
#include "../Pipeline/Pipeline.h"

#include <array>
#include <string>

namespace swish {

namespace {

// Per-face basis so a cube face pixel maps to a world direction as
// dir = normalize(F + s·R + t·U), s,t ∈ [-1,1]. Matches the hardware samplerCube
// face convention (+X,-X,+Y,-Y,+Z,-Z) so texture(cube, dir) reads the texel this
// bakes there. (Verified visually — a wrong sign scrambles the reflected sky.)
struct Basis {
    Vec3 r, u, f;
};
const Basis kFace[6] = {
    {{0, 0, -1}, {0, -1, 0}, {1, 0, 0}},   // +X
    {{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}},   // -X
    {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},     // +Y
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},   // -Y
    {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}},    // +Z
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}},  // -Z
};

// Barrier all subresources COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
// (the only post-bake transition needed; covers all cube layers + mips, unlike
// ResourceManager::insertImageBarrier which is 1-layer/1-mip).
void toShaderRead(VkCommandBuffer cmd, VkImage image, uint32_t mipCount, uint32_t layerCount) {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount};
    b.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);
}

void setViewportScissor(VkCommandBuffer cmd, uint32_t dim) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(dim), static_cast<float>(dim), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {dim, dim}};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════
void IBLManager::init(const RendererServices& svc) {
    m_svc    = svc;
    m_device = svc.device;

    createSamplers();
    createRenderPasses();
    createImagesAndViews();
    createFramebuffers();
    createDescriptors();
    createPipelines();
    bakeBRDF();  // sky-independent — baked once
}

VkImageView IBLManager::makeView(VkImage img, VkImageViewType type, VkFormat fmt, uint32_t baseMip, uint32_t mipCount,
                                 uint32_t baseLayer, uint32_t layerCount) {
    VkImageViewCreateInfo v{};
    v.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v.image                       = img;
    v.viewType                    = type;
    v.format                      = fmt;
    v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    v.subresourceRange            = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount};
    VkImageView view;
    VK_CHECK(vkCreateImageView(m_device, &v, nullptr, &view));
    return view;
}

void IBLManager::createSamplers() {
    VkSamplerCreateInfo s{};
    s.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter    = VK_FILTER_LINEAR;
    s.minFilter    = VK_FILTER_LINEAR;
    s.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.minLod       = 0.0f;
    s.maxLod       = static_cast<float>(kPrefilterMips);
    VK_CHECK(vkCreateSampler(m_device, &s, nullptr, &m_cubeSampler));

    s.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(m_device, &s, nullptr, &m_lutSampler));
}

void IBLManager::createRenderPasses() {
    auto makePass = [&](VkFormat fmt) -> VkRenderPass {
        VkAttachmentDescription att{};
        att.format         = fmt;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription  sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{};
        rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments    = &att;
        rp.subpassCount    = 1;
        rp.pSubpasses      = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies   = &dep;
        VkRenderPass out;
        VK_CHECK(vkCreateRenderPass(m_device, &rp, nullptr, &out));
        return out;
    };
    m_cubeRenderPass = makePass(kCubeFormat);
    m_brdfRenderPass = makePass(kBrdfFormat);
}

void IBLManager::createImagesAndViews() {
    auto makeCube = [&](uint32_t dim, uint32_t mips, GpuImage& img) {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.extent        = {dim, dim, 1};
        ii.mipLevels     = mips;
        ii.arrayLayers   = kFaces;
        ii.format        = kCubeFormat;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        img      = GpuImage(m_svc.allocator, ii, ai);
    };
    makeCube(kEnvDim, 1, m_envImage);
    makeCube(kIrrDim, 1, m_irrImage);
    makeCube(kPrefilterDim, kPrefilterMips, m_prefilterImage);

    {  // BRDF LUT — plain 2D
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.extent        = {kBrdfDim, kBrdfDim, 1};
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.format        = kBrdfFormat;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage    = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        m_brdfImage = GpuImage(m_svc.allocator, ii, ai);
    }

    // Cube sampling views (all faces).
    m_envCubeView = makeView(m_envImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, 1, 0, kFaces);
    m_irrCubeView = makeView(m_irrImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, 1, 0, kFaces);
    m_prefilterCubeView =
        makeView(m_prefilterImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, kPrefilterMips, 0, kFaces);
    m_brdfView = makeView(m_brdfImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kBrdfFormat, 0, 1, 0, 1);

    // Per-face render-target views.
    for (uint32_t f = 0; f < kFaces; f++) {
        m_envFaceViews[f] = makeView(m_envImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kCubeFormat, 0, 1, f, 1);
        m_irrFaceViews[f] = makeView(m_irrImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kCubeFormat, 0, 1, f, 1);
        for (uint32_t m = 0; m < kPrefilterMips; m++) {
            m_prefilterFaceMipViews[f * kPrefilterMips + m] =
                makeView(m_prefilterImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kCubeFormat, m, 1, f, 1);
        }
    }
}

void IBLManager::createFramebuffers() {
    auto makeFB = [&](VkRenderPass rp, VkImageView view, uint32_t dim) -> VkFramebuffer {
        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = rp;
        fb.attachmentCount = 1;
        fb.pAttachments    = &view;
        fb.width           = dim;
        fb.height          = dim;
        fb.layers          = 1;
        VkFramebuffer out;
        VK_CHECK(vkCreateFramebuffer(m_device, &fb, nullptr, &out));
        return out;
    };
    for (uint32_t f = 0; f < kFaces; f++) {
        m_envFBs[f] = makeFB(m_cubeRenderPass, m_envFaceViews[f], kEnvDim);
        m_irrFBs[f] = makeFB(m_cubeRenderPass, m_irrFaceViews[f], kIrrDim);
        for (uint32_t m = 0; m < kPrefilterMips; m++) {
            m_prefilterFBs[f * kPrefilterMips + m] =
                makeFB(m_cubeRenderPass, m_prefilterFaceMipViews[f * kPrefilterMips + m], kPrefilterDim >> m);
        }
    }
    m_brdfFB = makeFB(m_brdfRenderPass, m_brdfView, kBrdfDim);
}

void IBLManager::createDescriptors() {
    auto makeLayout = [&](uint32_t count) -> VkDescriptorSetLayout {
        std::array<VkDescriptorSetLayoutBinding, 3> b{};
        for (uint32_t i = 0; i < count; i++) {
            b[i].binding         = i;
            b[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b[i].descriptorCount = 1;
            b[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = count;
        ci.pBindings    = b.data();
        VkDescriptorSetLayout out;
        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &out));
        return out;
    };
    m_iblSetLayout = makeLayout(3);  // irradiance + prefiltered + BRDF LUT (set 3)
    m_envSetLayout = makeLayout(1);  // env cube (bake convolution)

    VkDescriptorPoolSize       size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &size;
    pi.maxSets       = 2;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool));

    auto alloc = [&](VkDescriptorSetLayout layout) -> VkDescriptorSet {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &layout;
        VkDescriptorSet set;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &set));
        return set;
    };
    m_iblSet = alloc(m_iblSetLayout);
    m_envSet = alloc(m_envSetLayout);

    auto write = [&](VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler) {
        VkDescriptorImageInfo img{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet  w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = binding;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo      = &img;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    };
    write(m_iblSet, 0, m_irrCubeView, m_cubeSampler);
    write(m_iblSet, 1, m_prefilterCubeView, m_cubeSampler);
    write(m_iblSet, 2, m_brdfView, m_lutSampler);
    write(m_envSet, 0, m_envCubeView, m_cubeSampler);
}

void IBLManager::createPipelines() {
    VkPushConstantRange skyPC{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush)};
    VkPushConstantRange convPC{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ConvolvePush)};
    m_skyLayout      = Pipeline::createLayout(m_device, {}, {skyPC});
    m_convolveLayout = Pipeline::createLayout(m_device, {m_envSetLayout}, {convPC});
    m_brdfLayout     = Pipeline::createLayout(m_device, {}, {});

    auto makePipe = [&](const std::string& frag, VkPipelineLayout layout, VkRenderPass rp, uint32_t dim) -> VkPipeline {
        PipelineConfig cfg{};
        cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
        cfg.fragShaderPath   = std::string(SHADER_DIR) + frag + ".spv";
        cfg.noVertexInput    = true;
        cfg.enableDepthTest  = false;
        cfg.enableDepthWrite = false;
        cfg.cullMode         = VK_CULL_MODE_NONE;
        cfg.pipelineLayout   = layout;
        return Pipeline::create(m_device, cfg, rp, {dim, dim});
    };
    m_skyPipeline       = makePipe("ibl_sky.frag", m_skyLayout, m_cubeRenderPass, kEnvDim);
    m_irrPipeline       = makePipe("ibl_irradiance.frag", m_convolveLayout, m_cubeRenderPass, kIrrDim);
    m_prefilterPipeline = makePipe("ibl_prefilter.frag", m_convolveLayout, m_cubeRenderPass, kPrefilterDim);
    m_brdfPipeline      = makePipe("ibl_brdf.frag", m_brdfLayout, m_brdfRenderPass, kBrdfDim);
}

// ── One-time command-buffer helpers ───────────────────────────────────
namespace {
VkCommandBuffer beginOneTime(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}
void endOneTime(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd) {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}
void beginRP(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, uint32_t dim) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo bi{};
    bi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass        = rp;
    bi.framebuffer       = fb;
    bi.renderArea.extent = {dim, dim};
    bi.clearValueCount   = 1;
    bi.pClearValues      = &clear;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}
}  // namespace

void IBLManager::bakeBRDF() {
    VkCommandBuffer cmd = beginOneTime(m_device, m_svc.commandPool);
    beginRP(cmd, m_brdfRenderPass, m_brdfFB, kBrdfDim);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_brdfPipeline);
    setViewportScissor(cmd, kBrdfDim);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    toShaderRead(cmd, m_brdfImage.handle(), 1, 1);
    endOneTime(m_device, m_svc.commandPool, m_svc.graphicsQueue, cmd);
}

void IBLManager::bake(const SkyBakeParams& sky) {
    // No in-flight frame may be sampling the cubes while we overwrite them.
    vkQueueWaitIdle(m_svc.graphicsQueue);
    VkCommandBuffer cmd = beginOneTime(m_device, m_svc.commandPool);

    // ── 1. Procedural sky → environment cube (6 faces) ──
    for (uint32_t f = 0; f < kFaces; f++) {
        SkyPush p{};
        p.faceR    = Vec4(kFace[f].r, 0.0f);
        p.faceU    = Vec4(kFace[f].u, 0.0f);
        p.faceF    = Vec4(kFace[f].f, 0.0f);
        p.horizon  = Vec4(sky.horizon, 0.0f);
        p.zenith   = Vec4(sky.zenith, sky.discExp);
        p.sunDir   = Vec4(sky.sunDir, sky.discStr);
        p.sunColor = Vec4(sky.sunColor, 0.0f);
        beginRP(cmd, m_cubeRenderPass, m_envFBs[f], kEnvDim);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
        setViewportScissor(cmd, kEnvDim);
        vkCmdPushConstants(cmd, m_skyLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush), &p);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    toShaderRead(cmd, m_envImage.handle(), 1, kFaces);

    // ── 2. Diffuse irradiance convolution (6 faces) ──
    for (uint32_t f = 0; f < kFaces; f++) {
        ConvolvePush p{};
        p.faceR = Vec4(kFace[f].r, 0.0f);
        p.faceU = Vec4(kFace[f].u, 0.0f);
        p.faceF = Vec4(kFace[f].f, 0.0f);
        beginRP(cmd, m_cubeRenderPass, m_irrFBs[f], kIrrDim);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_irrPipeline);
        setViewportScissor(cmd, kIrrDim);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_convolveLayout, 0, 1, &m_envSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_convolveLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ConvolvePush), &p);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    toShaderRead(cmd, m_irrImage.handle(), 1, kFaces);

    // ── 3. GGX specular prefilter (6 faces × roughness mips) ──
    for (uint32_t m = 0; m < kPrefilterMips; m++) {
        const uint32_t dim = kPrefilterDim >> m;
        const float rough  = kPrefilterMips > 1 ? static_cast<float>(m) / static_cast<float>(kPrefilterMips - 1) : 0.0f;
        for (uint32_t f = 0; f < kFaces; f++) {
            ConvolvePush p{};
            p.faceR    = Vec4(kFace[f].r, 0.0f);
            p.faceU    = Vec4(kFace[f].u, 0.0f);
            p.faceF    = Vec4(kFace[f].f, 0.0f);
            p.params.x = rough;
            beginRP(cmd, m_cubeRenderPass, m_prefilterFBs[f * kPrefilterMips + m], dim);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prefilterPipeline);
            setViewportScissor(cmd, dim);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_convolveLayout, 0, 1, &m_envSet, 0,
                                    nullptr);
            vkCmdPushConstants(cmd, m_convolveLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ConvolvePush), &p);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        }
    }
    toShaderRead(cmd, m_prefilterImage.handle(), kPrefilterMips, kFaces);

    endOneTime(m_device, m_svc.commandPool, m_svc.graphicsQueue, cmd);
}

void IBLManager::cleanup() {
    auto destroyView = [&](VkImageView& v) {
        if (v != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, v, nullptr);
            v = VK_NULL_HANDLE;
        }
    };
    auto destroyFB = [&](VkFramebuffer& fb) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        }
    };
    auto destroyPipe = [&](VkPipeline& p) {
        if (p != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    };
    auto destroyLayout = [&](VkPipelineLayout& l) {
        if (l != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, l, nullptr);
            l = VK_NULL_HANDLE;
        }
    };

    destroyPipe(m_skyPipeline);
    destroyPipe(m_irrPipeline);
    destroyPipe(m_prefilterPipeline);
    destroyPipe(m_brdfPipeline);
    destroyLayout(m_skyLayout);
    destroyLayout(m_convolveLayout);
    destroyLayout(m_brdfLayout);

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    if (m_iblSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_iblSetLayout, nullptr);
        m_iblSetLayout = VK_NULL_HANDLE;
    }
    if (m_envSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_envSetLayout, nullptr);
        m_envSetLayout = VK_NULL_HANDLE;
    }

    for (auto& fb : m_envFBs)
        destroyFB(fb);
    for (auto& fb : m_irrFBs)
        destroyFB(fb);
    for (auto& fb : m_prefilterFBs)
        destroyFB(fb);
    destroyFB(m_brdfFB);

    if (m_cubeRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_cubeRenderPass, nullptr);
        m_cubeRenderPass = VK_NULL_HANDLE;
    }
    if (m_brdfRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_brdfRenderPass, nullptr);
        m_brdfRenderPass = VK_NULL_HANDLE;
    }

    destroyView(m_envCubeView);
    destroyView(m_irrCubeView);
    destroyView(m_prefilterCubeView);
    destroyView(m_brdfView);
    for (auto& v : m_envFaceViews)
        destroyView(v);
    for (auto& v : m_irrFaceViews)
        destroyView(v);
    for (auto& v : m_prefilterFaceMipViews)
        destroyView(v);

    if (m_cubeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_cubeSampler, nullptr);
        m_cubeSampler = VK_NULL_HANDLE;
    }
    if (m_lutSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_lutSampler, nullptr);
        m_lutSampler = VK_NULL_HANDLE;
    }

    m_envImage.reset();
    m_irrImage.reset();
    m_prefilterImage.reset();
    m_brdfImage.reset();
}

}  // namespace swish
