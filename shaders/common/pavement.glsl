// pavement.glsl -- D1 mainline pavement detail (debug builds; tasks/todo.md "LIE full route" D1).
// gbuffer.frag calls pavementDetail() for draws CorridorScene tagged (DrawCall::surface, pushed as pave.x):
// concrete slab joints, asphalt patches / sealed cracks, wheel-path polish, lane-centre oil, faded paint, raised
// pavement markers and shoulder rumble strips. All keyed to the road's (lateral, arc) UVs, so nothing swims.
// Needs ruts.glsl (lane constants, rutAmount) and gbuffer.frag's hash()/noise()/fbm() first.
// Tuning notes and sources: docs/surface/tuning-notes.md.
#ifndef SWISH_PAVEMENT_GLSL
#define SWISH_PAVEMENT_GLSL

const float kPaveRpmPhaseWU = 7620.0;  // 25 ft: marker mid-gap on the 10 ft dash / 30 ft gap cycle (MUTCD 3B.11)
const float kPaveRumblePitch = 304.8;  // 12 in centres (FHWA TA 5040.39 milled shoulder rumble)
const float kPaveRumbleLen   = 177.8;  // 7 in groove along travel
const float kPaveRumbleW     = 406.4;  // 16 in across
const float kPaveRumbleOff   = 101.6;  // 4 in from the edge line (guess)
const float kPavePatchCell   = 25000.0;  // 25 m: divides the 300 m tile, so patches never cross a tile seam
const float kPaveCrackCell   = 12000.0;  // 12 m, same reason

// Box-filtered coverage of a band |d| < width/2 by a pixel footprint fw (no shimmer when the band is sub-pixel).
float paveBand(float d, float width, float fw) {
    float f = max(fw, 1e-3);
    float h = 0.5 * width;
    return clamp((min(d + 0.5 * f, h) - max(d - 0.5 * f, -h)) / f, 0.0, 1.0);
}

// Distance to the nearest multiple of `spacing`, on the global station grid (arc0 = tile start station).
float paveGrid(float arcL, float arc0, float spacing, float phase) {
    float t = arcL + mod(arc0, spacing) - phase;
    return t - spacing * floor(t / spacing + 0.5);
}

// Global cell index along the route (arc0 + arcL may exceed float precision for fine cells; split it).
float paveCell(float arcL, float arc0, float cell) {
    return floor(arc0 / cell) + floor((mod(arc0, cell) + arcL) / cell);
}

