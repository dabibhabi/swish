#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D aoTex;

layout(location = 0) out vec4 outColor;

#ifdef SWISH_DEBUG_UI
// Relative depth difference at which a tap's weight falls to 1/e. Loose enough that a road seen at a grazing angle
// (a few % depth change per half-res texel) still averages, tight enough to stop at the car / barrier silhouettes.
const float kDepthSigma = 0.1;

void main() {
    // Depth-aware 5x5 average. A uniform box smears dark AO across silhouettes (halos round the car and the
    // barrier tops); weighting each tap by its depth relative to the centre keeps the average on one surface. The
    // weight ignores the AO value itself, so it still removes the per-pixel rotation noise.
    vec2  texelSize = 1.0 / vec2(textureSize(aoTex, 0));
    vec2  centre    = texture(aoTex, fragUV).rg;  // r = AO, g = scaled view depth (ssao.frag)
    float sum       = 0.0;
    float wsum      = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2  s = texture(aoTex, fragUV + vec2(float(x), float(y)) * texelSize).rg;
            float w = exp(-abs(s.g - centre.g) / max(kDepthSigma * centre.g, 1e-6));
            sum += s.r * w;
            wsum += w;
        }
    }
    outColor = vec4(sum / wsum, centre.g, 0.0, 1.0);
}
#else
void main() {
    // Plain 5x5 box average. The SSAO pass rotates its kernel per-pixel, which
    // leaves full-frequency noise; a uniform average resolves it far better than
    // an edge-preserving (bilateral) weight, which by design keeps that noise.
    // AO is low-frequency, so softening edges here is fine.
    vec2 texelSize = 1.0 / vec2(textureSize(aoTex, 0));

    float result = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            result += texture(aoTex, fragUV + vec2(float(x), float(y)) * texelSize).r;
        }
    }

    outColor = vec4(vec3(result / 25.0), 1.0);
}
#endif
