# TODAY (2026-10-17): TEMPORARY, DELETE TONIGHT

> Scratch plan for one session. Not part of `tasks/todo.md`. At the end: move anything unfinished into
> `tasks/todo.md`, then `git rm`/delete this file. Owner = you (app runs, screenshots, learning exercises);
> Claude = fixes marked **[C]**. Visual rule: temp-force extreme, window-only screenshot, judge, REVERT the force,
> rain at 0 and 1.0.

## Order of the day
Morning: A1-A3 + C1-C2 (quick wins). Midday: C3 (CSM) with A4. Afternoon: B1-B2 with A5-A6. Evening: A7-A8, gate, changelog, delete this file.

## Track A: photorealism as math and computation (you learn; [C] explains)
Core idea: photorealism = approximating the rendering equation within a perceptual error budget, in real time.
$$L_o(x,\omega_o)=L_e+\int_\Omega f_r(x,\omega_i,\omega_o)\,L_i(x,\omega_i)\,(n\cdot\omega_i)\,d\omega_i$$
Each checkpoint: read, derive on paper, find it in code, tweak ONE slider and watch. Course: `docs/realism-learning-plan.md` M2/M3.

- [ ] A1 Radiometry vs photometry: radiance, irradiance, light units. Code: `shaders/common/lights.glsl`
- [ ] A2 BRDF: Lambert + GGX (D, Smith G, Schlick F), energy conservation, F0 = 0.04, wet Fresnel (`shaders/lighting.frag` ~389)
- [ ] A3 Why not integrate live: Monte Carlo variance -> split-sum IBL (irradiance cube, prefiltered cube, BRDF LUT; set 3)
- [ ] A4 Shadows as the visibility term: CSM, PCF, bias (feeds C3)
- [ ] A5 Participating media: Beer-Lambert / Koschmieder fog, airlight (feeds B2)
- [ ] A6 Sensor and display: exposure (EV), HDR -> AgX, sRGB; why energy "stacks past the shoulder" (feeds B1). Code: `shaders/composite.frag`
- [ ] A7 Perception and time: dithering vs banding, TAA jitter (Halton `utils/math/Sequences.h`)
- [ ] A8 Cost view: error vs cost per pixel; what Swish approximates (IBL, SSR, SSAO) and why no RT on MoltenVK
- [x] Artifacts published (private):
  - Math notebook (8 checkpoints, live sliders): https://claude.ai/artifact/U3jGhfi8UfLD1vqogjGHmV
  - Shader Lab (coding-first: build a lit sphere term by term, 4 repo exercises, 6 optional math challenges): https://claude.ai/artifact/CUMnfZpSEpnazwHvsVbmUH
- [ ] Optional code: promote `kHaloStrength` to a tunable; CIE overcast sky (M3). Hidden `[.learning]` tests are NOT today's gate.

## Track B: lighting and realism (one variable group at a time)
- [ ] B1 **[C]** E2 ambient/reflection energy rebalance + Fresnel-bounded sheen. `shaders/lighting.frag` (~419-430 ambient, ~573-580 wet sheen), `shaders/composite.frag`. Do NOT fix by lowering exposure. Lead: `wetSheen = Fgraze * skyIrr * ambient * wetLocal` (`lighting.frag` 578-581) is a second Fresnel-times-sky term on top of the IBL specular (line 444); bound the sum (deep dive ch. 08). Also question: ambient (~430) adds `sun_radiance` without `sunShadow`. Intended `.spv` change: needs changelog; goldens only via `SWISH_UPDATE_GOLDENS=1` for that. Verify: road, paint, shadow contact, rain 0 and 1.0.
- [ ] B2 **[C]** Haze / far-road tuning (small), depends on B1. `lighting.frag` aerial haze.
- [ ] B3 **[C]** Glass sun-disc set-4 bind (debug-only, small): `src/renderer/GlassPass/GlassPass.cpp` (has your uncommitted edit: read first) + `shaders/glass.frag`. Verify: sun-disc sliders move the windshield sky.
- [ ] B4 Stretch: car lights S5 lens glow (`*_Lens` emissive).
- [ ] Doc hygiene (optional): `docs/user-goals/CURRENT.md` (dated 09-29), `tasks/todo.md` "What's next" item 3 (shipped), `lighting.frag:36` IBL comment.

