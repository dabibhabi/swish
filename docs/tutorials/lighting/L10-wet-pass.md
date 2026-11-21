---
verified: 2026-11-14
against: 4f13766
owner: docs-scribe
---

# L10: wet pass

> **Drafted with AI assistance (Claude); owner review pending.** If this chapter and the code disagree, the code wins.

Back to the [index](00-index.md). Last phase. Needs [L9: remaining presets](L09-remaining-presets.md) signed off dry.

## Goal

Check all six presets at rain 0 and rain 1.0, then fix what the check finds. The wet model predates physical
units, so expect it to need work at night.

You will see: a wet road that is darker than dry, mirrors the sky at grazing angles by day, and carries long lamp and tail-light streaks at night. At rain 0 nothing changes from L9.

## Concept

Water changes asphalt in three ways:

1. **Darker.** Asphalt is porous. Water fills the pores, more light gets in, less gets out. Smooth, non-porous things (car paint, glass) do not darken.
2. **Shinier.** A thin water film lies on top. It is smooth, so it adds a sharp reflection the dry road never had.
3. **Weakly reflective head-on, strongly at grazing.** Water reflects only 2% straight down:

$$F_0 = \left(\frac{n - 1}{n + 1}\right)^2 = \left(\frac{1.33 - 1}{1.33 + 1}\right)^2 \approx 0.02$$

It approaches 100% at grazing angles. That is why the far road turns into a mirror and the near road does not.

A **layered** model keeps these apart. The film sits on top. Whatever the film reflects never reaches the base:

$$f_\text{wet} = f_\text{film}(F_0 = 0.02,\ \text{smooth}) + (1 - F_\text{film}) \cdot f_\text{base}(\text{darkened albedo},\ \text{original roughness})$$

Swish today uses one layer and bends its parameters instead (lower roughness, higher $F_0$). That is cheaper
but cannot show a sharp streak on top of a rough road.

Porosity can come from roughness, so no new texture is needed (Lagarde):

$$\text{porosity} = \text{clamp}(2.5 \cdot \text{roughness} - 1.25,\ 0,\ 1)$$

Roughness below 0.5 does not darken at all. Roughness above 0.9 darkens fully.

Two limits to know before judging:

- **SSR reflects only what is on screen.** A lamp head above the frame has no mirror image in a puddle. Its analytic highlight (from the light loop) is still there.
- **The "wet halo" is not fog glow.** It adds light to wet surface pixels near a lamp. Real halos come from bloom on bright emissive heads (L5).

## Where Swish is today

| What | File:line | Note |
|------|-----------|------|
| Wetness input | [lighting.frag:254](../../../shaders/lighting.frag) | `camera.camPos.w`, 0 to 1 |
| Per-pixel wet amount | [lighting.frag:292](../../../shaders/lighting.frag) | `wetLocal = max(wetness * wettable, puddle)` |
| Puddle mask | [lighting.frag:286-287](../../../shaders/lighting.frag), [puddle.glsl:19-24](../../../shaders/common/puddle.glsl) | Road tag only; fades in with wetness |
| Darkening | [lighting.frag:300-303](../../../shaders/lighting.frag) | Fixed floor 0.631, applied to every wettable pixel |
| Roughness | [lighting.frag:305-309](../../../shaders/lighting.frag) | Release factor is 1.0 (line 136): the film does nothing. Only puddles go to 0.04 |
| Normal flatten | [lighting.frag:313-315](../../../shaders/lighting.frag) | Up-facing surfaces only |
| Wet $F_0$ | [lighting.frag:401](../../../shaders/lighting.frag) | Goes 0.04 to 0.06. Water is 0.02 |
| Sky and reflection dim | [lighting.frag:262-263, 460](../../../shaders/lighting.frag) | Overcast greying, scaled by wetness (see L0.5) |
| Wet halo, point lights | [lighting.frag:546-547](../../../shaders/lighting.frag) | `att * att * wetLocal * 0.12` |
| Wet halo, spot lights | [lighting.frag:585-586](../../../shaders/lighting.frag) | Same constant |
| Grazing sheen | [lighting.frag:596-599](../../../shaders/lighting.frag) | Reflects sky irradiance, not lamps |
| Rain fog | [lighting.frag:619-621](../../../shaders/lighting.frag) | Off in release: `SP_FOG_MAX` is 0 (line 130) |
| SSR wet gate | [ssr.frag:72-82](../../../shaders/ssr.frag) | Release SSR fires only where `wetLocal` > 0 |
| Car is dry | [CarEntity.cpp:707](../../../src/scene/Entity/CarEntity.cpp), [SceneGeometry.cpp:143](../../../src/renderer/SceneGeometry/SceneGeometry.cpp) | `dc.dry = true` for every submesh |
| Wet tunables | [Tunables.def:57-60](../../../src/debug/Tunables.def) | porosity, roughness, puddles, coverage |
| Lens wetness model | [LensWetness.h](../../../src/renderer/WindshieldRainPass/LensWetness.h) | CPU only, debug only, not sent to any shader |

