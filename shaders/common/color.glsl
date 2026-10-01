// color.glsl -- luminance weights. Two DIFFERENT standards, deliberately separate:
//   LUMA_709  Rec.709 relative luminance (bloom, composite, TAA clamp).
//   LUMA_601  Rec.601 weights; lighting.frag uses it ONLY for the wet-albedo
//             desaturation, on purpose. Do NOT unify the two.
#ifndef SWISH_COLOR_GLSL
#define SWISH_COLOR_GLSL
// (macros, not global consts: a global const vec3 changes the emitted SPIR-V)
#define LUMA_709 vec3(0.2126, 0.7152, 0.0722)
#define LUMA_601 vec3(0.299, 0.587, 0.114)
#endif
