#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 material;   // unused here; declared so vertex/fragment push blocks match
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out mat3 fragTBN;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    mat3 normalMatrix = mat3(transpose(inverse(push.model)));
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragTBN      = mat3(T, B, N);
    fragNormal   = N;
    fragUV       = inUV;
    fragWorldPos = worldPos.xyz;
}