More background: [docs/rain/README.md](../../rain/README.md).

## Steps

1. **L10.1** Run the matrix in the Gate section as it is. Change nothing. Write one line per cell: fine, or what is wrong. This list drives the rest.
2. **L10.1** Confirm rain 0 equals L9. Every wet term is multiplied by `wetLocal`, so the six dry cells must match your L9 screenshots.
3. **L10.1** Make darkening depend on porosity (snippet A). This is what lets the car get wet without turning grey.
4. **L10.1** Try a wet car body. In `CarEntity.cpp:707` change to `dc.dry = s.is_interior;`. Look at rain 1.0. If the cabin still washes out, revert this one line and note why.
5. **L10.2** Add the water film as its own layer for the spot lights only (snippets B and C). Look at headlight and tail-light streaks at midnight.
6. **L10.2** If step 5 looks right, use the same call in the point-light loop (line 549) and the sun term (line 447). Then set line 401 back to a constant 0.04.
7. **L10.2** Re-judge the wet halo (lines 546 and 585). With physical lamp values 0.12 may be far too strong. Lower it or delete it. Bloom on emissive heads should carry the halo.
8. **L10.2** Dim the retro lobe from L8 when wet (snippet D). Flooded paint loses its beads' effect. This is real: wet markings are hard to see at night.
9. **L10.2 (optional) Lens rain.** `LensWetness.h` computes a per-lens coverage but nothing draws it. Skip unless the lenses look wrong at rain 1.0.
10. Re-run the full matrix. Sign off.

## Snippets

**A. `shaders/lighting.frag`, replacing line 303 (`albedo = mix(albedo, wetAlbedo, wetLocal);`):**

```glsl
    // Porosity from roughness (Lagarde): smooth surfaces (paint, glass) do not darken, metals never do.
    float porous = clamp(2.5 * roughness - 1.25, 0.0, 1.0) * (1.0 - metallic);
    albedo       = mix(albedo, wetAlbedo, wetLocal * porous);
```

This must stay above line 305, where roughness is changed.

**B. `shaders/lighting.frag`, after `ggxSpecular` (line 244):**

```glsl
// Layered wet BRDF (L10.2): a smooth water film (F0 0.02) over the base. Light the film reflects
// never reaches the base. Returns a BRDF value; the caller multiplies by NdotL and radiance.
vec3 wetLayered(vec3 baseBrdf, float NdotH, float NdotV, float NdotL, float HdotV, float wet) {
    const float kWaterRough = 0.08;  // rain-ruffled film; lower = sharper streaks
    float Fw   = 0.02 + 0.98 * pow(1.0 - HdotV, 5.0);
    vec3  film = ggxSpecular(NdotH, NdotV, NdotL, HdotV, kWaterRough, vec3(0.02));
    return mix(baseBrdf, baseBrdf * (1.0 - Fw) + film, wet);
}
```

**C. `shaders/lighting.frag`, spot loop, replacing line 587:**

```glsl
        vec3 brdf_s = wetLayered(kD_s * albedo / PI + spec_s, max(dot(N, H_s), 0.0), NdotV, NdotL_s, HdotV_s, wetLocal);
        lit_color  += brdf_s * NdotL_s * sCol * sInt * att;
```

**D. `shaders/lighting.frag`, in the L8 retro lobe, change the last line to:**

```glsl
            lit_color += albedo * kRetroRL * gain * sCol * sInt * att * (1.0 - 0.7 * wetLocal);
```

**Step 4. `src/scene/Entity/CarEntity.cpp:707`:**

