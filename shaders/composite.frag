#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 2) uniform sampler2D aoTex;

layout(push_constant) uniform Params {
    float threshold;
    float bloom_intensity;
    float exposure;
    float _pad0;
    vec2  texelSize;
    vec2  _pad1;
} params;

layout(location = 0) out vec4 outColor;

// ACES filmic tone mapping (rind pattern)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
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

    // ACES tone mapping (HDR → LDR)
    vec3 mapped = ACESFilm(hdr);

    outColor = vec4(mapped, 1.0);
}
