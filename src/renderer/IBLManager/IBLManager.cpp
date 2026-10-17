#include "IBLManager.h"

#include "renderer/Pipeline/Pipeline.h"
#include "renderer/ResourceManager/ResourceManager.h"
#include "utils/vulkan/Barrier.h"
#include "utils/vulkan/Check.h"
#include "utils/vulkan/PushConstants.h"
#include "utils/vulkan/RenderPass.h"
#include "utils/vulkan/Samplers.h"

#include <stb_image.h>

#include <array>
#include <cstring>
#include <iostream>
#include <string>

namespace swish {

namespace {

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

void toShaderRead(VkCommandBuffer cmd, VkImage image, uint32_t mipCount, uint32_t layerCount) {
    // COLOR_ATTACHMENT -> SHADER_READ: COLOR_OUTPUT/COLOR_WRITE -> FRAGMENT/SHADER_READ over
    // mips [0, mipCount) x layers [0, layerCount) — identical masks/range to the previous hand-rolled barrier.
    vkx::image_barrier<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL>(
        cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, vkx::SubresourceSpan{0, mipCount, 0, layerCount});
}

void setViewportScissor(VkCommandBuffer cmd, uint32_t dim) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(dim), static_cast<float>(dim), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {dim, dim}};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

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
    bakeBRDF();

    // Default weather HDRI is applied by Renderer::set_weather_preset at startup.
    // If that fails, WeatherController falls back to the procedural sky.
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
    m_cubeSampler = vkx::make_sampler(
        m_device, {.mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR, .maxLod = static_cast<float>(kPrefilterMips)});
    m_lutSampler = vkx::make_sampler(m_device, {.mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR});

    // Equirect: clamp (not repeat) to avoid a seam at ±180°.
    m_equirectSampler = vkx::make_sampler(m_device, {.mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR});
}

