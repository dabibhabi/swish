// lights.glsl -- point-light struct + LightsUBO (set 0, binding 1).
#ifndef SWISH_LIGHTS_GLSL
#define SWISH_LIGHTS_GLSL
#include "bindings.glsl"

struct PointLight {
    vec4 positionRadius;    // xyz = position, w = radius
    vec4 colorIntensity;    // rgb = color, a = intensity
};

#ifdef SWISH_DEBUG_UI
// Car spot lights (headlights / tail / brake). Debug-only layout: mirrors
// SceneTypes.h SpotLightData / LightsUBO under the same define.
struct SpotLight {
    vec4 positionRange;      // xyz = position (WU), w = range (WU)
    vec4 directionCosOuter;  // xyz = unit beam direction, w = cos(outer half-angle)
    vec4 colorIntensity;     // rgb = color, a = intensity
    vec4 params;             // x = cos(inner half-angle)
};
#endif

layout(set = SET_CAMERA, binding = BIND_LIGHTS) uniform LightsUBO {
    PointLight pointLights[32];
    uvec4      numPointLights;  // x = count
#ifdef SWISH_DEBUG_UI
    SpotLight  spotLights[8];
    uvec4      numSpotLights;   // x = count
#endif
} lights;

#endif
