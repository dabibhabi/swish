#version 450

// Windshield rain — REFRACTION model, "Heartfelt" drop architecture.
//
// Ported/adapted from Martijn Steinrucken (BigWings) "Heartfelt"
// (https://www.shadertoy.com/view/ltffzl) into the engine's glass-space pass:
//
//   - drops live in glass space (fragUV) so they stick to the pane
//   - each drop uses a stick-slip Saw() motion: it clings, then suddenly slides
//   - a drop = a bright lens body + a wet trail above it + small beads in the wake
//   - three composited layers (static fresh impacts + two sliding scales) give size
//     variety; layer weights are gated by rain intensity (more rain → more drops)
//   - the composited height field → finite-difference normal → refraction lookup
//     into a snapshot of the rendered HDR scene (sceneRefr), in screen space
//   - foggy glass: a multi-tap blur of the scene where the glass is wet, with drop
//     bodies and trails staying sharp (water lenses cut clear paths through the fog)
//   - the persistent wetness map (windshield_wetness.frag) gates where drops appear,
//     so the wiper — which clears that map — genuinely wipes them off
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

// ── Hashes / helpers (BigWings) ───────────────────────────────────────────
float N(float t) {
    return fract(sin(t * 12345.564) * 7658.76);
}

float S(float a, float b, float t) {
    return smoothstep(a, b, t);
}

// Stick-slip sawtooth: slow rise (drop clings) then sharp fall (drop slides).
float Saw(float b, float t) {
    return S(0.0, b, t) * S(1.0, b, t);
}

