#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D aoTex;

layout(location = 0) out vec4 outColor;

void main() {
    // Plain 5x5 box average. The SSAO pass rotates its kernel per-pixel, which
    // leaves full-frequency noise; a uniform average resolves it far better than
    // an edge-preserving (bilateral) weight, which by design keeps that noise.
    // AO is low-frequency, so softening edges here is fine.
    vec2 texelSize = 1.0 / vec2(textureSize(aoTex, 0));

    float result = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            result += texture(aoTex, fragUV + vec2(float(x), float(y)) * texelSize).r;
        }
    }

    outColor = vec4(vec3(result / 25.0), 1.0);
}
