#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform Params {
    mat4 invProjection;  // clip → view
    mat4 projection;     // view → clip (avoids a per-sample matrix inverse)
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
#include "hash.glsl"

#ifdef SWISH_DEBUG_UI
// g = view depth for ao_blur.frag's depth weight (R16G16_SFLOAT target). The blur only uses depth RATIOS, so the
// scale is arbitrary; 1e-3 keeps the corridor's ~4.3e6 WU view distance inside the half-float range.
const float kAoDepthScale = 1e-3;
const float kAoSkyDepth   = 60000.0;  // sky: beyond any surface, so the blur never averages it in
// Depth-relative bias: reconstructed positions lose precision with distance (grazing angles stretch it further), so
// a fixed bias turned the far barrier and road into grainy false occlusion. +0.3 % of the view depth; at ~50 m it
// reaches the kernel's reach, so AO fades out where it is invisible anyway.
const float kAoBiasPerDepth = 0.003;
#define AO_BIAS (params.aoBias + abs(fragPos.z) * kAoBiasPerDepth)
#else
#define AO_BIAS params.aoBias
#endif

void main() {
    float depth = texture(depthTex, fragUV).r;

    // Skip sky (reverse-Z: far/sky is depth 0.0)
    if (depth < 0.0001) {
#ifdef SWISH_DEBUG_UI
        outColor = vec4(1.0, kAoSkyDepth, 0.0, 1.0);
#else
        outColor = vec4(1.0);
#endif
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

        // Project sample to screen (view → clip via the supplied projection)
        vec4 sampleClip = params.projection * vec4(samplePos, 1.0);
        sampleClip.xyz /= sampleClip.w;
        vec2 sampleUV = sampleClip.xy * 0.5 + 0.5;

        // Reject samples that fall outside the screen or on the sky (depth≈1):
        // an unbounded far-plane position otherwise reads as a spurious occluder
        // along every silhouette against the sky, which shows up as edge speckle.
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;
        float sampleDepth = texture(depthTex, sampleUV).r;
        if (sampleDepth < 0.0001) continue;  // reverse-Z sky (far = 0)
        vec3 sampledPos = viewPosFromDepth(sampleUV, sampleDepth);

        // Range check + occlusion test
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampledPos.z));
        occlusion += (sampledPos.z >= samplePos.z + AO_BIAS ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(SAMPLES)) * params.aoIntensity;
#ifdef SWISH_DEBUG_UI
    outColor = vec4(clamp(ao, 0.0, 1.0), abs(fragPos.z) * kAoDepthScale, 0.0, 1.0);
#else
    outColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
#endif
}
