// hash.glsl -- 2D float hash (sin-dot). Kept separate from hash11.glsl so a pass only
// compiles the hash it uses (glslc emits unused functions, which would change its SPIR-V).
#ifndef SWISH_HASH_GLSL
#define SWISH_HASH_GLSL
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
#endif