## Track C: road fixes
- [ ] C1 **[C]** B3 road-config narrowing + fallbacks (small, unit-testable): `src/scene/Corridor/CorridorDriver.cpp:124`, `CorridorScene.cpp:48`. `road.bin` stays byte-identical.
- [ ] C2 **[C]** B2 persist the 14 unsaved sliders + clamp out-of-range on load: `src/debug/DebugParamsIO.cpp`, test in `tests/test_debug_params_io.cpp`.
- [ ] C3 **[C]** CSM near-cascade blackout (medium, the one confirmed bug): `Renderer::computeCascades` / `recordShadowPass`, shadow block in `lighting.frag`. Repro: `make debug`, `sunElevation` ~0.5, near road goes black, far lit. Suspect structure first. Not PCSS / exposure / reverse-Z. Two leads (deep dive ch. 06): (1) one normalized `SP_SHADOW_BIAS` 0.0018 for all cascades, so the near cascade's world-space bias is tiny; (2) `GeometryProducer::compute_cascades` (`src/renderer/GeometryProducer/GeometryProducer.cpp`) uses `lookAt(..., up=(0,1,0))`, which degenerates as the sun nears the zenith.
- [ ] C4 Only if time: endless-road seams (`docs/endless-road.md` ~104): post-spacing hiccup, intro-to-chunk UV seam, per-slot chunk lights.
- Out of scope today: 3b/3c interchanges, LOD/impostors.

## Track D: tutorial, realistic lighting and reflections (from Filament, glTF, Sascha Willems)

Goal: make the car paint, wet road and sky read as physically plausible. Every step is a small, visible change you can
judge by eye. Read the Filament and glTF code next to the step, then change one thing in `lighting.frag`.

### D0. Where Swish is today (read first)
- Ambient: [shaders/lighting.frag](../shaders/lighting.frag) lines 419–431. `ambientIrr = sun_radiance + skyIrr * SP_IBL_DIFFUSE`.
  The sun colour is added as ambient fill. Filament keeps indirect light separate from direct light, in
  [surface_light_indirect.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_indirect.fs).
- IBL specular: `lighting.frag` lines 436–444. Split-sum: `envColor * (F0 * envBRDF.x + envBRDF.y)`, with **no
  multiple-scattering term**.
- Wet sheen: `lighting.frag` lines 576–581, `wetSheen = Fgraze * skyIrr * ambient * wetLocal`, added to the IBL
  specular.
- Point lights: `lighting.frag` lines 484–520, windowed inverse-square (see [shaders/common/lights.glsl](../shaders/common/lights.glsl)).

### D1. Setup (5 min)
1. `make debug`. Drive the rigged car (default), with the cockpit camera (`C`).
2. Set rain to 0 (`R`) and keep a note of the look. Repeat at 1.0 for anything wet.
3. Take window-only screenshots only (`screencapture -R <window bounds>`, never the full display).
4. Temporarily force one value to an extreme, judge, then revert the force before moving on.

### D2. Make the sun stop doubling as ambient (highest value, from Filament's split)
Filament's shading keeps direct and indirect light apart. Swish adds the sun's colour to the ambient term (D0).

Try (proposed change, not applied):

```glsl
// was: vec3 ambientIrr = sun_radiance + skyIrr * SP_IBL_DIFFUSE;
vec3 ambientIrr = skyIrr * SP_IBL_DIFFUSE;   // sky only; the sun is the direct term below
```

Expect: shadows go deeper and the road looks less washed out. The sky-only fill may also come out too dark, so
re-tune `SP_IBL_DIFFUSE` or the ambient scale, not exposure. Verify: road, car paint, shadow contact, rain 0 and 1.0.

### D3. Multiple-scattering energy compensation for the IBL specular (Filament, the key formula)
GGX drops energy at high roughness because it ignores light that bounces between microfacets. Filament compensates:

