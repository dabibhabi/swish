// atmosphere.glsl -- a physically based clear sky without lookup tables (lighting L2.1).
// Single scattering of Rayleigh (air), Mie (haze) and ozone, marched along the view ray. It stays correct with
// the sun at and below the horizon (the planet shadows the upper air: twilight), which the old two-colour
// gradient could not do. The C++ copy is src/utils/light/Atmosphere.h; keep the two in step (tests pin the C++).
//
// Units: distances in km, radiance in cd/m^2 (nits) per lux of light arriving at the top of the atmosphere.
// Missing on purpose: multiple scattering (a constant 1.2 lift stands in for it) and clouds as shapes
// (cloud cover is a blend toward a grey CIE overcast sky).
#ifndef SWISH_ATMOSPHERE_GLSL
#define SWISH_ATMOSPHERE_GLSL

const float ATM_PI        = 3.14159265358979;
const float ATM_RG        = 6360.0;                           // planet radius
const float ATM_RT        = 6460.0;                           // top of the atmosphere
const float ATM_EYE       = 0.05;                             // observer height above the ground
const vec3  ATM_RAYLEIGH  = vec3(5.802e-3, 13.558e-3, 33.1e-3);  // scattering per km at sea level
const float ATM_MIE_SCA   = 3.996e-3;
const float ATM_MIE_EXT   = 4.40e-3;
const vec3  ATM_OZONE     = vec3(0.650e-3, 1.881e-3, 0.085e-3);  // absorption per km at the layer's peak
const float ATM_MIE_G     = 0.8;
const float ATM_MS_LIFT   = 1.2;   // stands in for multiple scattering
const int   ATM_VIEW_STEPS  = 20;
const int   ATM_LIGHT_STEPS = 6;
const float ATM_SUN_SOLID_ANGLE = 6.8e-5;  // steradians; the moon is nearly the same size
const float ATM_SUN_COS_RADIUS  = 0.9999892;  // cos(0.2665 degrees)

// Far intersection of a ray with a sphere at the origin, or -1.
float atmRaySphere(vec3 o, vec3 d, float r) {
    float b = dot(o, d), c = dot(o, o) - r * r, h = b * b - c;
    if (h < 0.0) return -1.0;
    return -b + sqrt(h);
}
// Near intersection, or -1 if the ray misses or the sphere is behind.
float atmRaySphereNear(vec3 o, vec3 d, float r) {
    float b = dot(o, d), c = dot(o, o) - r * r, h = b * b - c;
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return t > 0.0 ? t : -1.0;
}
// Relative density of air, haze and ozone at height h (km).
vec3 atmDensity(float h) {
    return vec3(exp(-h / 8.0), exp(-h / 1.2), max(0.0, 1.0 - abs(h - 25.0) / 15.0));
}
vec3 atmExtinction(vec3 depth, float mie) {
    return ATM_RAYLEIGH * depth.x + vec3(ATM_MIE_EXT * mie * depth.y) + ATM_OZONE * depth.z;
}
// Optical depth (per species) from p toward the light to the top of the atmosphere; x < 0 when the planet
// is in the way.
vec3 atmLightDepth(vec3 p, vec3 l) {
    if (atmRaySphereNear(p, l, ATM_RG) > 0.0) return vec3(-1.0);
    float len = atmRaySphere(p, l, ATM_RT);
    vec3  sum = vec3(0.0);
    float prev = 0.0;
    for (int i = 1; i <= ATM_LIGHT_STEPS; ++i) {
        float u = float(i) / float(ATM_LIGHT_STEPS), t = len * u * u;
        float mid = 0.5 * (prev + t);
        sum += atmDensity(length(p + l * mid) - ATM_RG) * (t - prev);
        prev = t;
    }
    return sum;
}
// Haze amount from turbidity: 2.5 (a clear day) is the reference.
float atmMie(float turbidity) { return max(turbidity - 1.0, 0.2) / 1.5; }

// Clear-sky radiance toward `dir` (unit, y up) for a light at `l`, in nits per lux at the top of the atmosphere.
vec3 atmScatter(vec3 dir, vec3 l, float turbidity) {
    float mie = atmMie(turbidity);
    vec3  o   = vec3(0.0, ATM_RG + ATM_EYE, 0.0);
    float len = atmRaySphere(o, dir, ATM_RT);
    float mu  = dot(dir, l);
    float phR = 3.0 / (16.0 * ATM_PI) * (1.0 + mu * mu);
    float g   = ATM_MIE_G;
    float phM = (1.0 - g * g) / (4.0 * ATM_PI * pow(max(1.0 + g * g - 2.0 * g * mu, 1.0e-4), 1.5));
    vec3  viewDepth = vec3(0.0), sum = vec3(0.0);
    float prev = 0.0;
    for (int i = 1; i <= ATM_VIEW_STEPS; ++i) {
        float u = float(i) / float(ATM_VIEW_STEPS), t = len * u * u;   // dense near the eye, where the air is
        float ds = t - prev, mid = 0.5 * (prev + t);
        vec3  p = o + dir * mid;
        vec3  dens = atmDensity(length(p) - ATM_RG);
        viewDepth += dens * ds * 0.5;
        vec3 lightDepth = atmLightDepth(p, l);
        if (lightDepth.x >= 0.0) {
            vec3 T = exp(-atmExtinction(viewDepth + lightDepth, mie));
            sum += T * (ATM_RAYLEIGH * dens.x * phR + vec3(ATM_MIE_SCA * mie * dens.y * phM)) * ds;
        }
        viewDepth += dens * ds * 0.5;
        prev = t;
    }
    return sum * ATM_MS_LIFT;
}
// Fraction of the light that reaches the ground from direction l (per colour): the sun's tint.
vec3 atmTransmittance(vec3 l, float turbidity) {
    vec3 depth = atmLightDepth(vec3(0.0, ATM_RG + ATM_EYE, 0.0), l);
    if (depth.x < 0.0) return vec3(0.0);
    return exp(-atmExtinction(depth, atmMie(turbidity)));
}

// The whole sky in nits.
//   sun.xyz / moon.xyz = unit directions, .w = illuminance at the top of the atmosphere (lux)
//   params = (turbidity, cloud cover 0..1, airglow nits, overcast zenith nits)
// Below the horizon it returns a dimmed copy of the horizon: a stand-in for the ground far away.
vec3 atmSky(vec3 dir, vec4 sun, vec4 moon, vec4 params) {
    vec3 d = normalize(vec3(dir.x, max(dir.y, 0.02), dir.z));
    vec3 clear = atmScatter(d, sun.xyz, params.x) * sun.w;
    if (moon.w > 0.0) clear += atmScatter(d, moon.xyz, params.x) * moon.w;
    // CIE overcast sky: the zenith is three times the horizon.
    vec3 overcast = vec3(params.w * (1.0 + 2.0 * d.y) / 3.0);
    vec3 sky = mix(clear, overcast, params.y) + vec3(params.z);
    return sky * mix(1.0, 0.25, smoothstep(0.0, 0.3, -dir.y));
}

// The disc of the sun or moon: `lux` of direct light spread over the disc's solid angle. Soft edge.
float atmDisc(vec3 dir, vec3 l) {
    return smoothstep(ATM_SUN_COS_RADIUS - 4.0e-6, ATM_SUN_COS_RADIUS + 2.0e-6, dot(dir, l)) / ATM_SUN_SOLID_ANGLE;
}

#endif
