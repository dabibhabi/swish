#version 450

#include "bindings.glsl"
#include "color.glsl"

// Deferred lighting pass — reads G-buffer, computes PBR lighting in screen space.
// Outputs HDR color (values can exceed 1.0 for bloom).

layout(location = 0) in vec2 fragUV;

// Sun shadow cascades (CSM). MUST match swish::NUM_CASCADES (SceneTypes.h) and
// kNumCascades (PostProcessManager). The shadow map is a horizontal atlas of
// NUM_CASCADES square slices side by side.
#define NUM_CASCADES 3

// ── Camera + Lights (same UBOs as scene pass, set 0) ──────────────
#define CAMERA_UBO_CSM
#include "camera.glsl"

#include "lights.glsl"

// ── G-Buffer textures (set 1) ────────────────────────────────────
layout(set = SET_GBUFFER, binding = BIND_GB_ALBEDO) uniform sampler2D gbAlbedo;
layout(set = SET_GBUFFER, binding = BIND_GB_NORMAL) uniform sampler2D gbNormal;
layout(set = SET_GBUFFER, binding = BIND_GB_MATERIAL) uniform sampler2D gbMaterial;
layout(set = SET_GBUFFER, binding = BIND_GB_DEPTH) uniform sampler2D gbDepth;
// Clearcoat + flake (always-on MRT — see gbuffer.frag). RG = octahedral-encoded
// flake-perturbed world normal, B = clearcoat lobe strength, A = flake mask.
layout(set = SET_GBUFFER, binding = BIND_GB_CLEARCOAT_FLAKE) uniform sampler2D gbClearcoatFlake;

// SSAO (debug builds: ssao.frag -> ao_blur.frag run before this pass, r = AO). It occludes the INDIRECT light only:
// the sun has its own shadow map and point lights are local, so the old composite `hdr *= ao` that darkened them
// too flattened every sunlit surface. Release has no SSAO, and these expand to the bare term.
#ifdef SWISH_DEBUG_UI
layout(set = SET_GBUFFER, binding = BIND_GB_AO) uniform sampler2D gbAo;
#define AO_DIFFUSE(x) ((x) * ssao)
#define AO_SPEC(x)    ((x) * specAO)
#else
#define AO_DIFFUSE(x) (x)
#define AO_SPEC(x)    (x)
#endif

// ── Sun shadow map (set 2) — CSM atlas of NUM_CASCADES horizontal slices.
//    PLAIN sampler2D + manual PCF compare: MoltenVK's portability subset has
//    no comparison samplers (no sampler2DShadow). ──
layout(set = SET_SHADOW, binding = BIND_SHADOW_MAP) uniform sampler2D shadowMap;

// ── IBL: prefiltered environment baked from the procedural sky (set 3) ──
// irradiance = diffuse hemisphere convolution; prefiltered = GGX specular with
// roughness mips; brdfLUT = split-sum scale+bias. Baked by IBLManager; present in
// BOTH builds (release included), so the set index is fixed here regardless of the
// debug scene-params UBO (which moves to set 4 when SWISH_DEBUG_UI is on).
layout(set = SET_IBL, binding = BIND_IBL_IRRADIANCE) uniform samplerCube irradianceMap;
layout(set = SET_IBL, binding = BIND_IBL_PREFILTERED) uniform samplerCube prefilteredMap;
layout(set = SET_IBL, binding = BIND_IBL_BRDF_LUT) uniform sampler2D   brdfLUT;
#define IBL_PREFILTER_MAX_MIP 4.0  // kPrefilterMips - 1

// ── Push constants: inverse matrices for position reconstruction ──
layout(push_constant) uniform LightingPC {
    mat4 invView;
    mat4 invProj;
} pc;

layout(location = 0) out vec4 outColor;

#include "math.glsl"

// ── Scene "look" constants ────────────────────────────────────────────
// These 13 values used to be hardcoded here. Under the debug UI they are
// promoted to a live uniform (set 3) so the in-engine panel can drive the
// look in real time; in a normal `make run` build the shader compiles
// without SWISH_DEBUG_UI and keeps the exact literals below, so release
// output is byte-for-byte unchanged (no set 3, no extra binding). The rest
// of the shader reads them through the SP_* macros regardless of build.
#ifdef SWISH_DEBUG_UI
layout(set = SET_SCENE_PARAMS, binding = BIND_SCENE_PARAMS) uniform SceneParamsUBO {
    vec4 skyHorizonOvercast;  // rgb
    vec4 skyHorizonClear;     // rgb
    vec4 skyZenithOvercast;   // rgb
    vec4 skyZenithClear;      // rgb
    vec4 sunDisc;             // x=expMin y=expMax z=strMin w=strMax
    vec4 fogColor;            // rgb
    vec4 fogParams;           // x=dist63 y=max z=envGlossExp
    vec4 shadowParams;        // x=bias y=floor
    vec4 wetParams;           // x=porosity y=roughnessCollapse
    vec4 iblParams;           // x=diffuse intensity y=specular intensity
    vec4 paintParams;         // x=clearcoat roughness y=flake roughness
    // Lighting lab roof volumes (util/light/SkyVisibility.h): boxes under which the sky is hidden.
    vec4 roofMin[12];         // xyz = box min (WU), w = fade distance from an open end (WU)
    vec4 roofMax[12];         // xyz = box max (WU), w = open axis (0 = X, 2 = Z)
    vec4 roofInfo[12];        // x = visibility left deep inside
    vec4 roofCount;           // x = number of volumes in use; yzw = wet streaks: on, alpha along, alpha across
    vec4 atmoSun;             // xyz = sun direction, w = lux at the top of the atmosphere
    vec4 atmoMoon;            // xyz = moon direction, w = its lux
    vec4 atmoParams;          // x = turbidity, y = cloud blend, z = airglow nits, w = overcast zenith nits
    vec4 atmoFlags;           // x = procedural sky on, y = stars on, z = rain-sky fade (0 procedural .. 1 HDRI), w = ground bounce
    vec4 lightFlags;          // x = cascade blend band, y = energy compensation on, z = retro gain, w = sun angular radius
} sp;
#include "atmosphere.glsl"
#define SP_SKY_HORIZON_OVERCAST sp.skyHorizonOvercast.rgb
#define SP_SKY_HORIZON_CLEAR    sp.skyHorizonClear.rgb
#define SP_SKY_ZENITH_OVERCAST  sp.skyZenithOvercast.rgb
#define SP_SKY_ZENITH_CLEAR     sp.skyZenithClear.rgb
#define SP_SUN_DISC_EXP_MIN     sp.sunDisc.x
#define SP_SUN_DISC_EXP_MAX     sp.sunDisc.y
#define SP_SUN_DISC_STR_MIN     sp.sunDisc.z
#define SP_SUN_DISC_STR_MAX     sp.sunDisc.w
#define SP_FOG_COLOR            sp.fogColor.rgb
#define SP_FOG_DIST63           sp.fogParams.x
#define SP_FOG_MAX              sp.fogParams.y
#define SP_ENV_GLOSS_EXP        sp.fogParams.z
#define SP_SHADOW_BIAS          sp.shadowParams.x
#define SP_SHADOW_FLOOR         sp.shadowParams.y
#define SP_SHADOW_LIGHT_SIZE    sp.shadowParams.z
#define SP_WET_POROSITY         sp.wetParams.x
#define SP_WET_ROUGHNESS        sp.wetParams.y
#define SP_PUDDLE_COVERAGE      sp.wetParams.z
#define SP_RUT_WATER            sp.wetParams.w
#define SP_RAIN_CLOCK           sp.shadowParams.w
#define SP_RIPPLE_STRENGTH      sp.iblParams.z
#define SP_RIPPLE_CELL          sp.iblParams.w
#define SP_RUT_ROUGHNESS        sp.paintParams.z
#define SP_IBL_DIFFUSE          sp.iblParams.x
#define SP_IBL_SPECULAR         sp.iblParams.y
// Aerial-perspective haze packs into spare UBO lanes (no new row): dist63 in
// fogParams.w, max in fogColor.w. Distinct from the wet rain fog above.
#define SP_HAZE_DIST            sp.fogParams.w
#define SP_HAZE_MAX             sp.fogColor.w
#define SP_CLEARCOAT_ROUGHNESS  sp.paintParams.x
#define SP_FLAKE_ROUGHNESS      sp.paintParams.y
#else
// Release literals — identical to the previously-hardcoded values.
#define SP_SKY_HORIZON_OVERCAST vec3(0.86, 0.87, 0.89)
#define SP_SKY_HORIZON_CLEAR    vec3(0.85, 1.0, 1.25)   // lie preset (was 0.55,0.82,1.30)
#define SP_SKY_ZENITH_OVERCAST  vec3(0.76, 0.79, 0.83)
#define SP_SKY_ZENITH_CLEAR     vec3(0.5, 0.8, 1.35)    // lie preset (was 0.28,0.52,1.50)
#define SP_SUN_DISC_EXP_MIN     32.0
#define SP_SUN_DISC_EXP_MAX     220.0
#define SP_SUN_DISC_STR_MIN     0.242  // lie preset (was 0.3)
#define SP_SUN_DISC_STR_MAX     0.9
// Depth-resolved rain fog (R-P1-1). fog colour = cool overcast airlight; dist63
// = distance (WU; 1 m = 1000 WU) at which fog reaches ~63% at full wetness. Large
// enough that the near cabin (~1000 WU away) is essentially fog-free.
#define SP_FOG_COLOR            vec3(0.52, 0.57, 0.63)
#define SP_FOG_DIST63           1200000.0  // ~1.2 km to 63% at full rain
#define SP_FOG_MAX              0.0         // lie preset: distance rain-fog OFF (was 0.65)
#define SP_ENV_GLOSS_EXP        3.0
#define SP_SHADOW_BIAS          0.0018 // EXPERIMENT: restored (lie preset had 0.0)
#define SP_SHADOW_FLOOR         0.25   // EXPERIMENT: shadows back on (lie preset had 1.0 = off)
#define SP_SHADOW_LIGHT_SIZE    0.0
#define SP_WET_POROSITY         0.631  // lie preset (was 0.35)
#define SP_WET_ROUGHNESS        1.0    // lie preset (was 0.12)
#define SP_PUDDLE_COVERAGE      0.501  // lie preset (was 0.0 = dry)
#define SP_IBL_DIFFUSE          1.30   // retuned between restored 1.0 and lie preset 1.832
#define SP_IBL_SPECULAR         1.0
// Always-on aerial perspective (distance haze) — not wet-gated, so a dry day still
// reads with depth to the horizon. dist63 ≈ 900 m to 63%; max 0.5 = distant geometry
// dissolves halfway into the sky. Near surfaces (cabin ~1 m) are unaffected.
#define SP_HAZE_DIST            900000.0
#define SP_HAZE_MAX             0.5
// Clearcoat + flake (car paint BRDF — see tasks/todo.md). Tight/near-mirror
// clearcoat, broader flake glint. Ships in release (always-on MRT); these are
// the shared (not per-material) lobe roughnesses.
#define SP_CLEARCOAT_ROUGHNESS  0.045
#define SP_FLAKE_ROUGHNESS      0.12
#endif

