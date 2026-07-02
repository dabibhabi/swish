#include "DepthOnlyPipeline.h"

#include "../../scene/SceneTypes.h"  // (kept for parity with ScenePipeline; not strictly required)
#include "../../utils/FileIO/FileIO.h"
#include "../../utils/VulkanCheck.h"
#include "../Pipeline/Pipeline.h"
#include "../Vertex.h"

#include <array>
#include <string>

namespace swish {

namespace {

// ── Depth-bias tunables (documented; tune per scene via a run) ─────────
// A shadow pass biases stored depth away from the light to avoid self-
// shadowing acne. constant offsets every fragment by N·(min. resolvable
// depth unit); slope scales with the polygon's depth gradient (steep,
// near-grazing polys need more). Too much bias → peter-panning (shadows
// detach from contact points). Starting point for a 2048² map framing a
// ~4 km highway:
constexpr float kDepthBiasConstantFactor = 4.0f;
constexpr float kDepthBiasSlopeFactor    = 1.5f;

// Vertex-stage push range covers the full 128-byte DepthPushConstants block.
constexpr uint32_t kDepthPushConstSize = static_cast<uint32_t>(sizeof(DepthPushConstants));
static_assert(kDepthPushConstSize == 128, "DepthPushConstants must be 128 B (2·mat4, 16-aligned for MoltenVK)");

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

}  // namespace

void DepthOnlyPipeline::init(VkDevice device, const Config& cfg) {
    // ── Pipeline layout: no descriptor sets, one vertex-stage push range ──
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = kDepthPushConstSize;

    m_layout = Pipeline::createLayout(device, {}, {pushConstantRange});

    // ── Load SPIR-V (same SHADER_DIR convention as ScenePipeline) ──
    auto vertCode = FileIO::readBinaryFile(std::string(SHADER_DIR) + "depth_only.vert.spv");
    auto fragCode = FileIO::readBinaryFile(std::string(SHADER_DIR) + "depth_only.frag.spv");

    VkShaderModule vertModule = createShaderModule(device, vertCode);
    VkShaderModule fragModule = createShaderModule(device, fragCode);

    // Scoped RAII destroy of shader modules (matches Pipeline::create).
    struct ScopedShaderModule {
        VkDevice       dev;
        VkShaderModule mod;
        ~ScopedShaderModule() {
            if (mod != VK_NULL_HANDLE)
                vkDestroyShaderModule(dev, mod, nullptr);
        }
        ScopedShaderModule(VkDevice d, VkShaderModule m) : dev(d), mod(m) {}
        ScopedShaderModule(const ScopedShaderModule&)            = delete;
        ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;
    };
    ScopedShaderModule scopedVert{device, vertModule};
    ScopedShaderModule scopedFrag{device, fragModule};

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName  = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName  = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

    // ── Vertex input: same binding/attributes as ScenePipeline (basic.vert). ──
    // The depth vert only reads position, but we declare the full Vertex layout
    // so the SAME vertex buffers bind without a separate stripped layout.
    auto binding    = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount   = 1;
    vertexInputInfo.pVertexBindingDescriptions      = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ── Dynamic viewport + scissor + depth bias (no pipeline recreation). ──
    // Depth bias is dynamic so the debug UI can tune constant/slope factors live
    // (vkCmdSetDepthBias per shadow pass); the static factors below are ignored.
    std::array<VkDynamicState, 3> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                   VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // ── Rasterizer: front-face cull + depth bias enabled. ──
    // Culling FRONT faces (rendering only back faces into the shadow map)
    // pushes the stored depth to the far side of solid geometry, which
    // reduces surface acne on the lit side without the peter-panning that
    // back-face culling can cause. The scene renders CCW front faces
    // (Pipeline uses VK_FRONT_FACE_COUNTER_CLOCKWISE), so we keep that
    // winding and cull FRONT here. (Tunable: switch to BACK + more bias if
    // thin/two-sided geometry leaks light.)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_TRUE;
    rasterizer.depthBiasConstantFactor = kDepthBiasConstantFactor;
    rasterizer.depthBiasSlopeFactor    = kDepthBiasSlopeFactor;
    rasterizer.depthBiasClamp          = 0.0f;

    // ── Multisampling (disabled — single-sample shadow map). ──
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Depth: test + write, LESS (repo uses GLM_FORCE_DEPTH_ZERO_TO_ONE;
    //    no reverse-Z, so nearer = smaller depth, same as the scene pass). ──
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;
    depthStencil.depthWriteEnable      = VK_TRUE;
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // ── Color blending: NO color attachments in a depth-only pass. ──
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments    = nullptr;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages             = shaderStages.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_layout;
    pipelineInfo.renderPass          = cfg.targetRenderPass;
    pipelineInfo.subpass             = 0;

    // NOTE: the shared process-wide VkPipelineCache (Pipeline::s_pipelineCache)
    // is file-private to Pipeline.cpp and only fed by Pipeline::create(). This
    // pass builds directly (to get depth bias), so it uses VK_NULL_HANDLE — a
    // valid cache argument; the driver just compiles from scratch. If cache
    // reuse matters here, the integrator can expose a getter on Pipeline.
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

    (void)cfg.extent;  // extent is set dynamically in bind(); kept in Config for symmetry.
}

void DepthOnlyPipeline::cleanup(VkDevice device) {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

void DepthOnlyPipeline::bind(VkCommandBuffer cmd, float depthBiasConst, float depthBiasSlope) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    // Dynamic depth bias (constant, clamp, slope) — replaces the static factors.
    vkCmdSetDepthBias(cmd, depthBiasConst, 0.0f, depthBiasSlope);
}

void DepthOnlyPipeline::set_cascade(VkCommandBuffer cmd, VkRect2D rect, const Mat4& lightViewProj) const {
    VkViewport vp{static_cast<float>(rect.offset.x),
                  static_cast<float>(rect.offset.y),
                  static_cast<float>(rect.extent.width),
                  static_cast<float>(rect.extent.height),
                  0.0f,
                  1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &rect);

    // Push this cascade's light-space matrix into the first 64 bytes.
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT, offsetof(DepthPushConstants, lightViewProj),
                       static_cast<uint32_t>(sizeof(Mat4)), &lightViewProj);
}

void DepthOnlyPipeline::push_model(VkCommandBuffer cmd, const Mat4& model) const {
    // Per-object model matrix occupies bytes [64, 128).
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT, offsetof(DepthPushConstants, model),
                       static_cast<uint32_t>(sizeof(Mat4)), &model);
}

}  // namespace swish
