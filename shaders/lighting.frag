#version 450

// Deferred lighting pass — reads G-buffer, computes PBR lighting in screen space.
// Outputs HDR color (values can exceed 1.0 for bloom).

layout(location = 0) in vec2 fragUV;

// Sun shadow cascades (CSM). MUST match swish::NUM_CASCADES (SceneTypes.h) and
// kNumCascades (PostProcessManager). The shadow map is a horizontal atlas of
// NUM_CASCADES square slices side by side.
#define NUM_CASCADES 3

// ── Camera + Lights (same UBOs as scene pass, set 0) ──────────────
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 weather;   // x = clarity (0 overcast .. 1 clear day), yzw reserved
    // CSM tail (appended AFTER weather; only this shader reads it):
    mat4 cascadeViewProj[NUM_CASCADES];  // world → cascade [0,1] clip
    vec4 cascadeSplits;                  // xyz = view-space far distance of cascades 0/1/2
} camera;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 0, binding = 1) uniform LightsUBO {
    PointLight pointLights[32];
    uvec4      numPointLights;
} lights;

// ── G-Buffer textures (set 1) ────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D gbAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gbNormal;
layout(set = 1, binding = 2) uniform sampler2D gbMaterial;
layout(set = 1, binding = 3) uniform sampler2D gbDepth;

// ── Sun shadow map (set 2) — single non-cascaded. PLAIN sampler2D + manual
//    compare: MoltenVK's portability subset has no comparison samplers. ──
layout(set = 2, binding = 0) uniform sampler2D shadowMap;

// ── Push constants: inverse matrices for position reconstruction ──
layout(push_constant) uniform LightingPC {
    mat4 invView;
    mat4 invProj;
} pc;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ── Scene "look" constants ────────────────────────────────────────────
// These 13 values used to be hardcoded here. Under the debug UI they are
// promoted to a live uniform (set 3) so the in-engine panel can drive the
// look in real time; in a normal `make run` build the shader compiles
// without SWISH_DEBUG_UI and keeps the exact literals below, so release
// output is byte-for-byte unchanged (no set 3, no extra binding). The rest
// of the shader reads them through the SP_* macros regardless of build.
#ifdef SWISH_DEBUG_UI
layout(set = 3, binding = 0) uniform SceneParamsUBO {
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
} sp;
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
#define SP_WET_POROSITY         sp.wetParams.x
#define SP_WET_ROUGHNESS        sp.wetParams.y
#define SP_IBL_DIFFUSE          sp.iblParams.x
#define SP_IBL_SPECULAR         sp.iblParams.y
#else
// Release literals — identical to the previously-hardcoded values.
#define SP_SKY_HORIZON_OVERCAST vec3(0.70, 0.80, 0.90)
#define SP_SKY_HORIZON_CLEAR    vec3(0.62, 0.80, 0.98)
#define SP_SKY_ZENITH_OVERCAST  vec3(0.35, 0.55, 0.85)
#define SP_SKY_ZENITH_CLEAR     vec3(0.09, 0.36, 0.86)
#define SP_SUN_DISC_EXP_MIN     32.0
#define SP_SUN_DISC_EXP_MAX     220.0
#define SP_SUN_DISC_STR_MIN     0.3
#define SP_SUN_DISC_STR_MAX     0.9
// Depth-resolved rain fog (R-P1-1). fog colour = cool overcast airlight; dist63
// = distance (WU; 1 m = 1000 WU) at which fog reaches ~63% at full wetness. Large
// enough that the near cabin (~1000 WU away) is essentially fog-free.
#define SP_FOG_COLOR            vec3(0.52, 0.57, 0.63)
#define SP_FOG_DIST63           1200000.0  // ~1.2 km to 63% at full rain
#define SP_FOG_MAX              0.65        // cap: distant geometry keeps ≥35% of its colour
#define SP_ENV_GLOSS_EXP        3.0
#define SP_SHADOW_BIAS          0.0018
#define SP_SHADOW_FLOOR         0.25
#define SP_WET_POROSITY         0.35
#define SP_WET_ROUGHNESS        0.12
#define SP_IBL_DIFFUSE          1.0
#define SP_IBL_SPECULAR         1.0
#endif

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