```cpp
        dc.dry         = s.is_interior;  // exterior gets wet; the cabin stays dry
```

## Tests to turn green

L10 has no stub and no hidden tests. Two small GPU-free tests you could add:

- **Porosity from roughness.** A C++ mirror of snippet A: 0 at roughness 0.5 and below, 1 at 0.9 and above, 0 for any metal.
- **Film Fresnel.** A C++ mirror of `Fw`: 0.02 head-on, 1 at grazing, never decreasing in between. Plus `f0_from_ior(1.33)` is 0.020 to three decimals.

Tests that already exist and will react:

- `tests/test_debug_params_io.cpp` pins the `SP_PUDDLE_COVERAGE` literal to the `Tunables.def` default. Change both or neither.
- The puddle mask has a C++ twin for wet grip (`src/scene/Entity/WetGrip.h`). If you touch `puddle.glsl`, mirror it there.

## Gate

- [ ] `make test` green
- [ ] `make test-debug` green
- [ ] 0 warnings
- [ ] `make check` OK
- [ ] Golden: every snippet here edits code that ships in release (A, B and the sun/point use of C). `lighting.frag.spv` changes: regenerate `release_spv.sha256` on purpose, once, at the end
- [ ] Changelog entry in `docs/changelog/2026-11.md`, then `python3 scripts/changelog_index.py`

In the app (owner). Wait for the wetness to settle before each screenshot. One window-only shot per cell, chase view; add hood and cockpit where a cell looks doubtful.

| Preset | Rain 0 | Rain 1.0 |
|--------|--------|----------|
| Dawn | [ ] | [ ] |
| Morning | [ ] | [ ] |
| Noon | [ ] | [ ] |
| Golden hour | [ ] | [ ] |
| Evening | [ ] | [ ] |
| Midnight | [ ] | [ ] |

What to judge at rain 1.0:

- [ ] Day: the road is darker than dry up close and mirrors the sky far ahead. It is not uniformly brighter
- [ ] Night: each lamp and tail light draws a streak on the road toward the camera
- [ ] Puddles: sharp mirror of on-screen lights; no lamp image when the lamp head is off screen (expected SSR limit)
- [ ] Car: paint keeps its colour (no grey veil); cabin is not washed out
- [ ] Lane lines: dimmer in the headlights than at rain 0, still visible
- [ ] Exposure: the frame does not pump brighter when rain starts

## Pitfalls

- **Three comments say puddles are off in release. They are stale.** `lighting.frag:226`, `gbuffer.frag:116` and `SceneGeometry.cpp:148-149`. The release literal is 0.501 (`lighting.frag:137`). Release has puddles today.
- **`SP_WET_*` literals must equal the `Tunables.def` defaults** (`Tunables.def:51`). Tune in the debug UI, then copy the final value to both places.
- **The car was made dry for a reason.** The comment at `CarEntity.cpp:695-699` says wet effects over-exposed the cabin. Step 3 removes one cause (darkening). The grazing sheen at line 596 is the other. If the cabin washes out again, gate the sheen, do not re-flag the whole car.
- **Do not layer twice.** Once the film is its own lobe, the old tricks (roughness factor at line 305, $F_0$ 0.06 at line 401) double the shine. Remove them in the same sitting as step 6.
- **Wetness lags the rain slider.** It builds and drains over time. A screenshot taken right after moving the slider shows a half-wet road.
- **`ssr.frag` has its own copy of the wet roughness rule** (line 75, factor 0.12). It does not read `SP_WET_ROUGHNESS`. If SSR and the lit pass disagree about how sharp the road is, look there.

## Resources

- [Lagarde, "Water drop 1: observe rainy world"](https://seblagarde.wordpress.com/2012/12/10/observe-rainy-world/): look at the night photos for what streaks and puddles should look like.
- [Lagarde, "Water drop 3a: physically based wet surfaces"](https://seblagarde.wordpress.com/2013/03/19/water-drop-3a-physically-based-wet-surfaces/): read why porous surfaces darken and where 0.02 comes from.
- [Lagarde, "Water drop 3b: physically based wet surfaces"](https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/): read the three-line shader that derives porosity from gloss (snippet A is its roughness form).
- [Filament: Physically Based Rendering](https://google.github.io/filament/Filament.md.html): read "Clear coat model" for the same two-layer energy rule on car paint.
