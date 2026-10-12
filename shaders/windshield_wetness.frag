#version 450

// Windshield WETNESS MAP update — a fullscreen pass that maintains a persistent
// screen-space scalar "how wet is the glass here" field across frames.
//
//   - rain ADDS wetness over time (∝ intensity)
//   - wetness ADVECTS along the flow (down at rest, up at speed) — water streams
//     in the direction the windshield pushes it
//   - the WIPER clears what its blades swept, so cleared glass stays clear and
//     only re-wets as rain rebuilds it
//   - slow evaporation drains it
//
// Read by windshield_rain.frag to gate where drops appear. Ping-pong: this reads
// the previous map (set 0) and writes the next; the CPU copies result → history.

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D prevWet;

layout(push_constant) uniform Params {
    vec2  flow;        // screen-space flow dir (down at rest → up at speed)
    float intensity;   // rain intensity [0,1]
    float dt;          // seconds since last frame
    float wiperOn;     // 0/1: a wiper mode is on or a blade is still parking
    float aspect;      // width / height
    float advect;      // upstream sample distance this frame (UV)
    float bladeCount;  // blades in prevSeg / curSeg (0..2)
    vec4  band;        // xy = clear-band feather radii around the blade (aspect-corrected UV)
    vec4  prevSeg[2];  // per blade: xy = inner end, zw = tip (screen UV) at LAST frame's angle
    vec4  curSeg[2];   // ... at THIS frame's angle
} p;

layout(location = 0) out float outWet;

// Screen-space wiper clear. The CPU projects each blade (the wiped part of the arm, from its
// inner end to its tip) onto the screen at last frame's and this frame's wiper angle
// (WindshieldRainPass::update_wiper): for the rigged car from its 3D Wiper_* arms, for the
// default car from the analytic screen-space blade. This clears the quad the blade swept
// since last frame plus a feathered band around the blade itself, so the cleared glass is
// exactly where the blade has been, at any frame rate. Distances are aspect-corrected.
float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

// 1 inside triangle abc (either winding), else 0.
float inTri(vec2 q, vec2 a, vec2 b, vec2 c) {
    float d0 = cross2(b - a, q - a);
    float d1 = cross2(c - b, q - b);
    float d2 = cross2(a - c, q - c);
    bool  neg = d0 < 0.0 || d1 < 0.0 || d2 < 0.0;
    bool  pos = d0 > 0.0 || d1 > 0.0 || d2 > 0.0;
    return (neg && pos) ? 0.0 : 1.0;
}

float segDist(vec2 q, vec2 a, vec2 b) {
    vec2  ab = b - a;
    float t  = clamp(dot(q - a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
    return length(q - a - ab * t);
}

float wiperClear(vec2 uv) {
    vec2  k     = vec2(p.aspect, 1.0);
    vec2  q     = uv * k;
    float clear = 0.0;
    for (int i = 0; i < 2; i++) {
        if (float(i) >= p.bladeCount)
            break;
        vec2 a0 = p.prevSeg[i].xy * k, b0 = p.prevSeg[i].zw * k;
        vec2 a1 = p.curSeg[i].xy * k, b1 = p.curSeg[i].zw * k;
        float swept = max(inTri(q, a0, b0, b1), inTri(q, a0, b1, a1));
        float blade = 1.0 - smoothstep(p.band.x, p.band.y, segDist(q, a1, b1));
        clear = max(clear, max(swept, blade));
    }
    return clear;
}

void main() {
    // Semi-Lagrangian advection: pull wetness from upstream so the field drifts
    // along the flow (new(x) = old(x - flow·advect)).
    vec2  src = fragUV - p.flow * p.advect;
    float w   = texture(prevWet, src).r;

    w += p.intensity * p.dt * 0.7;   // SLOW rebuild → cleared glass stays clear ~1.4 s before re-beading
    w -= p.dt * 0.04;                // slow evaporation

    if (p.wiperOn > 0.5)             // the blades clear what they swept (persistent)
        w *= (1.0 - wiperClear(fragUV));

    outWet = clamp(w, 0.0, 1.0);
}
