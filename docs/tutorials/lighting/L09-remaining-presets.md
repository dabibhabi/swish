---
verified: 2026-11-14
against: 4f13766
owner: docs-scribe
---

# L9: remaining presets

> **Drafted with AI assistance (Claude); owner review pending.** If this chapter and the code disagree, the code wins.

Back to the [index](00-index.md). Golden hour was signed off in [L4](L04-golden-hour.md). This chapter does the other five.

## Goal

Tune and sign off dawn, morning, noon, evening and midnight. This is a tuning chapter. The only new code is an
optional night look (L9.4).

You will see: six presets that each look like a real time of day from the chase, hood and cockpit views, switchable live in the debug UI.

## Concept

By now the renderer is physical. A preset is eight numbers in a TOML table. You should not need per-preset
shader hacks. If a preset looks wrong, the cause is almost always one of three things:

1. **The numbers are wrong.** Fix the TOML.
2. **Exposure is clamped wrong.** The auto-exposure wants to go outside `ev100_min` / `ev100_max`.
3. **A feature is missing.** For example midnight without emissive lamp heads. Go back to that chapter.

Exposure ties the numbers to the picture:

$$\text{exposure} = \frac{1}{1.2 \cdot 2^{EV_{100}}}$$

One stop of EV halves the image. Noon to midnight is about 15 stops, a factor of 30 000. That is why each
preset carries its own clamp.

What each preset looks like in real life (dry road, clear sky):

| Preset | Sun | Light on the road | EV100 | What correct looks like |
|--------|-----|-------------------|-------|-------------------------|
| Dawn | 2° up | A few thousand lux, mostly sky | 8 to 12 | Shadows about 30 times longer than the object, soft and blue. Pink-orange band at the sun, cool sky opposite. Lamps still on but weak against the sky. |
| Morning | 30° up | About 75 000 lux | 13 to 15 | Crisp shadows 1.7 times the object height. Light slightly warm. Blue sky, paler at the horizon. |
| Noon | 60° up | About 100 000 lux | 14 to 16 | Short shadows, 0.6 times the height. White light. Near-black under the car. Asphalt reads mid-grey, not white. |
| Golden hour | 6° up | About 20 000 lux | 11 to 14 | Signed off in L4. Use it as the reference for the others. |
| Evening | 4° below | Tens of lux, sky only | 3 to 8 | No sun shadows. Deep blue sky with an orange band low at the sunset side. Lamps and headlights now shape the road. |
| Midnight | Far below | Moon 0.2 lux; under a lamp 10 to 20 lux | -2 to 4 | Orange sodium pools with darker gaps. Sky nearly black. Headlight beam and tail lights are the brightest things. Lane lines glow (L8). |

Shadow length is $1/\tan(\text{elevation})$ times the object height. Use it as a quick check that the sun slider is wired.

## Where Swish is today

| What | File:line | Note |
|------|-----------|------|
| Preset tables | [time_of_day.toml:15, 25, 35, 45, 55, 65](../../../config/time_of_day.toml) | dawn, morning, noon, golden_hour, evening, midnight |
| File status | [time_of_day.toml:2](../../../config/time_of_day.toml) | "NOT LOADED YET" until L2.3 is wired |
| Preset struct | [TimeOfDay.h:44-53](../../../src/scene/TimeOfDay/TimeOfDay.h) | Defaults are golden hour |
| Preset enum and keys | [TimeOfDay.h:21-41](../../../src/scene/TimeOfDay/TimeOfDay.h) | Six values, TOML table names |
| Sun azimuth convention | [TimeOfDay.h:61-65](../../../src/scene/TimeOfDay/TimeOfDay.h) | From +Z toward +X, Y up |
| Exposure is applied | [composite.frag:120](../../../shaders/composite.frag) | `hdr *= params.exposure` |
| Tone map | [composite.frag:132](../../../shaders/composite.frag) | AgX, after white balance |
| Grade (saturation, contrast) | [composite.frag:145-149](../../../shaders/composite.frag) | Identity in release |
| Composite push block | [composite.frag:14-28](../../../shaders/composite.frag) | 13 floats = 52 bytes, not a 16-byte multiple (L0.11). Do not add fields here for L9.4 |

## Steps

Do one preset per sitting. For each one: switch to it in the debug UI, tune, take the three views, then write the
final numbers into `config/time_of_day.toml`.

1. **L9.1 Noon.** Needs [L3](L03-sun-shadows.md). Tune `sun_lux`, `turbidity`, then `ev100_min` / `ev100_max`. This is the hardest test of the shadow cascades: the sun is high, where the old blackout bug lived.
2. **L9.1 Morning.** Needs L3. Start from noon. Change `sun_elevation_deg` and `sun_azimuth_deg`. Lower `sun_lux` only if the sky model does not already do it.
3. **L9.2 Dawn.** Needs [L6](L06-local-lights.md). `lamps_on = true`. The sky does most of the lighting, so tune `turbidity` before `sun_lux`.
4. **L9.2 Evening.** Needs L6. `sun_lux = 0`. Only the sky model and the lamps light the scene. If the road is black between lamps, the sky term is too weak, not the lamps.
5. **L9.3 Midnight.** Needs [L5](L05-emissive.md), L6, [L7](L07-local-shadows.md), [L8](L08-retroreflection.md), plus the moon from L2.7. Set `ev100_max` first: it decides how bright the lamp pools read. Then `moon_lux`.
6. **L9.4 (optional) Mesopic night look.** Add the snippet below to `composite.frag`, debug-gated. Compare evening and midnight with it on and off. Keep it only if you like it.
7. Switch through all six presets in a row. Watch for a hitch on the IBL rebake and for exposure that takes too long to settle.