// Poisson-disk kernel for PCSS blocker search + variable-radius PCF (debug only).
const vec2 POISSON16[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

// ── Reconstruct world position from depth (rind pattern) ──────────
// `depth` is the raw Vulkan depth-buffer value in [0,1]. pc.invProj is the
// inverse of the same [0,1] clip-space projection (GLM_FORCE_DEPTH_ZERO_TO_ONE),
// so ndc.z = depth is already correct — no [0,1]->[-1,1] remap needed.
vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProj * ndc;
    viewPos /= viewPos.w;
    vec4 worldPos = pc.invView * viewPos;
    return worldPos.xyz;
}

// Octahedral normal decode (inverse of gbuffer.frag's octEncode) — recovers the
// flake-perturbed world normal packed into gbClearcoatFlake.rg.
#define SWISH_OCT_DECODE
#include "oct.glsl"

// ── Procedural road puddles (W9) ───────────────────────────────────
// Static low-frequency water pools evaluated in WORLD space (so they stay put as
// the camera moves). `coverage` raises the water level — more coverage lowers the
// threshold → bigger pools. Gated to the road tag (gbMaterial.a, written by
// gbuffer.frag from MAT_ASPHALT) so grass/barriers/car never turn to mirrors.
// Inside a pool the existing wet model collapses roughness + flattens the normal,
// so the prefiltered-sky IBL reflection resolves as a coherent puddle mirror.
// Zero in release (SP_PUDDLE_COVERAGE literal is 0) → byte-identical.
#include "puddle.glsl"

// ── Generic Cook-Torrance GGX specular lobe (rind pattern), reused for the
// clearcoat + flake lobes below so their D/G/F math doesn't duplicate the base
// lobe inline in main(). Takes NdotH/NdotV/NdotL/HdotV for a (possibly
// per-lobe-perturbed) normal, plus that lobe's own roughness + F0.
vec3 ggxSpecular(float NdotH, float NdotV, float NdotL, float HdotV, float roughness, vec3 F0) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D  = a2 / (PI * denom * denom + 0.0001);
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    float G  = G1V * G1L;
    vec3  F  = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
    return (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
}

#ifdef SWISH_DEBUG_UI
// ── Rectangle area lights (car showcase) ──────────────────────────────
// Solid angle of a rectangle p0..p3 (in order around it) seen from p.
float rectSolidAngle(vec3 p, vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
    vec3 v0 = p0 - p, v1 = p1 - p, v2 = p2 - p, v3 = p3 - p;
    vec3 n0 = normalize(cross(v0, v1)), n1 = normalize(cross(v1, v2));
    vec3 n2 = normalize(cross(v2, v3)), n3 = normalize(cross(v3, v0));
    float g0 = acos(clamp(dot(-n0, n1), -1.0, 1.0)), g1 = acos(clamp(dot(-n1, n2), -1.0, 1.0));
    float g2 = acos(clamp(dot(-n2, n3), -1.0, 1.0)), g3 = acos(clamp(dot(-n3, n0), -1.0, 1.0));
    return max(g0 + g1 + g2 + g3 - 2.0 * PI, 0.0);
}
// How much of the straight path from `from` to `to` the car's body blocks: 0 clear .. 1 blocked. The body is
// an oriented box (lights.carBox*). A soft edge: the more of the path runs inside the box, the darker.
// A point inside the box (the car's own surfaces) is never shadowed by it.
float carBoxBlock(vec3 from, vec3 to) {
    float yaw = lights.carBoxCenter.w;
    vec3  fwd = vec3(cos(yaw), 0.0, -sin(yaw)), rgt = vec3(sin(yaw), 0.0, cos(yaw));
    vec3  h   = lights.carBoxHalf.xyz;
    vec3  o   = from - lights.carBoxCenter.xyz, d = to - from;
    vec3  lo  = vec3(dot(o, fwd), o.y, dot(o, rgt)), ld = vec3(dot(d, fwd), d.y, dot(d, rgt));
    if (all(lessThan(abs(lo), h * 1.03))) return 0.0;
    ld = mix(ld, vec3(1.0e-5), lessThan(abs(ld), vec3(1.0e-5)));
    vec3  t0 = (-h - lo) / ld, t1 = (h - lo) / ld;
    vec3  tn = min(t0, t1), tf = max(t0, t1);
    float tNear = max(max(tn.x, tn.y), max(tn.z, 0.0)), tFar = min(min(tf.x, tf.y), min(tf.z, 1.0));
    return smoothstep(0.0, 0.06, tFar - tNear);
}
// GGX specular with an explicit alpha (the area-light lobe is widened by the light's size).
#ifdef SWISH_DEBUG_UI
// L10.2: a light's reflection on a wet road is a vertical streak, not a dot. The rain-roughened water film spreads
// its microfacets mostly along the viewing direction, so the lobe is anisotropic GGX (Burley 2012) with the long axis
// t = V projected onto the surface (alpha_along) and the short axis b = N x t (alpha_across):
//   D = 1 / (pi aT aB ((h.t / aT)^2 + (h.b / aB)^2 + (h.n)^2)^2),  G = Smith-Schlick with a = sqrt(aT aB).
vec3 wetStreakSpec(vec3 N, vec3 V, vec3 L, float aT, float aB, vec3 F0) {
    vec3  H  = normalize(V + L);
    vec3  t  = V - N * dot(V, N);
    float tl = length(t);
    t        = tl > 1.0e-4 ? t / tl : normalize(cross(N, vec3(1.0, 0.0, 0.0)));
    vec3  b  = cross(N, t);
    float ht = dot(H, t) / aT, hb = dot(H, b) / aB, hn = max(dot(H, N), 0.0);
    float dd = ht * ht + hb * hb + hn * hn;
    float D  = 1.0 / (PI * aT * aB * dd * dd);
    float NdotV = max(dot(N, V), 1.0e-3), NdotL = max(dot(N, L), 0.0);
    float k  = sqrt(aT * aB) * 0.5;
    float G  = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3  F  = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    return D * G * F / max(4.0 * NdotV * NdotL, 1.0e-3);
}
#endif
vec3 ggxSpecularAlpha(vec3 N, vec3 V, vec3 L, float alpha, vec3 F0) {
    vec3  H     = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0), NdotV = max(dot(N, V), 0.001), NdotL = max(dot(N, L), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    float a2    = alpha * alpha;
    float k     = (sqrt(alpha) + 1.0) * (sqrt(alpha) + 1.0) / 8.0;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D     = a2 / (PI * denom * denom + 1.0e-7);
    float G     = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3  F     = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
    return (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
}

// Lighting L3.3: a plain 3x3 PCF of one cascade, used to blend into the next cascade near a split so the
// change of shadow resolution is not a visible line.
float cascadePcf3(int cascade, vec3 worldPos, float bias) {
    vec4 lp = camera.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 sc = lp.xyz / lp.w;
    sc.xy   = sc.xy * 0.5 + 0.5;
    if (sc.z > 1.0 || any(lessThan(sc.xy, vec2(0.0))) || any(greaterThan(sc.xy, vec2(1.0)))) return 1.0;
    float invN = 1.0 / float(NUM_CASCADES);
    vec2  tx   = 1.0 / vec2(textureSize(shadowMap, 0));
    vec2  uv   = vec2((float(cascade) + sc.x) * invN, sc.y);
    float uMin = float(cascade) * invN + tx.x, uMax = float(cascade + 1) * invN - tx.x;
    float vis  = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 suv = uv + vec2(x, y) * tx;
            suv.x    = clamp(suv.x, uMin, uMax);
            vis += (sc.z - bias <= texture(shadowMap, suv).r) ? 1.0 : 0.0;
        }
    return vis / 9.0;
}
#endif

