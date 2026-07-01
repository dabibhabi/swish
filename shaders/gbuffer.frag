#version 450

// G-Buffer fragment shader — outputs material properties to 3 MRT attachments.
// NO lighting computation here. Lighting is deferred to lighting.frag.

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in mat3 fragTBN;

// PBR material textures (same descriptor set as forward pass)
layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D roughnessTex;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 material;   // x = real per-material metalness [0,1] (dielectric = 0)
} push;

// ── G-Buffer MRT outputs (3 color attachments) ──────────────────
layout(location = 0) out vec4 outAlbedo;    // RGB = base color, A = 1
layout(location = 1) out vec4 outNormal;    // RGB = encoded world normal, A = 1
layout(location = 2) out vec4 outMaterial;  // R = metallic, G = roughness, B = 0, A = 1

// ── Procedural noise for surface variation ────────────────────────
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

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

void main() {
    // ── Alpha test (billboard transparency) ───────────────────────
    vec4 albedoSample = texture(albedoTex, fragUV);
    if (albedoSample.a < 0.5) discard;

    // ── Sample PBR textures ───────────────────────────────────────
    vec3 albedo     = albedoSample.rgb * push.color.rgb;
    // Rain cabin wash (back-compatible sentinel on color.a). color.a is NOT used
    // for output alpha (outAlbedo.a is a hard 1.0 below), so this is free to repurpose:
    //   a <= 1  → wash = 0 → albedo unchanged (ALL existing draws use a = 1).
    //   a in (1,2] → wash in (0,1] → albedo tinted toward light gray (cabin only).
    float wash = clamp(push.color.a - 1.0, 0.0, 1.0);
    albedo     = mix(albedo, vec3(0.55), wash);  // tasteful LIGHT GRAY target (not white)
    vec3 normalMap  = texture(normalTex, fragUV).rgb * 2.0 - 1.0;
    float roughness = texture(roughnessTex, fragUV).r;

    // ── Surface variation (procedural weathering) ─────────────────
    float wear  = fbm(fragUV * 0.08);
    float crack = fbm(fragUV * 0.4) * 0.3;
    float variation = mix(0.82, 1.08, wear);
    albedo *= variation;
    albedo = mix(albedo, albedo * vec3(0.9, 0.88, 0.85), crack);
    roughness = clamp(roughness + crack * 0.15, 0.0, 1.0);

    // ── Normal mapping via TBN matrix ─────────────────────────────
    vec3 N = normalize(fragTBN * normalMap);

    // ── Specular anti-aliasing ────────────────────────────────────
    float filterWidth = length(fwidth(N));
    roughness = max(roughness, sqrt(2.0 * filterWidth));

    // ── Write G-Buffer ────────────────────────────────────────────
    outAlbedo   = vec4(albedo, 1.0);
    outNormal   = vec4(N * 0.5 + 0.5, 1.0);       // encode [-1,1] → [0,1]
    // R = real metalness (dielectric F0 is a separate constant in lighting.frag),
    // G = roughness, B = wettable mask (1 = rain-exposed, 0 = enclosed cabin) so the
    // lighting pass keeps wet-weather effects off the dry interior.
    // Previously R was hardcoded 0.04, conflating F0 with metalness.
    outMaterial = vec4(push.material.x, roughness, push.material.y, 1.0);
}
