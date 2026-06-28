#version 450

// Windshield rain — REFRACTION model (replaces the old additive placeholder).
//
// Drops are tiny lenses: we build a procedural drop HEIGHT FIELD in glass-space
// UV, take its NORMAL via finite differences, and use that normal to offset a
// lookup into a snapshot of the already-rendered HDR scene (bound as
// `sceneRefr`). No light is added — water refracts, it does not glow.
//
//   - drop field & wiper live in glass space (fragUV) so they stick to the pane
//   - the refraction lookup is in screen space (gl_FragCoord / screenSize)
//   - drops are confined to the forward-facing windshield via the local normal
//   - an analytic wiper sweep clears drops along its arc
//
// See issue.md and docs/rain/technique-refraction.md.

layout(location = 0) in vec2 fragUV;           // glass-space mesh UV
layout(location = 1) in vec3 fragLocalNormal;  // object space (nose = +X)
layout(location = 2) in vec3 fragNormal;       // world space (sun glint)
layout(location = 3) in vec3 fragWorldPos;     // world space (sun glint)

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
} camera;

layout(set = 1, binding = 0) uniform WindshieldRainUBO {
    vec4 flowAndTime;    // xy = flow dir (screen hint), z = speed [0,1], w = time
    vec4 params;         // x = wetness, y = intensity, z = drop density, w = refractStrength
    vec4 screenAndRefr;  // x = width, y = height, z = maxBlurLod (reserved), w = fresnelGain
    vec4 wiperState;     // x = blade angle, y = phase, z = enabled, w = speed
} rain;

layout(set = 1, binding = 1) uniform sampler2D sceneRefr;  // HDR scene snapshot
layout(set = 1, binding = 2) uniform sampler2D wetMap;     // persistent wetness map

layout(location = 0) out vec4 outColor;

// ── Hashes ──────────────────────────────────────────────────────────────
vec2 hash22(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// One layer of stick-slip drops: returns a height in [0,1] (1 at a drop core,
// 0 between drops). Drops cling, then suddenly slip along `flow` (sawtooth).
float dropLayer(vec2 uv, float scale, float seed, vec2 flow, float time) {
    vec2  p    = uv * scale;
    vec2  cell = floor(p);
    vec2  frac = fract(p);
    float minSD = 10.0;  // signed distance to nearest drop surface
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2  nb  = vec2(float(x), float(y));
            vec2  rnd = hash22(cell + nb + vec2(seed));
            float radius = mix(0.16, 0.40, rnd.x);          // per-drop size
            // Stick-slip: phase ramps; near the end the drop slips along flow.
            float spd   = 0.10 + 0.35 * rnd.y;
            float phase = fract(rnd.x * 0.37 + time * spd);
            float slip  = smoothstep(0.55, 1.0, phase);     // 0 stuck → 1 slipping
            vec2  center = rnd + flow * slip * 0.55;
            float d  = length(nb + center - frac);
            minSD = min(minSD, d - radius);
        }
    }
    // Inside (minSD<0) → ~1; tight body with a small soft rim.
    // (Well-defined smoothstep: edge0 < edge1, then invert.)
    return 1.0 - smoothstep(-0.05, 0.06, minSD);
}

// Combined drop height field (two scales for size variety).
float dropHeight(vec2 uv, vec2 flow, float time) {
    float density = rain.params.z;
    float h = dropLayer(uv, density,        0.0,  flow, time)
            + dropLayer(uv, density * 2.3,  17.0, flow, time) * 0.55;
    return h;
}

void main() {
    float wetness   = rain.params.x;
    float intensity = rain.params.y;
    if (wetness < 0.01 || intensity < 0.01) discard;

    // ── Confine to the forward-facing windshield pane ────────────────────
    // Mesh nose = +X, so the windshield's outward normal has a strong +X
    // component; side glass faces ±Z, rear faces −X. (If drops appear on the
    // wrong pane, flip the sign of `forwardness`.)
    float forwardness = fragLocalNormal.x;
    float front = smoothstep(0.12, 0.45, forwardness);
    if (front < 0.001) discard;

    float time  = rain.flowAndTime.w;
    float speed = rain.flowAndTime.z;

    // Glass-space flow: gravity pulls down at rest; aero pushes up at speed.
    vec2  gravity = vec2(0.0,  1.0);
    vec2  aero    = vec2(0.15, -1.0);
    // Crossover matches the CPU advection (terminal speedFactor ≈ 0.24): water
    // drifts down at rest and clearly races up near full throttle.
    float aeroBias = smoothstep(0.05, 0.20, speed);
    vec2  flow     = normalize(mix(gravity, aero, aeroBias));

    // ── Drop height field → surface normal (finite differences) ──────────
    const float e = 0.0016;
    float h  = dropHeight(fragUV, flow, time);
    float hx = dropHeight(fragUV + vec2(e, 0.0), flow, time);
    float hy = dropHeight(fragUV + vec2(0.0, e), flow, time);
    // Bounded slope → a well-conditioned drop normal (avoids blow-out from the
    // raw gradient magnitude). 0.03 scales the height gradient into a sane slope.
    vec2 slope = (vec2(hx, hy) - h) / e * 0.03;
    vec3 dropN = normalize(vec3(slope, 1.0));

    vec2 screenUV = gl_FragCoord.xy / rain.screenAndRefr.xy;

    // Gate drops by the persistent wetness map (windshield_wetness.frag): drops
    // appear only where the glass is wet, so the wiper — which clears the map —
    // genuinely wipes them off, and rain rebuilds them over time.
    float wmap     = texture(wetMap, screenUV).r;
    float coverage = clamp(h, 0.0, 1.0) * front * smoothstep(0.04, 0.45, wmap);

    float alpha = coverage * intensity;
    if (alpha < 0.004) discard;
    alpha = clamp(alpha, 0.0, 1.0);

    // ── Refraction: offset the scene-snapshot lookup along the drop normal ─
    vec2 uv = clamp(screenUV - dropN.xy * rain.params.w * coverage,
                    vec2(0.001), vec2(0.999));
    vec3 refracted = textureLod(sceneRefr, uv, 0.0).rgb;

    // ── Fresnel rim + small sun glint on the wet surface ──────────────────
    float fres = pow(1.0 - dropN.z, 3.0);               // 0 flat → 1 at the rim
    vec3  N = normalize(fragNormal);
    vec3  V = normalize(camera.camPos.xyz - fragWorldPos);
    vec3  L = normalize(camera.sunDir.xyz);
    vec3  H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 96.0) * camera.sunColor.a * 0.5;

    vec3 color = refracted
               + camera.sunColor.rgb * (fres * rain.screenAndRefr.w + spec * coverage);

    // Alpha-blended over the existing (clear) glass — only drops are visible.
    outColor = vec4(color, alpha);
}
