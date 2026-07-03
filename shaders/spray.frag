#version 450

// ── Road-spray billboard fragment shader (W9) ───────────────────────────
// Soft round additive sprite; alpha fades as the particle ages out. Colour is a
// cool water-mist white. Additive blend + no depth write → draw order is
// irrelevant. Overall strength comes from the pushed opacity (debug slider).

layout(location = 0) in vec2  vUV;
layout(location = 1) in float vLife;

layout(push_constant) uniform Push {
    vec4 tune;  // x = opacity
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vUV);
    if (r >= 1.0 || vLife <= 0.0)
        discard;
    float soft = smoothstep(1.0, 0.0, r);       // round radial falloff
    float fade = smoothstep(0.0, 0.25, vLife);  // fade out near death
    float a    = soft * fade * pc.tune.x;
    vec3  mist = vec3(0.82, 0.87, 0.95);         // cool water-white
    outColor   = vec4(mist * a, a);              // additive (composite adds rgb)
}
