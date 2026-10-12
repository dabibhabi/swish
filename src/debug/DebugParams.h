#pragma once

#include "scene/Entity/GaugeNeedles.h"  // kGaugeDefaults (Tunables.def "gauges" defaults)
#include "scene/SceneTypes.h"   // MaterialOverride, MAT_COUNT, MaterialId
#include "utils/input/GamepadMapping.h"  // GamepadConfig defaults (pure header)
#include "utils/types/Types.h"           // swish::Vec3 / Vec4 (glm aliases)

#include <glm/glm.hpp>
#include <optional>

namespace swish {

// Release-vs-debug default selection for registry tunables (see Tunables.def).
// `release_value` is the pure rule; `build_default` applies it for this build config:
// debug-UI builds use the debug default, release builds use the `.release` override
// when one exists (none do today, so release == debug == the shipped look).
template <class T>
constexpr T release_value(T def, std::optional<T> rel) {
    return rel.value_or(def);
}
template <class T>
constexpr T build_default(T def, std::optional<T> rel) {
#ifdef SWISH_DEBUG_UI
    (void)rel;
    return def;
#else
    return release_value<T>(def, rel);
#endif
}

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
    float exposure       = 1.55f;  // retuned 2026-08-15 (lie preset had 2.0) — matches the release literal
    float bloomThreshold = 1.77f;  // lie preset (was 1.0) — luminance above which bloom extracts
    float bloomIntensity = 0.45f;  // retuned 2026-08-15 (lie preset had 1.097) — matches the release literal

    // ── Auto-exposure (eye adaptation) ────────────────────────────────
    // When on, the composite exposure is driven by the smoothed scene luminance
    // (exposure = aeKey / adaptedLum, clamped) instead of the manual value above.
    bool  autoExposure = false;
    float aeKey        = 0.30f;  // target mid-grey the average maps toward
    float aeSpeed      = 2.0f;   // adaptation rate (per second)
    float aeMin        = 0.05f;  // exposure clamp (min)
    float aeMax        = 2.0f;   // exposure clamp (max)
    float brightness   = 0.0f;   // retuned 2026-08-15 (lie preset had 0.032) — matches the release literal
    float contrast     = 1.12f;  // retuned 2026-08-15 (lie preset had 1.499) — matches the release literal
    float saturation   = 1.2f;   // retuned 2026-08-15 (lie preset had 0.988) — matches the release literal
    float temperature  = 0.0f;   // warm/cool shift   [-1, 1]
    float tint         = 0.0f;   // green/magenta shift [-1, 1]

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
    float     sunAmbient = 0.50f;  // retuned 2026-08-15 (lie preset had 0.823) — matches the release literal
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

    // ── Registry tunables (src/debug/Tunables.def — single source) ─────
    // Fields + defaults below are generated from Tunables.def:
    //   ssr        SSR view-space depth ray-march, added to the composite (world units,
    //              1 m = 1000 WU). Artifact-prone — tune live against the scene.
    //   reflection envGlossExp (Fresnel/gloss falloff exponent) + IBL split-sum environment
    //              lighting: iblDiffuse = cosine-weighted sky irradiance, iblSpecular =
    //              prefiltered sky reflection × Karis env-BRDF. 1.0 = physically scaled.
    //   wet        wetPorosity / wetRoughness / puddlesEnabled / puddleCoverage (rest of the wet/rain block below)
#define TUNE_B(n, g, k, def, label, rel) bool n = build_default<bool>(def, rel);
#define TUNE_F(n, g, k, def, lo, hi, label, sfmt, prec, push, rel) float n = build_default<float>(def, rel);
#include "Tunables.def"

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
    float shadowBias       = 0.0018f;    // restored 2026-08-15 — required once shadows are live (acne)
    float shadowFloor      = 0.25f;      // restored 2026-08-15 (lie preset 1.0 disabled shadow darkening entirely)
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
    float rainIntensity = 1.0f;  // lie preset (was 0.0) — [0,1] rain amount (drives haze + wetness)
    // wetPorosity / wetRoughness (+ puddles below): generated from Tunables.def (see the registry block above).
    float streakLen = 5162.0f;  // lie preset (was 3200) — windshield / surface streak length

