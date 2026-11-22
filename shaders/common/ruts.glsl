// ruts.glsl -- wheel-track ruts on the LIE mainline (rain round 3, P3; debug builds).
// Tyres polish and slightly depress two bands per lane, which hold a film of water after rain: darker, glossier
// stripes. gbuffer.frag evaluates the profile from the road's lateral/arc UVs (u, v = lateral, arc / asphalt tile;
// MeshBuilder::emit_strip / the intro road quads) and packs it into the road tag (0.55 + 0.45 rut);
// lighting.frag and ssr.frag decode it. The cross-section constants mirror the ENDLESS CORRIDOR (config/lie_corridor.toml:
// lanes start at median half-width + inner shoulder, 12 ft wide), pinned by tests/test_road_params.cpp "ruts.glsl".
// The short authored intro straight uses 13 ft lanes from the barrier, so its ruts sit slightly off-centre.
#ifndef SWISH_RUTS_GLSL
#define SWISH_RUTS_GLSL

const float kRutLaneOriginWU = 1600.2;  // (median_half_width_ft 1.25 + inner_shoulder_ft 4.0) x 304.8: first lane edge
const float kRutLaneWidthWU  = 3657.6;  // lane_width_ft 12 x 304.8
const float kRutMaxLanes     = 3.0;  // ruts only in the first 3 lanes: 3-lane stretches would rut the outer shoulder
const float kRutOffsetWU     = 850.0;     // wheel path from the lane centre: half a ~1.7 m car/van track
const float kRutSigmaWU      = 180.0;     // Gaussian half-width of one rut (~0.4 m visible band)

float rutHash(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

// 0 = no rut .. 1 = the centre of a wheel path. lateralWU is signed from the corridor centre (the median);
// both carriageways are mirror images. arcWU breaks the bands into patchy wear along the road (~6 m cells).
float rutAmount(float lateralWU, float arcWU) {
    float x = abs(lateralWU) - kRutLaneOriginWU;
    if (x < 0.0 || x > kRutMaxLanes * kRutLaneWidthWU) return 0.0;
    float inLane = mod(x, kRutLaneWidthWU) - 0.5 * kRutLaneWidthWU;  // from this lane's centre
    float d      = min(abs(inLane - kRutOffsetWU), abs(inLane + kRutOffsetWU));
    float band   = exp(-0.5 * d * d / (kRutSigmaWU * kRutSigmaWU));
    float lane   = floor(x / kRutLaneWidthWU) + (lateralWU < 0.0 ? 7.0 : 0.0);
    float c      = arcWU / 6000.0;
    float wear   = mix(rutHash(floor(c) + lane * 31.0), rutHash(floor(c) + 1.0 + lane * 31.0), smoothstep(0.0, 1.0, fract(c)));
    return band * mix(0.55, 1.0, wear);
}

#endif
