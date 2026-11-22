#version 450

#include "color.glsl"

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform Params {
    float threshold;
    float intensity;
    float exposure;
} params;

layout(location = 0) out vec4 outColor;

void main() {
#ifdef SWISH_DEBUG_UI
    // Lighting L4.4: Karis average. The extract runs at quarter resolution, so one bilinear tap skips most of
    // the source pixels and a single very bright one flickers in and out (a firefly). Four taps cover the
    // whole footprint, and weighting each by 1 / (1 + its exposed luminance) keeps one hot pixel from
    // owning the average.
    vec2 ts    = 1.0 / vec2(textureSize(hdrScene, 0));
    vec3 color = vec3(0.0);
    float wSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2  o = vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * ts;
        vec3  c = texture(hdrScene, fragUV + o).rgb;
        float w = 1.0 / (1.0 + dot(c, LUMA_709) * params.exposure);
        color += c * w;
        wSum  += w;
    }
    color /= wSum;
#else
    vec3 color = texture(hdrScene, fragUV).rgb;
#endif

    // Luminance (Rec. 709) AFTER exposure (L1.6): the threshold means "brighter than the display can show", so a
    // sunlit scene and a dark one bloom by the same rule. The output stays in raw units because the composite
    // adds bloom first and multiplies by exposure after, so the bloom intensity needs no retune.
    float brightness = dot(color, LUMA_709) * params.exposure;

    // Soft extraction with knee curve
    float contribution = max(brightness - params.threshold, 0.0);
    contribution = contribution / (contribution + 1.0);

    outColor = vec4(color * contribution, 1.0);
}
