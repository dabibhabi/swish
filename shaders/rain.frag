#version 450

// Rain streak fragment shader.
// Draws a soft luminous streak with alpha falloff at edges and tips.
// Additive blending (src=ONE, dst=ONE) — no need for depth sorting.
//
// Retinal-persistence streaks (Rousseau et al., "Realistic Real-Time Rain
// Rendering" / "GPU Rainfall"): a falling drop vibrates during the exposure,
// so the streak the eye/camera records is the *time integral* of that
// vibrating drop across the exposure window — its outline therefore carries
// multiple internal highlights/speckles, not one. We approximate that
// integral by averaging several samples taken along the drop's oscillation
// outline, advancing the Garg & Nayar (SIGGRAPH 2006) oscillation phase per
// sample. The result is the characteristic multi-highlight, speckled streak.
//
// Shared by all world-rain parallax layers — keep layer-agnostic.

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragAlpha;
layout(location = 2) in float fragPhase;

layout(location = 0) out vec4 outColor;

void main() {
    // Feather edges along the width (x direction)
    float edge   = abs(fragUV.x - 0.5) * 2.0;   // 0 = center, 1 = edge

    // Garg & Nayar (SIGGRAPH 2006) oscillation modes (2,0) and (3,1) modulate
    // the effective streak width along its length. Rather than evaluating one
    // oscillation phase, we integrate the streak over the exposure: average N
    // samples taken along the drop's oscillation outline, advancing the phase
    // per sample. This accumulates the multiple highlights/speckles a real
    // motion-blurred night-rain streak carries (retinal persistence).
    // Subtle Garg–Nayar oscillation: enough to give the streak life, but small
    // enough that it stays a continuous thin bright LINE rather than dotting up.
    float A20 = 0.025;
    float A31 = 0.025;
    float phi = fragPhase * 6.2831853;
    float s   = fragUV.y;                 // position along streak (0 head -> 1 tail)

    const int N = 7;                      // 5–10 samples along the oscillation outline
    float acc = 0.0;
    for (int k = 0; k < N; ++k) {
        float sk   = float(k) / float(N - 1);                 // 0..1 across the exposure
        // Per-sample oscillation phase offset walks the vibration outline.
        float oscK = 1.0 + A20 * cos(2.0 * phi + s * 18.0 + sk * 6.2831853)
                         + A31 * sin(phi       + s * 30.0 + sk * 3.1415927);
        float effK = edge / max(oscK * 0.7, 0.20);
        // Thin bright core + soft thin edge → a delicate streak, not a fat bar.
        // The internal modulation is now a faint shimmer (amplitude 0.06), so the
        // averaged result reads as one continuous motion-blurred streak, not dots.
        acc += (1.0 - smoothstep(0.12, 0.72, effK))           // streak coverage
             * (0.94 + 0.06 * sin(s * 40.0 + phi * 5.0 + sk * 12.0));  // faint shimmer
    }
    float streakSpeckle = acc / float(N);   // averaged multi-highlight, speckled streak

    // Taper BOTH ends so the streak is a thin lozenge, not a solid-headed bar:
    // a short ramp in at the head and a long fade out toward the tail.
    float fade = smoothstep(0.0, 0.06, fragUV.y) * (1.0 - smoothstep(0.60, 1.0, fragUV.y));

    float alpha = streakSpeckle * fade * fragAlpha;
    if (alpha < 0.002) discard;

    // Cool blue-white: rain reflects sky and diffuses light. Kept translucent —
    // thin streaks you can see through, not glowing bars.
    vec3 color = vec3(0.72, 0.82, 0.97);

    // Output premultiplied for additive blending (src=ONE, dst=ONE).
    outColor = vec4(color * alpha * 1.15, alpha);
}
