#version 450

// Deferred lighting pass — reads G-buffer, computes PBR lighting in screen space.
// Outputs HDR color (values can exceed 1.0 for bloom).

layout(location = 0) in vec2 fragUV;

// ── Camera + Lights (same UBOs as scene pass, set 0) ──────────────
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 weather;   // x = clarity (0 overcast .. 1 clear day), yzw reserved
    mat4 lightViewProj;  // sun ortho * lookAt for shadow lookup (appended AFTER weather)
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

// ── Push constants: inverse matrices for position reconstruction ──
layout(push_constant) uniform LightingPC {
    mat4 invView;
    mat4 invProj;
} pc;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// Depth-resolved rain fog (R-P1-1). kFogColor = cool overcast airlight; kFogDist63
// = distance (WU; 1 m = 1000 WU) at which fog reaches ~63% at full wetness. Large
// enough that the near cabin (~1000 WU away) is essentially fog-free.
const vec3  kFogColor   = vec3(0.52, 0.57, 0.63);
const float kFogDist63  = 1200000.0;  // ~1.2 km to 63% at full rain (was 150 m — ~8× too dense)
const float kFogMax     = 0.65;       // cap: distant geometry keeps ≥35% of its colour (no white-out)

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
    vec3 horizon  = mix(vec3(0.70, 0.80, 0.90), vec3(0.62, 0.80, 0.98), clarity);
    vec3 zenith   = mix(vec3(0.35, 0.55, 0.85), vec3(0.09, 0.36, 0.86), clarity);
    vec3 sky = mix(horizon, zenith, pow(t, 1.5));
    float sun_dot = max(dot(view_dir, camera.sunDir.xyz), 0.0);
    sky += camera.sunColor.rgb * pow(sun_dot, mix(32.0, 220.0, clarity)) * mix(0.3, 0.9, clarity);
    return sky;
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
    const float kPorosity = 0.35;  // asphalt diffuse floor when saturated
    float luma      = dot(albedo, vec3(0.299, 0.587, 0.114));
    vec3  wetAlbedo = mix(albedo * kPorosity, vec3(luma * kPorosity), 0.25);  // darken + desaturate
    albedo          = mix(albedo, wetAlbedo, wetLocal);

    roughness = mix(roughness, roughness * 0.12, wetLocal);  // toward near-mirror, scaled by water
    roughness = clamp(roughness, 0.02, 1.0);

    // Flatten micro-normal toward the geometric plane on up-facing surfaces.
    float upFacing = clamp(N.y, 0.0, 1.0);
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetLocal * upFacing * 0.6));

    // Reconstruct world position from depth
    vec3 fragWorldPos = reconstructWorldPos(fragUV, depth);
    vec3 V = normalize(camera.camPos.xyz - fragWorldPos);
    float NdotV = max(dot(N, V), 0.001);

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

    // ── Ambient: flat fill + hemispheric sky boost (P0 #3) ────────────
    // The flat term is kept as a floor; a cool sky term is *added* on up-facing
    // surfaces for directionality. Additive-only so it never darkens a surface
    // below the previous fill (dark interior panels can't crush to black).
    float ambient      = camera.sunColor.a;
    vec3  sun_radiance = camera.sunColor.rgb;
    float skyFacing    = max(N.y, 0.0);              // 0 (down/side) .. 1 (up)
    const vec3 skyTint = vec3(0.55, 0.70, 1.00);     // cool overcast sky
    vec3  ambientIrr   = sun_radiance + skyFacing * skyTint * 0.5;
    vec3  lit_color = albedo * ambient * ambientIrr
                   + (kD * albedo / PI + specular_sun) * NdotL * sun_radiance;

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
    // the surge that makes a wet night road mirror the sky and lamps. With no
    // IBL/SSR yet (P1), reflect the cool sky ambient; the point-light loop above
    // supplies the sharp lamp streaks (now that wet roughness is near-zero).
    float grazing  = pow(1.0 - NdotV, 5.0);
    vec3  Fgraze   = F0 + (1.0 - F0) * grazing;              // Schlick, → 1 at grazing
    vec3  wetSheen = Fgraze * skyTint * ambient * wetLocal;  // reflect sky, gated by exposure·wetness
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
    float fogT     = (1.0 - exp(-fogDist * wetness / kFogDist63)) * kFogMax * fogClear;
    lit_color      = mix(lit_color, kFogColor, fogT);

    outColor = vec4(lit_color, 1.0);
}
