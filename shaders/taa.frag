#version 450

// ── Temporal anti-aliasing resolve + per-pixel motion blur (W9, debug-only) ──
// Reprojection TAA: reconstruct each pixel's world position from depth, project it
// with the PREVIOUS frame's (un-jittered) view-proj to find where it was last frame,
// sample the history there, and blend. With the per-frame sub-pixel camera jitter
// this converges to a supersampled image. Robustness (the difference between "clean"
// and "foggy/grainy in motion"):
//   • the reprojected history is clamped to the current 3×3 colour box in YCoCg
//     space (a much tighter chroma/luma box than raw HDR RGB) → kills ghosting;
//   • the blend is velocity-adaptive (less history where the scene moves fast) and
//     inverse-luma weighted (suppresses bright-sample fireflies) → kills grain.
// The resolved colour is copied back into the HDR buffer, so the bloom/composite
// chain is unchanged. Debug builds only (SSAA remains the release AA).

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D curHdr;   // this frame's lit+forward HDR (jittered)
layout(set = 0, binding = 1) uniform sampler2D depthTex; // scene depth (this frame)
layout(set = 0, binding = 2) uniform sampler2D history;  // last frame's resolved output

layout(push_constant) uniform Push {
    mat4 prevViewProj;  // world → clip, PREVIOUS frame, un-jittered
    mat4 invViewProj;   // clip → world, current frame (matches the jittered depth)
    vec4 params;        // x = history blend, y = motion-blur scale, zw = texel size (1/extent)
} pc;

layout(location = 0) out vec4 outColor;

vec3 rgb2ycocg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                0.5 * c.r - 0.5 * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 ycocg2rgb(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    vec2  texel = pc.params.zw;
    float d     = texture(depthTex, fragUV).r;

    // Reconstruct world position, reproject through the previous frame → velocity.
    vec4 ndc = vec4(fragUV * 2.0 - 1.0, d, 1.0);
    vec4 wp  = pc.invViewProj * ndc;
    wp /= wp.w;
    vec4  pc4      = pc.prevViewProj * vec4(wp.xyz, 1.0);
    vec2  prevUV   = (pc4.xy / pc4.w) * 0.5 + 0.5;
    vec2  velocity = fragUV - prevUV;

    vec3 cur   = texture(curHdr, fragUV).rgb;
    vec3 curYC = rgb2ycocg(cur);

    // Current 3×3 colour box in YCoCg — the tight AABB the reprojected history is
    // clamped into (linear-HDR RGB boxes are far too loose → the "foggy" ghosting).
    vec3 nmin = curYC, nmax = curYC;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0)
                continue;
            vec3 s = rgb2ycocg(texture(curHdr, fragUV + vec2(x, y) * texel).rgb);
            nmin   = min(nmin, s);
            nmax   = max(nmax, s);
        }

    vec3  histYC   = clamp(rgb2ycocg(texture(history, prevUV).rgb), nmin, nmax);
    vec3  hist      = ycocg2rgb(histYC);

    // Reject history that reprojects off-screen (nothing to blend there).
    float onScreen = (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) ? 1.0 : 0.0;
    // Velocity-adaptive: trust history less where the scene moves fast (in pixels/frame).
    float pxSpeed  = length(velocity / texel);
    float blend    = mix(pc.params.x, 0.6, clamp(pxSpeed / 24.0, 0.0, 1.0)) * onScreen;

    // Inverse-luma weighted blend (Karis) — a bright jittered sample contributes less,
    // suppressing the sparkle/grain that a naive lerp leaves in motion.
    float wc = (1.0 - blend) / (1.0 + luma(cur));
    float wh = blend / (1.0 + luma(hist));
    vec3  taa = (cur * wc + hist * wh) / max(wc + wh, 1e-5);

    // Per-pixel motion blur: smear the resolved colour back along the velocity.
    vec3 result = taa;
    if (pc.params.y > 0.0) {
        vec2 mv = clamp(velocity, vec2(-0.1), vec2(0.1)) * pc.params.y;
        const int MB = 6;
        vec3      acc = taa;
        for (int i = 1; i <= MB; ++i)
            acc += texture(curHdr, fragUV - mv * (float(i) / float(MB))).rgb;
        result = acc / float(MB + 1);
    }

    outColor = vec4(result, 1.0);
}
