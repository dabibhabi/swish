#pragma once

#include "../scene/SceneTypes.h"  // MaterialOverride, MAT_COUNT, MaterialId
#include "../utils/Types.h"       // swish::Vec3 / Vec4 (glm aliases)

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
    float exposure       = 2.0f;    // lie preset (was 1.25) — tone-mapping exposure (manual)
    float bloomThreshold = 1.77f;   // lie preset (was 1.0) — luminance above which bloom extracts
    float bloomIntensity = 1.097f;  // lie preset (was 0.3) — additive bloom blend strength

    // ── Auto-exposure (eye adaptation) ────────────────────────────────
    // When on, the composite exposure is driven by the smoothed scene luminance
    // (exposure = aeKey / adaptedLum, clamped) instead of the manual value above.
    bool  autoExposure = false;
    float aeKey        = 0.30f;   // target mid-grey the average maps toward
    float aeSpeed      = 2.0f;    // adaptation rate (per second)
    float aeMin        = 0.05f;   // exposure clamp (min)
    float aeMax        = 2.0f;    // exposure clamp (max)
    float brightness   = 0.032f;  // lie preset (was 0.0) — post-grade lift   [-1, 1]
    float contrast     = 1.499f;  // lie preset (was 1.12) — post-grade contrast around 0.5
    float saturation   = 0.988f;  // lie preset (was 1.2) — 0 = greyscale, 1 = neutral
    float temperature  = 0.0f;    // warm/cool shift   [-1, 1]
    float tint         = 0.0f;    // green/magenta shift [-1, 1]

    // ── Sky (gradient endpoints lerped by `clarity`; sun disc) ────────
    glm::vec3 skyHorizonOvercast{0.86f, 0.87f, 0.89f};  // LIE overcast: even light grey-white
    glm::vec3 skyHorizonClear{0.85f, 1.0f, 1.25f};  // lie preset (was 0.55,0.82,1.30) — bright light blue near horizon
    glm::vec3 skyZenithOvercast{0.76f, 0.79f, 0.83f};  // overcast: slightly darker grey (subtle gradient)
    glm::vec3 skyZenithClear{0.5f, 0.8f, 1.35f};       // lie preset (was 0.28,0.52,1.50) — deep azure
    float     clarity       = 0.242f;                  // lie preset (was 0.0) — 0 = overcast, 1 = clear
    float     sunDiscExpMin = 32.0f;                   // disc sharpness at overcast
    float     sunDiscExpMax = 220.0f;                  // disc sharpness at clear
    float     sunDiscStrMin = 0.242f;                  // lie preset (was 0.3) — disc strength at overcast
    float     sunDiscStrMax = 0.9f;                    // disc strength at clear

    // ── Sun / directional light ───────────────────────────────────────
    glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
    float     sunAmbient = 0.823f;  // lie preset (was 0.35) — ambient floor added to lit surfaces
    // azimuth/elevation drive the sun direction vector; the Renderer converts
    // these spherical angles → sunDir (world space) before uploading.
    float sunAzimuth   = 0.0f;  // [-PI, PI], 0 = +Z
    float sunElevation = 0.0f;  // [0, PI/2], 0 = horizon, PI/2 = zenith

    // ── Fog (distance-based atmospheric haze) ─────────────────────────
    glm::vec3 fogColor{0.52f, 0.57f, 0.63f};
    float     fogDist63 = 1200000.0f;  // distance (world units) at which fog ≈ 63%
    float     fogMax    = 0.0f;        // lie preset (was 0.65) — distance rain-fog OFF
    // Always-on aerial-perspective haze (dry days too, not wet-gated). Distant geometry
    // dissolves into the horizon sky so the road reads with depth to the far plane.
    float hazeDist = 900000.0f;  // distance (WU) at which haze reaches ~63%
    float hazeMax  = 0.5f;       // haze ceiling [0,1] — fraction dissolved into sky at range

    // ── Reflection (environment / gloss) ──────────────────────────────
    float envGlossExp = 3.0f;  // Fresnel/gloss falloff exponent

    // ── IBL (split-sum environment lighting from the procedural sky) ──
    // Diffuse = cosine-weighted sky irradiance; specular = prefiltered sky
    // reflection × Karis env-BRDF. 1.0 = physically-scaled; tune to taste.
    float iblDiffuse  = 1.832f;  // lie preset (was 1.0)
    float iblSpecular = 1.0f;

    // ── SSR (screen-space reflections) ────────────────────────────────
    // View-space ray-march of the depth buffer, added to the composite. Units are
    // world units (1 m = 1000 WU). Artifact-prone — tune live against the scene.
    bool  ssrEnabled   = true;
    float ssrMaxDist   = 36018.0f;  // lie preset (was 120000) — max reflected-ray travel (WU)
    float ssrThickness = 1835.0f;   // lie preset (was 4000) — depth-intersection tolerance (WU)
    float ssrStride    = 8063.0f;   // lie preset (was 2500) — initial march step (WU)
    float ssrIntensity = 0.714f;    // lie preset (was 0.6) — reflection strength

    // ── God-rays (screen-space light shafts, Mitchell 2007) ───────────
    // Sun-anchored radial blur of the lit HDR, added at composite. UN-gated in the
    // renderer (ships in release); these defaults ARE the release look — the record
    // path reads them via a default-constructed DebugParams when SWISH_DEBUG_UI is off,
    // so the debug defaults and the shipped literals are the same source of truth.
    bool  godraysEnabled  = true;
    float godrayDensity   = 0.9f;   // how far the samples span toward the sun (0..1)
    float godrayDecay     = 0.95f;  // per-step attenuation along the march
    float godrayWeight    = 0.35f;  // per-sample weight
    float godrayIntensity = 0.04f;  // overall additive strength (gain ≈ weight×Σdecayⁱ is large)

    // ── SSAO (screen-space ambient occlusion) ─────────────────────────
    // Runs at 1/2 render res, multiplied into the composite. radius/bias are in
    // view-space world units (1 m = 1000 WU); tune live toward subtle contact
    // darkening. Disabled → intensity forced to 0 (shader outputs 1 = no AO).
    bool  ssaoEnabled   = true;
    float ssaoRadius    = 1200.0f;  // hemisphere sample radius (WU) — contact-focused
    float ssaoBias      = 50.0f;    // view-space depth bias to fight self-occlusion (WU)
    float ssaoIntensity = 1.0f;     // occlusion strength multiplier

    // ── Shadows (single sun shadow map + depth bias) ──────────────────
    float shadowBias       = 0.0f;       // lie preset (was 0.0018) — slope-scaled shadow-compare bias
    float shadowFloor      = 1.0f;       // lie preset (was 0.25) — min visibility in full shadow (1.0 = no darkening)
    float shadowHalfExtent = 45000.0f;   // (legacy single-map; unused by CSM)
    float shadowDepthRange = 200000.0f;  // (legacy single-map; unused by CSM)
    float depthBiasConst   = 4.0f;       // vkCmdSetDepthBias constant factor
    float depthBiasSlope   = 1.5f;       // vkCmdSetDepthBias slope factor
    float shadowLightSize  = 2.0f;       // PCSS penumbra scale (0 = hard/off in the debug path)
    // ── CSM (cascaded shadow maps) ────────────────────────────────────
    float csmShadowFar = 400000.0f;  // furthest distance shadows are cast (WU ≈ 400 m)
    float csmLambda    = 0.7f;       // split blend: 0 = uniform, 1 = logarithmic

    // ── Culling (per-draw distance + frustum; ships in release) ────────
    // Defaults are chosen so the RENDERED IMAGE is identical to draw-everything:
    // the G-buffer pass frustum-culls (off-screen draws don't change pixels) and
    // distance-culls only past the camera far; the shadow pass drops casters
    // farther from the camera than the shadow range can reach. The record path
    // reads these unconditionally (like god-rays/DOF), so debug defaults == the
    // shipped literals. Pull `cullMainViewDist` in to preview streaming (Layer 1).
    bool  cullEnabled      = true;        // master; off = submit every draw (pre-cull behavior)
    bool  cullFrustum      = true;        // frustum-cull the G-buffer pass (zero visible change)
    float cullMainViewDist = 4300000.0f;  // main-pass max draw distance (WU); default = camera far
    float cullShadowDist   = 600000.0f;   // shadow-pass max caster distance from camera (WU) > csmShadowFar
    // Read-only last-frame tallies (written by the Renderer each frame).
    int cullMainSubmitted   = 0;
    int cullMainTotal       = 0;
    int cullShadowSubmitted = 0;
    int cullShadowTotal     = 0;

    // ── Wet / rain ────────────────────────────────────────────────────
    float rainIntensity = 1.0f;     // lie preset (was 0.0) — [0,1] rain amount (drives haze + wetness)
    float wetPorosity   = 0.631f;   // lie preset (was 0.35) — how much water darkens/soaks the surface
    float wetRoughness  = 1.0f;     // lie preset (was 0.12) — roughness of wet (specular) surfaces
    float streakLen     = 5162.0f;  // lie preset (was 3200) — windshield / surface streak length

    // ── Puddles (W9 — road-gated standing water; ships, but dry release = off) ──
    // A world-space procedural pool mask on the asphalt (gbMaterial.a road tag) that
    // locally saturates the wet model to a mirror, reflecting the sky (IBL) and — in
    // debug — the scene (SSR). Faded in with wetness, so only shows when raining.
    bool  puddlesEnabled = true;
    float puddleCoverage = 0.501f;  // lie preset (was 0.5) — [0,1] fraction of road that pools water

    // ── Road spray (W9 — GPU compute particles behind the car) ──────────
    // Additive mist kicked up off a wet road at speed. Emission is gated by
    // wetness × speed, so a dry road (and the release build) shows nothing.
    bool  sprayEnabled  = true;
    float sprayDensity  = 0.42f;   // per-dead-particle respawn chance at full wetness × speed
    float sprayLifetime = 1.5f;    // particle lifetime (s)
    float spraySize     = 450.0f;  // billboard base size (WU ≈ 0.45 m); grows ~2.6× over life
    float sprayOpacity  = 0.16f;   // additive strength

    // ── Car (paint override for tuning) ───────────────────────────────
    float     carMetalness = 0.0f;
    glm::vec3 carPaint{1.0f, 1.0f, 1.0f};
    float     carRoughnessMul = 1.0f;
    bool      carOverride     = false;  // when true, use the above instead of the asset's material

    // ── Per-material override table (per-submesh material editor) ─────
    // Indexed by MaterialId; an enabled entry replaces that material's metalness /
    // roughness / colour in the G-buffer draw. matEditSlot is the panel's selection.
    MaterialOverride matOverrides[MAT_COUNT];
    int              matEditSlot = MAT_CAR_0;

    // ── Quality ───────────────────────────────────────────────────────
    float ssaaScale          = 2.0f;   // lie preset (was 1.5) — internal supersample factor (matches kRenderScale)
    bool  ssaaApplyRequested = false;  // set by the UI "Apply" button; Renderer consumes + clears it

    // ── TAA + motion blur (W9 — debug-only alternative to SSAA) ────────
    // Reprojection TAA with per-frame sub-pixel jitter + neighborhood clamp. Off by
    // default → the frame is identical to today until toggled. SSAA remains the
    // shipped release AA; flip taaEnabled (+ drop SSAA scale to 1.0) to trial TAA.
    bool  taaEnabled        = false;
    float taaHistoryBlend   = 0.9f;  // reprojected-history weight [0, 1]
    bool  motionBlurEnabled = false;
    float motionBlurScale   = 1.0f;  // velocity smear strength

    // ── Depth of field (debug-only post-process) ──────────────────────
    // Single-pass gather DOF: CoC grows with distance from the focus plane, then a
    // 16-tap disk of the HDR is averaged. Off by default → identical to today.
    bool  dofEnabled    = false;
    float dofFocusDist  = 40000.0f;   // in-focus distance (WU)
    float dofFocusRange = 120000.0f;  // distance over which CoC ramps to max (WU)
    float dofMaxCoC     = 6.0f;       // max circle-of-confusion radius (texels)

    // ── Sun-direction gizmo (ImGuizmo rotate handle) ──────────────────
    // When on (and in edit mode), a rotate gizmo at the origin orients the sun.
    // sunGizmoRot accumulates the rotation; the Renderer derives the sun direction
    // as normalize(mat3(sunGizmoRot) * baseSunDir).
    bool      showSunGizmo = false;
    glm::mat4 sunGizmoRot  = glm::mat4(1.0f);

    // ── Steering-wheel gizmo / override ───────────────────────────────
    // steerOverride (edit mode) poses the wheel from steerAngleDeg instead of the
    // sim. steerPivotWorld is written by App each frame so the gizmo sits on the
    // wheel; showSteerGizmo draws the rotate handle. steerMaxDeg mirrors the lock.
    bool      showSteerGizmo  = false;
    bool      steerOverride   = false;
    float     steerAngleDeg   = 0.0f;
    float     steerMaxDeg     = 35.0f;
    glm::mat4 steerPivotWorld = glm::mat4(1.0f);
    // Spin-axis calibration: edit pitch/yaw/roll OR the raw quaternion (canonical)
    // to reorient how the wheel rotates. steerAxisEdit applies it (edit mode).
    bool      steerAxisEdit = false;
    glm::vec3 steerEuler{0.0f, 0.0f, 0.0f};       // pitch, yaw, roll (degrees) — slider state
    glm::vec4 steerQuat{0.0f, 0.0f, 0.0f, 1.0f};  // x, y, z, w — quaternion editor (applied)

    // ── Road wheels (kinematic spin + steer articulation) ─────────────
    // Defaults ARE the shipped behavior (spin + steer on, 1× rate) — App
    // pushes these into CarEntity every frame under SWISH_DEBUG_UI, and the
    // entity's own defaults match, so an untouched panel changes nothing.
    // wheelSpinMul exaggerates ω for eyeballing slow footage.
    bool  wheelSpinEnabled  = true;
    bool  wheelSteerEnabled = true;
    float wheelSpinMul      = 1.0f;

    // ── UI state (not a scene parameter, but lives with the rest) ─────
    bool editMode  = false;  // true = cursor free to drive the panel; false = drive-mode (panel ignores mouse)
    bool showPanel = true;   // master visibility toggle for the debug window
};

}  // namespace swish
