#pragma once

#include "../utils/Types.h"  // swish::Vec3 / Vec4 (glm aliases)

#include <glm/glm.hpp>

namespace swish {

// ══════════════════════════════════════════════════════════════════════
// DebugParams — every live-tunable scene parameter, in one plain struct.
//
// Defaults here MUST match the current shipped look exactly; the debug UI
// mutates this struct in-place each frame and the Renderer reads it when
// composing push constants / UBOs. This whole subsystem is compiled only
// under -DSWISH_DEBUG_UI, but the struct itself is header-only and harmless
// to include unconditionally (it pulls in no ImGui/Vulkan symbols).
// ══════════════════════════════════════════════════════════════════════
struct DebugParams {
    // ── Grade / composite (tone map + color grade in composite.frag) ──
    float exposure       = 0.45f;  // tone-mapping exposure
    float bloomThreshold = 1.0f;   // luminance above which bloom extracts
    float bloomIntensity = 0.3f;   // additive bloom blend strength
    float brightness     = 0.0f;   // post-grade lift   [-1, 1]
    float contrast       = 1.0f;   // post-grade contrast around 0.5
    float saturation     = 1.0f;   // 0 = greyscale, 1 = neutral
    float temperature    = 0.0f;   // warm/cool shift   [-1, 1]
    float tint           = 0.0f;   // green/magenta shift [-1, 1]

    // ── Sky (gradient endpoints lerped by `clarity`; sun disc) ────────
    glm::vec3 skyHorizonOvercast{0.70f, 0.80f, 0.90f};
    glm::vec3 skyHorizonClear{0.62f, 0.80f, 0.98f};
    glm::vec3 skyZenithOvercast{0.35f, 0.55f, 0.85f};
    glm::vec3 skyZenithClear{0.09f, 0.36f, 0.86f};
    float     clarity       = 0.0f;    // 0 = fully overcast, 1 = fully clear
    float     sunDiscExpMin = 32.0f;   // disc sharpness at overcast
    float     sunDiscExpMax = 220.0f;  // disc sharpness at clear
    float     sunDiscStrMin = 0.3f;    // disc strength at overcast
    float     sunDiscStrMax = 0.9f;    // disc strength at clear

    // ── Sun / directional light ───────────────────────────────────────
    glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
    float     sunAmbient = 0.22f;  // ambient floor added to lit surfaces
    // azimuth/elevation drive the sun direction vector; the Renderer converts
    // these spherical angles → sunDir (world space) before uploading.
    float sunAzimuth   = 0.0f;  // [-PI, PI], 0 = +Z
    float sunElevation = 0.0f;  // [0, PI/2], 0 = horizon, PI/2 = zenith

    // ── Fog (distance-based atmospheric haze) ─────────────────────────
    glm::vec3 fogColor{0.52f, 0.57f, 0.63f};
    float     fogDist63 = 1200000.0f;  // distance (world units) at which fog ≈ 63%
    float     fogMax    = 0.65f;       // saturation ceiling of fog blend

    // ── Reflection (environment / gloss) ──────────────────────────────
    float envGlossExp = 3.0f;  // Fresnel/gloss falloff exponent

    // ── SSAO (screen-space ambient occlusion) ─────────────────────────
    // Runs at 1/2 render res, multiplied into the composite. radius/bias are in
    // view-space world units (1 m = 1000 WU); tune live toward subtle contact
    // darkening. Disabled → intensity forced to 0 (shader outputs 1 = no AO).
    bool  ssaoEnabled   = true;
    float ssaoRadius    = 1200.0f;  // hemisphere sample radius (WU) — contact-focused
    float ssaoBias      = 50.0f;    // view-space depth bias to fight self-occlusion (WU)
    float ssaoIntensity = 1.0f;     // occlusion strength multiplier

    // ── Shadows (single sun shadow map + depth bias) ──────────────────
    float shadowBias        = 0.0018f;    // slope-scaled shadow-compare bias
    float shadowFloor       = 0.25f;      // min visibility in full shadow
    float shadowHalfExtent  = 45000.0f;   // ortho half-extent of the shadow frustum
    float shadowDepthRange  = 200000.0f;  // ortho near→far depth range
    float depthBiasConst    = 4.0f;       // vkCmdSetDepthBias constant factor
    float depthBiasSlope    = 1.5f;       // vkCmdSetDepthBias slope factor

    // ── Wet / rain ────────────────────────────────────────────────────
    float rainIntensity = 0.0f;    // [0, 1] rain amount (drives haze + wetness)
    float wetPorosity   = 0.35f;   // how much water darkens/soaks the surface
    float wetRoughness  = 0.12f;   // roughness of wet (specular) surfaces
    float streakLen     = 3200.0f; // windshield / surface streak length

    // ── Car (paint override for tuning) ───────────────────────────────
    float     carMetalness    = 0.0f;
    glm::vec3 carPaint{1.0f, 1.0f, 1.0f};
    float     carRoughnessMul = 1.0f;
    bool      carOverride     = false;  // when true, use the above instead of the asset's material

    // ── Quality ───────────────────────────────────────────────────────
    float ssaaScale          = 1.5f;    // internal supersample factor (matches PostProcessManager::kRenderScale)
    bool  ssaaApplyRequested = false;   // set by the UI "Apply" button; Renderer consumes + clears it

    // ── UI state (not a scene parameter, but lives with the rest) ─────
    bool editMode  = false;  // true = cursor free to drive the panel; false = drive-mode (panel ignores mouse)
    bool showPanel = true;   // master visibility toggle for the debug window
};

}  // namespace swish
