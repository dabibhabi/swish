// camera.glsl -- CameraUBO (set 0, binding 0), one host buffer shared by every pass.
// Passes read a byte-exact PREFIX of the host struct, chosen with a define BEFORE
// the #include (so each pass's std140 block, and its compiled SPIR-V, is unchanged):
//   (none)               view, proj, camPos, sunDir, sunColor
//   CAMERA_UBO_WEATHER   + weather
//   CAMERA_UBO_CSM       + weather + cascadeViewProj[NUM_CASCADES] + cascadeSplits
//                        (consumer must #define NUM_CASCADES first; only lighting.frag)
// camPos.w = wetness; sunDir.xyz = direction, w = sun illuminance in lux; sunColor.rgb = tint,
// a = ambient gain; weather.y = sky scale in cd/m^2, weather.w = metres per world unit, weather.z = pre-exposure (lights are multiplied
// by it before shading so the half-float HDR target holds light * preExposure; the composite undoes it).
#ifndef SWISH_CAMERA_GLSL
#define SWISH_CAMERA_GLSL
#include "bindings.glsl"

layout(set = SET_CAMERA, binding = BIND_CAMERA) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
#if defined(CAMERA_UBO_WEATHER) || defined(CAMERA_UBO_CSM)
    vec4 weather;   // x = clarity (0 overcast .. 1 clear day), y = sky nits, z = pre-exposure, w = metres per WU
#endif
#ifdef CAMERA_UBO_CSM
    mat4 cascadeViewProj[NUM_CASCADES];  // world -> cascade [0,1] clip
    vec4 cascadeSplits;                  // xyz = view-space far distance of cascades 0/1/2
#endif
} camera;

#endif
