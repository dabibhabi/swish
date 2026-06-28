#version 450

// Windshield rain (refraction) — vertex stage.
//
// Emits GLASS-SPACE coordinates so the drop field sticks to the pane:
//   - fragUV          : mesh UV [0,1] across the windshield (drop field space)
//   - fragLocalNormal : object-space normal (nose = +X) used to confine drops
//                       to the forward-facing windshield pane in the fragment
//   - fragNormal/Pos  : world-space, for the small sun glint on drops
// The screen-space coordinate needed for the refraction lookup is derived in
// the fragment shader from gl_FragCoord (no screen UV passed here).

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
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragLocalNormal;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position   = camera.proj * camera.view * worldPos;

    fragUV          = inUV;                 // glass-space drop field coordinate
    fragLocalNormal = normalize(inNormal);  // object space (nose = +X)

    mat3 normalMat = mat3(transpose(inverse(push.model)));
    fragNormal     = normalize(normalMat * inNormal);
    fragWorldPos   = worldPos.xyz;
}