// ── Sky gradient (same as forward pass) ───────────────────────────
// clarity (camera.weather.x): 0 = overcast hazy blue, 1 = clear-day deep azure with
// a sharper, brighter sun disc. The overcast greying is applied separately by wetness.
vec3 compute_sky_color(vec3 view_dir) {
    float t       = clamp(view_dir.y * 2.0 + 0.3, 0.0, 1.0);
    float clarity = camera.weather.x;
    vec3 horizon  = mix(SP_SKY_HORIZON_OVERCAST, SP_SKY_HORIZON_CLEAR, clarity);
    vec3 zenith   = mix(SP_SKY_ZENITH_OVERCAST, SP_SKY_ZENITH_CLEAR, clarity);
    vec3 sky = mix(horizon, zenith, pow(t, 1.5));
    float sun_dot = max(dot(view_dir, camera.sunDir.xyz), 0.0);
    sky += camera.sunColor.rgb * pow(sun_dot, mix(SP_SUN_DISC_EXP_MIN, SP_SUN_DISC_EXP_MAX, clarity))
         * mix(SP_SUN_DISC_STR_MIN, SP_SUN_DISC_STR_MAX, clarity);
    return sky;
}

// ── IBL from the procedural sky (split-sum, no cubemap) ───────────
// Diffuse irradiance: an orientation-dependent integral of the sky hemisphere.
// The sky is low-frequency, so a zenith/horizon/ground blend along N approximates
// the cosine-weighted integral well and stays coloured with the current weather
// (overcast grey vs clear blue) — a real upgrade over the old fixed skyTint term.
vec3 skyIrradiance(vec3 n) {
    vec3  zenith  = compute_sky_color(vec3(0.0, 1.0, 0.0));
    vec3  horizon = compute_sky_color(normalize(vec3(n.x, 0.15, n.z)));
    vec3  ground  = 0.25 * horizon;                 // dim tinted ground bounce
    vec3  base    = mix(horizon, zenith, clamp(n.y, 0.0, 1.0));  // side → up
    return mix(base, ground, clamp(-n.y, 0.0, 1.0));            // → ground when down-facing
}