Which keys to touch, per preset:

| Key | Dawn | Morning | Noon | Evening | Midnight |
|-----|------|---------|------|---------|----------|
| `sun_elevation_deg`, `sun_azimuth_deg` | tune | tune | tune | tune (band position) | leave |
| `sun_lux` | tune | cross-check | cross-check | 0 | 0 |
| `moon_lux` | 0 | 0 | 0 | 0 | tune |
| `turbidity` | tune | small | small | tune | leave |
| `ev100_min`, `ev100_max` | tune | tune | tune | tune | tune first |
| `lamps_on` | true | false | false | true | true |

## Snippets

**L9.4 only. `shaders/composite.frag`, directly above `// Exposure adjustment` (line 119):**

```glsl
#ifdef SWISH_DEBUG_UI
    // Mesopic night look (L9.4): below about 3 cd/m2 the eye loses colour and shifts toward blue.
    // Must run BEFORE exposure: it depends on real scene luminance (cd/m2, valid once L1 landed).
    {
        const vec3  kRodTint     = vec3(0.85, 0.95, 1.20);  // blue-grey; tune by eye
        const float kMesopicMax  = 0.6;                     // 0 = off, 1 = full monochrome
        float lum   = dot(hdr, LUMA_709);
        float night = 1.0 - smoothstep(-2.0, 0.5, log(max(lum, 1e-6)) / log(10.0));  // 0.01 .. 3 cd/m2
        hdr = mix(hdr, vec3(lum) * kRodTint, night * kMesopicMax);
    }
#endif
```

Lamp pools and headlight beams sit above 3 cd/m², so they keep their colour. Dark verges go blue-grey.

**A final preset table, `config/time_of_day.toml` (shape only; the numbers are yours):**

```toml
[noon]
sun_elevation_deg = 60.0
sun_azimuth_deg   = 180.0
sun_lux           = 100000.0
moon_lux          = 0.0
turbidity         = 2.5
ev100_min         = 14.0
ev100_max         = 16.0
lamps_on          = false
```

## Tests to turn green

L9 has no stub and no new hidden tests. One L2 test already guards this file: "L2: the shipped config has six
valid presets" in [tests/test_time_of_day.cpp](../../../tests/test_time_of_day.cpp). It must stay green after every TOML edit.

Two small tests you could add:

- **Preset sanity.** For each shipped preset: `sun_lux == 0` when the elevation is below 0, and `lamps_on` is true when `ev100_max <= 8`. Catches a copy-paste slip between tables.
- **A C++ mirror of the mesopic weight** (only if you keep L9.4). Weight is 0 at 10 cd/m², 1 at 0.001 cd/m², and never increases with luminance.

## Gate

- [ ] `make test` green
- [ ] `make test-debug` green
- [ ] 0 warnings
- [ ] `make check` OK
- [ ] Golden: TOML tuning does not touch `.spv`. If L9.4 leaves the debug gate, `composite.frag.spv` changes: regenerate on purpose and note it
- [ ] Changelog entry in `docs/changelog/2026-11.md` (one per sitting is fine), then `python3 scripts/changelog_index.py`

In the app (owner), dry road. One window-only screenshot per box:

| Preset | Chase | Hood | Cockpit |
|--------|-------|------|---------|
| Dawn | [ ] | [ ] | [ ] |
| Morning | [ ] | [ ] | [ ] |
| Noon | [ ] | [ ] | [ ] |
| Evening | [ ] | [ ] | [ ] |
| Midnight | [ ] | [ ] | [ ] |

What to judge in each view:

- [ ] Chase: shadow length matches the table above; the car sits on the road, not floating
- [ ] Hood: paint reflects the right sky colour; no blown-out far road
- [ ] Cockpit: the sky through the glass matches the sky outside (L2.6); the dashboard is readable at night
- [ ] Evening and midnight: lamp heads glow, pools overlap as expected, lane lines return the headlights
- [ ] Switching presets: no visible hitch, exposure settles within a couple of seconds

## Pitfalls

- **Do not tune a preset against a broken dependency.** If noon shadows flicker, stop and go back to L3. Tuning around a bug bakes it into the TOML.
- **`sun_lux` becomes a cross-check.** The TOML comment (line 8) says the L2 sky model derives sun strength from elevation. If both are applied you get the sun twice.
- **Midnight EV can go negative.** Check the auto-exposure code accepts `ev100_min = -2.0` and does not clamp at 0.
- **The mesopic snippet needs physical units.** With the old arbitrary units every pixel is "dark" and the whole frame turns blue.
- **Do not add a mesopic strength field to the composite push block.** It is already not a 16-byte multiple (L0.11), and MoltenVK misbehaves silently. Use a constant until that block is fixed.
- **Judge night on the real display, in a dim room.** A dark frame on a bright desktop always looks too dark, and you will over-brighten it.

## Resources

- [Filament: Physically Based Rendering](https://google.github.io/filament/Filament.md.html): read "Imaging pipeline > Physically based camera > Exposure value" for EV100 and the 1.2 factor.
- [Patry, "Real-Time Samurai Cinema" (SIGGRAPH 2021)](https://www.advances.realtimerendering.com/s2021/jpatry_advances2021/index.html): read "Tonemapping > Purkinje Shift" for a production night look built on rod response.
- [Kirk and O'Brien, "Perceptually Based Tone Mapping for Low-Light Conditions"](https://objf.ai/papers/Kirk-PBT-2011-08/): read the abstract and the rod/cone model if you want L9.4 to be more than a tint.
