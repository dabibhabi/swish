#version 450

// Windshield WETNESS MAP update — a fullscreen pass that maintains a persistent
// screen-space scalar "how wet is the glass here" field across frames.
//
//   - rain ADDS wetness over time (∝ intensity)
//   - wetness ADVECTS along the flow (down at rest, up at speed) — water streams
//     in the direction the windshield pushes it
//   - the WIPER subtracts along its swept band, so cleared glass stays clear and
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
    float wiperAngle;  // blade angle (rad)
    float wiperOn;     // 0/1
    float aspect;      // width / height
    float advect;      // upstream sample distance this frame (UV)
} p;

layout(location = 0) out float outWet;

// Screen-space wiper clear — a wide swept SECTOR, not a thin line. The blade
// arm is the radial segment at `angle`; we widen it into a real band so the
// cleared swath reads, and feather the radial ends so it sweeps a sector.
float wiperClear(vec2 uv, float angle, float aspect) {
    vec2  pivot = vec2(0.5, 0.92);
    vec2  rel   = (uv - pivot) * vec2(aspect, 1.0);
    vec2  dir   = vec2(sin(angle), -cos(angle));
    float t     = dot(rel, dir);
    float along = clamp(t, 0.0, 0.95);          // longer reach up the glass
    float d     = length(rel - dir * along);    // perpendicular distance to the arm
    // Wide band (≈0.13 of the glass) so the cleared swath is clearly visible,
    // and a soft radial cap so the swath fades out beyond the blade tip.
    float band  = 1.0 - smoothstep(0.04, 0.13, d);
    float reach = 1.0 - smoothstep(0.80, 0.98, t);
    return band * reach * step(0.0, t + 0.05);
}

void main() {
    // Semi-Lagrangian advection: pull wetness from upstream so the field drifts
    // along the flow (new(x) = old(x - flow·advect)).
    vec2  src = fragUV - p.flow * p.advect;
    float w   = texture(prevWet, src).r;

    w += p.intensity * p.dt * 0.7;   // SLOW rebuild → cleared glass stays clear ~1.4 s before re-beading
    w -= p.dt * 0.04;                // slow evaporation

    if (p.wiperOn > 0.5)             // wiper clears its swept band (persistent)
        w *= (1.0 - wiperClear(fragUV, p.wiperAngle, p.aspect));

    outWet = clamp(w, 0.0, 1.0);
}
