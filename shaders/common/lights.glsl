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
// Rectangle area lights (car showcase): a glowing panel that lights softly and shows in glossy paint.
// Mirrors SceneTypes.h RectLightData. The panel emits along cross(right, up).
struct RectLight {
    vec4 centerRange;  // xyz = centre (WU), w = range (WU)
    vec4 rightHalfW;   // xyz = unit right, w = half width (WU)
    vec4 upHalfH;      // xyz = unit up, w = half height (WU)
    vec4 colorNits;    // rgb = colour, a = luminance (cd/m^2)
};
#endif

layout(set = SET_CAMERA, binding = BIND_LIGHTS) uniform LightsUBO {
    PointLight pointLights[32];
    uvec4      numPointLights;  // x = count
#ifdef SWISH_DEBUG_UI
    SpotLight  spotLights[40];  // MAX_SPOT_LIGHTS in SceneTypes.h
    uvec4      numSpotLights;   // x = count
    RectLight  rectLights[16];  // MAX_RECT_LIGHTS in SceneTypes.h
    uvec4      numRectLights;   // x = count
    vec4       carBoxCenter;    // xyz = the car body's box centre (WU), w = yaw (radians)
    vec4       carBoxHalf;      // xyz = half extents along forward / up / right (WU), w = 1 when the box occludes
#endif
} lights;

#endif
