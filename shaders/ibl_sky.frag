#version 450

// ── IBL bake stage 1: procedural sky → one cube face ──────────────────
// Renders compute_sky_color() (from lighting.frag) into a cube face. All sky
// params arrive via push constants — the CPU pre-mixes the clarity blend and
// supplies the per-face basis — so the bake is decoupled from the camera/
// scene-params UBOs and runs at init (before the camera/scene exist) and again
// whenever the weather changes. Output is the environment cubemap the irradiance
// + specular-prefilter stages convolve.

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform P {
    vec4 faceR;     // xyz = face right basis (world)
    vec4 faceU;     // xyz = face up basis
    vec4 faceF;     // xyz = face forward/centre basis
    vec4 horizon;   // rgb = clarity-mixed horizon colour
    vec4 zenith;    // rgb = clarity-mixed zenith colour, w = sun-disc exponent
    vec4 sunDir;    // xyz = sun direction (world),        w = sun-disc strength
    vec4 sunColor;  // rgb = sun colour
} pc;

void main() {
    // Direction for this face pixel: centre + right·s + up·t, s,t ∈ [-1,1].
    vec2 st  = fragUV * 2.0 - 1.0;
    vec3 dir = normalize(pc.faceF.xyz + st.x * pc.faceR.xyz + st.y * pc.faceU.xyz);

    // Identical math to lighting.frag::compute_sky_color (clarity already folded
    // into horizon/zenith/exp/str on the CPU).
    float t   = clamp(dir.y * 2.0 + 0.3, 0.0, 1.0);
    vec3  sky = mix(pc.horizon.rgb, pc.zenith.rgb, pow(t, 1.5));
    float sun_dot = max(dot(dir, pc.sunDir.xyz), 0.0);
    sky += pc.sunColor.rgb * pow(sun_dot, pc.zenith.w) * pc.sunDir.w;

    outColor = vec4(sky, 1.0);
}
