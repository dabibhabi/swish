// lights.glsl -- point-light struct + LightsUBO (set 0, binding 1).
#ifndef SWISH_LIGHTS_GLSL
#define SWISH_LIGHTS_GLSL
#include "bindings.glsl"

struct PointLight {
    vec4 positionRadius;    // xyz = position, w = radius
    vec4 colorIntensity;    // rgb = color, a = intensity
};

layout(set = SET_CAMERA, binding = BIND_LIGHTS) uniform LightsUBO {
    PointLight pointLights[32];
    uvec4      numPointLights;  // x = count
} lights;

#endif