// Analytic environment-BRDF (Karis "Mobile") — the split-sum scale+bias that
// weights the prefiltered environment, replacing an ad-hoc gloss falloff with an
// energy-conserving, roughness-aware Fresnel term (no precomputed LUT needed).
vec2 envBRDFApprox(float NoV, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    vec4  r    = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

void main() {
    // ── Read G-Buffer ─────────────────────────────────────────────
    vec3  albedo    = texture(gbAlbedo, fragUV).rgb;
    vec3  rawNormal = texture(gbNormal, fragUV).rgb;
    vec4  material  = texture(gbMaterial, fragUV);
    float depth     = texture(gbDepth, fragUV).r;

    float wetness = camera.camPos.w;  // packed by CameraUniforms::set_wetness()

    // Sky pixels (depth = 1.0, no geometry)
    if (depth > 0.9999) {
        // Any valid depth along the pixel's view ray yields the same direction,
        // so the 0.5 sample is arbitrary (and in-range under [0,1] clip-Z).
        vec3 viewDir = normalize(reconstructWorldPos(fragUV, 0.5) - camera.camPos.xyz);
        vec3 sky = compute_sky_color(viewDir);
        // Overcast: grey out the sky as wetness increases
        vec3 overcast = sky * (1.0 - wetness * 0.45);
        outColor = vec4(mix(sky, overcast, wetness * 0.9), 1.0);
        return;
    }

    // Decode normal: [0,1] → [-1,1]
    vec3  N         = normalize(rawNormal * 2.0 - 1.0);
    float metallic  = material.r;
    float roughness = material.g;
    float wettable  = material.b;  // 1 = rain-exposed (road/body), 0 = enclosed cabin

    // Wet-weather effects apply only to rain-exposed surfaces. The enclosed car
    // cabin stays dry (wettable = 0 → wetLocal = 0), so rain never washes it out.
    float wetLocal = wetness * wettable;

    // ── Wet-surface model (P0 #9) ─────────────────────────────────────
    // As water accumulates (wetLocal ∝ rain rate R, gated by exposure): diffuse
    // darkens toward a porosity floor (asphalt soaks up light), roughness collapses
    // toward a mirror, and micro-normals on up-facing (road-like) surfaces flatten
    // toward the plane so reflections stay coherent. The grazing Fresnel surge that
    // actually reads as "wet" is added after lighting, below.
    float kPorosity = SP_WET_POROSITY;  // asphalt diffuse floor when saturated
    float luma      = dot(albedo, vec3(0.299, 0.587, 0.114));
    vec3  wetAlbedo = mix(albedo * kPorosity, vec3(luma * kPorosity), 0.25);  // darken + desaturate
    albedo          = mix(albedo, wetAlbedo, wetLocal);

    roughness = mix(roughness, roughness * SP_WET_ROUGHNESS, wetLocal);  // toward near-mirror, scaled by water
    roughness = clamp(roughness, 0.02, 1.0);

    // Flatten micro-normal toward the geometric plane on up-facing surfaces.
    float upFacing = clamp(N.y, 0.0, 1.0);
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetLocal * upFacing * 0.6));

    // Reconstruct world position from depth
    vec3 fragWorldPos = reconstructWorldPos(fragUV, depth);
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
    // Keep 3×3 PCF taps inside this cascade's slice so they can't sample a neighbour.
    float uMin = float(cascade) * invN + tx.x;
    float uMax = float(cascade + 1) * invN - tx.x;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 suv = atlasUV + vec2(x, y) * tx;
            suv.x    = clamp(suv.x, uMin, uMax);
            float occluder = texture(shadowMap, suv).r;
            vis += (sc.z - kShadowBias <= occluder) ? 1.0 : 0.0;
        }
    vis /= 9.0;
    if (sc.z > 1.0 || any(lessThan(sc.xy, vec2(0.0))) || any(greaterThan(sc.xy, vec2(1.0))))
        vis = 1.0;  // outside this cascade's frustum = lit
    // Shadow floor 0.25: ambient still lights shadowed areas, so shadows read as
    // darkened rather than pitch black. Works in all weather (no clarity gate).
    float sunShadow = mix(SP_SHADOW_FLOOR, 1.0, vis);

    // ── PBR setup ─────────────────────────────────────────────────
    float a  = roughness * roughness;
    float a2 = a * a;
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;

    // F0: metallic workflow (lerp between dielectric and albedo for metals).
    // Wet film raises dielectric reflectance for a stronger specular/Fresnel.
    float dielectricF0 = mix(0.04, 0.06, wetLocal);
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

    // Energy conservation: diffuse reduces with Fresnel
    vec3 kD = (vec3(1.0) - F_sun) * (1.0 - metallic);

    // ── Diffuse IBL: cosine-weighted sky irradiance (G-P0-3) ──────────
    // Replaces the old fixed cool-tint hemisphere boost with the actual sky
    // irradiance in the surface's orientation, so ambient tracks the weather
    // (grey overcast vs blue clear) and reads directionally. `ambient` (sunColor.a)
    // stays the overall floor/scale; SP_IBL_DIFFUSE tunes the sky contribution.
    // Additive so dark interior panels can't crush to black; gated (1-metallic)
    // because metals have no diffuse (their env response is the specular term).
    float ambient      = camera.sunColor.a;
    vec3  sun_radiance = camera.sunColor.rgb;
    vec3  skyIrr       = skyIrradiance(N);
    vec3  ambientIrr   = sun_radiance + skyIrr * SP_IBL_DIFFUSE;
    // Sun (direct) term is shadowed; ambient/sky fill is not (see sunShadow above).
    vec3  sun_term  = (kD * albedo / PI + specular_sun) * NdotL * sun_radiance;
    vec3  lit_color = albedo * ambient * ambientIrr * (1.0 - metallic * 0.5)
                   + sun_term * sunShadow;

    // ── Specular IBL: prefiltered sky reflection × split-sum env BRDF ──
    // The dominant "Blender gloss" cue: surfaces mirror the environment. With no
    // cubemap, reflect the procedural sky and approximate a roughness prefilter by
    // blurring toward the low-frequency sky irradiance as roughness rises (glossy
    // paint → sharp sky, rough asphalt → soft ambient). The Karis env-BRDF then
    // weights it energy-conservingly (grazing rims brighten; matte cabin barely
    // reflects, so the interior isn't frosted). Reuses the overcast greying.
    vec3  R          = reflect(-V, N);
    vec3  envSharp   = compute_sky_color(R);
    vec3  envColor   = mix(envSharp, skyIrr, roughness);           // cheap prefilter
    envColor         = mix(envColor, envColor * (1.0 - wetness * 0.45), wetness * 0.9);
    vec2  envBRDF    = envBRDFApprox(NdotV, roughness);
    lit_color       += envColor * (F0 * envBRDF.x + envBRDF.y) * SP_IBL_SPECULAR;

    // ── Point light accumulation ──────────────────────────────────
    for (uint i = 0u; i < lights.numPointLights.x; ++i) {
        vec3  lPos = lights.pointLights[i].positionRadius.xyz;
        float lRad = lights.pointLights[i].positionRadius.w;
        vec3  lCol = lights.pointLights[i].colorIntensity.rgb;
        float lInt = lights.pointLights[i].colorIntensity.a;

        vec3  toLight = lPos - fragWorldPos;
        float dist    = length(toLight);
        vec3  L_pt    = toLight / max(dist, 0.001);

        // Windowed inverse-square falloff (G-P1-1). The old model was a windowed
        // *linear* ramp; real irradiance falls as 1/d². Use the bounded inverse-
        // square dRef²/(dRef²+d²) — 1 at the source, →0 with distance, no
        // singularity — windowed (Karis) to reach exactly 0 at the lamp radius so
        // each lamp stays local and the 32-light budget holds. dRef = 0.3·radius
        // preserves the previously-tuned mid-range brightness while tightening the
        // near-lamp pool; att stays in [0,1], so lamp intensity, the wet halo
        // (att²), and bloom are all unchanged.
        float dRef  = 0.3 * lRad;
        float win   = clamp(1.0 - pow(dist / lRad, 4.0), 0.0, 1.0);
        win         = win * win;
        float att   = win * (dRef * dRef) / (dRef * dRef + dist * dist);
        if (att < 0.001) continue;

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

        vec3 kD_pt = (vec3(1.0) - F_pt) * (1.0 - metallic);
        vec3 radiance = lCol * lInt * att;

        // Rainy glow: wet air halos bright lamps. View-independent (no BRDF / NdotL),
        // scaled by wetness; att*att concentrates the bloom nearer the lamp and reuses
        // the existing radius falloff. The bloom pass then spreads it into a wet halo.
        const float kHaloStrength = 0.12;  // small/tasteful — tune via run
        lit_color += lCol * lInt * att * att * wetLocal * kHaloStrength;

        lit_color += (kD_pt * albedo / PI + spec_pt) * NdotL_pt * radiance;
    }

    // ── Wet grazing Fresnel surge (P0 #9) ─────────────────────────────
    // At grazing view angles a wet surface reflects the environment with F→1 —
    // the surge that makes a wet night road mirror the sky and lamps. Reflect the
    // sky irradiance (skyIrr, computed above); the point-light loop supplies the
    // sharp lamp streaks (now that wet roughness is near-zero).
    float grazing  = pow(1.0 - NdotV, 5.0);
    vec3  Fgraze   = F0 + (1.0 - F0) * grazing;             // Schlick, → 1 at grazing
    vec3  wetSheen = Fgraze * skyIrr * ambient * wetLocal;  // reflect sky, gated by exposure·wetness
    lit_color += wetSheen;

    // ── Depth-resolved rain fog (R-P1-1, Koschmieder) ─────────────────
    // Airlight accumulates with distance: L = L·e^(−βd) + L_air·(1−e^(−βd)).
    // Near surfaces (the cabin!) stay crisp; only distance hazes out — unlike the
    // old flat screen-wide blend that washed the whole frame. Gated by wetness so
    // it fades in with rain. β expressed via a "63%-fog distance" for readability.
    // Gate by (1 - clarity) so a clear day has ZERO fog instantly (not waiting for the
    // wetness to drain); cap by kFogMax so even the far end of the 4.2 km road stays
    // legible instead of dissolving into grey.
    float fogDist  = length(fragWorldPos - camera.camPos.xyz);   // WU (1 m = 1000 WU)
    float fogClear = 1.0 - camera.weather.x;                      // 1 overcast/rain .. 0 clear day
    float fogT     = (1.0 - exp(-fogDist * wetness / SP_FOG_DIST63)) * SP_FOG_MAX * fogClear;
    lit_color      = mix(lit_color, SP_FOG_COLOR, fogT);

    outColor = vec4(lit_color, 1.0);
}
