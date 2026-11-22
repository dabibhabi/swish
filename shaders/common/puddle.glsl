// puddle.glsl -- world-space road puddle mask, shared by lighting.frag and ssr.frag so
// SSR reflects the scene in exactly the pools the wet model turns to mirrors. A C++
// mirror exists for the wet-grip physics (src/scene/Entity/CarPhysics.h /
// CarEntity.cpp): any change to the hash, scales, weights or smoothstep here MUST be
// mirrored there (and vice versa).
// Static low-frequency pools evaluated in WORLD space; `coverage` raises the water level
// (more coverage lowers the threshold -> bigger pools). Gated to the road tag (road >= 0.5,
// gbMaterial.a from MAT_ASPHALT). 1 m = 1000 WU.
#ifndef SWISH_PUDDLE_GLSL
#define SWISH_PUDDLE_GLSL
float puddleHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float puddleNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = puddleHash(i),                  b = puddleHash(i + vec2(1.0, 0.0));
    float c = puddleHash(i + vec2(0.0, 1.0)), d = puddleHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float puddleAmount(vec2 worldXZ, float coverage, float road) {
    if (coverage <= 0.0 || road < 0.5) return 0.0;
    vec2  p = worldXZ * 0.00013;  // ~7.7 m puddle cells
    float n = puddleNoise(p) * 0.65 + puddleNoise(p * 2.7 + 11.0) * 0.35;
    return smoothstep(1.0 - coverage, 1.0 - coverage + 0.12, n);  // soft shoreline
}
#endif
