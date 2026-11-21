#version 450

// Rain impact splashes (debug-only draw, RainSystem). No buffers: every instance is a stateless function of
// its world cell, a stable hash and the rain time (the per-instance phase idea from "burn to Gaussians").
//
// The field is a G x G grid of world-snapped cells around the camera. Each cell fires once per period at a
// hashed offset, with a hashed spawn chance, so a splash stays fixed in the world while the car drives.
// Instance = cell * 5 + sub: sub 0 is the crown ring lying flat on the road plane, subs 1..4 are droplet
// billboards on short ballistic arcs. The phase weights reach the fragment shader packed into one flat uint:
// x = ring weight, y = dot weight, z = fade, w = per-splash brightness variation.

#include "camera.glsl"
#include "hash11.glsl"

layout(push_constant) uniform SplashPush {
    vec4 a;  // x = rain time (s), y = road plane Y (WU), z = cell size (WU), w = spawn chance (intensity * density)
    vec4 b;  // x = period (s), y = life (s), z = ring radius (WU), w = ring growth tau (s)
    vec4 c;  // x = ring band width (uv), y = droplet count, z = droplet speed (WU/s), w = droplet size (WU)
    vec4 d;  // x = brightness, y = grid size G (cells per side), zw = unused
} pc;

layout(location = 0) out vec2 vUV;       // quad corner in [-1, 1]
layout(location = 1) flat out uint vFx;  // packUnorm4x8(ring, dot, fade, variation)

const uint  kSubs      = 5u;
const float kRingUV    = 0.85;              // ring sits at |uv| = 0.85 so the band fits inside the quad
const float kGravityWU = 9.81 * 1000.0;     // WU/s^2
const float kDropWeight = 0.35;             // droplets are secondary: the crown ring is what reads as a splash
const float kRingWeight = 0.5;              // crowns on wet asphalt are faint, a few cm across (not white circles)
const float kRingLift   = 15.0;             // WU (1.5 cm) above the plane: a ring exactly on the road z-fights into arcs

const vec2 kCorners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                vec2(1.0, 1.0), vec2(-1.0, 1.0), vec2(-1.0, -1.0));

void cull() {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);  // degenerate offscreen (skips the fragment work)
    vUV         = vec2(0.0);
    vFx         = 0u;
}

void main() {
    uint cell = uint(gl_InstanceIndex) / kSubs;
    uint sub  = uint(gl_InstanceIndex) % kSubs;
    int  G    = int(pc.d.y);
    float cs  = pc.a.z;

    ivec2 base = ivec2(floor(camera.camPos.xz / cs));
    ivec2 wc   = base + ivec2(int(cell) % G - G / 2, int(cell) / G - G / 2);
    uint  key  = (uint(wc.x) * 73856093u) ^ (uint(wc.y) * 19349663u);

    // Time since this cell last fired, and which firing cycle it is in.
    float period = pc.b.x;
    float life   = pc.b.y;
    float tc     = pc.a.x / period + hash11(key);
    float tl     = fract(tc) * period;
    uint  ck     = key ^ (uint(floor(tc)) * 2654435761u);
    if (hash11(ck) >= pc.a.w || tl >= life) { cull(); return; }

    vec2  jitter = vec2(hash11(ck + 1u), hash11(ck + 2u));
    vec3  centre = vec3((float(wc.x) + jitter.x) * cs, pc.a.y, (float(wc.y) + jitter.y) * cs);
    float k      = tl / life;
    float fade   = 1.0 - smoothstep(0.55, 1.0, k);
    float vary   = 0.5 + 0.5 * hash11(ck + 3u);
    vec2  c      = kCorners[gl_VertexIndex];
    vUV          = c;

    if (sub == 0u) {
        // Crown ring: grows fast and slows, r = R (1 - e^{-t/tau}), lying flat on the road.
        float r     = pc.b.z * (1.0 - exp(-tl / pc.b.w));
        float half_ = max(r, 1.0) / kRingUV;
        vec3  world = centre + vec3(c.x * half_, kRingLift, c.y * half_);
        gl_Position = camera.proj * camera.view * vec4(world, 1.0);
        vFx         = packUnorm4x8(vec4(kRingWeight, 0.0, fade, vary));
        return;
    }

    if (float(sub) > pc.c.y) { cull(); return; }

    // Droplet: hashed direction, thrown up and out, ballistic fall back to the road.
    uint  dk    = ck ^ (sub * 0x9E3779B9u);
    float ang   = 6.2831853 * hash11(dk);
    float v0    = pc.c.z * (0.6 + 0.6 * hash11(dk + 1u));
    vec3  dir   = vec3(cos(ang), 0.0, sin(ang));
    vec3  pos   = centre + dir * (0.5 * v0 * tl) + vec3(0.0, v0 * tl - 0.5 * kGravityWU * tl * tl, 0.0);
    if (pos.y < pc.a.y && tl > 0.02) { cull(); return; }  // landed

    vec4 vs     = camera.view * vec4(pos, 1.0);
    vs.xy      += c * pc.c.w;
    gl_Position = camera.proj * vs;
    vFx         = packUnorm4x8(vec4(0.0, kDropWeight, fade, vary));
}
