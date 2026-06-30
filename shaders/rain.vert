#version 450

// Rain billboard vertex shader.
// Each instance is one rain streak. The vertex shader generates a camera-facing
// billboard quad aligned with the rain fall direction, entirely on the GPU.
// No per-frame instance-buffer updates — all motion comes from time + seed.

// ── Vertex inputs ─────────────────────────────────────────────────────
layout(location = 0) in vec2 localPos;  // billboard corner: x in [-0.5,0.5], y in [0,1]
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 seed;     // xyz = position seed [0,1], w = size/alpha variation [0,1]

// ── Descriptor sets ───────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;  // xyz = camera position, w = wetness
    vec4 sunDir;
    vec4 sunColor;
} camera;

layout(set = 1, binding = 0) uniform RainUBO {
    vec4 windAndTime;  // xyz = wind (WU/s), w = time
    vec4 params;       // x = intensity, y = streakLen (WU), z = dropSpeed (WU/s), w = halfExtent (WU)
} rain;

// ── Outputs ───────────────────────────────────────────────────────────
layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragAlpha;
layout(location = 2) out float fragPhase;

void main() {
    float time     = rain.windAndTime.w;
    vec3  wind     = rain.windAndTime.xyz;
    float intensity = rain.params.x;
    float streakLen = rain.params.y;
    float dropSpeed = rain.params.z;
    float halfExt   = rain.params.w;

    // Rain falls downward (−Y) plus wind drift
    vec3 vel = vec3(wind.x, -dropSpeed, wind.z);

    // Animate the seed position within the rain volume box.
    // The box is centered on the camera and wraps around it.
    vec3 seedOffset = (seed.xyz * 2.0 - 1.0) * halfExt;
    vec3 rawOffset  = seedOffset + vel * time;

    // Wrap offset into [−halfExt, +halfExt] so drops recycle seamlessly
    vec3 wrappedOffset = mod(rawOffset + halfExt, halfExt * 2.0) - halfExt;
    vec3 worldPos      = camera.camPos.xyz + wrappedOffset;

    // Transform drop center to view space
    vec4 vsCenter = camera.view * vec4(worldPos, 1.0);

    // Build camera-facing billboard aligned with the view-space fall direction.
    vec3 vsVelDir = normalize((camera.view * vec4(vel, 0.0)).xyz);
    vec3 camFwd   = vec3(0.0, 0.0, -1.0);

    // Perpendicular axis for billboard width; use X fallback if nearly parallel
    vec3 vsPerp;
    if (abs(dot(vsVelDir, camFwd)) < 0.99) {
        vsPerp = normalize(cross(vsVelDir, camFwd));
    } else {
        vsPerp = vec3(1.0, 0.0, 0.0);
    }

    // Width: 3–8 WU, varied per instance. Thin streaks read as delicate rain
    // (Garg & Nayar) rather than fat bright bars.
    float width = (3.0 + 5.0 * seed.w);
    float len   = streakLen * (0.5 + 0.5 * seed.w);

    // localPos.x spans perp (width), localPos.y spans fall (length)
    // Subtract vsVelDir so the streak trails BEHIND the drop head:
    // fragUV.y=0 is the leading head, fragUV.y=1 is the trailing tail.
    vec3 vsCorner = vsCenter.xyz
                  + vsPerp * (localPos.x * width)
                  - vsVelDir * (localPos.y * len);

    // Fade out drops inside the cabin bubble so rain does not render inside the
    // car. The volume is centered on the (in-cabin) camera, so the nearest shell
    // of drops spawns in the cabin air in front of the dashboard; depth only
    // rejects rain BEHIND opaque surfaces, not these near drops. View-space
    // distance from the camera (origin in view space) gates them out.
    float camDist  = length(vsCenter.xyz);
    float nearFade = smoothstep(2500.0, 5000.0, camDist);  // 2.5 m → 5 m

    gl_Position = camera.proj * vec4(vsCorner, 1.0);
    fragUV      = uv;
    fragAlpha   = intensity * (0.20 + 0.60 * seed.w) * nearFade;  // semi-transparent
    fragPhase   = seed.x;
}