#ifdef SWISH_DEBUG_UI
// Rain P4: procedural rain ripples on standing water (Lagarde "Water drop 2b": circles that grow and fade, no texture).
// A jittered world-space grid; each cell fires one drop per period at a hashed phase and spawn chance. A drop sends a
// ring wave packet h(x) = sin(k x) exp(-x^2 / 2w^2), x = d - v t, faded by (1 - t/T)^2. Returns the height gradient
// (dh/dx, dh/dz) summed over the 3x3 neighbouring cells, so rings crossing cell borders stay whole.
float rippleHash(vec2 c, float s) { return fract(sin(dot(c, vec2(127.1, 311.7)) + s * 74.7) * 43758.5453); }
vec2 rippleGradient(vec2 xz, float t, float cell) {
    const float kPeriod = 0.9;     // s between drops in one cell
    const float kLife   = 0.75;    // s a ring is visible
    const float kSpeed  = 0.35;    // ring growth, as a fraction of the cell per second... in WU below
    vec2  base = floor(xz / cell);
    vec2  g    = vec2(0.0);
    float v    = kSpeed * cell / kLife * 2.0;  // the ring reaches ~70 % of a cell over its life
    float k    = 6.2831853 / (cell * 0.12);    // wavelength 12 % of a cell (~4 cm at 0.35 m)
    float w    = cell * 0.06;                  // packet half-width
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            vec2  c     = base + vec2(i, j);
            float phase = rippleHash(c, 1.0);
            float tc    = t / kPeriod + phase;
            float tl    = fract(tc) * kPeriod;
            if (tl > kLife || rippleHash(c, floor(tc)) > 0.7) continue;  // ~70 % of cells fire each period
            vec2  ctr = (c + vec2(rippleHash(c, 2.0), rippleHash(c, 3.0))) * cell;
            vec2  dv  = xz - ctr;
            float d   = length(dv);
            float x   = d - v * tl;
            float env = exp(-x * x / (2.0 * w * w));
            float fade = (1.0 - tl / kLife);
            fade *= fade;
            float dh  = (k * cos(k * x) - x / (w * w) * sin(k * x)) * env * fade;  // dh/dd
            g += (d > 1e-3 ? dv / d : vec2(0.0)) * dh;
        }
    return g;
}
#endif

