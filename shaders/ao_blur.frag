#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D aoTex;

layout(location = 0) out vec4 outColor;

void main() {
    // 4x4 bilateral-style blur (edge-preserving via simple averaging)
    vec2 texelSize = 1.0 / vec2(textureSize(aoTex, 0));

    float result = 0.0;
    float totalWeight = 0.0;
    float centerAO = texture(aoTex, fragUV).r;

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float sampleAO = texture(aoTex, fragUV + offset).r;

            // Bilateral weight: reject samples too different from center
            float diff = abs(sampleAO - centerAO);
            float weight = exp(-diff * diff * 50.0);

            result += sampleAO * weight;
            totalWeight += weight;
        }
    }

    outColor = vec4(vec3(result / totalWeight), 1.0);
}
