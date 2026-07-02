#pragma once

#include "../../utils/Types.h"

#include <vulkan/vulkan.h>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// DepthOnlyPipeline — renders scene geometry from the sun's point of view
// into a depth-only render pass (no color attachments) to produce a single
// (non-cascaded) shadow map. Mirrors ScenePipeline's structure/style, but
// builds its VkPipeline directly rather than through the shared Pipeline
// builder: the shared builder hard-codes rasterizer.depthBiasEnable = FALSE
// and does not expose depth bias, which a shadow pass requires to fight
// self-shadowing acne / peter-panning. (Do NOT edit the shared builder to
// add this — this pass owns its rasterizer state.)
//
// Descriptor / push-constant contract (see DepthPushConstants below):
//   - NO descriptor sets are bound. The depth pass is self-contained.
//   - One push-constant block, vertex stage, 128 bytes (16-aligned, safe on
//     MoltenVK): { mat4 lightViewProj; mat4 model; }.
//     * lightViewProj is the sun ortho * lookAt, recomputed per frame by the
//       integrator from car position + sunDir and pushed once per pass.
//     * model is the same per-object model matrix ScenePipeline pushes
//       (PushConstantData::model) — pushed once per draw call.
// ══════════════════════════════════════════════════════════════════════

// Vertex-stage push-constant block for the depth pass. 128 B = 2·mat4,
// a multiple of 16 (MoltenVK mis-maps push blocks whose total size is not
// 16-byte aligned). Matches the `DepthPush` block in depth_only.vert.
struct DepthPushConstants {
    Mat4 lightViewProj;  // 64 B — sun ortho * lookAt, pushed once per pass
    Mat4 model;          // 64 B — per-object model matrix, pushed per draw
};

class DepthOnlyPipeline {
public:
    struct Config {
        // Depth-only render pass owned by PostProcessManager: a single
        // D32_SFLOAT depth attachment, no color attachments, finalLayout
        // DEPTH_STENCIL_READ_ONLY_OPTIMAL (sampled by lighting.frag).
        VkRenderPass targetRenderPass = VK_NULL_HANDLE;
        // Shadow-map extent (design target 2048×2048).
        VkExtent2D extent = {0, 0};
    };

    // Default depth-bias factors (see .cpp for the reasoning). Exposed so the
    // shadow pass can pass them explicitly now that depth bias is a DYNAMIC
    // pipeline state (the static rasterizer factors are ignored when dynamic).
    static constexpr float kDefaultDepthBiasConst = 4.0f;
    static constexpr float kDefaultDepthBiasSlope = 1.5f;

    DepthOnlyPipeline() = default;

    void init(VkDevice device, const Config& cfg);
    void cleanup(VkDevice device);  // idempotent + handle-guarded

    // Bind the pipeline, set the (shadow-map-sized) viewport/scissor, set the
    // dynamic depth bias, and push the per-pass light-space matrix. Call once at
    // the start of the shadow pass; then call push_model() before each draw.
    void bind(VkCommandBuffer cmd, VkExtent2D extent, const Mat4& lightViewProj,
              float depthBiasConst = kDefaultDepthBiasConst, float depthBiasSlope = kDefaultDepthBiasSlope) const;

    // Push the per-object model matrix (offset 64 within the 128-byte block).
    void push_model(VkCommandBuffer cmd, const Mat4& model) const;

    VkPipelineLayout get_layout() const { return m_layout; }

private:
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

}  // namespace swish
