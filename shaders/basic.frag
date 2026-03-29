#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in mat3 fragTBN;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;    // xyz = direction, w = intensity
    vec4 sunColor;  // rgb = color, a = ambient strength
} camera;

// PBR material textures
layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D roughnessTex;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} push;

layout(location = 0) out vec4 outColor;

// ── Procedural noise for surface variation ────────────────────────
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // smoothstep
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Multi-octave fractal noise for surface patches
float fbm(vec2 p) {
    float val = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        val += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return val;
}

// ── Sky gradient from view direction ──────────────────────────────
vec3 compute_sky_color(vec3 view_dir) {
    // Vertical gradient: horizon → zenith
    float t = clamp(view_dir.y * 2.0 + 0.3, 0.0, 1.0);
    vec3 horizon_color = vec3(0.70, 0.80, 0.90);  // hazy light blue
    vec3 zenith_color  = vec3(0.35, 0.55, 0.85);  // deep sky blue
    vec3 sky = mix(horizon_color, zenith_color, pow(t, 1.5));

    // Subtle sun glow near the sun direction
    float sun_dot = max(dot(view_dir, camera.sunDir.xyz), 0.0);
    sky += camera.sunColor.rgb * pow(sun_dot, 32.0) * 0.3;

    return sky;
}

void main() {
    // ── Sample PBR textures ───────────────────────────────────────
    vec3 albedo     = texture(albedoTex, fragUV).rgb * push.color.rgb;
    vec3 normalMap  = texture(normalTex, fragUV).rgb * 2.0 - 1.0;
    float roughness = texture(roughnessTex, fragUV).r;

    // ── Surface variation (procedural weathering/patches) ─────────
    // Low-frequency noise adds dark patches and slight color shifts
    float wear = fbm(fragUV * 0.08);             // large-scale variation
    float crack = fbm(fragUV * 0.4) * 0.3;       // medium-scale cracks
    float variation = mix(0.82, 1.08, wear);      // wider weathering variation
    albedo *= variation;
    albedo = mix(albedo, albedo * vec3(0.9, 0.88, 0.85), crack);  // slight warm shift in cracks
    roughness = clamp(roughness + crack * 0.15, 0.0, 1.0);  // cracks are rougher

    // ── Normal mapping via TBN matrix ─────────────────────────────
    vec3 N = normalize(fragTBN * normalMap);
    vec3 V = normalize(camera.camPos.xyz - fragWorldPos);
    vec3 L = camera.sunDir.xyz;
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // ── GGX specular (simplified Cook-Torrance) ───────────────────
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * denom * denom + 0.0001);

    // Fresnel (Schlick) — F0 = 0.04 for dielectric surfaces
    float F = 0.04 + 0.96 * pow(1.0 - HdotV, 5.0);
    float specular = D * F * 0.25;

    // ── Combine lighting ──────────────────────────────────────────
    float ambient = camera.sunColor.a;  // ambient strength from UBO
    float diffuse = NdotL * 0.70;
    vec3 lit_color = albedo * (ambient + diffuse) * camera.sunColor.rgb
                   + vec3(specular) * NdotL * camera.sunColor.rgb;

    // ── Fog with sky gradient blend ───────────────────────────────
    float dist = length(fragWorldPos - camera.camPos.xyz);
    float fog_density = 0.00000008;
    float fog_factor = exp(-pow(dist * fog_density, 2.0));
    fog_factor = clamp(fog_factor, 0.0, 1.0);

    // Fog blends toward the sky color in the view direction (not a flat color)
    vec3 fog_dir = normalize(fragWorldPos - camera.camPos.xyz);
    vec3 sky_color = compute_sky_color(fog_dir);
    vec3 final_color = mix(sky_color, lit_color, fog_factor);

    // Alpha test — discard transparent fragments (for tree billboards)
    float alpha = texture(albedoTex, fragUV).a;
    if (alpha < 0.5) discard;

    outColor = vec4(final_color, 1.0);
}
