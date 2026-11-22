# Road surface tuning notes (D1)

> Written by AI (Claude) on 2026-11-21. Debug build only. Architecture: [README.md](README.md).

Sliders are in the debug panel under **Corridor** (TOML table `[corridor]`, keys `surface_*`). They are pushed per
draw, so they change live; no rebuild. The stretch table itself is `config/lie_corridor.toml [surface]`
(re-bake: `python3 tools/lie_corridor.py bake`, no network).

| Slider | Key | Default | Range | What it does |
|--------|-----|---------|-------|--------------|
| Slab joint spacing (m) | `surface_joint_m` | 4.572 (15 ft) | 3 to 20 | Concrete transverse joints; reflective cracks on asphalt sit at 4x this |
| Joint / crack sealant | `surface_sealant` | 0.6 | 0 to 1 | Darkness and gloss of sealant lines, patch seams, tar snakes |
| Wheel-path polish | `surface_polish` | 0.6 | 0 to 1 | Roughness and rubber darkening along the ruts profile |
| Wear | `surface_wear` | 0.7 | 0 to 1 | Patch and crack density, paint fade and breakup, oil line |
| Reflector spacing (m) | `surface_reflector_m` | 24.384 (80 ft) | 12.2 to 48.8 | Raised pavement markers; keep a multiple of 12.19 m or they land on dashes |
| Shoulder rumble strips | `surface_rumble` | 1.0 | 0 to 2 | Groove darkening and normal tilt |

## Math

**Anti-aliased lines.** Every line (joint, sealant, seam, marker edge) is the box-filtered coverage of a band of
width $w$ by a pixel footprint $f = \text{fwidth}(d)$:

$$c(d) = \operatorname{clamp}\!\left(\frac{\min(d + f/2,\ w/2) - \max(d - f/2,\ -w/2)}{f},\ 0,\ 1\right)$$

A 12 mm joint 60 m away covers a fraction of a pixel, so $c \approx w/f$: the joint fades to a faint tone instead of
flickering on and off. This is what keeps the joints and rumble grooves from shimmering when moving.

**Global grid from tile-local UVs.** Surface UVs restart every 300 m tile ($v$ = arc in tile). With the tile start
station $s_0$ pushed per draw, the signed distance to the nearest joint is

$$t = v + (s_0 \bmod S) - \phi,\qquad d_j = t - S\left\lfloor \frac{t}{S} + \tfrac12 \right\rfloor$$

In float32, $s_0$ up to $4.8\times10^7$ WU has an error of a few WU (a few mm) in the mod, which is invisible.

**Rumble far field.** Past about 60 WU per pixel along the road, the grooves are replaced by their mean coverage
$L/P = 7/12$, so the band reads as a darker stripe instead of a moire pattern.

**Reflector phase.** The dash cycle is $N = 10 + 30 = 40$ ft. MUTCD 3B.11 spaces markers at $2N = 80$ ft. Markers sit
mid-gap at $\phi = 10 + 15 = 25$ ft (7,620 WU) past a dash start.

## What to look at

- Joints too strong at distance: lower `surface_sealant`, not the spacing.
- Patches reading as black boxes: the patch tint is `albedo * 0.74` in `pavement.glsl`; raise it toward 0.85.
- Paint looking chewed: lower `surface_wear`; fresh stretches ignore most of it (`age` 0.15).
- Rain: the wet look comes from lighting.frag; check rain 0 and rain 1.0 after any change here.

## Sources and reading

- MUTCD 2009 edition, section 3B.11 (raised pavement markers), FHWA.
- FHWA Technical Advisory T 5040.39, "Shoulder and Edge Line Rumble Strips".
- Torbic, D. J. et al., NCHRP Report 641, *Guidance for the Design and Application of Shoulder and Centerline Rumble
  Strips*, 2009 (Pennsylvania State University / MRI).
- Huang, Y. H., *Pavement Analysis and Design*, University of Kentucky, ch. on rigid pavement joints.
- NYSDOT Comprehensive Pavement Design Manual, ch. 5 (joint spacing). Not opened in this run.
- Wikipedia, "Interstate 495 (New York)", route description (concrete east of exit 64).
