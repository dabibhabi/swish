#version 450

// ── God-rays / volumetric light shafts (screen-space radial blur) ─────
// A dedicated post-lighting pass (Mitchell 2007, GPU Gems 3 Ch.13). For each
// fragment it marches toward the projected screen-space sun position, summing
// the lit-scene brightness at SKY pixels only. Geometry (the car, signs, road)
// has scene depth < 1, contributes nothing, and so punches dark "shadow shafts"
// into the light — the crepuscular-ray look. The result is added on top of the
// HDR in the composite pass (bright shafts fanning from the sun).
//
// This is a cheap analytic precursor to true froxel volumetrics (still deferred).
// It runs at half render-res: radial blur is inherently low-frequency, so the
// composite's linear upsample is smooth. Its knobs are exposed in the debug
// panel (density/decay/weight) because the look is scene-dependent; release bakes
// the DebugParams defaults (see Renderer::recordGodRaysPass).

layout(location = 0) in vec2 fragUV;

// set 0 = the G-buffer (reuses the deferred-lighting texture layout). Only depth
// is read here (to distinguish sky from occluding geometry).
layout(set = 0, binding = 0) uniform sampler2D gbAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbNormal;
layout(set = 0, binding = 2) uniform sampler2D gbMaterial;
layout(set = 0, binding = 3) uniform sampler2D gbDepth;

// set 1 = the lit HDR scene (the shaft "source" — bright sky/sun radiance).
layout(set = 1, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform GRParams {
    vec4 sunUV;   // xy = screen-space sun position, z = visibility [0,1], w = pad
    vec4 tune;    // x = density, y = decay, z = weight, w = intensity
} pc;

layout(location = 0) out vec4 outColor;

const int NUM_SAMPLES = 48;

void main() {
    float vis = pc.sunUV.z;
    if (vis <= 0.0) {  // sun off-screen / behind the camera — no shafts
        outColor = vec4(0.0);
        return;
    }
    vec2 sunUV = pc.sunUV.xy;

    // March from this pixel toward the sun, accumulating sky/sun brightness with
    // exponential decay; `density` scales how far the samples span toward the sun.
    vec2  delta = (fragUV - sunUV) * (pc.tune.x / float(NUM_SAMPLES));
    vec2  uv    = fragUV;
    float illum = 1.0;
    vec3  accum = vec3(0.0);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        uv -= delta;
        float d = texture(gbDepth, uv).r;
        // Emit only from sky pixels (depth ≈ far); geometry occludes → dark shafts.
        vec3 s = (d >= 0.9999) ? texture(hdrScene, uv).rgb : vec3(0.0);
        accum += s * illum * pc.tune.z;  // per-sample weight
        illum *= pc.tune.y;              // per-step decay
    }

    // Overall strength × on-screen visibility (soft-faded near the frame edge on
    // the CPU). Shafts carry the sky/sun colour already baked into the HDR source.
    outColor = vec4(accum * (pc.tune.w * vis), 1.0);
}