vec3 N13(float p) {
    vec3 p3 = fract(vec3(p) * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract(vec3((p3.x + p3.y) * p3.z,
                      (p3.x + p3.z) * p3.y,
                      (p3.y + p3.z) * p3.x));
}

// ── Wiper blade (screen-space SDF) ──────────────────────────────────────────
// A real sweeping wiper arm: a thin dark capsule pivoting at (0.5, 0.92) in
// screen space (same pivot as the wetness clear band), plus a thin bright rubber
// edge so the blade reads as a solid arm rather than a smudge. Returns blade
// coverage in [0,1]. Pivot/aspect mirror windshield_wetness.frag::wiperClear.
float wiperBlade(vec2 uv, float angle, float aspect) {
    vec2  pivot = vec2(0.5, 0.92);
    vec2  rel   = (uv - pivot) * vec2(aspect, 1.0);
    vec2  dir   = vec2(sin(angle), -cos(angle));
    float t     = dot(rel, dir);                 // distance along the arm
    float along = clamp(t, 0.0, 0.9);            // arm reaches up the glass
    float d     = length(rel - dir * along);     // perpendicular distance to the arm
    // Solid arm: a thin capsule (≈0.018 wide) along the radial segment.
    float arm   = 1.0 - smoothstep(0.012, 0.022, d);
    float reach = step(0.0, t) * (1.0 - smoothstep(0.85, 0.92, t));
    return arm * reach;
}

// ── Drop layers ────────────────────────────────────────────────────────────
// A sliding-drop layer. Returns (dropHeight, trailMask). The drop body slides
// along +uv.y (the flow direction is applied by the caller via a rotated UV).
vec2 DropLayer2(vec2 uv, float t) {
    vec2 UV = uv;

    uv.y += t * 0.75;
    // More columns → more, SMALLER beads. a.x sets the across-flow cell count;
    // raising it shrinks each drop's lens body and packs more sliding beads in.
    vec2 a    = vec2(6.0, 1.0);
    vec2 grid = a * 2.0;
    vec2 id   = floor(uv * grid);

    float colShift = N(id.x);
    uv.y += colShift;

    id = floor(uv * grid);
    vec3 n  = N13(id.x * 35.2 + id.y * 2376.1);
    vec2 st = fract(uv * grid) - vec2(0.5, 0.0);

    float x = n.x - 0.5;

    float y      = UV.y * 20.0;
    float wiggle = sin(y + sin(y));
    x += wiggle * (0.5 - abs(x)) * (n.z - 0.5);
    x *= 0.7;

    float ti = fract(t + n.z);
    y = (Saw(0.85, ti) - 0.5) * 0.9 + 0.5;     // stick-slip vertical position
    vec2 p = vec2(x, y);

    float d        = length((st - p) * a.yx);
    float mainDrop = S(0.30, 0.0, d);           // round lens body — small, discrete bead

    float r          = sqrt(S(1.0, y, st.y));
    float cd         = abs(st.x - x);
    float trail      = S(0.25 * r, 0.14 * r * r, cd);  // rivulet — thinner so glass stays clear between
    float trailFront = S(-0.02, 0.02, st.y - y); // trail only ABOVE the drop
    trail *= trailFront * r * r;

    y = UV.y;
    float trail2   = S(0.2 * r, 0.0, cd);
    float droplets = max(0.0, (sin(y * (1.0 - y) * 120.0) - st.y)) * trail2 * trailFront * n.z;
    y = fract(y * 10.0) + (st.y - 0.5);
    float dd = length(st - vec2(x, y));
    droplets = S(0.22, 0.0, dd);                // beads left in the wake (fine)

    float m = mainDrop + droplets * r * trailFront;
    return vec2(m, trail);
}

// Tiny static drops — fresh impacts that pop in (birth) and slowly fade before
// they start sliding. The Saw() fade gives each a quick "landed" transient.
float StaticDrops(vec2 uv, float t) {
    uv *= 55.0;  // MANY small "impact" beads — fresh rain hitting the glass (denser when raining hard)

    vec2 id = floor(uv);
    uv = fract(uv) - 0.5;
    vec3 n = N13(id.x * 107.45 + id.y * 3543.654);
    vec2 p = (n.xy - 0.5) * 0.7;
    float d = length(uv - p);

    float fade = Saw(0.025, fract(t + n.z));    // rapid birth → slow fade
    float c    = S(0.28, 0.0, d) * fract(n.z * 10.0) * fade;
    return c;
}

// Composite the three drop layers. l0/l1/l2 are intensity-derived weights.
vec2 Drops(vec2 uv, float t, float l0, float l1, float l2) {
    float s  = StaticDrops(uv, t) * l0;
    vec2  m1 = DropLayer2(uv, t) * l1;
    vec2  m2 = DropLayer2(uv * 1.9, t) * l2;  // finer scale → smaller second-layer beads

    // Combine layers with max(), not addition: fine layers add STRUCTURE, not
    // thickness. Summing stacks the three layers into a heavy, milky coverage
    // (the "too thick" look); max keeps each drop crisp and delicate.
    float c = max(s, max(m1.x, m2.x));
    // Tighter contrast band: drop cores read, the glass BETWEEN beads stays near 0
    // so the pane is mostly see-through even at heavy rain (no uniform gray sheet).
    c = S(0.35, 0.62, c);
    // Gate both trail layers through the SLIDING weights (l1/l2), not the
    // static-dot weight, so rivulet trails track rain intensity directly and
    // are no longer throttled by (the now-damped) l0.
    return vec2(c, max(m1.y * l1, m2.y * l2));
}

// Evaluate the drop field at a glass-space UV, sliding along `flow`.
vec2 dropField(vec2 meshUV, float t, float l0, float l1, float l2, vec2 flow) {
    vec2 f    = normalize(flow);
    vec2 perp = vec2(-f.y, f.x);
    // Rotate into flow space: x = across flow, y = along flow ("down").
    vec2 fuv = vec2(dot(meshUV, perp), dot(meshUV, f));
    return Drops(fuv, t, l0, l1, l2);
}

// 9-tap blur of the scene snapshot (no mips on sceneRefr, so tap by UV offset).
vec3 blurScene(vec2 uv, float r) {
    if (r < 0.0002) return textureLod(sceneRefr, uv, 0.0).rgb;

    vec2 o = vec2(r);
    vec3 sum = textureLod(sceneRefr, uv, 0.0).rgb * 0.25;
    sum += textureLod(sceneRefr, uv + vec2( o.x, 0.0), 0.0).rgb * 0.125;
    sum += textureLod(sceneRefr, uv + vec2(-o.x, 0.0), 0.0).rgb * 0.125;
    sum += textureLod(sceneRefr, uv + vec2(0.0,  o.y), 0.0).rgb * 0.125;
    sum += textureLod(sceneRefr, uv + vec2(0.0, -o.y), 0.0).rgb * 0.125;
    sum += textureLod(sceneRefr, uv + vec2( o.x,  o.y), 0.0).rgb * 0.0625;
    sum += textureLod(sceneRefr, uv + vec2(-o.x,  o.y), 0.0).rgb * 0.0625;
    sum += textureLod(sceneRefr, uv + vec2( o.x, -o.y), 0.0).rgb * 0.0625;
    sum += textureLod(sceneRefr, uv + vec2(-o.x, -o.y), 0.0).rgb * 0.0625;
    return sum;
}

void main() {
    float wetness   = rain.params.x;
    float intensity = rain.params.y;
    if (wetness < 0.01 && intensity < 0.01) discard;

    // ── Confine to the forward-facing windshield pane ────────────────────
    // Mesh nose = +X, so the windshield's outward normal has a strong +X
    // component; side glass faces ±Z, rear faces −X. (If drops appear on the
    // wrong pane, flip the sign of `forwardness`.)
    float forwardness = fragLocalNormal.x;
    float front = smoothstep(0.30, 0.60, forwardness);
    if (front < 0.001) discard;

    float time  = rain.flowAndTime.w;
    float speed = rain.flowAndTime.z;

    // Glass-space flow: gravity pulls DOWN at rest; aero pushes UP at speed.
    // NOTE: this windshield's mesh-UV V axis is INVERTED vs the naive screen
    // assumption — empirically a +V flow slides drops visually UP. So gravity
    // must point along −V (and aero along +V) to make at-rest drops fall DOWN.
    vec2  gravity = vec2(0.0,  -1.0);
    vec2  aero    = vec2(0.15,  1.0);
    // Crossover lifted to a higher band: water only streams UP at genuinely high
    // speed, not a gentle cruise (kept consistent with the CPU wetness advect).
    float aeroBias = smoothstep(0.25, 0.60, speed);
    vec2  flow     = normalize(mix(gravity, aero, aeroBias));

    // Drops slide faster the faster the car goes.
    float t = time * (0.18 + 0.5 * speed);

    // Intensity-derived layer weights — more rain reveals more/denser drops.
    // Static dots are damped (×0.65) and ramped to track rain instead of
    // saturating, so they no longer swamp the sliding rivulets; the two sliding
    // layers ramp in much earlier so clinging→sliding trails read at light rain.
    float rainAmount = intensity;
    // Static "impact" beads scale strongly with rain: sparse at light rain, but a
    // dense spray of fresh drops hitting the glass when it's raining hard.
    float l0 = S(0.15, 0.7, rainAmount) * 0.95;  // static impact dots (heavy-rain spray)
    float l1 = S(0.05, 0.45, rainAmount);         // sliding layer 1 (early ramp)
    float l2 = S(0.0,  0.40, rainAmount);         // sliding layer 2 (early ramp)

    // ── Drop + trail height field → surface normal (finite differences) ──
    // Fold the wet trail (fld.y) INTO the height field so rivulets get a real
    // lens normal and refract/glint like the drop bodies. Previously the trail
    // only reduced blur, so the clinging→sliding rivulets were invisible.
    const float e = 0.0016;
    vec2  f0 = dropField(fragUV,                t, l0, l1, l2, flow);
    vec2  fx = dropField(fragUV + vec2(e, 0.0), t, l0, l1, l2, flow);
    vec2  fy = dropField(fragUV + vec2(0.0, e), t, l0, l1, l2, flow);

    float trail = f0.y;
    float h  = clamp(f0.x + f0.y * 0.35, 0.0, 1.0);  // body + thin rivulet (kept discrete)
    float hx = clamp(fx.x + fx.y * 0.35, 0.0, 1.0);
    float hy = clamp(fy.x + fy.y * 0.35, 0.0, 1.0);

    // Bounded slope → a well-conditioned drop normal. 0.03 scales the height
    // gradient into a sane slope (avoids blow-out from the raw gradient).
    vec2 slope = (vec2(hx, hy) - h) / e * 0.03;
    vec3 dropN = normalize(vec3(slope, 1.0));

    vec2 screenUV = gl_FragCoord.xy / rain.screenAndRefr.xy;

    // Gate drops by the persistent wetness map: drops appear only where the
    // glass is wet, so the wiper (which clears the map) wipes them off and rain
    // rebuilds them over time. Lower threshold = drops appear soon after rain.
    float wmap    = texture(wetMap, screenUV).r;
    float wetGate = smoothstep(0.02, 0.20, wmap);

    // Drop coverage now includes rivulets (folded into h above).
    float coverage = clamp(h, 0.0, 1.0) * front * wetGate;

    // Foggy glass: a VERY light base haze wherever the glass is wet — kept minimal
    // so the road/lights read through the clear glass between beads (the beads and
    // rivulets carry the look, not a flat gray wash). Halved vs before to avoid the
    // uniform gray veil at heavy rain.
    float clarity = clamp(h, 0.0, 1.0);
    float haze    = smoothstep(0.12, 0.60, wmap) * front * (0.018 + 0.012 * intensity);

    // ── Wiper blade — a real dark arm sweeping across the pane ────────────
    // Drawn only when the wiper is enabled (wiperState.z), gated to the front
    // pane. The blade is nearly opaque so it reads as a solid arm; it overrides
    // the drop alpha where present.
    float blade = (rain.wiperState.z > 0.5)
                ? wiperBlade(screenUV, rain.wiperState.x, rain.screenAndRefr.x / rain.screenAndRefr.y) * front
                : 0.0;

    // Keep the glass SEMI-TRANSPARENT — the refracted scene must always read
    // through, or drops pile into a milky opaque veil. Cap well below 1.0.
    // The opaque blade is added on top and lifts alpha toward its own value.
    float alpha = clamp(coverage * 0.9 + haze, 0.0, 0.62);
    alpha = max(alpha, blade * 0.88);
    if (alpha < 0.004) discard;

    // ── Refraction: offset the scene-snapshot lookup along the drop normal ─
    vec2 baseUV = clamp(screenUV - dropN.xy * rain.params.w * coverage,
                        vec2(0.001), vec2(0.999));

    // Blur radius: a light haze-blur in the wet glass, ~0 through drops/rivulets.
    // Reduced (0.006 → 0.0025) so the road/lights stay readable through the clear
    // glass between beads instead of smearing into a gray sheet.
    float blurR    = mix(0.0025, 0.0, clarity) * wetness;
    vec3  refracted = blurScene(baseUV, blurR);

    // Water darkens what it refracts — gives the lens body contrast against a
    // dull overcast sky so the bead reads as a glass droplet, not a ring.
    refracted *= mix(1.0, 0.85, coverage);

    // ── Per-drop specular glint — the dominant "wet droplet" cue ──────────
    // Use the PER-DROP normal (not the macro glass normal) against a fixed
    // overhead sky light, so every bead and rivulet catches a tight highlight
    // even under overcast lighting where the sun glint is weak. The highlight
    // lands on the upper-left flank of each dome (where the slope faces skyL).
    vec3  skyL   = normalize(vec3(-0.25, -0.55, 1.0));
    vec3  skyCol = vec3(0.92, 0.96, 1.0);
    float glint  = pow(max(dot(dropN, skyL), 0.0), 30.0) * coverage;  // tight point

    // Fresnel rim — the lens edge. Secondary now that the glint carries the read.
    float fres = pow(1.0 - dropN.z, 3.0);               // 0 flat → 1 at the rim

    // Sun glint retained but secondary (overcast → weak), per-drop now.
    vec3  Vw  = normalize(camera.camPos.xyz - fragWorldPos);
    vec3  Lw  = normalize(camera.sunDir.xyz);
    vec3  Hw  = normalize(Vw + Lw);
    float sun = pow(max(dot(dropN, Hw), 0.0), 96.0) * camera.sunColor.a * 0.5 * coverage;

    vec3 color = refracted
               + skyCol * (glint * 1.25)
               + camera.sunColor.rgb * (fres * rain.screenAndRefr.w + sun);

    // Composite the wiper blade on top: a dark rubber arm with a faint lit edge.
    // Mixing toward near-black (not pure black) keeps it reading as a glossy blade.
    color = mix(color, vec3(0.02, 0.02, 0.025), blade);
    color += skyCol * (blade * 0.06);  // faint sheen along the rubber edge

    // Alpha-blended over the existing (clear) glass.
    outColor = vec4(color, alpha);
}
