#version 450

#include "bindings.glsl"
#include "color.glsl"

layout(location = 0) in vec2 fragUV;

layout(set = SET_COMPOSITE, binding = BIND_COMP_HDR) uniform sampler2D hdrScene;
layout(set = SET_COMPOSITE, binding = BIND_COMP_BLOOM) uniform sampler2D bloomTex;
layout(set = SET_COMPOSITE, binding = BIND_COMP_AO) uniform sampler2D aoTex;
layout(set = SET_COMPOSITE, binding = BIND_COMP_SSR) uniform sampler2D ssrTex;   // SSR reflection (rgb = radiance, a = mask)
layout(set = SET_COMPOSITE, binding = BIND_COMP_GODRAYS) uniform sampler2D godraysTex;  // god-rays light shafts (rgb = radiance, half-res)

layout(push_constant) uniform Params {
    float threshold;
    float bloom_intensity;
    float exposure;
    float rain_intensity;  // [0,1] — drives atmospheric mist effect
    vec2  texelSize;
    float fog_density;     // atmospheric grey blend strength per rain_intensity unit
    float _pad1;
    // Camera-app grade (debug UI); identity at defaults → no-op in release.
    float brightness;
    float contrast;
    float saturation;
    float temperature;
    float tint;
} params;

layout(location = 0) out vec4 outColor;

// AgX tonemapper — Blender 3.4 (Troy Sobotka).
// Better hue preservation and saturation handling than ACES Narkowicz.
// GLSL mat3 is column-major.
const mat3 kAgXInset = mat3(
    0.842479062253094,  0.0784335999999992, 0.0792237451477643,
    0.0423282422610123, 0.878468636469772,  0.0791661274605434,
    0.0423756549057051, 0.0784336,          0.879465981976014
);
const mat3 kAgXOutset = mat3(
     1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
    -0.0528968517574562,  1.15190312990417,   -0.0989611768448433,
    -0.0529716355144438, -0.0980434501171241,  1.15107367264116
);

float agxCurve(float x) {
    float x2 = x * x, x4 = x2 * x2;
    return + 15.5    * x4 * x2
           - 40.14   * x4 * x
           + 31.96   * x4
           - 6.868   * x2 * x
           + 0.4298  * x2
           + 0.1191  * x
           - 0.00232;
}

vec3 AgX(vec3 c) {
    c = max(c, 0.0);
    c = kAgXInset * c;
    c = clamp((log2(max(c, 1e-10)) + 12.47393) / 16.5, 0.0, 1.0);
    c = vec3(agxCurve(c.r), agxCurve(c.g), agxCurve(c.b));

    // Outset matrix cross-mixes the three independently-curved channels (its
    // off-diagonal terms are negative, see kAgXOutset above). Near the top of the
    // range this means one channel can cross 1.0 while the others — pulled DOWN by
    // that same channel's negative cross-terms — are still short of it. Clamping
    // each channel independently right after this matrix then clips the leading
    // channel but leaves the lagging ones low, so blown highlights skew toward
    // whichever channel got there first (reads as a magenta cast) instead of
    // converging on neutral white. Fix: before the (single, final) clamp below,
    // pull the lagging channels up toward the brightest channel, with the pull
    // strength ramping in only over the last stretch of the range approaching the
    // clip boundary (a pre-shoulder chroma compression) — normal mid-tone hues sit
    // well below the ramp's lower edge and pass through completely unaffected,
    // while anything that would actually clip is fully neutral (equal channels)
    // by the time it gets there, so the shared clamp below can no longer skew hue.
    // Ramp edge sits high on purpose. This value is display-encoded — the EOTF below
    // raises it by 2.2 — so the edge maps to maxC^2.2 of final linear peak: 0.78 would
    // be only ~0.58, desaturating ordinary bright saturated content (taillights, sunset
    // sky, paint speculars) that was never going to clip. 0.94 maps to ~0.87, confining
    // the pull to genuinely near-blown pixels. Sanity check: a saturated red at
    // outC=(0.85,0.2,0.3) has maxC=0.85 < 0.94 → shoulder=0 → passes through untouched.
    vec3 outC = kAgXOutset * c;
    float maxC = max(max(outC.r, outC.g), outC.b);
    float shoulder = smoothstep(0.94, 1.0, maxC);
    outC = mix(outC, vec3(maxC), shoulder);
    c = clamp(outC, 0.0, 1.0);

    // AgX EOTF — the sigmoid above outputs a display-encoded (~2.2-gamma) signal;
    // linearise it here. Our swapchain expects LINEAR input (extended-sRGB-linear, or
    // an sRGB format whose hardware re-encodes on write), so without this step the
    // signal was gamma-encoded twice → the washed-out, low-contrast "milky" look.
    return pow(c, vec3(2.2));
}

void main() {
    vec3 hdr  = texture(hdrScene, fragUV).rgb;
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    float ao  = texture(aoTex, fragUV).r;

    // Apply ambient occlusion
    hdr *= ao;

    // Screen-space reflections: add the reflected radiance on top of the lit HDR
    // (already Fresnel/intensity-weighted in the SSR pass; zero on a miss, where
    // the sky IBL baked into hdr is the fallback).
    hdr += texture(ssrTex, fragUV).rgb;

    // God-rays: sun-anchored light shafts (half-res, linearly upsampled here),
    // already density/decay/weight/intensity-weighted in the god-rays pass.
    hdr += texture(godraysTex, fragUV).rgb;

    // Add bloom
    hdr += bloom * params.bloom_intensity;

    // Exposure adjustment
    hdr *= params.exposure;

    // White balance (linear, pre-tonemap): temperature warms (+R/-B), tint -G (magenta).
    hdr *= vec3(1.0 + params.temperature * 0.20, 1.0 - params.tint * 0.20, 1.0 - params.temperature * 0.20);

    // Rain fog is now applied per-fragment and depth-resolved in lighting.frag
    // (Koschmieder), so near surfaces like the cabin stay crisp. The old flat
    // screen-wide grey blend here washed the whole frame (including the interior)
    // and has been removed. rain_intensity / fog_density are retained in the push
    // block for compatibility but no longer drive a composite-stage veil.

    // AgX tone mapping (HDR → LDR)
    vec3 mapped = AgX(hdr);

    // Post-tonemap grade (camera-app style). Identity at defaults (sat=1, contrast=1,
    // brightness=0), so release — which passes those defaults — is unchanged.
    //
    // Decomposed into luma + chroma so each control acts on its own axis: contrast
    // and brightness reshape luma only, saturation scales chroma only. Previously
    // contrast was `(mapped - 0.5) * contrast + 0.5` applied per-channel directly on
    // RGB — algebraically that scales the chroma component (channel - luma) by the
    // SAME factor as luma, so at contrast > 1 it amplified whatever chroma error the
    // tonemap let through (the magenta cast) before the two of them ever recombine.
    // Operating on luma/chroma explicitly and recombining once keeps that
    // amplification from happening, and leaves exactly one shared clamp at the end.
    float gLuma       = dot(mapped, LUMA_709);
    vec3  gChroma     = (mapped - gLuma) * params.saturation;       // saturation: chroma only
    float gGradedLuma = (gLuma - 0.5) * params.contrast + 0.5;      // contrast: luma only, about mid-grey
    gGradedLuma += params.brightness;                                // brightness: luma only
    mapped = clamp(gGradedLuma + gChroma, 0.0, 1.0);

    outColor = vec4(mapped, 1.0);
}
