#version 450

// Bloom pyramid combine (lighting L4.4, debug builds): draws one blurred pyramid level over the bloom image
// with alpha blending, so the levels end up as a normalised mix (wide glow plus the tight core) and the
// bloom's total energy is unchanged. One texture, the shared post-process push block.

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D inputTex;

layout(push_constant) uniform Params {
    float threshold;
    float intensity;  // this level's blend weight (the output alpha)
    float exposure;
    float _pad0;
    vec2  texelSize;
    vec2  _pad1;
} params;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(inputTex, fragUV).rgb, params.intensity);
}
