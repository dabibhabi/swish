#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D inputTex;

layout(push_constant) uniform Params {
    float threshold;
    float intensity;
    float exposure;
    float _pad0;
    vec2  texelSize;  // (1/w, 0) for horizontal, (0, 1/h) for vertical
    vec2  _pad1;
} params;

layout(location = 0) out vec4 outColor;

void main() {
    // 13-tap Gaussian blur (optimized: 7 bilinear taps)
    // Weights: [0.0044, 0.0540, 0.2420, 0.3991, 0.2420, 0.0540, 0.0044]
    vec2 dir = params.texelSize;

    vec3 result = vec3(0.0);
    result += texture(inputTex, fragUV - 6.0 * dir).rgb * 0.0044;
    result += texture(inputTex, fragUV - 4.0 * dir).rgb * 0.0540;
    result += texture(inputTex, fragUV - 2.0 * dir).rgb * 0.2420;
    result += texture(inputTex, fragUV              ).rgb * 0.3991;
    result += texture(inputTex, fragUV + 2.0 * dir).rgb * 0.2420;
    result += texture(inputTex, fragUV + 4.0 * dir).rgb * 0.0540;
    result += texture(inputTex, fragUV + 6.0 * dir).rgb * 0.0044;

    outColor = vec4(result, 1.0);
}
