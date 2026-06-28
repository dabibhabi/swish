#version 450

// Glass fragment shader — Fresnel-modulated tint + sun specular.
// Blended with SRC_ALPHA / ONE_MINUS_SRC_ALPHA onto the HDR buffer.

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec4 fragColor;  // xyz = base tint, w = base alpha

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;   // xyz = camera world position, w = wetness
    vec4 sunDir;   // xyz = normalized sun direction
    vec4 sunColor; // rgb = sun color, a = intensity
} camera;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);

    // View direction from fragment toward camera
    vec3 V = normalize(camera.camPos.xyz - fragWorldPos);

    // Fresnel (Schlick) — glass edges become more reflective/opaque
    float NoV     = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NoV, 4.0);

    // Final alpha: base tint alpha + Fresnel rim
    float alpha = clamp(fragColor.a + fresnel * 0.55, 0.0, 0.92);

    // Sun specular highlight (sharp, R=256 to simulate near-perfect mirror)
    vec3  L        = normalize(camera.sunDir.xyz);
    vec3  H        = normalize(V + L);
    float specular = pow(max(dot(N, H), 0.0), 256.0) * camera.sunColor.a;

    // Combine tint + specular
    vec3 color = fragColor.rgb + camera.sunColor.rgb * specular * 0.4;

    outColor = vec4(color, alpha);
}
