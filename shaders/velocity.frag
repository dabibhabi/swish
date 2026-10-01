#version 450

// ── Per-object velocity buffer — fragment stage (debug-only) ────────────────
// Converts the interpolated current/previous clip-space positions (perspective-
// correct across the triangle) into a UV-space motion vector using the SAME
// convention taa.frag's depth-based estimate uses: velocity = curUV - prevUV,
// i.e. how far this surface point moved TOWARD its current screen position
// since last frame. Alpha is a validity flag: 1.0 wherever this pass draws
// (car body / wheels), 0.0 everywhere else (cleared) — taa.frag uses it to
// pick this authoritative value over its depth-only fallback.

layout(location = 0) in vec4 inCurClip;
layout(location = 1) in vec4 inPrevClip;

layout(location = 0) out vec4 outVelocity;

void main() {
    vec2 curUV  = (inCurClip.xy / inCurClip.w) * 0.5 + 0.5;
    vec2 prevUV = (inPrevClip.xy / inPrevClip.w) * 0.5 + 0.5;
    outVelocity = vec4(curUV - prevUV, 0.0, 1.0);
}