- Filament: `energyCompensation = 1 + f0 * (1/dfg.y - 1)`
  ([surface_shading_lit.fs:269](https://github.com/google/filament/blob/HEAD/shaders/src/surface_shading_lit.fs)), applied to the
  indirect specular ([surface_light_indirect.fs:759](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_indirect.fs)).
- **Convention trap:** Filament's `dfg.y` is the F0 = 1 albedo. In Swish's LUT, that is `envBRDF.x + envBRDF.y`,
  because Swish writes the split-sum as `F0 * envBRDF.x + envBRDF.y`.

Try (proposed):

```glsl
float Ess        = envBRDF.x + envBRDF.y;                  // F0 = 1 albedo (Filament dfg.y)
vec3  energyComp = 1.0 + F0 * (1.0 / max(Ess, 1e-4) - 1.0);
lit_color       += envColor * (F0 * envBRDF.x + envBRDF.y) * energyComp * SP_IBL_SPECULAR;
```

Expect: rough asphalt and matte trim get a little brighter, and the effect is largest at grazing angles. Keep it
behind a debug tunable first, so the release build stays byte-identical until you approve it.

### D4. Dominant reflection direction for rough surfaces (Filament)
A rough surface should reflect a wider cone around the mirror direction. Filament blends `R` toward `N`:
[surface_light_indirect.fs `getSpecularDominantDirection`](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_indirect.fs)
(`mix(r, n, roughness * roughness)`).

Swish uses a plain `R = reflect(-V, N)` at `lighting.frag` line 440. Try:

```glsl
vec3 R = normalize(mix(reflect(-V, N), N, roughness * roughness));
```

Expect: glossy paint is unchanged, rough wet road gets a softer, more diffuse sheen. Check the car paint still has
crisp highlights at mip 0.

### D5. Roughness-aware Fresnel in the ambient term (Sascha Willems)
Sascha's PBR IBL shader uses a roughness-clamped Fresnel for the ambient diffuse split,
`F_SchlickR(cos, F0, roughness) = F0 + (max(1 - roughness, F0) - F0) * (1 - cos)^5`
([pbribl.frag](https://github.com/SaschaWillems/Vulkan/blob/HEAD/shaders/glsl/pbribl/pbribl.frag), around line 74).
This keeps grazing rough surfaces from losing their diffuse term. Use it only for the kD split in the ambient term,
not for the specular.

### D6. Bound the wet sheen (today's B1 lead, learned from the glTF specular stack)
`wetSheen` (lines 576–581) is a second Fresnel-times-sky term on top of the IBL specular. Do not just lower exposure.
Options to try, one at a time:
- (a) Scale the existing IBL specular by `1 + wetness * k` instead of adding `wetSheen`.
- (b) Take `max(existing_specular, wetSheen)` per channel, so the two cannot stack.
Pick the one whose road looks right at rain 0 and 1.0.

### D7. Point-light falloff, the Filament way (from the earlier answer)
Filament's `getDistanceAttenuation` in
[surface_light_punctual.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_punctual.fs)
applies a far window and then divides by `distanceSquare`. Swish's `dRef = 0.3 * radius` window (`lighting.frag`
line 502) is the same idea with a different reference distance. Experiment with the reference distance on one lamp
at a time, and leave the 32-light budget alone.

### D8. Reflections: check what SSR already does
Swish has SSR plus IBL (`shaders/ssr.frag`, see [docs/realism-features.md](realism-features.md)). Filament's
screen-space reflection is a McGuire–Mara ray march
([surface_light_reflections.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_reflections.fs)).
Compare the two on a mirror-flat puddle. Tutorial task: find where SSR fades out, and whether the IBL fallback meets
it without a seam.

### D9. Verify like a physicist, not just by eye
- **Numeric oracle:** Khronos's
  [glTF-Sample-Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer/tree/HEAD/source/Renderer/shaders)
  implements the same lobes in small files. Render one material sphere in both and compare the luminance ratio at
  three angles. Start with `brdf.glsl`, `ibl.glsl`, `pbr.frag`, and `material_info.glsl`.
- **Sphere test:** a white dielectric sphere under the sun should show a clear Fresnel rim, and no extra glow in the
  shadow.
- **Gate:** any `.spv` change needs a changelog entry, and goldens change only through `SWISH_UPDATE_GOLDENS=1` for
  an intended behaviour change (see `tests/golden/README.md`). Run `make test` and `make test-debug`.

### Links to click, in order
1. Filament shading model overview: [docs/Filament.md.html](https://github.com/google/filament/blob/HEAD/docs/Filament.md.html)
   (the material system chapter covers energy compensation and the split-sum IBL).
2. Filament shaders: [surface_shading_lit.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_shading_lit.fs),
   [surface_light_indirect.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_indirect.fs),
   [surface_light_punctual.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_punctual.fs),
   [surface_light_reflections.fs](https://github.com/google/filament/blob/HEAD/shaders/src/surface_light_reflections.fs).
3. Khronos glTF renderer: [brdf.glsl](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/HEAD/source/Renderer/shaders/brdf.glsl),
   [ibl.glsl](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/HEAD/source/Renderer/shaders/ibl.glsl),
   [pbr.frag](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/HEAD/source/Renderer/shaders/pbr.frag).
4. Sascha Willems: [pbribl.frag](https://github.com/SaschaWillems/Vulkan/blob/HEAD/shaders/glsl/pbribl/pbribl.frag),
   [genbrdflut.frag](https://github.com/SaschaWillems/Vulkan/blob/HEAD/shaders/glsl/pbribl/genbrdflut.frag) (how the
   BRDF LUT is built), and the example
   [pbribl.cpp](https://github.com/SaschaWillems/Vulkan/blob/HEAD/examples/pbribl/pbribl.cpp).
5. Shadows if you touch C3: [shadowmappingcascade.cpp](https://github.com/SaschaWillems/Vulkan/blob/HEAD/examples/shadowmappingcascade/shadowmappingcascade.cpp).
6. Papers: Karis, *Real Shading in Unreal Engine 4* (split-sum; already in
   [realism-learning-plan.md](realism-learning-plan.md)); Heitz et al., *Multiple-Scattering Microfacet BSDFs with the
   Smith Model* (SIGGRAPH 2016, the basis for energy compensation).

### Done when
- [ ] D2 and D3 each tried at rain 0 and 1.0, screenshots judged, reverted or kept behind a tunable
- [ ] D6 (wet sheen) bounded, with one option chosen by eye
- [ ] `make test`, `make test-debug`, `.spv` status recorded, changelog entry for any approved change

## Track E: test and tune (owner), keyboard steering + SSAO (built 2026-10-18, values are yours to set)

Both are built and pass the tests (release 387, debug 412, release `.spv` 33/33 unchanged, 0 Vulkan validation
messages). Nothing is tuned by eye yet: play with the knobs below, keep what feels right.

### E1. Keyboard steering (`config/car.toml` `[keyboard_steer]`)
What changed: an arrow key now ramps a "virtual stick", and a fully held key steers at most `grip_gain` x the angle
where the tyres reach their grip limit. A tap at speed is a small correction instead of 9 deg of lock.
Gamepad, replay and bench input are unchanged. Code: [KeyboardSteer.h](../src/scene/Entity/KeyboardSteer.h),
`CarEntity::keyboard_input_frame`.

$$\delta_{full}(v)=\min\Big(\text{lock},\ \max\big(\text{min\_deg},\ \text{grip\_gain}\cdot\arctan\tfrac{L\,\mu\,g}{v^2}\big)\Big)$$

With the defaults on the 993 (L = 2.259 m, dry mu ~1.05): parked = full lock (29.5 deg), 50 km/h ~ 10 deg,
100 km/h ~ 2.6 deg. In the rain mu drops, so the limit shrinks by itself.

**How to change a value:** edit `config/car.toml`, then rerun `make debug` (or `make run`). The game reads the copy
in `build*/config/`, which CMake refreshes on rebuild, so a plain restart does not pick up the edit. There is no live
slider yet.
**Test pin:** `tests/test_keyboard_steer.cpp` checks that `car.toml` equals `kKeyboardSteerDefaults`. If you change a
default in the TOML, change the C++ default too (or the test fails on purpose).

| Key | Default | Range (clamped) | What it does | Try |
|---|---|---|---|---|
| `rise_rate` | 2.5 /s | 0.1–20 | How fast a held key builds to full (0.4 s) | 1.5 (gentler) to 4 (snappier) |
| `return_rate` | 6.0 /s | 0.1–50 | How fast the stick recentres on release or reversal | 4–10 |
| `grip_gain` | 1.5 | 0.25–5 | Full key as a multiple of the grip limit; > 1 lets you provoke a slide | 1.0 (never slides) to 2.5 (drifty) |
| `min_deg` | 1.0 deg | 0–10 | Floor on full-key steer at very high speed / low grip | 0.5–2 |

What to test (cockpit `C`, rain with `R`):
- [ ] Parked: hold left/right -> full lock, the steering wheel turns ~2.7 turns lock to lock.
- [ ] 100 km/h, dry: quick taps -> small lane-change corrections, no spin.
- [ ] 100 km/h, dry: hold a key 2 s -> the car corners near the grip limit, at most a mild slide.
- [ ] 100 km/h, rain 1.0: same taps and holds -> still controllable (the limit follows mu).
- [ ] 150+ km/h: a held key should not snap the car round.
- [ ] Physics "rigid body (stub)" ticked: compare. It may still be twitchy; that model is a stub (RigidChassis.h TODO list).
- [ ] Driving tools: record a keyboard drive, replay it -> same path.

### E2. SSAO (debug panel -> SSAO; release has no SSAO, so `make run` is unaffected)
What changed:
- SSAO now runs before Lighting.
- It darkens only the indirect light (sky / IBL ambient, IBL and clearcoat reflections, wet sheen), not the sun,
  headlights, SSR or fog.
- The radius default is 400 WU (was 1200).
- The bias grows with distance (no grainy far barrier and road).
- The blur stops at depth edges (no dark halo across silhouettes).
Code: `shaders/ssao.frag`, `shaders/ao_blur.frag`, `shaders/lighting.frag` (`AO_DIFFUSE` / `AO_SPEC`).

| Knob | Where | Default | What it does | Try |
|---|---|---|---|---|
| Enabled | panel | on | Off = AO forced to 1 | Toggle at the same view: sunlit road and paint must NOT change |
| Radius (WU) | panel | 400 | Contact reach (kernel taps reach ~0.43 x radius) | 200–800; > 1500 brings back the big blotches |
| Bias (WU) | panel | 50 | Near self-occlusion guard | 20–100; higher = less grain, less contact |
| Intensity | panel | 1.0 | Strength (indirect only now) | 0.5–1.5; the cabin headliner is the most sensitive |
| `kAoBiasPerDepth` | `ssao.frag` (rebuild) | 0.003 | Extra bias per unit of depth; AO fades out beyond ~50 m | 0.002–0.006 |
| `kDepthSigma` | `ao_blur.frag` (rebuild) | 0.1 | Blur depth tolerance (relative) | 0.05 (sharper edges, more grain) to 0.2 (smoother) |

To keep a panel value: save a TOML preset with the panel's Save / Load row (presets go to `~/.config/swish/presets/`),
or change the default in `DebugParams.h`.

What to test:
- [ ] Cockpit, rain 0: Enabled on vs off. Only shaded areas change (dash crevices, door pockets, footwell). The
      headliner gets darker because it is lit by ambient only. Decide whether that reads right; if not, lower Intensity.
- [ ] Chase / side camera: a soft contact shadow under the car, no rim of light around the body.
- [ ] Rain 1.0: the wet road stays reflective, with no dark grain on it.
- [ ] Far barrier / far road: clean (no grain). If grain returns at your radius, raise `kAoBiasPerDepth` a little.
- Before/after screenshots from the build session: `/tmp/ssao-compare.png` (before / after / off), and
  `/tmp/ssao-buffer2.png` (the AO buffer itself). They are deleted on reboot.

### E3. Noticed while testing (not fixed, check when you can)
- In most runs the camera cycled through all 8 modes in the first second (`[camera] chase ... cockpit` in the log =
  8 `C` presses through `App::on_cycle_camera_mode`). It also happened before these changes. Likely keystrokes
  reaching the window as it opens. Rule it out by launching with hands off the keyboard; if it still cycles, check
  `App::KeyEdge` / `process_key_bindings`.

### E4. Still owed before committing
- [ ] Changelog entries for E1 and E2 (`docs/changelog/2026-10.md`, then `python3 scripts/changelog_index.py`)
- [ ] `docs/car-rig.md` config-key table: add the `[keyboard_steer]` rows; `docs/realism-features.md` SSAO section

## End-of-day gate
- [ ] `make test` and `make test-debug` green (baseline 387 / 412 after Track E)
- [ ] 33/33 `.spv` unchanged, or intentionally changed with changelog (B1/B2)
- [ ] `make check` (known stray: `.cursor/`, `goals.md`)
- [ ] Owner screenshots, window-only, rain 0 and 1.0, day and horizon sun (B1, B2, C3)
- [ ] Changelog entries in `docs/changelog/2026-10.md`, then `python3 scripts/changelog_index.py`
- [ ] Delete this file
