#version 450

// ── IBL bake stage 1 (HDRI): equirectangular panorama → one cube face ──
// Samples a Radiance .hdr equirect (loaded by IBLManager) into a cube face
// using the same F/R/U basis as ibl_sky.frag, so irradiance + prefilter
// convolutions stay unchanged. params.x = exposure, params.y = yaw (radians).

layout(set = 0, binding = 0) uniform sampler2D equirectMap;

layout(push_constant) uniform P {
    vec4 faceR;   // xyz = face right basis (world)
    vec4 faceU;   // xyz = face up basis
    vec4 faceF;   // xyz = face forward/centre basis
    vec4 params;  // x = exposure, y = yaw radians, zw unused
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec2 dirToEquirect(vec3 d) {
    vec2 uv = vec2(atan(d.z, d.x), asin(clamp(d.y, -1.0, 1.0)));
    uv *= vec2(0.15915494309, 0.31830988618);  // 1/(2π), 1/π
    uv += 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

void main() {
    vec2 st  = fragUV * 2.0 - 1.0;
    vec3 dir = normalize(pc.faceF.xyz + st.x * pc.faceR.xyz + st.y * pc.faceU.xyz);

    // Yaw so the HDRI horizon / sun can be aligned with the road corridor.
    float c = cos(pc.params.y);
    float s = sin(pc.params.y);
    dir = vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);

    vec3 col = texture(equirectMap, dirToEquirect(dir)).rgb * pc.params.x;
    outColor = vec4(col, 1.0);
}
