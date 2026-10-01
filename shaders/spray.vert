#version 450

// ── Road-spray billboard vertex shader (W9) ─────────────────────────────
// Reads the compute-simulated particle buffer (set 1, storage) and expands each
// LIVE particle into a camera-facing quad. No vertex/index buffers — 6 verts come
// from gl_VertexIndex, kSprayMaxParticles instances from gl_InstanceIndex. Dead
// particles collapse to a degenerate offscreen point (cheap cull).

// set 0 = camera UBO (same layout prefix as the other forward passes; only the
// view/proj are used here). Bound by the Renderer before the draw.
#include "camera.glsl"

struct Particle {
    vec4 posLife;  // xyz world pos (WU), w life [0,1]
    vec4 velSize;  // xyz velocity, w size (WU)
};
layout(set = 1, binding = 0) readonly buffer Particles { Particle particles[]; };

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vLife;
layout(location = 2) out float vSeed;  // stable per-particle random → brightness variation

#include "hash11.glsl"

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

    // Droplets shrink (not balloon) as they age — the old 2.6x growth was literally
    // how a sprite becomes a soft overlapping disc; a defined droplet should hold
    // its size or thin out, never bloom.
    float shrink = mix(1.0, 0.5, 1.0 - life);
    float size   = pt.velSize.w * shrink;

    // Velocity-aligned stretch: project world velocity onto the camera-facing
    // (right, up) plane to get the on-screen travel axis, then draw the quad
    // elongated along it and pinched across it — a fast droplet becomes a short
    // streak instead of a round camera-facing disc. The quad's footprint scales as
    // stretch*pinch, so pinch = 1/stretch is what actually holds area (and additive
    // energy) constant — 1/sqrt(stretch) would leave it growing as sqrt(stretch),
    // i.e. ~73% larger at the stretch=3 cap, reintroducing the ballooning this
    // change exists to remove.
    float vRight    = dot(pt.velSize.xyz, right);
    float vUp       = dot(pt.velSize.xyz, up);
    float screenSpd = length(vec2(vRight, vUp));
    vec3  sDir      = (screenSpd > 1.0) ? (right * vRight + up * vUp) / screenSpd : up;
    vec3  sPerp     = (screenSpd > 1.0) ? (right * -vUp + up * vRight) / screenSpd : right;
    float stretch   = 1.0 + clamp(screenSpd / 3500.0, 0.0, 2.0);
    float pinch     = 1.0 / stretch;

    vec3  world = pt.posLife.xyz + (c.x * sPerp * pinch + c.y * sDir * stretch) * size;
    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
}
