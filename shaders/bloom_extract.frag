#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform Params {
    float threshold;
    float intensity;
    float exposure;
} params;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(hdrScene, fragUV).rgb;

    // Luminance (Rec. 709)
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft extraction with knee curve
    float contribution = max(brightness - params.threshold, 0.0);
    contribution = contribution / (contribution + 1.0);

    outColor = vec4(color * contribution, 1.0);
}
