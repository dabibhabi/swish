#version 450

// ── Depth-of-field resolve (debug-only) ──────────────────────────────
// A single-pass gather DOF. For each pixel it reconstructs the view-space distance
// from the reverse-Z depth, derives a circle-of-confusion (CoC) that grows with the
// distance from the focus plane, and averages a 16-tap Poisson disk of the HDR scene
// scaled by that CoC. In-focus pixels (tiny CoC) pass the centre sample through. The
// resolved colour is copied back into the HDR buffer so bloom/composite are unchanged.
// Debug builds only.

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;  // this frame's lit HDR
layout(set = 0, binding = 1) uniform sampler2D depthTex;  // scene depth (reverse-Z)

layout(push_constant) uniform P {
    mat4 invProj;  // clip → view (reconstruct view-space position from depth)
    vec4 focus;    // x = focusDist (WU), y = focusRange (WU), z = maxCoC (texels), w = pad
    vec4 texel;    // xy = texel size (1/extent), zw = pad
} pc;

layout(location = 0) out vec4 outColor;

// 16-tap Poisson disk (unit circle) — fixed sample offsets for the gather.
const vec2 kDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

void main() {
    float d = texture(depthTex, fragUV).r;

    // Reverse-Z + GLM_FORCE_DEPTH_ZERO_TO_ONE: ndc.z = d directly. View space looks
    // down -Z, so scene distance = -viewZ (positive).
    vec4  c    = pc.invProj * vec4(fragUV * 2.0 - 1.0, d, 1.0);
    float viewZ = c.z / c.w;
    float dist  = -viewZ;

    float focusDist  = pc.focus.x;
    float focusRange = pc.focus.y;
    float maxCoC     = pc.focus.z;

    // Sky (reverse-Z far = 0) → push it to full blur instead of trusting the near-degenerate
    // reconstruction at d ≈ 0.
    if (d <= 0.0001)
        dist = focusDist + focusRange * 4.0;

    float coc = clamp(abs(dist - focusDist) / max(focusRange, 1.0), 0.0, 1.0) * maxCoC;

    // In focus → pass the centre sample through untouched.
    if (coc < 0.5) {
        outColor = vec4(texture(hdrScene, fragUV).rgb, 1.0);
        return;
    }

    // Gather the disk (equal weights). Offsets are CoC texels × texel size → UV.
    vec3 result = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 off = kDisk[i] * coc * pc.texel.xy;
        result += texture(hdrScene, fragUV + off).rgb;
    }
    result /= 16.0;

    outColor = vec4(result, 1.0);
}