void IBLManager::createRenderPasses() {
    auto makePass = [&](VkFormat fmt) -> VkRenderPass {
        const std::array<VkSubpassDependency, 1> deps = {vkx::subpass_dependency(
            VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)};
        vkx::ColorPassDesc                       d;
        d.color        = {.format = fmt, .load = VK_ATTACHMENT_LOAD_OP_CLEAR};
        d.dependencies = deps;
        return vkx::make_color_pass(m_device, d);
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

    {
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

    m_envCubeView = makeView(m_envImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, 1, 0, kFaces);
    m_irrCubeView = makeView(m_irrImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, 1, 0, kFaces);
    m_prefilterCubeView =
        makeView(m_prefilterImage.handle(), VK_IMAGE_VIEW_TYPE_CUBE, kCubeFormat, 0, kPrefilterMips, 0, kFaces);
    m_brdfView = makeView(m_brdfImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kBrdfFormat, 0, 1, 0, 1);

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
    m_iblSetLayout      = makeLayout(3);
    m_envSetLayout      = makeLayout(1);
    m_equirectSetLayout = makeLayout(1);

    VkDescriptorPoolSize       size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &size;
    pi.maxSets       = 3;
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
    m_iblSet      = alloc(m_iblSetLayout);
    m_envSet      = alloc(m_envSetLayout);
    m_equirectSet = alloc(m_equirectSetLayout);

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
    // equirect write happens after loadEquirect (view may be null until then)
}

void IBLManager::createPipelines() {
    const VkPushConstantRange skyPC  = vkx::push_range<SkyPush>(VK_SHADER_STAGE_FRAGMENT_BIT);
    const VkPushConstantRange eqPC   = vkx::push_range<EquirectPush>(VK_SHADER_STAGE_FRAGMENT_BIT);
    const VkPushConstantRange convPC = vkx::push_range<ConvolvePush>(VK_SHADER_STAGE_FRAGMENT_BIT);
    m_skyLayout                      = pipeline::create_layout(m_device, {}, {skyPC});
    m_equirectLayout                 = pipeline::create_layout(m_device, {m_equirectSetLayout}, {eqPC});
    m_convolveLayout                 = pipeline::create_layout(m_device, {m_envSetLayout}, {convPC});
    m_brdfLayout                     = pipeline::create_layout(m_device, {}, {});

    auto makePipe = [&](const std::string& frag, VkPipelineLayout layout, VkRenderPass rp, uint32_t dim) -> VkPipeline {
        PipelineConfig cfg{};
        cfg.vertShaderPath   = std::string(SHADER_DIR) + "fullscreen.vert.spv";
        cfg.fragShaderPath   = std::string(SHADER_DIR) + frag + ".spv";
        cfg.noVertexInput    = true;
        cfg.enableDepthTest  = false;
        cfg.enableDepthWrite = false;
        cfg.cullMode         = VK_CULL_MODE_NONE;
        cfg.pipelineLayout   = layout;
        return pipeline::create_graphics(m_device, m_svc.pipelineCache, cfg, rp, {dim, dim});
    };
    m_skyPipeline       = makePipe("ibl_sky.frag", m_skyLayout, m_cubeRenderPass, kEnvDim);
    m_equirectPipeline  = makePipe("ibl_equirect.frag", m_equirectLayout, m_cubeRenderPass, kEnvDim);
    m_irrPipeline       = makePipe("ibl_irradiance.frag", m_convolveLayout, m_cubeRenderPass, kIrrDim);
    m_prefilterPipeline = makePipe("ibl_prefilter.frag", m_convolveLayout, m_cubeRenderPass, kPrefilterDim);
    m_brdfPipeline      = makePipe("ibl_brdf.frag", m_brdfLayout, m_brdfRenderPass, kBrdfDim);
}

bool IBLManager::loadEquirect(const std::string& path) {
    int    w = 0, h = 0, n = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &n, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        std::cerr << "[IBL] Failed to load HDRI: " << path
                  << (stbi_failure_reason() ? std::string(" (") + stbi_failure_reason() + ")" : "") << std::endl;
        return false;
    }

    // Replace any previous equirect before allocating the new one.
    destroyEquirect();

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4 * sizeof(float);
    GpuBuffer          staging   = gpu::hostVisibleBuffer(m_svc.allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(staging.mapped(), data, static_cast<size_t>(imageSize));
    stbi_image_free(data);

    m_equirectImage =
        gpu::deviceLocalImage(m_svc.allocator, static_cast<uint32_t>(w), static_cast<uint32_t>(h), kEquirectFormat,
                              VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    resource::transitionImageLayout(m_device, m_svc.commandPool, m_svc.graphicsQueue, m_equirectImage.handle(),
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    resource::copyBufferToImage(m_device, m_svc.commandPool, m_svc.graphicsQueue, staging.handle(),
                                m_equirectImage.handle(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    resource::transitionImageLayout(m_device, m_svc.commandPool, m_svc.graphicsQueue, m_equirectImage.handle(),
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_equirectView = makeView(m_equirectImage.handle(), VK_IMAGE_VIEW_TYPE_2D, kEquirectFormat, 0, 1, 0, 1);

    VkDescriptorImageInfo img{m_equirectSampler, m_equirectView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet  wdesc{};
    wdesc.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wdesc.dstSet          = m_equirectSet;
    wdesc.dstBinding      = 0;
    wdesc.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wdesc.descriptorCount = 1;
    wdesc.pImageInfo      = &img;
    vkUpdateDescriptorSets(m_device, 1, &wdesc, 0, nullptr);

    m_hdriLoaded = true;
    return true;
}

void IBLManager::destroyEquirect() {
    if (m_equirectView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_equirectView, nullptr);
        m_equirectView = VK_NULL_HANDLE;
    }
    m_equirectImage.reset();
    m_hdriLoaded = false;
}

bool IBLManager::set_hdri(const std::string& path, float exposure, float yaw_rad) {
    m_hdriExposure = exposure;
    m_hdriYawRad   = yaw_rad;
    if (!loadEquirect(path)) return false;
    bakeFromHdri();
    std::cout << "[IBL] Weather HDRI: " << path << " (exposure=" << exposure << ", yaw=" << yaw_rad << ")\n";
    return true;
}

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

void IBLManager::bakeConvolutions(VkCommandBuffer cmd) {
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
}

void IBLManager::bakeFromHdri() {
    if (!m_hdriLoaded) return;

    vkQueueWaitIdle(m_svc.graphicsQueue);
    VkCommandBuffer cmd = beginOneTime(m_device, m_svc.commandPool);

    for (uint32_t f = 0; f < kFaces; f++) {
        EquirectPush p{};
        p.faceR  = Vec4(kFace[f].r, 0.0f);
        p.faceU  = Vec4(kFace[f].u, 0.0f);
        p.faceF  = Vec4(kFace[f].f, 0.0f);
        p.params = Vec4(m_hdriExposure, m_hdriYawRad, 0.0f, 0.0f);
        beginRP(cmd, m_cubeRenderPass, m_envFBs[f], kEnvDim);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_equirectPipeline);
        setViewportScissor(cmd, kEnvDim);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_equirectLayout, 0, 1, &m_equirectSet, 0,
                                nullptr);
        vkCmdPushConstants(cmd, m_equirectLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(EquirectPush), &p);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    toShaderRead(cmd, m_envImage.handle(), 1, kFaces);

    bakeConvolutions(cmd);
    endOneTime(m_device, m_svc.commandPool, m_svc.graphicsQueue, cmd);
}

void IBLManager::bake(const SkyBakeParams& sky) {
    // HDRI env is static — weather-driven procedural rebakes would overwrite it.
    if (m_hdriLoaded) return;

    vkQueueWaitIdle(m_svc.graphicsQueue);
    VkCommandBuffer cmd = beginOneTime(m_device, m_svc.commandPool);

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

    bakeConvolutions(cmd);
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
    destroyPipe(m_equirectPipeline);
    destroyPipe(m_irrPipeline);
    destroyPipe(m_prefilterPipeline);
    destroyPipe(m_brdfPipeline);
    destroyLayout(m_skyLayout);
    destroyLayout(m_equirectLayout);
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
    if (m_equirectSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_equirectSetLayout, nullptr);
        m_equirectSetLayout = VK_NULL_HANDLE;
    }

    for (auto& fb : m_envFBs) destroyFB(fb);
    for (auto& fb : m_irrFBs) destroyFB(fb);
    for (auto& fb : m_prefilterFBs) destroyFB(fb);
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
    destroyEquirect();
    for (auto& v : m_envFaceViews) destroyView(v);
    for (auto& v : m_irrFaceViews) destroyView(v);
    for (auto& v : m_prefilterFaceMipViews) destroyView(v);

    if (m_cubeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_cubeSampler, nullptr);
        m_cubeSampler = VK_NULL_HANDLE;
    }
    if (m_lutSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_lutSampler, nullptr);
        m_lutSampler = VK_NULL_HANDLE;
    }
    if (m_equirectSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_equirectSampler, nullptr);
        m_equirectSampler = VK_NULL_HANDLE;
    }

    m_envImage.reset();
    m_irrImage.reset();
    m_prefilterImage.reset();
    m_brdfImage.reset();
}

}  // namespace swish
