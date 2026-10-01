#version 450

// ── Per-object velocity buffer — vertex stage (debug-only) ──────────────────
// See tasks/todo.md "Per-object velocity buffer" and VelocityPass.h. Renders the
// SAME car-body + wheel draw calls as the G-buffer pass a second time, into a
// small dedicated target, outputting the CURRENT and PREVIOUS frame's clip-space
// position per vertex so the fragment stage can derive an exact rigid-motion
// screen-space velocity (translation + rotation) instead of TAA's pure depth-
// reprojection estimate, which has no notion of a moving object at all.

layout(location = 0) in vec3 inPosition;
// Locations 1-3 (normal/uv/tangent) exist in the shared car vertex buffer's
// binding but are unused here — Vulkan allows a vertex shader to consume a
// subset of the bound attributes.

layout(push_constant) uniform Push {
    mat4 curViewProj;   // this frame's camera view*proj (jittered — matches the depth this pass tests against)
    mat4 prevViewProj;  // last frame's UN-jittered camera view*proj (matches TaaPass's convention)
    mat4 curModel;      // this draw's CURRENT object transform
    mat4 prevModel;      // this draw's PREVIOUS-frame object transform (identical to curModel = zero velocity)
} pc;

layout(location = 0) out vec4 outCurClip;
layout(location = 1) out vec4 outPrevClip;

void main() {
    vec4 curClip  = pc.curViewProj * (pc.curModel * vec4(inPosition, 1.0));
    vec4 prevClip = pc.prevViewProj * (pc.prevModel * vec4(inPosition, 1.0));

    gl_Position = curClip;
    outCurClip  = curClip;
    outPrevClip = prevClip;
}
