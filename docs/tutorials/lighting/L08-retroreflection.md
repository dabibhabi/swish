---
verified: 2026-11-14
against: 4f13766
owner: docs-scribe
---

# L8: retroreflection

> **Drafted with AI assistance (Claude); owner review pending.** If this chapter and the code disagree, the code wins.

Back to the [index](00-index.md). Needs [L6: local lights](L06-local-lights.md) (headlights on the corridor).

## Goal

Tag the lane markings as retroreflective and add one small lobe to the spot-light loop.
Signs come later: the corridor has none yet.

You will see: at night, lane lines far ahead glow in your own headlights, brighter than the asphalt lit by the same beam, and fade when you look at them from the side.

## Concept

Road paint holds glass beads. A bead bends light back toward where it came from. So the paint is bright only for
an eye that sits close to the lamp. A driver sits about half a metre above the headlights. That is why markings
shine for the driver and look dull to someone on the verge.

Two angles matter:

- **Observation angle**: between "surface to light" and "surface to eye". Small angle means bright.
- **Surface normal**: barely matters. Beads work even when the beam skims the road.

Road engineers measure paint with the coefficient of retroreflected luminance:

$$R_L = \frac{L}{E_\perp} \quad \left[\frac{\text{cd}}{\text{m}^2 \cdot \text{lx}}\right]$$

$L$ is the luminance the driver sees. $E_\perp$ is the headlight illuminance on a plane facing the beam.
The US rule requires at least 50 mcd/m²/lx and recommends 100 on fast roads. New paint is higher.
So a good value for Swish is $R_L \approx 0.15$ cd/m²/lx.

Compare with plain white paint (albedo 0.8) at the standard test geometry, where the beam meets the road at 88.76° from the normal:

$$\frac{L}{E_\perp} = \frac{0.8}{\pi}\cos(88.76^\circ) \approx 0.0055$$

Retro paint is about 25 times brighter than diffuse paint at that angle. That gap is the whole effect.

The cheap lobe, per spot light:

$$g = \text{clamp}(V \cdot L, 0, 1)^{\,n} \qquad L_\text{retro} = \text{tint} \cdot R_L \cdot g \cdot E_\perp$$

$V$ points from the surface to the camera. $L$ points from the surface to the light. Pick $n$ from the angle
$\theta_{1/2}$ where the lobe drops to half:

$$n = \frac{\ln 0.5}{\ln \cos\theta_{1/2}} \qquad \theta_{1/2}=5^\circ \Rightarrow n \approx 180, \quad 10^\circ \Rightarrow n \approx 45$$

Real paint is much tighter (about 1°). A chase camera sits metres above the headlights, so start wide at 5°.

```mermaid
graph LR
    A["CorridorScene: MAT_MARKING"] --> B["SceneGeometry: material.w = 0.25"]
    B --> C["gbuffer.frag: outMaterial.a"]
    C --> D["lighting.frag: retro mask"]
    D --> E["spot loop: + R_L * g * E"]
```

## Where Swish is today

| What | File:line | Note |
|------|-----------|------|
| Edge lines | [CorridorScene.cpp:167-170](../../../src/scene/Corridor/CorridorScene.cpp) | No material argument, so `MAT_DEFAULT` |
| Dashed lane lines | [CorridorScene.cpp:179-182](../../../src/scene/Corridor/CorridorScene.cpp) | Same |
| Default material of the sweep builders | [MeshBuilder.h:75-76, 89-90](../../../src/scene/RoadScene/MeshBuilder.h) | `material = MAT_DEFAULT` |
| Material ids | [SceneTypes.h:11-62](../../../src/scene/SceneTypes.h) | Ends `MAT_CAR_LAST`, then `MAT_COUNT` |
| Texture name per material | [MaterialDescriptors.cpp:17-37](../../../src/renderer/MaterialDescriptors/MaterialDescriptors.cpp) | One string per id, sized `MAT_COUNT` |
| Road tag is produced | [SceneGeometry.cpp:150](../../../src/renderer/SceneGeometry/SceneGeometry.cpp) | `MAT_ASPHALT` gives 1, all else 0 |
| Road tag is written | [gbuffer.frag:118](../../../shaders/gbuffer.frag) | `outMaterial.a = push.material.w` |
| Material target format | [PostProcessManager_Images.cpp:50](../../../src/renderer/PostProcessManager/PostProcessManager_Images.cpp) | `R8G8B8A8_UNORM`: 256 steps per channel |
| Road tag is read | [lighting.frag:272](../../../shaders/lighting.frag), [ssr.frag:67](../../../shaders/ssr.frag) | Only use today: puddles |
| Puddle gate | [puddle.glsl:20](../../../shaders/common/puddle.glsl) | `road < 0.5` means no puddle |
| Spot-light loop | [lighting.frag:552-589](../../../shaders/lighting.frag) | Inside `#ifdef SWISH_DEBUG_UI` until L6.3 promotes it |
| Spot struct | [lights.glsl:14-19](../../../shaders/common/lights.glsl) | No change needed |

