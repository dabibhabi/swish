#version 450

// ── IBL bake stage 3: GGX specular prefilter ──────────────────────────
// Importance-sampled convolution of the environment cubemap for a given
// roughness — one draw per (cube face × roughness mip). lighting.frag samples
// this with textureLod(prefiltered, R, roughness·maxMip) for the specular-IBL
// lobe (replacing the analytic mix of sharp/blurred sky). Split-sum, Karis 2013.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(push_constant) uniform P {
    vec4 faceR;   // xyz = face right basis
    vec4 faceU;   // xyz = face up basis
    vec4 faceF;   // xyz = face forward/centre basis
    vec4 params;  // x = roughness [0,1]
} pc;

const float PI = 3.14159265359;

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), radicalInverse_VdC(i)); }

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float rough) {
    float a    = rough * rough;
    float phi  = 2.0 * PI * Xi.x;
    float cosT = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3  H    = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    vec3  up   = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3  tx   = normalize(cross(up, N));
    vec3  ty   = cross(N, tx);
    return normalize(tx * H.x + ty * H.y + N * H.z);
}

void main() {
    vec2 st = fragUV * 2.0 - 1.0;
    vec3 N  = normalize(pc.faceF.xyz + st.x * pc.faceR.xyz + st.y * pc.faceU.xyz);
    vec3 V  = N;  // split-sum simplification (V = R = N)

    float rough = pc.params.x;
    const uint SAMPLES = 256u;
    vec3  prefiltered  = vec3(0.0);
    float totalWeight  = 0.0;
    for (uint i = 0u; i < SAMPLES; i++) {
        vec2 Xi = hammersley(i, SAMPLES);
        vec3 H  = importanceSampleGGX(Xi, N, rough);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefiltered += texture(envMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    outColor = vec4(prefiltered / max(totalWeight, 0.001), 1.0);
}
