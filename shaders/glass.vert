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
    vec4 color;  // xyz = base tint, w = base alpha
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec4 fragColor;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position   = camera.proj * camera.view * worldPos;

    mat3 normalMat = mat3(transpose(inverse(push.model)));
    fragNormal     = normalize(normalMat * inNormal);
    fragWorldPos   = worldPos.xyz;
    fragColor      = push.color;
}
