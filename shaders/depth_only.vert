#version 450

// ── Depth-only (shadow map) vertex shader ─────────────────────────────
// Renders scene geometry from the sun's point of view into a depth-only
// render pass. Consumes the SAME vertex buffer layout as basic.vert
// (position/normal/uv/tangent, binding 0), but only position is needed.
//
// The light-space view-projection is supplied as a single mat4 push
// constant (64 bytes, 16-byte aligned — MoltenVK-safe). This keeps the
// depth pass fully self-contained: it does NOT read the CameraUBO (set 0),
// so the sun matrix can be recomputed each frame on the CPU and pushed
// directly, with no descriptor set bound.
//
// gl_Position = lightViewProj * push.model * vec4(inPosition, 1.0);

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;    // unused, declared to match Vertex layout
layout(location = 2) in vec2 inUV;        // unused
layout(location = 3) in vec4 inTangent;   // unused

layout(push_constant) uniform DepthPush {
    mat4 lightViewProj;  // sun ortho * lookAt, computed per-frame on the CPU
    mat4 model;          // same per-object model matrix as basic.vert push.model
} push;

void main() {
    gl_Position = push.lightViewProj * push.model * vec4(inPosition, 1.0);
}
