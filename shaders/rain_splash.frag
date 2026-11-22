#version 450

// Rain impact splashes (debug-only draw, RainSystem). Picks the fragment's shape from the packed per-instance
// weights: a thin anti-aliased crown ring or a hard droplet disc. Additive (src=ONE, dst=ONE), same water
// colour as the rain streaks. Rings thinner than about a pixel fade out instead of aliasing into a filled disc.

layout(push_constant) uniform SplashPush {
    vec4 a;
    vec4 b;
    vec4 c;  // x = ring band width (uv)
    vec4 d;  // x = brightness
} pc;

layout(location = 0) in vec2      vUV;
layout(location = 1) flat in uint vFx;

layout(location = 0) out vec4 outColor;

const float kRingUV = 0.85;  // must match rain_splash.vert

void main() {
    vec4  fx   = unpackUnorm4x8(vFx);  // ring, dot, fade, variation
    float r    = length(vUV);
    float fw   = fwidth(r);
    float w    = max(pc.c.x, fw * 1.5);
    float big  = 1.0 - smoothstep(0.15, 0.4, fw);  // sub-pixel rings would fill in: drop them
    float ring = (1.0 - smoothstep(0.0, w, abs(r - kRingUV))) * big;
    float dotA = 1.0 - smoothstep(0.5, 1.0, r);

    float a = (fx.x * ring + fx.y * dotA) * fx.z * fx.w * pc.d.x;
    if (a < 0.002) discard;

    vec3 color = vec3(0.72, 0.82, 0.97);  // rain.frag's cool blue-white
    outColor   = vec4(color * a, a);
}