    // ── Puddles (W9 — road-gated standing water) ──
    // puddlesEnabled / puddleCoverage: generated from Tunables.def (wet group). A world-space
    // procedural pool mask on the asphalt (gbMaterial.a road tag) that locally saturates the wet
    // model to a mirror, reflecting the sky (IBL) and the scene (SSR, same coverage in release
    // since 2026-09-29). Faded in with wetness, so only shows when raining.

    // ── Road spray (W9 — GPU compute particles behind the car) ──────────
    // Additive mist kicked up off a wet road at speed. Emission is gated by
    // wetness × speed, so a dry road (and the release build) shows nothing.
    bool  sprayEnabled  = true;
    float sprayDensity  = 0.42f;   // per-dead-particle respawn chance at full wetness × speed
    float sprayLifetime = 1.5f;    // particle lifetime (s)
    float spraySize     = 450.0f;  // billboard base size (WU ≈ 0.45 m); grows ~2.6× over life
    float sprayOpacity  = 0.16f;   // additive strength

    // ── Per-wheel spray (debug-only; OFF = the legacy single rear-axle emitter, i.e. today's look) ──
    // App builds four wheel emitters (WheelSpray.h) from the car and hands them to SpraySystem::set_emitters
    // while this is on; release never reads it. TOML table [wheel_spray].
    bool perWheelSpray = false;

    // ── Wet lens model (debug-only CPU model, no renderer upload yet; LensWetness.h) ──
    // Defaults == LensWetnessParams{}. TOML table [lens_wet].
    float lensWetRate         = 0.35f;  // 1/s coverage gain at rain = 1
    float lensClearSpeedStart = 8.0f;   // m/s: airflow starts clearing
    float lensClearSpeedFull  = 25.0f;  // m/s: airflow clears at full rate
    float lensClearRate       = 3.0f;   // 1/s clearing at full airflow
    float lensDrainRate       = 0.02f;  // 1/s slow gravity drain

    // ── Slip visuals thresholds (debug-only; SlipVisuals.h, defaults == SlipVisualParams{}) ──
    // Angles are in degrees here (radians in the struct). TOML table [slip].
    float slipSkidRatio     = 0.20f;  // |slip ratio| above which a wheel skids
    float slipSkidAngleDeg  = 8.0f;   // |slip angle| above which a wheel skids
    float slipRatioStart    = 0.08f;  // spray-boost ramp start (boost 1)
    float slipRatioFull     = 0.30f;  // spray-boost ramp end (boost = slipSprayBoostMax)
    float slipAngleStartDeg = 4.0f;
    float slipAngleFullDeg  = 12.0f;
    float slipSprayBoostMax = 3.0f;

    // ── Car (paint override for tuning) ───────────────────────────────
    float     carMetalness = 0.0f;
    glm::vec3 carPaint{1.0f, 1.0f, 1.0f};
    float     carRoughnessMul = 1.0f;
    bool      carOverride     = false;  // when true, use the above instead of the asset's material

    // ── Clearcoat + flake (car paint BRDF) ─────────────────────────────
    // clearcoatMul/flakeDensityMul are GLOBAL multipliers over the per-material
    // authored defaults (SceneTypes.h: default_clearcoat_strength/default_flake_density),
    // same "global multiplier over an authored table" pattern as ssaoIntensity/iblDiffuse.
    // Identity (1,1) here matches the shipped release literals exactly. Set
    // flakeDensityMul to 0 to isolate the clearcoat lobe while tuning (todo.md).
    // clearcoatRoughness/flakeRoughness are the SHARED (not per-material) lobe
    // roughnesses — real automotive clearcoat is tight/near-mirror regardless of
    // the base paint; flake's highlight is broader, giving discrete glints rather
    // than a mirror. flakeScale/flakeJitter size and strength the procedural
    // per-pixel flake-cell normal perturbation (gbuffer.frag) — broadcast into
    // every draw's push constants (paint.zw), not authored per-material.
    float clearcoatMul       = 1.0f;
    float flakeDensityMul    = 1.0f;
    float clearcoatRoughness = 0.045f;  // tight, near-mirror
    float flakeRoughness     = 0.12f;   // broader than clearcoat — discrete glints, not a mirror
    float flakeScale         = 8.0f;    // flake cell size (WU; 1 m = 1000 WU) ≈ 8 mm
    float flakeJitter        = 0.35f;   // flake normal-perturbation strength [0,1]

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

