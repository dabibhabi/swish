#version 450

// Rain streak fragment shader.
// Draws a soft luminous streak with alpha falloff at edges and tips.
// Additive blending (src=ONE, dst=ONE) — no need for depth sorting.

layout(location = 0) in vec2  fragUV;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // Feather edges along the width (x direction)
    float edge   = abs(fragUV.x - 0.5) * 2.0;   // 0 = center, 1 = edge
    float streak = 1.0 - smoothstep(0.0, 0.8, edge);

    // Fade tip (bottom) and tail (top) of the streak
    float fade = smoothstep(0.0, 0.15, fragUV.y) * smoothstep(1.0, 0.72, fragUV.y);

    float alpha = streak * fade * fragAlpha;
    if (alpha < 0.002) discard;

    // Cool blue-white: rain reflects sky and diffuses light
    vec3 color = vec3(0.65, 0.76, 0.90);

    // Output premultiplied for additive blending
    outColor = vec4(color * alpha, alpha);
}
