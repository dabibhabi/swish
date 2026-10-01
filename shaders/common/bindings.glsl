// bindings.glsl -- single source of truth for descriptor set / binding numbers.
// Plain C-preprocessor text (only #define NAME <int>) so it can be #included from
// C++ as well as GLSL. Only numbers shared across passes live here; the per-pass
// "set 0" inputs of post-process passes stay as literals in their own shader.
#ifndef SWISH_BINDINGS_GLSL
#define SWISH_BINDINGS_GLSL

/* set 0: per-frame camera + lights (geometry / lighting passes) */
#define SET_CAMERA 0
#define BIND_CAMERA 0
#define BIND_LIGHTS 1

/* set 1: G-buffer (lighting pass) */
#define SET_GBUFFER 1
#define BIND_GB_ALBEDO 0
#define BIND_GB_NORMAL 1
#define BIND_GB_MATERIAL 2
#define BIND_GB_DEPTH 3
#define BIND_GB_CLEARCOAT_FLAKE 4

/* set 2: shadow map (CSM atlas) */
#define SET_SHADOW 2
#define BIND_SHADOW_MAP 0

/* set 3: image-based lighting */
#define SET_IBL 3
#define BIND_IBL_IRRADIANCE 0
#define BIND_IBL_PREFILTERED 1
#define BIND_IBL_BRDF_LUT 2

/* set 4: scene params / debug live-tunables */
#define SET_SCENE_PARAMS 4
#define BIND_SCENE_PARAMS 0

/* composite pass: set 0 = the post-chain inputs. The host writes them from the declared list
   swish::kCompositeInputs (src/renderer/Passes/PassInputs.h), which uses these numbers. */
#define SET_COMPOSITE 0
#define BIND_COMP_HDR 0
#define BIND_COMP_BLOOM 1
#define BIND_COMP_AO 2
#define BIND_COMP_SSR 3
#define BIND_COMP_GODRAYS 4

#endif
