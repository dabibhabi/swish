#version 450

// ── Road-spray billboard vertex shader (W9) ─────────────────────────────
// Reads the compute-simulated particle buffer (set 1, storage) and expands each
// LIVE particle into a camera-facing quad. No vertex/index buffers — 6 verts come
// from gl_VertexIndex, kSprayMaxParticles instances from gl_InstanceIndex. Dead
// particles collapse to a degenerate offscreen point (cheap cull).

// set 0 = camera UBO (same layout prefix as the other forward passes; only the
// view/proj are used here). Bound by the Renderer before the draw.
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
} camera;

struct Particle {
    vec4 posLife;  // xyz world pos (WU), w life [0,1]
    vec4 velSize;  // xyz velocity, w size (WU)
};
layout(set = 1, binding = 0) readonly buffer Particles { Particle particles[]; };

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vLife;
layout(location = 2) out float vSeed;  // stable per-particle random → brightness variation

float hash11(uint n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x00ffffffu) / float(0x01000000u);
}

void main() {
    Particle pt   = particles[gl_InstanceIndex];
    float    life = pt.posLife.w;

    // Two-triangle quad in [-1,1]².
    vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                             vec2(1.0, 1.0), vec2(-1.0, 1.0), vec2(-1.0, -1.0));
    vec2 c = corners[gl_VertexIndex];
    vUV    = c;
    vLife  = life;
    vSeed  = hash11(uint(gl_InstanceIndex) * 2654435761u);

    if (life <= 0.0) {  // dead → degenerate offscreen (skips the fragment work)
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    // Camera-facing basis = rows of the view rotation (world-space right / up).
    vec3  right = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    vec3  up    = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
    // Droplets grow as they age (life 1→0), so the plume disperses into a thinning
    // veil instead of staying a fixed-size clump.
    float grow  = 1.0 + (1.0 - life) * 1.6;
    float size  = pt.velSize.w * grow;
    vec3  world = pt.posLife.xyz + (c.x * right + c.y * up) * size;
    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
}
