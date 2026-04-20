#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform Params {
    mat4 invProjection;
    float aoRadius;
    float aoBias;
    float aoIntensity;
    float _pad0;
} params;

layout(location = 0) out vec4 outColor;

// Reconstruct view-space position from depth
vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = params.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Pseudo-random from UV (no noise texture needed)
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    float depth = texture(depthTex, fragUV).r;

    // Skip sky (depth = 1.0 or very close)
    if (depth > 0.9999) {
        outColor = vec4(1.0);
        return;
    }

    vec3 fragPos = viewPosFromDepth(fragUV, depth);

    // Reconstruct normal from depth derivatives
    vec3 dPdx = dFdx(fragPos);
    vec3 dPdy = dFdy(fragPos);
    vec3 normal = normalize(cross(dPdx, dPdy));

    // Random rotation per pixel
    float angle = hash(fragUV * 1000.0) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rot = mat2(cosA, -sinA, sinA, cosA);

    // 16-sample hemisphere kernel
    const int SAMPLES = 16;
    float occlusion = 0.0;
    float radius = params.aoRadius;

    // Poisson-distributed hemisphere samples
    const vec3 kernel[16] = vec3[](
        vec3( 0.04, 0.04, 0.04), vec3(-0.12, 0.18, 0.06),
        vec3( 0.18,-0.04, 0.12), vec3(-0.06,-0.12, 0.18),
        vec3( 0.22, 0.10, 0.08), vec3(-0.16, 0.22, 0.14),
        vec3( 0.08,-0.22, 0.16), vec3(-0.24,-0.08, 0.20),
        vec3( 0.30, 0.14, 0.10), vec3(-0.20, 0.28, 0.18),
        vec3( 0.14,-0.30, 0.22), vec3(-0.32,-0.12, 0.24),
        vec3( 0.36, 0.20, 0.14), vec3(-0.26, 0.34, 0.22),
        vec3( 0.18,-0.36, 0.28), vec3(-0.38,-0.16, 0.30)
    );

    for (int i = 0; i < SAMPLES; i++) {
        // Rotate sample around normal axis
        vec3 sampleDir = kernel[i];
        sampleDir.xy = rot * sampleDir.xy;

        // Flip if below surface
        if (dot(sampleDir, normal) < 0.0) sampleDir = -sampleDir;

        // Compute sample position
        vec3 samplePos = fragPos + sampleDir * radius;

        // Project sample to screen
        vec4 sampleClip = inverse(params.invProjection) * vec4(samplePos, 1.0);
        sampleClip.xyz /= sampleClip.w;
        vec2 sampleUV = sampleClip.xy * 0.5 + 0.5;

        // Sample depth at projected position
        float sampleDepth = texture(depthTex, sampleUV).r;
        vec3 sampledPos = viewPosFromDepth(sampleUV, sampleDepth);

        // Range check + occlusion test
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampledPos.z));
        occlusion += (sampledPos.z >= samplePos.z + params.aoBias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(SAMPLES)) * params.aoIntensity;
    outColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
}