void main() {
    // ── Read G-Buffer ─────────────────────────────────────────────
    vec3  albedo    = texture(gbAlbedo, fragUV).rgb;
#ifdef SWISH_DEBUG_UI
    // Lighting L5 / L8: the two spare G-buffer alphas (see gbuffer.frag).
    float gbLane     = texture(gbAlbedo, fragUV).a;  // 0.5 = retroreflective; else 0.25 x the wet-car amount
    float gbRetro    = step(0.4, gbLane);            // 1 = retroreflective (lane markings)
    float carWet     = (1.0 - gbRetro) * clamp(gbLane * 4.0, 0.0, 1.0);  // a rain-wet car surface (showcase)
    float gbEmissive = texture(gbNormal, fragUV).a;  // emissive strength, already pre-exposed
    vec3  emissiveAlbedo = albedo;                   // the surface colour before wet darkening
#endif
    vec3  rawNormal = texture(gbNormal, fragUV).rgb;
    vec4  material  = texture(gbMaterial, fragUV);
    float depth     = texture(gbDepth, fragUV).r;
    vec4  ccFlake   = texture(gbClearcoatFlake, fragUV);

    float wetness = camera.camPos.w;  // packed by CameraUniforms::set_wetness()

    // L1.3: the sun is in lux and the sky in nits. The HDR target is half-float (max 65,504), so each light is
    // multiplied by the pre-exposure here and the composite divides it out. With kPreExposure = 1e-5 and
    // sunLux = skyNits = 1e5 both scales are 1.0, which is exactly the old frame.
    float sunScale = camera.sunDir.w * camera.weather.z;   // lux  * pre-exposure
    float skyScale = camera.weather.y * camera.weather.z;  // nits * pre-exposure

    // Sky pixels (reverse-Z: far/sky is depth 0.0, no geometry). Sample the
    // prefiltered env at mip 0 so the visible sky matches IBL reflections
    // (HDRI or procedural — whichever filled the cubes).
#ifdef SWISH_DEBUG_UI
    if (depth <= 0.0) {  // only the clear value is sky: near 10 WU puts geometry past 100 m below 1e-4
#else
    if (depth < 0.0001) {
#endif
        vec3 viewDir = normalize(reconstructWorldPos(fragUV, 0.5) - camera.camPos.xyz);
        vec3 sky     = textureLod(prefilteredMap, viewDir, 0.0).rgb * SP_IBL_SPECULAR * skyScale;
#ifdef SWISH_DEBUG_UI
        if (sp.atmoFlags.x > 0.5) {
            // Lighting L2.5: the visible sky comes straight from the atmosphere model, not the 256^2 cube, so it
            // is sharp and matches the bake. Plus the disc of the active light (the sun by day, the moon at night)
            // and stars. Everything in nits, then pre-exposed; the disc is capped so bloom stays local.
            vec3 nits = atmSky(viewDir, sp.atmoSun, sp.atmoMoon, sp.atmoParams);
            if (viewDir.y > 0.0) {
                nits += camera.sunColor.rgb * camera.sunDir.w * atmDisc(viewDir, camera.sunDir.xyz);
                if (sp.atmoFlags.y > 0.5) {
                    vec3  cellPos = viewDir * 260.0;
                    vec3  cell    = floor(cellPos);
                    float h       = fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);
                    float d       = length(fract(cellPos) - 0.5);
                    nits += vec3(step(0.992, h) * smoothstep(0.22, 0.0, d) * 0.05 * (1.0 - sp.atmoParams.y));
                }
            }
            sky = min(nits * camera.weather.z, vec3(30.0));
        } else if (sp.atmoFlags.z < 0.999) {
            // Rain round 3: rain onset fades the visible sky from the analytic procedural sky (still uploaded) to
            // the rain HDRI in the cube, like clouds rolling in. atmoFlags.z = 0 procedural .. 1 HDRI.
            vec3 nits = atmSky(viewDir, sp.atmoSun, sp.atmoMoon, sp.atmoParams);
            sky       = mix(min(nits * camera.weather.z, vec3(30.0)), sky, sp.atmoFlags.z);
        }
#endif
        vec3 overcast = sky * (1.0 - wetness * 0.45);
        outColor = vec4(mix(sky, overcast, wetness * 0.9), 1.0);
        return;
    }

    // Decode normal: [0,1] → [-1,1]
    vec3  N         = normalize(rawNormal * 2.0 - 1.0);
    float metallic  = material.r;
    float roughness = material.g;
    float wettable  = material.b;  // 1 = rain-exposed (road/body), 0 = enclosed cabin
    float road      = material.a;  // 1 = asphalt (road tag) — drives puddles

    // Clearcoat + flake (gbuffer.frag). flakeN decodes to N itself where no flake
    // cell is active (flakeMask 0), so the flake lobe below only contributes where
    // gbuffer.frag actually perturbed the normal.
    float clearcoatStrength = ccFlake.b;
    float flakeMask         = ccFlake.a;
    vec3  flakeN            = octDecode(ccFlake.rg);

    // World position (needed early for the world-space puddle mask; reused below).
    vec3 fragWorldPos = reconstructWorldPos(fragUV, depth);

    // Sky visibility (lighting lab, debug builds). Only the SUN is shadow-mapped; the sky's ambient light and
    // reflections ignore geometry, so a tunnel would stay bright. Inside a roof volume the sky fades from 1 at
    // an open end to the volume's floor. The GLSL copy of util::light::roof_sky_visibility; keep them in step.
    float skyVis = 1.0;
#ifdef SWISH_DEBUG_UI
    float roofSun = 1.0;  // C4: closed rooms and one-ended volumes (a tunnel tube) also hide the sun
    for (int i = 0; i < int(sp.roofCount.x); ++i) {
        vec3 lo = sp.roofMin[i].xyz, hi = sp.roofMax[i].xyz;
        if (all(greaterThanEqual(fragWorldPos, lo)) && all(lessThanEqual(fragWorldPos, hi))) {
            float open = sp.roofMax[i].w;  // 0 = X ends, 2 = Z ends, 3 = all four sides, -1 = a closed room
            float dx   = min(fragWorldPos.x - lo.x, hi.x - fragWorldPos.x);
            float dz   = min(fragWorldPos.z - lo.z, hi.z - fragWorldPos.z);
            float depthIn = open < -0.5 ? 1.0e9
                          : open < 1.0 ? dx
                          : open > 3.5 ? fragWorldPos.z - lo.z   // only the -Z end is open
                          : open > 2.5 ? min(dx, dz) : dz;
            float v = mix(1.0, sp.roofInfo[i].x, smoothstep(0.0, sp.roofMin[i].w, depthIn));
            skyVis  = min(skyVis, v);
            if (open < -0.5 || open > 3.5) roofSun = min(roofSun, v);
        }
    }
#endif

    // Road puddles: patchy pools where standing water saturates the asphalt to a
    // mirror. Faded in with wetness so a dry road never pools; zero in release.
    float puddle = puddleAmount(fragWorldPos.xz, SP_PUDDLE_COVERAGE, road)
                 * smoothstep(0.05, 0.5, wetness);

    // Wet-weather effects apply only to rain-exposed surfaces. The enclosed car
    // cabin stays dry (wettable = 0 → wetLocal = 0), so rain never washes it out.
    // A puddle locally saturates the surface regardless of the global rain amount.
#ifdef SWISH_DEBUG_UI
    // Rain P3: wheel-track ruts (gbuffer.frag packs them into the road tag as 0.55 + 0.45 rut). Polished, slightly
    // sunken bands hold standing water once the road is wet, i.e. thin linear puddles: they join the puddle term
    // (flatter normal, near-mirror), which an already-wet road's film alone would not show.
    float rut      = road >= 0.5 ? clamp((road - 0.55) / 0.45, 0.0, 1.0) : 0.0;
    float rutWater = rut * SP_RUT_WATER * smoothstep(0.1, 0.6, wetness) * wettable;
    puddle         = max(puddle, rutWater);
#endif
    float wetLocal = max(wetness * wettable, puddle);
#ifdef SWISH_DEBUG_UI
    float roadWet  = wetLocal;                 // what may flatten the normal toward 'up' (a road, not a car)
    wetLocal       = max(wetLocal, carWet);    // car showcase: a wet car is wet whatever the weather
#endif

    // ── Wet-surface model (P0 #9) ─────────────────────────────────────
    // As water accumulates (wetLocal ∝ rain rate R, gated by exposure): diffuse
    // darkens toward a porosity floor (asphalt soaks up light), roughness collapses
    // toward a mirror, and micro-normals on up-facing (road-like) surfaces flatten
    // toward the plane so reflections stay coherent. The grazing Fresnel surge that
    // actually reads as "wet" is added after lighting, below.
#ifdef SWISH_DEBUG_UI
    // Rain P2: Lekner & Dorf 1988 (Appl. Opt. 27(7), Victoria Univ. of Wellington), per channel. Light enters the
    // water film (1 - R1), part is absorbed by the surface (a = 1 - dry albedo); diffusely reflected light beyond the
    // critical angle is totally reflected back down (probability p) and gets another chance to be absorbed:
    //   A = (1 - R1) a_w / (1 - p (1 - a_w)),   wet albedo = 1 - A,   R1 = ((n-1)/(n+1))^2 = 0.02, p = 0.475 (water)
    // a_w = a (1 + 0.08 (1 - a)): absorption rises slightly under water (their Eq. 11, n_r = 2). The water film's own
    // mirror reflection is the specular lobe + wet sheen below, so it is not added here.
    const float kLdR1 = 0.02, kLdP = 0.475;
    vec3 ldA       = vec3(1.0) - albedo;
    vec3 ldAw      = min(ldA * (1.0 + 0.08 * (1.0 - ldA)), vec3(1.0));
    vec3 wetAlbedo = vec3(1.0) - (1.0 - kLdR1) * ldAw / (vec3(1.0) - kLdP * (1.0 - ldAw));
    albedo         = mix(albedo, wetAlbedo, wetLocal);
    // Rain P3: standing water in the ruts covers the bright aggregate and traps more light under the film (more
    // Lekner-Dorf internal-reflection passes), so the tyre paths read darker than the merely wet road beside them.
    // Partly phenomenological: 0.4 is tuned against the owner's LIE photos.
    albedo *= 1.0 - 0.4 * rutWater;
#else
    float kPorosity = SP_WET_POROSITY;  // asphalt diffuse floor when saturated
    float luma      = dot(albedo, LUMA_601);
    vec3  wetAlbedo = mix(albedo * kPorosity, vec3(luma * kPorosity), 0.25);  // darken + desaturate
    albedo          = mix(albedo, wetAlbedo, wetLocal);
#endif

    roughness = mix(roughness, roughness * SP_WET_ROUGHNESS, wetLocal);  // toward near-mirror, scaled by water
    // Standing water is a near-perfect mirror regardless of the asphalt beneath, so
    // collapse roughness further inside a pool (independent of the wet-film scale).
    roughness = mix(roughness, 0.04, puddle);
#ifdef SWISH_DEBUG_UI
    roughness = mix(roughness, SP_RUT_ROUGHNESS, rutWater);
#endif
    roughness = clamp(roughness, 0.02, 1.0);

    // Flatten micro-normal toward the geometric plane on up-facing surfaces; a
    // puddle surface is flatter still (a calm water plane), so it flattens harder.
    float upFacing = clamp(N.y, 0.0, 1.0);
#ifdef SWISH_DEBUG_UI
    float flatten  = max(roadWet * upFacing * 0.6, puddle * upFacing * 0.95);
#else
    float flatten  = max(wetLocal * upFacing * 0.6, puddle * upFacing * 0.95);
#endif
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), flatten));
#ifdef SWISH_DEBUG_UI
    // Rain P4: ripples only where water stands (puddles, incl. rut water); a film alone is too thin to ring.
    if (SP_RIPPLE_STRENGTH > 0.0 && puddle * upFacing > 0.02) {
        vec2 g = rippleGradient(fragWorldPos.xz, SP_RAIN_CLOCK, SP_RIPPLE_CELL);
        // gradient (per WU) -> slope: x cell/k-scale so strength 1 tilts ~0.3 rad at the crest
        vec2 slope = g * (SP_RIPPLE_CELL * 0.02) * SP_RIPPLE_STRENGTH * puddle * upFacing;
        N = normalize(N + vec3(-slope.x, 0.0, -slope.y));
    }
#endif

#ifdef SWISH_DEBUG_UI
    // Car showcase: beaded rain drops on a wet car. Cells in the CAR's own frame (so drops ride with the
    // car), some of them holding a drop; inside a drop the normal bulges away from its centre and the
    // surface is mirror-smooth, so every drop catches a small highlight.
    if (carWet > 0.0) {
        float yaw = lights.carBoxCenter.w;
        vec3  fwd = vec3(cos(yaw), 0.0, -sin(yaw)), rgt = vec3(sin(yaw), 0.0, cos(yaw));
        vec3  o   = fragWorldPos - lights.carBoxCenter.xyz;
        vec3  cp  = vec3(dot(o, fwd), o.y, dot(o, rgt)) / 14.0;   // 14 mm cells
        vec3  f   = fract(cp) - 0.5;
        float h   = fract(sin(dot(floor(cp), vec3(127.1, 311.7, 74.7))) * 43758.5453);
        float r   = mix(0.18, 0.42, fract(h * 7.31));
        float drop = step(0.55, h) * smoothstep(r, r * 0.55, length(f)) * carWet;
        N         = normalize(N + (fwd * f.x + vec3(0.0, f.y, 0.0) + rgt * f.z) * (drop * 1.6));
        roughness = mix(roughness, 0.04, drop);
    }