All four `outMaterial` channels are taken (metallic, roughness, wettable, road tag). The road tag only uses the
values 0 and 1, so it has room for a third value.

**Use 0.25, not 0.5.** In 8 bits, 0.5 stores as 127 or 128. That lands on either side of the puddle gate
(`road < 0.5`), so markings would get puddles on one GPU and not on another. 0.25 stores as 63 or 64
(0.247 or 0.251). Both are far from 0, 0.5 and 1. Puddles stay off the paint, exactly as today.

## Steps

1. **L8.1** Add the material id. Open `src/scene/SceneTypes.h`. Add `MAT_MARKING` after the `MAT_CAR_LAST` line and before `MAT_COUNT`. Add `kRetroTag` next to it (snippet A).
2. **L8.1** Give it a texture name. Open `MaterialDescriptors.cpp` and append `"default"` to `kMaterialNames`. The array is sized `MAT_COUNT`, so the compiler tells you if you forget.
3. **L8.1** Tag the markings. In `CorridorScene.cpp` pass `MAT_MARKING` to the four edge-line calls and the two dash calls (snippet B).
4. **L8.1** Emit the tag. In `SceneGeometry.cpp:150` write `kRetroTag` for `MAT_MARKING` (snippet C). Build and run `make test`. Nothing visible changes yet.
5. **L8.2** Decode the tag in `lighting.frag`, next to `float road` (snippet D).
6. **L8.2** Add the lobe at the end of the spot loop body (snippet E). Start with $n = 180$ and $R_L = 0.15$.
7. **L8.2** Owner check at night with headlights on. Tune $n$ first (how far to the side the glow survives), then $R_L$ (how bright).
8. **L8.3** Blocked. Signs need corridor signage from endless-road Layer 5 / Phase 3c ([endless-road.md](../../endless-road.md)). When signs exist, give them their own material id that also maps to `kRetroTag`, with a higher $R_L$. Do not start this now.

## Snippets

**A. `src/scene/SceneTypes.h`, inside `enum MaterialId`, between `MAT_CAR_LAST = MAT_CAR_31,` and `MAT_COUNT`:**

```cpp
    MAT_MARKING,  // retroreflective road paint (L8); textures reuse "default"
```

**A. Same file, after the enum:**

```cpp
// Road-tag values written to the G-buffer material alpha (8-bit UNORM). 1 = asphalt (puddles).
// 0.25 = retroreflective paint: stores as 63 or 64, clear of the puddle gate at 0.5.
inline constexpr float kRoadTagAsphalt = 1.0f;
inline constexpr float kRetroTag       = 0.25f;
```

**B. `src/scene/Corridor/CorridorScene.cpp:167` (repeat for lines 168 to 170 and the dashes at 179 to 182):**

```cpp
builder.emit_sweep_band(sl.frames, filled(n, m + si), filled(n, m + si + ew), my, look.yellow, MAT_MARKING);
```

**C. `src/renderer/SceneGeometry/SceneGeometry.cpp:150`, replacing the one line:**

```cpp
pushData.material.w = (dc.material == MAT_ASPHALT)   ? kRoadTagAsphalt
                      : (dc.material == MAT_MARKING) ? kRetroTag
                                                     : 0.0f;
```

**D. `shaders/lighting.frag`, directly under `float road = material.a;` (line 272):**

```glsl
// Road tag 0.25 = retroreflective paint (8-bit: 0.247 or 0.251). Band 0.125..0.375 excludes 0, 0.5 and 1.
float retro = step(0.125, road) * step(road, 0.375);
```

**E. `shaders/lighting.frag`, inside the spot loop, after the last `lit_color +=` (line 587):**

