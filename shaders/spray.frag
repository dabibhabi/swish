#version 450

// ── Road-spray billboard fragment shader (W9) ───────────────────────────
// Soft round additive sprite; alpha fades as the particle ages out. Colour is a
// cool water-mist white. Additive blend + no depth write → draw order is
// irrelevant. Overall strength comes from the pushed opacity (debug slider).

layout(location = 0) in vec2  vUV;
layout(location = 1) in float vLife;
layout(location = 2) in float vSeed;
#ifdef SWISH_DEBUG_UI
layout(location = 3) flat in float vMist;
#endif

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
#ifdef SWISH_DEBUG_UI
    // Spray v2: a droplet is a small hard bright core (reads as a water streak once stretched); mist is a very faint
    // wide gaussian. Additive, so the mist only lifts the background slightly, as in the reference photos.
    if (vMist < 0.5) {
        float core = 1.0 - smoothstep(0.25, 0.9, r);
        float fade = smoothstep(0.0, 0.25, vLife);
        float a    = core * fade * (0.5 + 0.5 * vSeed) * pc.tune.x * 0.9;
        outColor   = vec4(vec3(0.80, 0.84, 0.90) * a, a);
        return;
    }
    {
        float g    = exp(-3.0 * r * r);
        float fade = smoothstep(1.0, 0.7, vLife) * smoothstep(0.0, 0.5, vLife);
        float a    = g * fade * pc.tune.x * 0.3;
        outColor   = vec4(vec3(0.70, 0.74, 0.80) * a, a);
        return;
    }
#endif
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
