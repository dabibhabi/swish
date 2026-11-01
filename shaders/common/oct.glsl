// oct.glsl -- octahedral unit-vector <-> 2xUNORM packing (G-buffer clearcoat/flake target).
// Opt-in per function (define before #include) so a pass compiles only what it calls:
//   SWISH_OCT_ENCODE  -> octEncode  (gbuffer.frag)
//   SWISH_OCT_DECODE  -> octDecode  (lighting.frag)
// The two are exact inverses; keep them here together.
#ifndef SWISH_OCT_GLSL
#define SWISH_OCT_GLSL
#ifdef SWISH_OCT_ENCODE
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 enc = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * sign(n.xy);
    return enc * 0.5 + 0.5;
}
#endif
#ifdef SWISH_OCT_DECODE
vec3 octDecode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3  n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x    += n.x >= 0.0 ? -t : t;
    n.y    += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
#endif
#endif
