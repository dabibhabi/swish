#version 450

// Glass fragment shader — Fresnel-driven transmission/reflection + atmospheric
// sun disc. Blended with SRC_ALPHA / ONE_MINUS_SRC_ALPHA onto the HDR buffer.
//
// Physical shape: real glass at near-normal incidence transmits ~96% of the
// light behind it (only ~4% reflects, per the dielectric Fresnel term at
// F0=0.04); reflectance rises toward 1.0 only at grazing angles. Alpha below
// follows that curve, so straight-on the windshield lets the already-correct
// exterior scene (lighting.frag's deferred output, drawn on the HDR buffer
// just before this pass) show through almost unmodified — that scene already
// contains the correct sky + sun for this exact camera ray, which is what
// makes the interior and exterior views agree and brings the sun disc back.

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec4 fragColor;  // xyz = base tint, w = base alpha

#define CAMERA_UBO_WEATHER
#include "camera.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.camPos.xyz - fragWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;   // two-sided: face the camera
    float NoV = max(dot(N, V), 0.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);

    // Effective alpha: transmission dominates near-normal incidence (windshield
    // reads see-through, matching the exterior view behind it); reflection takes
    // over toward grazing. The authored tint alpha now only nudges the floor —
    // it can no longer push opacity up near the 0.92 cap on its own the way the
    // old `fragColor.a + fresnel * 0.55` sum did.
    float alpha = clamp(fresnel + fragColor.a * 0.1, 0.0, 0.92);

    // Reflected sun: an ATMOSPHERIC disc, not a pow(...,256) mirror glint. Uses
    // the reflection direction (R = reflect(-V, N), same convention as the R in
    // lighting.frag/ssr.frag) against the sun, run through the exact same
    // exponent/strength shape as compute_sky_color()'s sun term in
    // lighting.frag (32..220 exponent, 0.242..0.9 strength, blended by
    // clarity = camera.weather.x) — release literals, since this shader has no
    // access to the debug-UI scene-params UBO (set 4). Matching that formula
    // means the sun reflected off the glass reads at the same size/intensity as
    // the real one seen directly through it.
    vec3  R        = reflect(-V, N);
    float sunDot   = max(dot(R, camera.sunDir.xyz), 0.0);
    float clarity  = camera.weather.x;
    vec3  sunDisc  = camera.sunColor.rgb
                   * pow(sunDot, mix(32.0, 220.0, clarity))
                   * mix(0.242, 0.9, clarity);

    // Combine: a reduced authored tint (no longer the dominant term — see alpha
    // above, and note it is carried by the same low alpha at normal incidence)
    // plus the reflected sun disc.
    vec3 color = fragColor.rgb * 0.4 + sunDisc;

    outColor = vec4(color, alpha);
}
