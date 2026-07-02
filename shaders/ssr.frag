#version 450

// ── Screen-space reflections (SSR) ────────────────────────────────────
// A dedicated post-lighting pass. For each fragment it reflects the view ray
// about the (depth-derived) surface normal and marches that ray through the
// depth buffer in view space; on a hit it samples the already-lit HDR scene as
// the reflected colour. The result is Fresnel-weighted (strongest at grazing
// angles — the wet-road / glossy look) and returned with a hit mask in alpha.
// The composite pass adds it on top of the HDR; on a miss nothing is added, so
// the split-sum sky IBL already baked into the HDR is the graceful fallback.
//
// This is intentionally a cheap, tunable approximation (linear view-space march,
// geometric stride growth, depth-derivative normals). Its knobs are exposed in
// the debug panel because SSR quality is scene-dependent and wants live tuning.

layout(location = 0) in vec2 fragUV;

// set 0 = G-buffer (reuses the deferred-lighting texture layout: albedo/normal/
// material/depth). Only material + depth are read here.
layout(set = 0, binding = 0) uniform sampler2D gbAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbNormal;
layout(set = 0, binding = 2) uniform sampler2D gbMaterial;  // r=metal g=rough b=wettable
layout(set = 0, binding = 3) uniform sampler2D gbDepth;

// set 1 = the lit HDR scene (colour sampled at the ray hit).
layout(set = 1, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform Params {
    mat4  proj;       // view → clip (project a march point to screen)
    mat4  invProj;    // clip → view (reconstruct view-space position from depth)
    float maxDist;    // max ray travel (view units / WU)
    float thickness;  // depth-intersection tolerance (WU)
    float stride;     // initial march step (WU)
    float intensity;  // reflection strength
} pc;

layout(location = 0) out vec4 outColor;

vec3 viewFromDepth(vec2 uv, float d) {
    vec4 c = pc.invProj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return c.xyz / c.w;
}

void main() {
    float d = texture(gbDepth, fragUV).r;
    if (d > 0.9999) {  // sky pixel — nothing to reflect from
        outColor = vec4(0.0);
        return;
    }

    vec3 P = viewFromDepth(fragUV, d);                 // view-space position (−Z forward)
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));       // geometric view-space normal
    vec3 V = normalize(-P);                            // surface → camera
    vec3 R = reflect(-V, N);                           // reflected ray (view space)

    // March the reflected ray, growing the step geometrically.
    float step   = pc.stride;
    vec3  rayPos = P;
    vec3  hitCol = vec3(0.0);
    float hit    = 0.0;
    vec2  hitUV  = vec2(0.0);
    const int STEPS = 40;
    for (int i = 0; i < STEPS; ++i) {
        rayPos += R * step;
        if (-rayPos.z > -P.z + pc.maxDist)
            break;  // travelled past the max reflection distance

        vec4 clip = pc.proj * vec4(rayPos, 1.0);
        if (clip.w <= 0.0)
            break;
        vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            break;  // ray left the screen — SSR can't resolve it (sky IBL covers it)

        float sd = texture(gbDepth, suv).r;
        if (sd > 0.9999) {  // sample is sky: keep marching
            step *= 1.4;
            continue;
        }
        vec3  sampleP   = viewFromDepth(suv, sd);
        float depthDiff = sampleP.z - rayPos.z;  // >0 ⇒ ray went behind the surface
        if (depthDiff > 0.0 && depthDiff < pc.thickness) {
            hitCol = texture(hdrScene, suv).rgb;
            hitUV  = suv;
            hit    = 1.0;
            break;
        }
        step *= 1.4;
    }

    // Grazing-weighted Fresnel — reflections read strongest at glancing angles.
    float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    // Fade toward screen edges of the hit so reflections don't pop at the border.
    vec2  edge = smoothstep(0.0, 0.15, hitUV) * smoothstep(0.0, 0.15, 1.0 - hitUV);
    float fade = hit * edge.x * edge.y;

    outColor = vec4(hitCol * fade * fres * pc.intensity, fade);
}