void pavementDetail(inout vec3 albedo, inout float roughness, inout vec3 N, inout float retro, vec2 uvWU, vec3 T,
                    vec4 pave, vec4 pave2, bool paint) {
    int   sb       = int(pave.x + 0.5);
    int   kind     = sb & 7;  // Pavement: 1 concrete, 2 fresh, 3 worn, 4 patched
    bool  shoulder = (sb & 8) != 0;
    float lanes    = float(sb >> 4);
    float lat = uvWU.x, arcL = uvWU.y, arc0 = pave.y;
    float latA = abs(lat), side = lat < 0.0 ? 1.0 : 0.0;
    float x    = latA - kRutLaneOriginWU;  // from the inner edge of lane 1
    float lw   = kRutLaneWidthWU;
    float age  = kind == 2 ? 0.15 : kind == 1 ? 0.5 : kind == 3 ? 0.85 : 1.0;  // how much `wear` shows per kind
    float wear = pave2.y * age;

    // ── Faded, broken paint (marking draws) ──
    if (paint) {
        float g    = fbm(uvWU / 400.0) * 0.7 + noise(uvWU / 40.0) * 0.3;
        float lost = smoothstep(0.6 - 0.12 * wear, 0.68 - 0.12 * wear, g);
        albedo     = mix(albedo * mix(1.0, 0.72, wear), vec3(kind == 1 ? 0.4 : 0.12), lost);  // the road shows through
        roughness  = mix(roughness, 0.85, lost);
        if (lost > 0.5) retro = 0.0;  // the beads went with the paint
        return;
    }

    float fwA = fwidth(arcL), fwL = fwidth(latA);

    // ── Shoulder: milled rumble strips next to the edge line ──
    if (shoulder) {
        float d    = x < 0.0 ? -x : latA - (kRutLaneOriginWU + lanes * lw);  // from the inner / outer edge line
        float band = paveBand(d - kPaveRumbleOff - 0.5 * kPaveRumbleW, kPaveRumbleW, fwidth(d)) * pave2.w;
        float p    = paveGrid(arcL, arc0, kPaveRumblePitch, 0.0);
        float near = 1.0 - smoothstep(20.0, 60.0, fwA);  // far away the grooves average to a darker band
        float pit  = mix(kPaveRumbleLen / kPaveRumblePitch, paveBand(p, kPaveRumbleLen, fwA), near);
        albedo *= 1.0 - 0.18 * band * pit;  // groove floor: shadowed, dusty
        roughness = min(roughness + 0.1 * band * pit, 1.0);
        float ramp = smoothstep(55.0, 72.0, abs(p)) * (1.0 - smoothstep(80.0, 96.0, abs(p)));
        N          = normalize(N - T * sign(p) * ramp * 0.6 * band * near);
        return;
    }

    float inLane = mod(max(x, 0.0), lw) - 0.5 * lw;
    float laneId = floor(max(x, 0.0) / lw) + side * 17.0;
    bool  inLanes = x > 0.0 && x < lanes * lw;

    // ── Wheel-path polish (the debug ruts profile) and the lane-centre oil drip line ──
    float rut = rutAmount(lat, arcL);
    roughness *= 1.0 - 0.4 * pave2.x * rut;
    albedo *= 1.0 - 0.10 * pave2.x * rut;  // rubber
    float oil = exp(-0.5 * inLane * inLane / (350.0 * 350.0)) *
                smoothstep(0.35, 0.65, noise(vec2(arcL / 2500.0, laneId * 3.7))) * (inLanes ? 1.0 : 0.0);
    albedo *= 1.0 - 0.22 * oil * wear;

    float seal = 0.0;
    if (kind == 1) {
        // ── Concrete: transverse joints on the station grid, longitudinal joints under the lane lines ──
        float S     = pave.z;
        float slab  = paveCell(arcL, arc0, S);
        float h     = hash(vec2(slab, laneId));
        albedo *= mix(0.93, 1.06, h);  // slab-to-slab tone (each slab cured on its own day)
        roughness   = clamp(roughness + (hash(vec2(slab + 3.1, laneId)) - 0.5) * 0.08, 0.0, 1.0);
        float dj    = paveGrid(arcL, arc0, S, 0.0);
        float dl    = x - lw * floor(x / lw + 0.5);
        float joint = paveBand(dj, 12.0, fwA) + 0.5 * paveBand(dj, 80.0, fwA);  // sealant + dirty chamfer
        float lj    = (paveBand(dl, 12.0, fwL) + 0.5 * paveBand(dl, 80.0, fwL)) * (x > 0.0 ? 1.0 : 0.0);
        seal        = clamp(max(joint, lj), 0.0, 1.0);
    } else {
        // ── Asphalt over old concrete: reflective transverse cracks every 4 slabs (~60 ft, guess), sealed ──
        float Sc  = 4.0 * pave.z;
        float k   = paveCell(arcL + 0.5 * Sc, arc0, Sc);
        float dc  = paveGrid(arcL, arc0, Sc, 0.0) + 70.0 * sin(lat / 900.0 + k * 1.7) + 25.0 * sin(lat / 210.0 + k * 4.3);
        float on  = step(hash(vec2(k, side + 5.0)), (kind == 4 ? 0.9 : kind == 3 ? 0.5 : 0.0) * pave2.y);
        float trc = paveBand(dc, 50.0, fwA) * on;
        // ── Longitudinal sealed cracks along wheel-path edges, a few metres long ──
        float c   = paveCell(arcL, arc0, kPaveCrackCell);
        float hc  = hash(vec2(c, laneId + 40.0));
        float u   = (arcL + mod(arc0, kPaveCrackCell)) / kPaveCrackCell - c + floor(arc0 / kPaveCrackCell);  // 0..1 in cell
        float a0  = hash(vec2(c, laneId + 41.0)) * 0.5;
        float len = smoothstep(a0, a0 + 0.05, u) * (1.0 - smoothstep(a0 + 0.4, a0 + 0.45, u));
        float off = (hash(vec2(c, laneId + 42.0)) - 0.5) * 2400.0;
        float lc  = inLane - off - 90.0 * sin(arcL / 1400.0 + hc * 6.0) - 30.0 * sin(arcL / 330.0 + hc * 11.0);
        float lon = paveBand(lc, 40.0, fwL) * len * step(hc, (kind == 4 ? 0.35 : kind == 3 ? 0.15 : 0.0) * pave2.y);
        seal      = clamp(max(trc, lon), 0.0, 1.0) * (inLanes ? 1.0 : 0.0);

        // ── Patches: lane-wide or wheel-path rectangles of newer, darker asphalt with sealed edges ──
        float pc = paveCell(arcL, arc0, kPavePatchCell);
        float hp = hash(vec2(pc, laneId + 60.0));
        if (inLanes && hp < (kind == 4 ? 0.45 : kind == 3 ? 0.12 : 0.0) * pave2.y) {
            float L   = kPavePatchCell;
            float pu  = (arcL + mod(arc0, L)) - L * (pc - floor(arc0 / L));  // WU into the cell
            float p0  = hash(vec2(pc, laneId + 61.0)) * 0.5 * L;
            float p1  = min(p0 + mix(3000.0, 0.5 * L, hash(vec2(pc, laneId + 62.0))), L - 500.0);
            bool  wpOnly = hash(vec2(pc, laneId + 63.0)) > 0.5;
            float l0 = wpOnly ? 0.0 : -0.5 * lw + 150.0, l1 = 0.5 * lw - 150.0;
            float ca = paveBand(pu - 0.5 * (p0 + p1), p1 - p0, fwA);
            float cl = paveBand(inLane - 0.5 * (l0 + l1), l1 - l0, fwL);
            float pm = ca * cl;
            albedo    = mix(albedo, albedo * 0.74, pm);
            roughness = mix(roughness, roughness * 0.9, pm);
            float core = paveBand(pu - 0.5 * (p0 + p1), p1 - p0 - 80.0, fwA) *
                         paveBand(inLane - 0.5 * (l0 + l1), l1 - l0 - 80.0, fwL);
            seal = max(seal, clamp(pm - core, 0.0, 1.0));  // 40 WU sealed seam round the patch
        }
    }
    seal *= pave2.z;
    albedo    = mix(albedo, vec3(0.02), seal * 0.9);  // tar sealant
    roughness = mix(roughness, 0.3, seal);           // and it is glossier than the road

    // ── Raised pavement markers mid-gap on the lane lines, lens facing traffic ──
    float j = floor(x / lw + 0.5);
    if (lanes >= 2.0 && j >= 1.0 && j <= lanes - 1.0) {
        float dr   = paveGrid(arcL, arc0, pave.w, kPaveRpmPhaseWU);
        float dl   = x - j * lw;
        float body = paveBand(dr, 254.0, fwA) * paveBand(dl, 150.0, fwL);  // snowplowable casting (size guess)
        float face = lat > 0.0 ? -105.0 : 105.0;                            // EB travels +arc, WB -arc
        float lens = paveBand(dr - face, 30.0, fwA) * paveBand(dl, 100.0, fwL);
        albedo     = mix(albedo, vec3(0.30), body);
        roughness  = mix(roughness, 0.45, body);
        albedo     = mix(albedo, vec3(0.85), lens);
        if (lens > 0.4) retro = 1.0;
    }
}

#endif