#endif
    vec3 V = normalize(camera.camPos.xyz - fragWorldPos);
    float NdotV = max(dot(N, V), 0.001);

    // ── Sun shadow visibility (3×3 PCF, manual depth compare) ──────────
    // MoltenVK has no comparison samplers, so sample the depth as a plain texture
    // and compare here. Project world pos into light space, map xy→[0,1]; a
    // fragment is lit where its light-space depth is not beyond the stored
    // occluder depth (+ bias to fight self-shadow acne). Off-map / beyond the far
    // plane reads as fully lit. Applied to the sun term ONLY — ambient/sky/point
    // lights still illuminate shadowed surfaces.
    // Cascade selection: the fragment's view-space forward distance (+Z) picks the
    // first cascade whose split far-distance covers it (crisp near, coverage far).
    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    int   cascade   = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i) {
        if (viewDepth <= camera.cascadeSplits[i]) { cascade = i; break; }
    }
    vec4  lp = camera.cascadeViewProj[cascade] * vec4(fragWorldPos, 1.0);
    vec3  sc = lp.xyz / lp.w;
    sc.xy    = sc.xy * 0.5 + 0.5;
    // Remap into this cascade's horizontal atlas slice [cascade/N, (cascade+1)/N].
    float invN     = 1.0 / float(NUM_CASCADES);
    vec2  atlasUV  = vec2((float(cascade) + sc.x) * invN, sc.y);
    float vis = 0.0;
    vec2  tx  = 1.0 / vec2(textureSize(shadowMap, 0));
    float kShadowBias = SP_SHADOW_BIAS;  // depth-compare bias (tunable; complements raster depth-bias)
#ifdef SWISH_DEBUG_UI
    // Lighting L3.1: slope-scaled bias. A surface the light only grazes (the road at golden hour) needs more
    // bias than one it faces, or it self-shadows in stripes. The bias is in normalised cascade depth, so it
    // already grows with the cascade's texel size.
    {
        float ndl = max(dot(N, camera.sunDir.xyz), 0.0);
        kShadowBias *= clamp(1.0 + 1.5 * sqrt(1.0 - ndl * ndl) / max(ndl, 0.15), 1.0, 8.0);
    }
#endif
    // Keep PCF taps inside this cascade's slice so they can't sample a neighbour.
    float uMin = float(cascade) * invN + tx.x;
    float uMax = float(cascade + 1) * invN - tx.x;
#ifdef SWISH_DEBUG_UI
    // ── PCSS: contact-hardening soft shadows (debug-tunable via SP_SHADOW_LIGHT_SIZE) ──
    // (1) Blocker search — average the depth of taps closer to the sun than us.
    float searchR    = SP_SHADOW_LIGHT_SIZE * tx.x * 4.0;  // atlas-UV search radius
    float blockerSum = 0.0;
    int   blockerCnt = 0;
    for (int i = 0; i < 16; ++i) {
        vec2 suv = atlasUV + POISSON16[i] * searchR;
        suv.x    = clamp(suv.x, uMin, uMax);
        float occ = texture(shadowMap, suv).r;
        if (occ < sc.z - kShadowBias) {  // occluder nearer the sun → blocker
            blockerSum += occ;
            blockerCnt++;
        }
    }
    if (blockerCnt == 0 || SP_SHADOW_LIGHT_SIZE <= 0.0) {
        vis = 1.0;  // nothing blocks (or hard mode) → fully lit
    } else {
        // (2) Penumbra estimate (similar triangles): bigger blocker→receiver gap = softer.
        float avgBlocker = blockerSum / float(blockerCnt);
        float penumbra   = (sc.z - avgBlocker) / avgBlocker * SP_SHADOW_LIGHT_SIZE;
        // Lighting L3.4: the physical penumbra of a distant disc light. The cascade is orthographic with a
        // depth range of 3 radii and texels of 2 radii / N, so the radius cancels:
        //   width in texels = (depth gap) * 3 * tan(angular radius) * N
        if (sp.lightFlags.w > 0.0)
            penumbra = (sc.z - avgBlocker) * 3.0 * tan(sp.lightFlags.w) * float(textureSize(shadowMap, 0).y);
        float radius     = max(penumbra * tx.x, tx.x);  // never below 1 texel
        // (3) Variable-radius PCF over the Poisson disk.
        for (int i = 0; i < 16; ++i) {
            vec2 suv = atlasUV + POISSON16[i] * radius;
            suv.x    = clamp(suv.x, uMin, uMax);
            float occ = texture(shadowMap, suv).r;
            vis += (sc.z - kShadowBias <= occ) ? 1.0 : 0.0;
        }
        vis /= 16.0;
    }
#else
    // Release path: fixed 3×3 PCF (byte-identical to before).
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 suv = atlasUV + vec2(x, y) * tx;
            suv.x    = clamp(suv.x, uMin, uMax);
            float occluder = texture(shadowMap, suv).r;
            vis += (sc.z - kShadowBias <= occluder) ? 1.0 : 0.0;
        }
    vis /= 9.0;
#endif
    if (sc.z > 1.0 || any(lessThan(sc.xy, vec2(0.0))) || any(greaterThan(sc.xy, vec2(1.0))))
        vis = 1.0;  // outside this cascade's frustum = lit
#ifdef SWISH_DEBUG_UI
    // Lighting L3.3: blend into the next cascade over the last part of this one.
    if (sp.lightFlags.x > 0.0 && cascade < NUM_CASCADES - 1) {
        float split = camera.cascadeSplits[cascade];
        float t     = (viewDepth - split * (1.0 - sp.lightFlags.x)) / max(split * sp.lightFlags.x, 1.0);
        if (t > 0.0) vis = mix(vis, cascadePcf3(cascade + 1, fragWorldPos, kShadowBias), clamp(t, 0.0, 1.0));
    }
#endif
    // Shadow floor 0.25: ambient still lights shadowed areas, so shadows read as
    // darkened rather than pitch black. Works in all weather (no clarity gate).
#ifdef SWISH_DEBUG_UI
    // C4: the floor stands in for sky light, so under a roof (a tunnel) it goes with the sky
    float sunShadow = mix(SP_SHADOW_FLOOR * skyVis, 1.0, min(vis, roofSun));
#else
    float sunShadow = mix(SP_SHADOW_FLOOR, 1.0, vis);
#endif

    // ── PBR setup ─────────────────────────────────────────────────
    float a  = roughness * roughness;
    float a2 = a * a;
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;

    // F0: metallic workflow (lerp between dielectric and albedo for metals).
    // Wet film raises dielectric reflectance for a stronger specular/Fresnel.
#ifdef SWISH_DEBUG_UI
    // Rain P2: the wet film's top interface is water-air, F0 = ((1.33-1)/(1.33+1))^2 = 0.02 (Lagarde's puddle F0).
    // The mirror look comes from Fresnel at grazing and low roughness, not from a raised F0.
    float dielectricF0 = mix(0.04, 0.02, wetLocal);
#else
    float dielectricF0 = mix(0.04, 0.06, wetLocal);
#endif
    vec3  F0 = mix(vec3(dielectricF0), albedo, metallic);

    // ── Directional sun ───────────────────────────────────────────
    vec3 L = camera.sunDir.xyz;
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // GGX distribution
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom + 0.0001);

    // Smith-Schlick-GGX geometry
    float G1_V = NdotV / (NdotV * (1.0 - k) + k);
    float G1_L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G1_V * G1_L;

    // Fresnel (Schlick) with metallic F0
    vec3 F_sun = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);

    // Cook-Torrance specular
    vec3 specular_sun = (D * G * F_sun) / max(4.0 * NdotV * NdotL, 0.001);
#ifdef SWISH_DEBUG_UI
    // Lighting L4.1: multiscatter energy compensation. A single-bounce GGX lobe loses the light that bounces
    // between microfacets, so rough metals come out too dark. dfg.x + dfg.y is the lobe's directional albedo
    // at F0 = 1 (the split-sum LUT); what is missing is put back, tinted by F0 (Kulla-Conty, as Filament).
    vec3 energyComp = vec3(1.0);
    if (sp.lightFlags.y > 0.5) {
        vec2 dfg   = texture(brdfLUT, vec2(NdotV, roughness)).rg;
        energyComp = 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 0.05) - 1.0);
    }
    specular_sun *= energyComp;
#endif

    // Energy conservation: diffuse reduces with Fresnel
    vec3 kD = (vec3(1.0) - F_sun) * (1.0 - metallic);

    // ── Diffuse IBL: cosine-weighted sky irradiance (G-P0-3) ──────────
    // Replaces the old fixed cool-tint hemisphere boost with the actual sky
    // irradiance in the surface's orientation, so ambient tracks the weather
    // (grey overcast vs blue clear) and reads directionally. `ambient` (sunColor.a)
    // stays the overall floor/scale; SP_IBL_DIFFUSE tunes the sky contribution.
    // Additive so dark interior panels can't crush to black; gated (1-metallic)
    // because metals have no diffuse (their env response is the specular term).
#ifdef SWISH_DEBUG_UI
    // Specular occlusion from the AO, sharper on glossy surfaces: Lagarde & de Rousiers 2014 ("Moving Frostbite
    // to PBR"), as Filament's SpecularAO_Lagarde; `a` is the GGX alpha.
    float ssao   = texture(gbAo, fragUV).r;
    float specAO = clamp(pow(NdotV + ssao, exp2(-16.0 * a - 1.0)) - 1.0 + ssao, 0.0, 1.0);
