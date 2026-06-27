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

// Narkowicz 2015 ACES filmic tone mapping approximation
const float kAcesA = 2.51;
const float kAcesB = 0.03;
const float kAcesC = 2.43;
const float kAcesD = 0.59;
const float kAcesE = 0.14;

vec3 ACESFilm(vec3 x) {
    return clamp((x * (kAcesA * x + kAcesB)) / (x * (kAcesC * x + kAcesD) + kAcesE), 0.0, 1.0);
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
