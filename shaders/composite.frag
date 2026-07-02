#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 2) uniform sampler2D aoTex;

layout(push_constant) uniform Params {
    float threshold;
    float bloom_intensity;
    float exposure;
    float rain_intensity;  // [0,1] — drives atmospheric mist effect
    vec2  texelSize;
    float fog_density;     // atmospheric grey blend strength per rain_intensity unit
    float _pad1;
    // Camera-app grade (debug UI); identity at defaults → no-op in release.
    float brightness;
    float contrast;
    float saturation;
    float temperature;
    float tint;
} params;

layout(location = 0) out vec4 outColor;

// AgX tonemapper — Blender 3.4 (Troy Sobotka).
// Better hue preservation and saturation handling than ACES Narkowicz.
// GLSL mat3 is column-major.
const mat3 kAgXInset = mat3(
    0.842479062253094,  0.0784335999999992, 0.0792237451477643,
    0.0423282422610123, 0.878468636469772,  0.0791661274605434,
    0.0423756549057051, 0.0784336,          0.879465981976014
);
const mat3 kAgXOutset = mat3(
     1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
    -0.0528968517574562,  1.15190312990417,   -0.0989611768448433,
    -0.0529716355144438, -0.0980434501171241,  1.15107367264116
);

float agxCurve(float x) {
    float x2 = x * x, x4 = x2 * x2;
    return + 15.5    * x4 * x2
           - 40.14   * x4 * x
           + 31.96   * x4
           - 6.868   * x2 * x
           + 0.4298  * x2
           + 0.1191  * x
           - 0.00232;
}

vec3 AgX(vec3 c) {
    c = max(c, 0.0);
    c = kAgXInset * c;
    c = clamp((log2(max(c, 1e-10)) + 12.47393) / 16.5, 0.0, 1.0);
    c = vec3(agxCurve(c.r), agxCurve(c.g), agxCurve(c.b));
    return clamp(kAgXOutset * c, 0.0, 1.0);
}

void main() {
    vec3 hdr  = texture(hdrScene, fragUV).rgb;
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    float ao  = texture(aoTex, fragUV).r;

    // Apply ambient occlusion
    hdr *= ao;

    // Add bloom
    hdr += bloom * params.bloom_intensity;

    // Exposure adjustment
    hdr *= params.exposure;

    // White balance (linear, pre-tonemap): temperature warms (+R/-B), tint -G (magenta).
    hdr *= vec3(1.0 + params.temperature * 0.20, 1.0 - params.tint * 0.20, 1.0 - params.temperature * 0.20);

    // Rain fog is now applied per-fragment and depth-resolved in lighting.frag
    // (Koschmieder), so near surfaces like the cabin stay crisp. The old flat
    // screen-wide grey blend here washed the whole frame (including the interior)
    // and has been removed. rain_intensity / fog_density are retained in the push
    // block for compatibility but no longer drive a composite-stage veil.

    // AgX tone mapping (HDR → LDR)
    vec3 mapped = AgX(hdr);

    // Post-tonemap grade (camera-app style). Identity at defaults (sat=1, contrast=1,
    // brightness=0), so release — which passes those defaults — is unchanged.
    float gLuma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(gLuma), mapped, params.saturation);   // saturation
    mapped = (mapped - 0.5) * params.contrast + 0.5;        // contrast about mid-grey
    mapped += params.brightness;                            // brightness
    mapped = clamp(mapped, 0.0, 1.0);

    outColor = vec4(mapped, 1.0);
}
