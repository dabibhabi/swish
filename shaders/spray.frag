#version 450

// ── Road-spray billboard fragment shader (W9) ───────────────────────────
// Soft round additive sprite; alpha fades as the particle ages out. Colour is a
// cool water-mist white. Additive blend + no depth write → draw order is
// irrelevant. Overall strength comes from the pushed opacity (debug slider).

layout(location = 0) in vec2  vUV;
layout(location = 1) in float vLife;
layout(location = 2) in float vSeed;

layout(push_constant) uniform Push {
    vec4 tune;  // x = opacity
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vUV);
    if (r >= 1.0 || vLife <= 0.0)
        discard;
    float soft    = smoothstep(1.0, 0.0, r);        // round radial falloff
    float fadeIn  = smoothstep(1.0, 0.82, vLife);   // ramp up just after spawn (no pop-in)
    float fadeOut = smoothstep(0.0, 0.30, vLife);   // fade out near death
    float pv      = 0.30 + 0.70 * vSeed;            // per-particle brightness → stochastic, not uniform
    float a       = soft * fadeIn * fadeOut * pv * pc.tune.x;
    vec3  mist    = vec3(0.85, 0.88, 0.93);         // bright cool water-white (droplets scatter light)
    outColor      = vec4(mist * a, a);              // additive (composite adds rgb)
}
