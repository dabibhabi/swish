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
vec3 compute_sky_color(vec3 view_dir) {
    float t = clamp(view_dir.y * 2.0 + 0.3, 0.0, 1.0);
    vec3 horizon = vec3(0.70, 0.80, 0.90);
    vec3 zenith  = vec3(0.35, 0.55, 0.85);
    vec3 sky = mix(horizon, zenith, pow(t, 1.5));
    float sun_dot = max(dot(view_dir, camera.sunDir.xyz), 0.0);
    sky += camera.sunColor.rgb * pow(sun_dot, 32.0) * 0.3;
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

    // ── Wet-surface model (P0 #9) ─────────────────────────────────────
    // As water accumulates (wetness ∝ rain rate R): diffuse darkens toward a
    // porosity floor (asphalt soaks up light), roughness collapses toward a
    // mirror, and micro-normals on up-facing (road-like) surfaces flatten toward
    // the plane so reflections stay coherent. The grazing Fresnel surge that
    // actually reads as "wet" is added after lighting, below.
    const float kPorosity = 0.35;  // asphalt diffuse floor when saturated
    float luma      = dot(albedo, vec3(0.299, 0.587, 0.114));
    vec3  wetAlbedo = mix(albedo * kPorosity, vec3(luma * kPorosity), 0.25);  // darken + desaturate
    albedo          = mix(albedo, wetAlbedo, wetness);

    roughness = mix(roughness, roughness * 0.12, wetness);  // toward near-mirror, scaled by water
    roughness = clamp(roughness, 0.02, 1.0);

    // Flatten micro-normal toward the geometric plane on up-facing surfaces.
    float upFacing = clamp(N.y, 0.0, 1.0);
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetness * upFacing * 0.6));

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
    float dielectricF0 = mix(0.04, 0.06, wetness);
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

        float t_att = clamp(1.0 - dist / lRad, 0.0, 1.0);
        float att   = t_att * t_att;
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
        lit_color += lCol * lInt * att * att * wetness * kHaloStrength;

        lit_color += (kD_pt * albedo / PI + spec_pt) * NdotL_pt * radiance;
    }

    // ── Wet grazing Fresnel surge (P0 #9) ─────────────────────────────
    // At grazing view angles a wet surface reflects the environment with F→1 —
    // the surge that makes a wet night road mirror the sky and lamps. With no
    // IBL/SSR yet (P1), reflect the cool sky ambient; the point-light loop above
    // supplies the sharp lamp streaks (now that wet roughness is near-zero).
    float grazing  = pow(1.0 - NdotV, 5.0);
    vec3  Fgraze   = F0 + (1.0 - F0) * grazing;             // Schlick, → 1 at grazing
    vec3  wetSheen = Fgraze * skyTint * ambient * wetness;  // reflect sky, gated by wetness
    lit_color += wetSheen;

    outColor = vec4(lit_color, 1.0);
}
