#pragma shader_stage(fragment)
// Shader Studio / Shadertoy-style fragment. Inputs: iTime (seconds), iResolution (pixels).
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    float wave = 0.52 + 0.5 * sin(uv.x * 132.0 + iTime * 23.0);
    vec3 color = mix(vec3(0.04, 0.10, 0.24), vec3(0.964706, 0.992157, 1.0), wave * uv.y);
    fragColor = vec4(color, 1.0);
}