```glsl
        // Retroreflection (L8.2): bright only when the eye is near the lamp. No NdotL: beads work at grazing.
        if (retro > 0.0 && NdotL_s > 0.0) {
            const float kRetroExp = 180.0;  // half brightness at 5 degrees: ln(0.5) / ln(cos(5 deg))
            const float kRetroRL  = 0.15;   // cd/m2 per lux; US minimum is 0.05
            float gain = pow(clamp(dot(V, L_s), 0.0, 1.0), kRetroExp);
            lit_color += albedo * kRetroRL * gain * sCol * sInt * att;
        }
```

`albedo` is the paint colour, so yellow lines return yellow light. `sCol * sInt * att` is the beam's illuminance at the pixel.

## Tests to turn green

L8 has no stub and no hidden tests. Two small GPU-free tests are worth adding:

- **A C++ mirror of the gain curve.** A header-only `retro_gain(cosObs, n)` and `retro_exponent(halfAngleDeg)` in `src/utils/light/`. Check: gain is 1 at 0°, 0.5 at the half angle, below 0.01 at 20° for $n = 180$.
- **The tag survives 8 bits.** For stored values 63/255 and 64/255 the band test is true. For 0, 127/255, 128/255 and 1 it is false. This pins the reason for 0.25.

One existing test already helps: `tests/test_road_scene.cpp:96` checks every draw has `material < MAT_COUNT`.

## Gate

- [ ] `make test` green
- [ ] `make test-debug` green
- [ ] 0 warnings
- [ ] `make check` OK
- [ ] Golden: if the spot loop is still inside `#ifdef SWISH_DEBUG_UI`, keep snippet D inside it too and `release_spv.sha256` must not change. If L6.3 already promoted the loop, `lighting.frag.spv` changes: regenerate on purpose and note it
- [ ] Changelog entry in `docs/changelog/2026-11.md`, then `python3 scripts/changelog_index.py`

In the app (owner), evening or midnight preset, headlights on:

- [ ] Cockpit view: dashed lines 30 to 80 m ahead are clearly brighter than the asphalt beside them
- [ ] Chase view: the glow is weaker than in the cockpit but still there
- [ ] Orbit the camera to the side of the car: the glow fades to plain paint
- [ ] Brake: lines behind the car pick up a faint red return, not a white one
- [ ] Daytime preset, lights off: markings look exactly as before
- [ ] Rain 1.0: no puddle mirrors appear on the paint

## Pitfalls

- **Line 226 of `lighting.frag` is stale.** The comment says puddle coverage is 0 in release. The literal at line 137 is 0.501, so release does have puddles. The road tag is live in release. Treat it with care.
- **Snippet D outside the `#ifdef` changes the release `.spv`** even if nothing reads `retro`. Keep it inside the gate until the spot loop leaves the gate.
- **`MAT_COUNT` grows by one.** It sizes the debug material-override table and the preset files (`DebugParamsIO.cpp:190, 265`). Appending at the end keeps old slot numbers valid. Do not insert in the middle.
- **Tag only `CorridorScene`.** The retired `RoadScene` also paints markings, and `tests/golden/road_mesh.txt` pins its per-material counts. Leave it on `MAT_DEFAULT`.
- **Point lights get no retro lobe.** Street lamps sit high above the eye, so their observation angle is large. That is correct. Do not copy the lobe into the point loop.
- **Keep the `clamp` in snippet E.** `dot(V, L_s)` can land slightly above 1 from rounding, and `pow` with $n = 180$ turns that into sparkle.
- **Bloom will find the lines.** If they bloom too much, lower $R_L$. Do not touch the bloom threshold here.

## Resources

- [Federal Register 2022-16781, "Maintaining Pavement Marking Retroreflectivity"](https://www.govinfo.gov/content/pkg/FR-2022-08-05/html/2022-16781.htm): read the summary for the 50 and 100 mcd/m²/lx minimums and the 30 m geometry (entrance 88.76°, observation 1.05°).
- [FHWA-HRT-07-059, chapter 6](https://www.fhwa.dot.gov/publications/research/safety/07059/06.cfm): read Table 11 for recommended $R_L$ by speed.
- [MUTCD 2009, chapter 2A](https://mutcd.fhwa.dot.gov/htm/2009/part2/part2a.htm): read Section 2A.08 and Table 2A-3 for sign sheeting levels (for L8.3 later).
- [Filament: Physically Based Rendering](https://google.github.io/filament/Filament.md.html): read "Lighting > Direct lighting > Punctual lights" to see where a per-light lobe fits.