#endif
    float ambient      = camera.sunColor.a;
    // Sun illuminance (camera.sunDir.w, lux) times pre-exposure; the tint is sunColor.rgb (L0.6, L1.3).
    vec3  sun_radiance = camera.sunColor.rgb * sunScale;
    vec3  skyIrr     = texture(irradianceMap, N).rgb * (skyScale * skyVis);  // baked-sky diffuse irradiance (set 3)
#ifdef SWISH_DEBUG_UI
    // Rain P2c: the car's underbody hides sky from the road under and around it, which screen-space AO cannot see
    // (the occluder is not visible from the camera). Analytic sky visibility against the car's box (lights.carBox*):
    // 8 cosine-weighted directions (Malley: uniform disk points lifted to the hemisphere), so the mean blocked
    // fraction IS the lost fraction of sky irradiance, V_d = 1 - mean(block). Specular: one ray along the mirror
    // direction, V_s = 1 - block(R). Only near the car; the car's own surfaces (inside the box) are never blocked.
    float carVisD = 1.0, carVisS = 1.0;
    if (lights.carBoxHalf.w > 0.5 && distance(fragWorldPos, lights.carBoxCenter.xyz) < 6000.0) {
        vec3  T       = normalize(abs(N.y) < 0.99 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
        vec3  B       = cross(N, T);
        float blocked = 0.0;
        for (int i = 0; i < 8; ++i) {
            float r   = sqrt((float(i) + 0.5) / 8.0);
            float phi = float(i) * 2.3999632;  // golden angle: an even spread over the disk
            vec3  dir = T * (r * cos(phi)) + B * (r * sin(phi)) + N * sqrt(1.0 - r * r);
            blocked += carBoxBlock(fragWorldPos, fragWorldPos + dir * 5000.0);
        }
        carVisD = 1.0 - blocked / 8.0;
        // Where the mirror ray meets the car, the road mirrors the car body, not black: under overcast a light car
        // body is roughly 0.45 x the sky it replaces (paint albedo x sky + its own sky reflection). SSR refines this
        // wherever its ray resolves the body on screen (P2d).
        const float kCarSpecRadiance = 0.45;
        carVisS = 1.0 - (1.0 - kCarSpecRadiance) * carBoxBlock(fragWorldPos, fragWorldPos + reflect(-V, N) * 5000.0);
    }
    skyIrr *= carVisD;
#endif
    // Sun (direct) term is shadowed; ambient/sky fill is not (see sunShadow above).
    vec3 ambientIrr = (skyIrr * SP_IBL_DIFFUSE) ;
    vec3  sun_term  = (kD * albedo / PI + specular_sun) * NdotL * sun_radiance;
    vec3  F_env   = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3  kD_amb  = (vec3(1.0) - F_env) * (1.0 - metallic);

    // Clearcoat energy (L0.4): the coat reflects F_coat of the view-angle light, so every
    // base-lobe term below keeps only (1 - F_coat). Exactly 1.0 when clearcoatStrength is 0,
    // so non-car surfaces are unchanged.
    float coatF    = clearcoatStrength * (0.04 + 0.96 * pow(1.0 - NdotV, 5.0));
    float coatKeep = 1.0 - coatF;

    vec3  lit_color = (AO_DIFFUSE(albedo * ambient * ambientIrr * kD_amb)
                   + sun_term * sunShadow) * coatKeep;
#ifdef SWISH_DEBUG_UI
    // D4 ground bounce: the sky cube's lower half is a dimmed horizon, not the sunlit road, so every face turned
    // from the sun (bridge fascias and soffits, backlit steel) went black. The road's own rendered irradiance E_g,
    // reflected by a Lambertian ground of albedo rho, reaches a surface through the lower half-space's view factor:
    // E_b = rho * E_g * (1 - N.y) / 2.
    vec3 groundE = texture(irradianceMap, vec3(0.0, 1.0, 0.0)).rgb * (skyScale * skyVis) * SP_IBL_DIFFUSE * ambient
                 + sun_radiance * max(L.y, 0.0) * roofSun / PI;
    float groundF = clamp(0.5 - 0.5 * N.y, 0.0, 1.0);  // clamp also drops a NaN normal (car glass) instead of spreading it
    lit_color += AO_DIFFUSE(albedo * kD_amb * groundE * (sp.atmoFlags.w * groundF)) * coatKeep;
#endif

    // ── Specular IBL: GGX-prefiltered environment × split-sum BRDF LUT ──
    // The dominant "Blender gloss" cue: surfaces mirror the environment. The
    // reflection vector samples the prefiltered cubemap (baked from the procedural
    // sky) at a mip chosen by roughness — glossy paint → sharp mip 0, rough asphalt
    // → blurred high mip. The baked BRDF LUT supplies the split-sum scale+bias so it
    // stays energy-conserving (grazing rims brighten; matte cabin barely reflects).
    // Wetness dim is intentional: the sky background is dimmed by the same term
    // (sky shader, `wetness * 0.45`), so reflections match the sky the viewer sees.
    vec3  R          = reflect(-V, N);
    vec3  envColor   = textureLod(prefilteredMap, R, roughness * IBL_PREFILTER_MAX_MIP).rgb * (skyScale * skyVis);
    envColor         = mix(envColor, envColor * (1.0 - wetness * 0.45), wetness * 0.9);
#ifdef SWISH_DEBUG_UI
    envColor *= carVisS;  // rain P2c: the road under the car does not mirror the sky
#endif
    vec2  envBRDF    = texture(brdfLUT, vec2(NdotV, roughness)).rg;
#ifdef SWISH_DEBUG_UI
    // Rain P2d: keep the env-specular amount so SSR can SWAP it for the traced hit instead of adding on top.
    vec3 envSpecTerm = coatKeep * AO_SPEC(envColor * (F0 * envBRDF.x + envBRDF.y) * energyComp * SP_IBL_SPECULAR);
    lit_color       += envSpecTerm;
#else
    lit_color       += coatKeep * AO_SPEC(envColor * (F0 * envBRDF.x + envBRDF.y) * SP_IBL_SPECULAR);
#endif

    // ── Clearcoat: second GGX lobe, dielectric F0, tight/near-mirror roughness ──
    // Real automotive clearcoat: a thin, nearly-flat, near-mirror dielectric layer
    // over the base paint — most visible at grazing angles (Fresnel), where it
    // stays a sharp mirror highlight while the (broader, possibly-metallic) base
    // lobe has already gone diffuse-ish. The base lobes are attenuated by coatKeep
    // (L0.4); the coat's own lobes are added on top. Coat IBL uses the same split-sum
    // BRDF LUT as the base (L0.3). Strength is authored per-material (SceneTypes.h)
    // and defaults to 0 off the car, so this is a no-op everywhere else.
    if (clearcoatStrength > 0.0) {
        float ccRough = SP_CLEARCOAT_ROUGHNESS;
        vec3  ccSpec  = ggxSpecular(NdotH, NdotV, NdotL, HdotV, ccRough, vec3(0.04));
        lit_color    += ccSpec * NdotL * sun_radiance * sunShadow * clearcoatStrength;

        // Clearcoat IBL: same sharp mirror lobe sampling the environment directly
        // (R, computed above from the base N — the clearcoat's own micro-normal is
        // effectively flat, so it shares R with the base specular).
        vec3 ccEnv   = textureLod(prefilteredMap, R, ccRough * IBL_PREFILTER_MAX_MIP).rgb * (skyScale * skyVis);
        ccEnv        = mix(ccEnv, ccEnv * (1.0 - wetness * 0.45), wetness * 0.9);
        vec2 ccBRDF  = texture(brdfLUT, vec2(NdotV, ccRough)).rg;
        lit_color   += AO_SPEC(ccEnv * (vec3(0.04) * ccBRDF.x + ccBRDF.y) * clearcoatStrength * SP_IBL_SPECULAR);
    }

    // ── Flake: stochastic per-cell normal-perturbation sparkle ─────────────
    // Uses flakeN (decoded from gbClearcoatFlake, == N where no cell is active —
    // flakeMask 0 there) instead of the base N, so the glint only lights up where
    // a flake cell's random tilt happens to catch the sun — the "metal-flake
    // glitter" look, not a uniform sheen. Sun-only (no IBL/point-light term): the
    // sparkle read is dominated by the single strong directional highlight: a
    // diffuse env term would just look like uniform extra sheen, not a glint.
    if (flakeMask > 0.0) {
        float NfdotH = max(dot(flakeN, H), 0.0);
        float NfdotV = max(dot(flakeN, V), 0.001);
        float NfdotL = max(dot(flakeN, L), 0.0);
        vec3  fSpec  = ggxSpecular(NfdotH, NfdotV, NfdotL, HdotV, SP_FLAKE_ROUGHNESS, F0);
        lit_color   += coatKeep * fSpec * NfdotL * sun_radiance * sunShadow * flakeMask;
    }

    // ── Point light accumulation ──────────────────────────────────
    // Local lights (L1.4, L1.5) are in candela and lux. kHaloPerCd keeps the wet halo at its old strength
    // for the standard street lamp (5.0 old units = 1,273 cd). kMinLocalHdr skips a light whose pre-exposed
    // illuminance is below 1e-4 of the old unit scale.
    const float kHaloPerCd   = 5.0 / 1273.24;
    const float kMinLocalHdr = 1.0e-4;
    for (uint i = 0u; i < lights.numPointLights.x; ++i) {
        vec3  lPos = lights.pointLights[i].positionRadius.xyz;
        float lRad = lights.pointLights[i].positionRadius.w;
        vec3  lCol = lights.pointLights[i].colorIntensity.rgb;
        float lInt = lights.pointLights[i].colorIntensity.a;

        vec3  toLight = lPos - fragWorldPos;
        float dist    = length(toLight);
        vec3  L_pt    = toLight / max(dist, 0.001);

        // Physical falloff (L1.5). The light carries candela, so the illuminance at the surface is
        // I / d^2 with d in METRES (camera.weather.w = metres per world unit; never divide by a distance in WU,
        // that is a factor of a million). The Karis window ends the light at its radius so every lamp stays
        // local and the 32-light budget holds.
        float dM   = dist * camera.weather.w;
        float inv  = 1.0 / max(dM * dM, 1.0e-4);                   // 1 / max(d^2, (1 cm)^2)
        float win  = clamp(1.0 - pow(dist / lRad, 4.0), 0.0, 1.0);
        win        = win * win;
        float E_pt = lInt * inv * win;                             // lux at the surface (per unit NdotL)

        // Rainy glow: wet air halos bright lamps. An art effect, not a lux, so it keeps the old bounded,
        // dimensionless shape (dRef^2 / (dRef^2 + d^2), windowed) and its own reach. kHaloPerCd maps a lamp's
        // candela back onto the old 5.0 gain (a 16,000 lm sodium lamp is 1,273 cd). View-independent (no BRDF /
        // NdotL), scaled by wetness; the bloom pass spreads it into a wet halo.
        const float kHaloStrength = 0.12;  // small/tasteful, tune via run
        float dRef    = 0.3 * lRad;
        float haloAtt = win * (dRef * dRef) / (dRef * dRef + dist * dist);
        if (haloAtt > 0.001) lit_color += lCol * (lInt * kHaloPerCd) * haloAtt * haloAtt * wetLocal * kHaloStrength;

        // Nothing visible to add: skip the BRDF. The threshold is in pre-exposed HDR units.
        if (E_pt * camera.weather.z < kMinLocalHdr) continue;

        vec3  H_pt     = normalize(V + L_pt);
        float NdotL_pt = max(dot(N, L_pt), 0.0);
        float NdotH_pt = max(dot(N, H_pt), 0.0);
        float HdotV_pt = max(dot(H_pt, V), 0.0);

        // PBR per light
        float denom_pt = NdotH_pt * NdotH_pt * (a2 - 1.0) + 1.0;
        float D_pt     = a2 / (PI * denom_pt * denom_pt + 0.0001);
        float G1_V_pt  = NdotV / (NdotV * (1.0 - k) + k);
        float G1_L_pt  = NdotL_pt / (NdotL_pt * (1.0 - k) + k);
        float G_pt     = G1_V_pt * G1_L_pt;
        vec3  F_pt     = F0 + (1.0 - F0) * pow(1.0 - HdotV_pt, 5.0);
        vec3  spec_pt  = (D_pt * G_pt * F_pt) / max(4.0 * NdotV * NdotL_pt, 0.001);
#ifdef SWISH_DEBUG_UI
        spec_pt *= energyComp;
        // L10.2: on wet up-facing surfaces the lamp's reflection becomes the stretched streak.
        float streakMix = sp.roofCount.y * clamp(wetLocal * upFacing, 0.0, 1.0);
        if (streakMix > 0.0)
            spec_pt = mix(spec_pt, wetStreakSpec(N, V, L_pt, sp.roofCount.z, sp.roofCount.w, F0) * energyComp, streakMix);
#endif

        vec3 kD_pt = (vec3(1.0) - F_pt) * (1.0 - metallic);
        vec3 radiance = lCol * (E_pt * camera.weather.z);           // pre-exposed illuminance

        lit_color += coatKeep * (kD_pt * albedo / PI + spec_pt) * NdotL_pt * radiance;
    }

#ifdef SWISH_DEBUG_UI
    // ── Car spot lights (headlights / tail / brake; tasks/todo.md "Car lights") ──
    // Same physical falloff (candela, metres) and PBR terms as the point lights,
    // times a smoothstep cone. The wet halo is the street lamps' (att² · wetLocal ·
    // kHalo), masked by the cone, so beams bloom on a wet road. Debug-only until the
    // feature is turned on for release; zero lights (the default) adds nothing.
    for (uint i = 0u; i < lights.numSpotLights.x; ++i) {
        vec3  sPos   = lights.spotLights[i].positionRange.xyz;
        float sRange = lights.spotLights[i].positionRange.w;
        vec3  sDir   = lights.spotLights[i].directionCosOuter.xyz;
        float cosOut = lights.spotLights[i].directionCosOuter.w;
        float cosIn  = lights.spotLights[i].params.x;
        vec3  sCol   = lights.spotLights[i].colorIntensity.rgb;
        float sInt   = lights.spotLights[i].colorIntensity.a;

        vec3  toLight = sPos - fragWorldPos;
        float dist    = length(toLight);
        vec3  L_s     = toLight / max(dist, 0.001);

        float cone = smoothstep(cosOut, cosIn, dot(-L_s, sDir));
        // Lighting L6.4: low-beam cutoff. A headlight (params.y = 1) throws almost nothing above the height of
        // its own axis, so it lights the road and not the eyes of oncoming drivers.
        if (lights.spotLights[i].params.y > 0.5) cone *= mix(1.0, 0.06, smoothstep(-0.01, 0.03, (-L_s).y - sDir.y));
        float dM   = dist * camera.weather.w;                      // metres (see the point loop)
        float inv  = 1.0 / max(dM * dM, 1.0e-4);
        float win  = clamp(1.0 - pow(dist / sRange, 4.0), 0.0, 1.0);
        win        = win * win;
        float E_s  = sInt * inv * win * cone;                      // lux at the surface (per unit NdotL)
        float dRef    = 0.15 * sRange;
        float haloAtt = win * (dRef * dRef) / (dRef * dRef + dist * dist) * cone;
        if (E_s * camera.weather.z < kMinLocalHdr && haloAtt < 0.0005) continue;

        vec3  H_s     = normalize(V + L_s);
        float NdotL_s = max(dot(N, L_s), 0.0);
        float HdotV_s = max(dot(H_s, V), 0.0);
        vec3  spec_s  = ggxSpecular(max(dot(N, H_s), 0.0), NdotV, NdotL_s, HdotV_s, roughness, F0) * energyComp;
        {   // L10.2: head/tail/brake light reflections streak on the wet road too
            float streakMix = sp.roofCount.y * clamp(wetLocal * upFacing, 0.0, 1.0);
            if (streakMix > 0.0)
                spec_s = mix(spec_s, wetStreakSpec(N, V, L_s, sp.roofCount.z, sp.roofCount.w, F0) * energyComp, streakMix);
        }
        vec3  F_s     = F0 + (1.0 - F0) * pow(1.0 - HdotV_s, 5.0);
        vec3  kD_s    = (vec3(1.0) - F_s) * (1.0 - metallic);

        const float kSpotHalo = 0.12;  // == the street lamps' kHaloStrength
        lit_color += sCol * (sInt * kHaloPerCd) * haloAtt * haloAtt * wetLocal * kSpotHalo;
        lit_color += coatKeep * (kD_s * albedo / PI + spec_s) * NdotL_s * sCol * (E_s * camera.weather.z);
        // Lighting L8.2: retroreflection. Glass beads in road paint send light back toward its source, so a
        // marking is brightest when the eye sits next to the lamp (V close to L): your own headlights.
        lit_color += emissiveAlbedo * sCol * (E_s * camera.weather.z) *
                     (gbRetro * sp.lightFlags.z * pow(max(dot(V, L_s), 0.0), 64.0) / PI);
    }

    // ── Rectangle area lights (car showcase) ──────────────────────────
    // A glowing panel of luminance L. Diffuse: illuminance = L x solid angle x mean cosine (centre + corners).
    // Specular: the "representative point", the point of the panel nearest the mirror direction, with the
    // lobe widened by the panel's angular size so a mirror-smooth surface reflects about F x L: the panel
    // appears in the paint as a rectangle. No shadow map; the car's box gives the grounding shadow.
    for (uint i = 0u; i < lights.numRectLights.x; ++i) {
        vec3  rc    = lights.rectLights[i].centerRange.xyz;
        float range = lights.rectLights[i].centerRange.w;
        vec3  rdir  = lights.rectLights[i].rightHalfW.xyz, udir = lights.rectLights[i].upHalfH.xyz;
        float hw    = lights.rectLights[i].rightHalfW.w, hh = lights.rectLights[i].upHalfH.w;
        vec3  ln    = normalize(cross(rdir, udir));
        vec3  toC   = rc - fragWorldPos;
        float dist  = length(toC);
        if (dist > range || dot(ln, -toC) <= 0.0) continue;  // out of reach, or behind the panel

        vec3  rx = rdir * hw, ry = udir * hh;
        vec3  p0 = rc - rx - ry, p1 = rc + rx - ry, p2 = rc + rx + ry, p3 = rc - rx + ry;
        float sa = rectSolidAngle(fragWorldPos, p0, p1, p2, p3);
        if (sa <= 1.0e-6) continue;
        float win = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        win *= win;

        float cosSum = max(dot(N, toC / dist), 0.0) + max(dot(N, normalize(p0 - fragWorldPos)), 0.0) +
                       max(dot(N, normalize(p1 - fragWorldPos)), 0.0) + max(dot(N, normalize(p2 - fragWorldPos)), 0.0) +
                       max(dot(N, normalize(p3 - fragWorldPos)), 0.0);
        float E_r = sa * 0.2 * cosSum;  // illuminance per unit luminance

        float vis = 1.0;
        if (lights.carBoxHalf.w > 0.5) {
            float block = carBoxBlock(fragWorldPos, rc) + carBoxBlock(fragWorldPos, rc - 0.6 * rx - 0.6 * ry) +
                          carBoxBlock(fragWorldPos, rc + 0.6 * rx - 0.6 * ry) + carBoxBlock(fragWorldPos, rc + 0.6 * rx + 0.6 * ry) +
                          carBoxBlock(fragWorldPos, rc - 0.6 * rx + 0.6 * ry);
            vis = 1.0 - 0.2 * block;
        }
        vec3 Lr = lights.rectLights[i].colorNits.rgb * (lights.rectLights[i].colorNits.a * camera.weather.z * win * vis);

        // Representative point: where the mirror ray meets the panel's plane, clamped to the panel.
        vec3  Rr  = reflect(-V, N);
        float den = dot(Rr, ln);
        vec3  q   = (den < -1.0e-4) ? fragWorldPos + Rr * (dot(toC, ln) / den) - rc : (fragWorldPos + Rr * dist) - rc;
        vec3  rep = rc + rdir * clamp(dot(q, rdir), -hw, hw) + udir * clamp(dot(q, udir), -hh, hh);
        vec3  L_r = normalize(rep - fragWorldPos);
        float NdotL_r = max(dot(N, L_r), 0.0);
        float widen   = 0.5 * sqrt(sa / PI);  // the panel's angular radius, halved: makes a mirror return F x L
        vec3  F_r     = F0 + (1.0 - F0) * pow(1.0 - max(dot(normalize(V + L_r), V), 0.0), 5.0);
        vec3  kD_r    = (vec3(1.0) - F_r) * (1.0 - metallic);
        vec3  spec_r  = ggxSpecularAlpha(N, V, L_r, clamp(a + widen, 0.0, 1.0), F0) * energyComp;
        lit_color += coatKeep * (kD_r * albedo / PI * E_r + spec_r * (NdotL_r * sa)) * Lr;
        if (clearcoatStrength > 0.0) {
            float ccA = SP_CLEARCOAT_ROUGHNESS * SP_CLEARCOAT_ROUGHNESS;
            lit_color += ggxSpecularAlpha(N, V, L_r, clamp(ccA + widen, 0.0, 1.0), vec3(0.04)) * (NdotL_r * sa) * Lr * clearcoatStrength;
        }
    }
#endif

    // ── Wet grazing Fresnel surge (P0 #9) ─────────────────────────────
    // At grazing view angles a wet surface reflects the environment with F→1 —
    // the surge that makes a wet night road mirror the sky and lamps. Reflect the
    // sky irradiance (skyIrr, computed above); the point-light loop supplies the
    // sharp lamp streaks (now that wet roughness is near-zero).
    float grazing  = pow(1.0 - NdotV, 5.0);
    vec3  Fgraze   = F0 + (1.0 - F0) * grazing;             // Schlick, → 1 at grazing
#ifdef SWISH_DEBUG_UI
    // Lighting L4.2: the sheen is bounded by Fresnel. It adds only the reflection the base lobe did not
    // already take (Fgraze minus the split-sum reflectance), of the mirror environment, so base + sheen can
    // never reflect more than arrives. The old form added sky irradiance on top, which over-lit the far road.
    vec3  wetSheen = max(Fgraze - (F0 * envBRDF.x + envBRDF.y), vec3(0.0)) * envColor * wetLocal;
#else
    vec3  wetSheen = Fgraze * skyIrr * ambient * wetLocal;  // reflect sky, gated by exposure·wetness
#endif
    lit_color += coatKeep * AO_SPEC(wetSheen);
#ifdef SWISH_DEBUG_UI
    envSpecTerm += coatKeep * AO_SPEC(wetSheen);  // the sheen is env reflection too
#endif
#ifdef SWISH_DEBUG_UI
    // Lighting L5: emissive surfaces (lamp lenses, street-lamp heads). The strength is a luminance, already
    // pre-exposed; the colour is the surface's own, so a red lens glows red. Added before the haze and fog.
    lit_color += emissiveAlbedo * gbEmissive;
#endif

    // ── Depth-resolved rain fog (R-P1-1, Koschmieder) ─────────────────
    // Airlight accumulates with distance: L = L·e^(−βd) + L_air·(1−e^(−βd)).
    // Near surfaces (the cabin!) stay crisp; only distance hazes out — unlike the
    // old flat screen-wide blend that washed the whole frame. Gated by wetness so
    // it fades in with rain. β expressed via a "63%-fog distance" for readability.
    // Gate by (1 - clarity) so a clear day has ZERO fog instantly (not waiting for the
    // wetness to drain); cap by kFogMax so even the far end of the 4.2 km road stays
    // legible instead of dissolving into grey.
    vec3  toFrag  = fragWorldPos - camera.camPos.xyz;
    float fogDist = length(toFrag);                              // WU (1 m = 1000 WU)

    // Always-on aerial perspective: distant surfaces dissolve into the env sky in
    // their own view direction (same cubemap as IBL / background), so haze matches
    // reflections. Near surfaces (cabin ~1000 WU) are ~0.
    float hazeT = (1.0 - exp(-fogDist / SP_HAZE_DIST)) * SP_HAZE_MAX;
#ifdef SWISH_DEBUG_UI
    hazeT *= skyVis;  // C4: no sunlit air scatters into a roof volume (a tunnel tube)
#endif
    lit_color   = mix(lit_color, textureLod(prefilteredMap, normalize(toFrag), 0.0).rgb * SP_IBL_SPECULAR * skyScale, hazeT);

    // Rain fog (wet-gated) layered on top toward the cool overcast airlight colour.
    float fogClear = 1.0 - camera.weather.x;                     // 1 overcast/rain .. 0 clear day
    float fogT     = (1.0 - exp(-fogDist * wetness / SP_FOG_DIST63)) * SP_FOG_MAX * fogClear;
#ifdef SWISH_DEBUG_UI
    // Lighting L10: rain fog is airlight, so with the procedural sky it takes the sky's own colour and level
    // near the horizon (a blurred mip of the baked cube). The fixed grey stays for the HDRI path.
    // Rain round 3: the rain HDRI is a real overcast sky, so the airlight takes the cube's horizon colour on that
    // path too (the fixed grey matched neither sky).
    vec3 fogColor = textureLod(prefilteredMap, normalize(vec3(toFrag.x, 0.08 * length(toFrag.xz), toFrag.z)), 3.0).rgb
                  * SP_IBL_SPECULAR * skyScale;
    fogT *= skyVis;  // C4: likewise the rain fog
    lit_color      = mix(lit_color, fogColor, fogT);
#else
    lit_color      = mix(lit_color, SP_FOG_COLOR, fogT);
#endif

#ifdef SWISH_DEBUG_UI
    // Rain P2d: alpha = luminance of the env reflection still in lit_color (after haze and fog attenuate it), read
    // by ssr.frag right after this pass. Nothing else reads HDR alpha (every consumer samples .rgb).
    float envSpecLum = dot(envSpecTerm, vec3(0.2126, 0.7152, 0.0722)) * (1.0 - hazeT) * (1.0 - fogT);
    outColor = vec4(lit_color, envSpecLum);
#else
    outColor = vec4(lit_color, 1.0);
#endif
}
