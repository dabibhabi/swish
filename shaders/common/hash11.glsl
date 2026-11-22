// hash11.glsl -- integer (Wang-style) hash -> float in [0,1). Used by spray sim + draw
// so a particle's seed maps identically on both sides.
#ifndef SWISH_HASH11_GLSL
#define SWISH_HASH11_GLSL
float hash11(uint n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x00ffffffu) / float(0x01000000u);
}
#endif
