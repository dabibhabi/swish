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
    // Sharpened radial falloff: raising the old smoothstep to a power concentrates
    // brightness into a small defined core and drops the mid/outer radius off much
    // faster, so a droplet reads as a bright dot with a thin edge instead of one
    // wide soft gaussian haze (the peak at r=0 is unchanged — only the shoulder
    // steepens).
    float soft    = pow(smoothstep(1.0, 0.0, r), 2.4);
    float fadeIn  = smoothstep(1.0, 0.82, vLife);   // ramp up just after spawn (no pop-in)
    float fadeOut = smoothstep(0.0, 0.30, vLife);   // fade out near death
    float pv      = 0.25 + 0.55 * vSeed;            // per-particle brightness → stochastic, not uniform
    float a       = soft * fadeIn * fadeOut * pv * pc.tune.x;
    // Pulled slightly below pure white so several overlapping additive cores have
    // headroom before they clip flat white.
    vec3  mist    = vec3(0.72, 0.78, 0.88);         // cool water-white (droplets scatter light)
    outColor      = vec4(mist * a, a);              // additive (composite adds rgb)
}
