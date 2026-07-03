#version 450

// ── IBL bake stage 2: diffuse irradiance convolution ──────────────────
// Cosine-weighted hemisphere integral of the environment cubemap for each
// output direction — the diffuse-IBL term lighting.frag samples per fragment
// (replacing the analytic skyIrradiance() approximation). Low-res target
// (the result is very low-frequency). One draw per cube face.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(push_constant) uniform P {
    vec4 faceR;  // xyz = face right basis
    vec4 faceU;  // xyz = face up basis
    vec4 faceF;  // xyz = face forward/centre basis
} pc;

const float PI = 3.14159265359;

void main() {
    vec2 st = fragUV * 2.0 - 1.0;
    vec3 N  = normalize(pc.faceF.xyz + st.x * pc.faceR.xyz + st.y * pc.faceU.xyz);

    // Tangent frame around N.
    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    vec3  irradiance = vec3(0.0);
    float nrSamples  = 0.0;
    const float dPhi   = 0.025;
    const float dTheta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += dPhi) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += dTheta) {
            // Spherical (tangent space) → world.
            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleV = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += texture(envMap, sampleV).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }
    irradiance = PI * irradiance / nrSamples;
    outColor   = vec4(irradiance, 1.0);
}
