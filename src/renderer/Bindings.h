#pragma once
// ============================================================================
// renderer/Bindings.h
//
// Descriptor set indices and binding numbers, shared with GLSL. The single source of truth is
// shaders/common/bindings.glsl (plain `#define NAME <int>` lines, valid C and GLSL). This header includes that
// very file (the swish target has shaders/common on its include path) and re-exports each number as a typed
// constexpr in `swish::bind`, so no host code re-types a `set = N` / `binding = M` literal that a shader also
// spells. Adding a binding: add the #define to bindings.glsl, add the constexpr + #undef below, use the constexpr.
//
// Only numbers that are shared across passes live in bindings.glsl. Post-process passes whose descriptor set 0 is
// private to one pipeline (SSR, god-rays, bloom, SSAO) keep their literals in their own shader.
// ============================================================================

#include <cstdint>

namespace swish::bind {

#include "bindings.glsl"

// set 0: camera + lights (geometry / lighting passes)
inline constexpr uint32_t kSetCamera  = SET_CAMERA;
inline constexpr uint32_t kBindCamera = BIND_CAMERA;
inline constexpr uint32_t kBindLights = BIND_LIGHTS;

// set 1: G-buffer (lighting pass)
inline constexpr uint32_t kSetGbuffer           = SET_GBUFFER;
inline constexpr uint32_t kBindGbAlbedo         = BIND_GB_ALBEDO;
inline constexpr uint32_t kBindGbNormal         = BIND_GB_NORMAL;
inline constexpr uint32_t kBindGbMaterial       = BIND_GB_MATERIAL;
inline constexpr uint32_t kBindGbDepth          = BIND_GB_DEPTH;
inline constexpr uint32_t kBindGbClearcoatFlake = BIND_GB_CLEARCOAT_FLAKE;

// set 2: shadow map (CSM atlas)
inline constexpr uint32_t kSetShadow     = SET_SHADOW;
inline constexpr uint32_t kBindShadowMap = BIND_SHADOW_MAP;

// set 3: image-based lighting
inline constexpr uint32_t kSetIbl             = SET_IBL;
inline constexpr uint32_t kBindIblIrradiance  = BIND_IBL_IRRADIANCE;
inline constexpr uint32_t kBindIblPrefiltered = BIND_IBL_PREFILTERED;
inline constexpr uint32_t kBindIblBrdfLut     = BIND_IBL_BRDF_LUT;

// set 4: scene params / debug live-tunables
inline constexpr uint32_t kSetSceneParams  = SET_SCENE_PARAMS;
inline constexpr uint32_t kBindSceneParams = BIND_SCENE_PARAMS;

// composite pass inputs (set 0 of the composite pipeline)
inline constexpr uint32_t kSetComposite    = SET_COMPOSITE;
inline constexpr uint32_t kBindCompHdr     = BIND_COMP_HDR;
inline constexpr uint32_t kBindCompBloom   = BIND_COMP_BLOOM;
inline constexpr uint32_t kBindCompAo      = BIND_COMP_AO;
inline constexpr uint32_t kBindCompSsr     = BIND_COMP_SSR;
inline constexpr uint32_t kBindCompGodrays = BIND_COMP_GODRAYS;

}  // namespace swish::bind

// The macros were only needed to build the constants above; drop them so they do not leak into every includer.
#undef SWISH_BINDINGS_GLSL
#undef SET_CAMERA
#undef BIND_CAMERA
#undef BIND_LIGHTS
#undef SET_GBUFFER
#undef BIND_GB_ALBEDO
#undef BIND_GB_NORMAL
#undef BIND_GB_MATERIAL
#undef BIND_GB_DEPTH
#undef BIND_GB_CLEARCOAT_FLAKE
#undef SET_SHADOW
#undef BIND_SHADOW_MAP
#undef SET_IBL
#undef BIND_IBL_IRRADIANCE
#undef BIND_IBL_PREFILTERED
#undef BIND_IBL_BRDF_LUT
#undef SET_SCENE_PARAMS
#undef BIND_SCENE_PARAMS
#undef SET_COMPOSITE
#undef BIND_COMP_HDR
#undef BIND_COMP_BLOOM
#undef BIND_COMP_AO
#undef BIND_COMP_SSR
#undef BIND_COMP_GODRAYS