    // ── Telemetry HUD (speed/gear/rpm/grip readout; issue 5's readout half) ──
    // The cockpit gauge cluster is BAKED PIXELS in the GLB texture atlas
    // (verified: zero gauge/needle/dial nodes across 3341 nodes — the needle
    // is painted at zero and can never move), so it cannot serve as a speed
    // readout; this panel is the only one available. Read-only — no tuning
    // here. Like cullMainSubmitted/steerPivotWorld above, these fields are
    // NOT written by the debug UI itself: the app pushes CarEntity's live
    // state into them once per frame (mirroring the steerPivotWorld push in
    // App.cpp, under SWISH_DEBUG_UI), and DebugUI::begin_frame only displays
    // them. Consequently they're excluded from TOML presets, same as the
    // other transient/runtime fields in this struct.
    bool  showTelemetry     = true;   // master visibility toggle for the Telemetry panel
    float telemetrySpeedMps = 0.0f;   // CarEntity::get_speed() / kWorldUnitsPerMeter (m/s)
    int   telemetryGear     = 0;      // CarEntity::get_gear(), 0 = 1st ... 6 = 7th
    float telemetryRpm      = 0.0f;   // CarEntity::get_engine_rpm()
    float telemetryRedline  = 7200.0f;  // CarEntity::get_redline_rpm() (active CarProfile's fuel cut)
    float telemetryMu       = 1.05f;  // CarEntity::get_effective_mu(); matches CarParams::tireMu dry default

    // ── Driving tools (record / replay / gamepad / FFB readout) ──────
    // Debug-only and transient (NOT in Tunables.def / TOML presets: the registry is for converted
    // scene-look groups and is size-pinned by tests). Identity defaults: keyboard source, nothing
    // recording or replaying, so an untouched panel leaves the drive path exactly as before.
    // The UI writes the control block; App (AppDrivingTools.cpp) consumes the *Requested pulses and
    // writes the readout block once per frame. Deadzone/expo defaults are GamepadConfig's own.
    int   drvInputSource = 0;  // 0 = Keyboard (CarEntity::handle_input), 1 = Gamepad
    float drvDeadzone    = GamepadConfig{}.steer_deadzone;
    float drvExpo        = GamepadConfig{}.steer_expo;
    bool  drvRecord      = false;  // rising edge starts a fresh clip
    bool  drvReplay      = false;  // rising edge replays the ring's recorded inputs
    bool  drvExportRequested = false;  // one-shot: App writes build-debug/telemetry/*.csv, then clears
    // Readouts (written by App).
    bool  drvGamepadConnected = false;
    char  drvGamepadName[64]  = "";
    float drvAxisSteer        = 0.0f;  // [-1,1] driver command (gamepad: mapped frame; else the car's applied value)
    float drvAxisThrottle     = 0.0f;  // [0,1]
    float drvAxisBrake        = 0.0f;  // [0,1]
    int   drvRingSize         = 0;
    int   drvRingCapacity     = 0;
    int   drvReplayPos        = 0;
    int   drvReplaySize       = 0;
    char  drvStatus[96]       = "";    // last export / replay message
    float drvFfbTorqueNm      = 0.0f;  // front-axle self-aligning torque target (no device output)
    float drvFfbNormalized    = 0.0f;  // torque / FfbParams::max_torque_nm in [-1,1]
    // Per-wheel slip readout (0 FL, 1 FR, 2 BL, 3 BR; CarEntity::get_wheel_slip_visuals) and per-lens wet readout
    // (0 Headlight_L, 1 Headlight_R, 2 TailLight_L, 3 TailLight_R; LensWetnessModel). Pushed by App, not persisted.
    struct TelemetryWheel {
        float slipRatio  = 0.0f;
        float slipAngle  = 0.0f;  // rad
        float sprayBoost = 1.0f;
        bool  skid       = false;
    };
    struct TelemetryLens {
        float coverage = 0.0f;
        float dropSize = 0.0f;
        float streak   = 0.0f;
    };
    TelemetryWheel telemetryWheel[4];
    TelemetryLens  telemetryLens[4];

    // ── UI state (not a scene parameter, but lives with the rest) ─────
    bool editMode  = false;  // true = cursor free to drive the panel; false = drive-mode (panel ignores mouse)
    bool showPanel = true;   // master visibility toggle for the debug window
};

}  // namespace swish
