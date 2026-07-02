# ACTIVE — "Blender-look" realism pass (2026-07-01)

Goal: close the gap to Blender Material-Preview realism on the glossy black 911. Root cue is
**environment reflection** (paint mirroring the sky), NOT tonemapping (AgX already present) nor raw
resolution. Four features, user-approved, worked with agents on the isolated parts.

Orchestration: shared hot files (`lighting.frag`, `Renderer.cpp`, `PostProcessManager`,
`CameraUniforms`) force serial edits → agents author isolated NEW files; I integrate shared files
sequentially, building between each.

- [x] **1. Clear-day preset** — `G` toggles bright sunny day (deep-blue sky, high white sun, brighter
      ambient); dry (rain forced off, mutually exclusive with `R`). `weather.x`=clarity in CameraUBO.
      DONE: build clean, ctest 52/52, validation-clean. Visual taste-check pending.
- [x] **2. Sky reflections (IBL-lite)** — reflect `compute_sky_color()` about N, Fresnel + roughness
      weighted → the Blender gloss. `lighting.frag` only. DONE (build/tests/validation clean).
- [x] **(fog fix, mid-stream)** — rain fog was ~8× too dense (63% @ 150 m). Thinned to ~1.2 km, capped
      0.65, gated to 0 on clear day. Cabin re-wash from clear-day fixed (ambient 0.45→0.33 + cubic
      roughness-gate on reflection). Verified in-app (clear-day + heavy-rain cockpit screenshots).
- [x] **3. Sun shadows** — DONE + verified. Shadow map + light matrix + PCF wired. MoltenVK has NO
      comparison samplers → used plain sampler2D + manual PCF compare (the sampler2DShadow path was
      rejected). Roof self-shadows the cabin → fixed the interior overexposure. Works in all weather.
- [x] **Exposure trim** — composite exposure 1.0 → 0.45 + interior ambient 0.30→0.22 (clear 0.24) per
      user ("too conservative"). Verified darker in-app.
- [x] **4. SSAA** — DONE + verified. renderExtent = swap×1.5 (clamped to maxImageDimension2D);
      offscreen chain (G-buffer/HDR/lighting/forward/bloom) render-sized, composite downsamples to
      swapchain. Build clean, ctest 52/52, validation-clean.

## Review
All four features + fog/exposure fixes landed, each build-clean / ctest 52/52 / validation-clean and
visually verified via screenshots. Clear-day cabin now dark + contrasty (was over-exposed): the fix
chain was (a) whole-car dry mask, (b) roughness-gated sky reflection, (c) sun shadows binding (roof
self-shadows cabin — the real fix; MoltenVK has no compare samplers so manual PCF), (d) exposure 1.0→
0.45 + ambient cut. Fog thinned ~8× + capped + off on clear day. Bonus (reflections+shadows in rain):
reflections show on the wet road; shadows are weather-independent. NOT committed (awaiting user).
Tunables if further taste passes wanted: exposure 0.45, SSAA scale 1.5, shadow bias/frustum, reflection
strength.
- [ ] **4. Supersampling (SSAA)** — render offscreen chain at scale×, composite downsamples.

Rules: full-execution (user-authorized for Swish); CHANGELOG per feature; verify each (build, ctest
52/52, validation-clean) before next; update Obsidian RFI note (G-P1-2/G-P0-3/G-P1-3) when landed.

## Deferred — big GPU features (paused 2026-07-02, user requested, do 1 per turn)

All debug-UI tunable + release-safe like the landed realism features. Order = ROI/effort.
- [ ] **God-rays / volumetric light shafts** — sun screen-pos → occlusion-masked radial-blur pass,
      composited additively; later froxel volumetric fog. Debug density/decay/weight sliders. (Medium.)
- [ ] **Real HDRI IBL** — load an `.hdr` equirect → cubemap → irradiance convolution + specular
      prefilter mips + BRDF LUT → sample via the existing `skyIrradiance`/reflection hooks in
      `lighting.frag`. Needs an `.hdr` asset (or bake the procedural sky into the cubemap). (Large.)
- [ ] **Puddles + road spray** — screen-space puddle mask reflecting through the existing SSR; GPU
      particle spray/mist behind the car (compute). (Large.)
- [ ] **Motion vectors → TAA + motion blur** — velocity G-buffer target (prev vs cur clip pos),
      history buffer + reproject/neighborhood-clamp TAA (replacing SSAA), per-pixel motion blur. (Largest.)

Already landed this run (branch `debug-ui`, not pushed): live SSAA · toml presets · SSAO · CSM · IBL ·
SSR (+roughness gate) · per-material editor · sun gizmo · steering gizmo (+pitch/roll/quat) · auto-exposure.

## Next plans (post realism-pass, 2026-07-01)

Ordered by visual-impact-per-effort, building on what just landed.

**Lighting / reflections**
- [ ] **Reflection ↔ ambient energy rebalance** (Cursor "lever 2"): the sky reflection and the
      hemispheric sky-ambient (`skyFacing·skyTint·0.5`) both model sky light and aren't discounted
      against each other. Drop/shrink the ambient sky term now that the reflection covers it — prevents
      bright panels stacking past AgX's shoulder.
- [ ] **Live exposure control + auto-exposure**: bind keys to nudge exposure; then log-average /
      histogram auto-exposure so night vs day self-levels (Cursor "lever 3"; RFI C3).
- [ ] **Full HDRI IBL** — prefiltered environment cubemap + irradiance map + BRDF LUT so glossy paint
      reflects a *real* environment (trees/buildings), not just the analytic sky. The "true Blender"
      reflection (RFI G-P1-2 / G-P0-3).
- [ ] **SSR on the wet road** — screen-space reflections of on-screen lamps/geometry in wet asphalt
      (RFI C5 / G-P1-2); reuses the wet-road material.
- [ ] **SSAO** — wire ambient occlusion for contact/crevice darkening (RFI G-P1-4); the correct tool
      the sun shadow map doesn't cover (under the car, dash recesses).

**Shadows**
- [ ] **CSM (cascaded shadow maps)** — the current single 2048² frustum follows the camera (~45 m).
      Cascades for crisp long-range shadows down the 4.2 km highway (RFI G-P1-3).
- [ ] **Shadow polish** — soft shadows (PCSS), tighter frustum fit, bias auto-tune; confirm the car's
      ground shadow in free-fly and tune `halfExtent`/`depthRange`/bias/floor.

**AA / perf**
- [ ] **SSAA polish** — make render-scale a key/config; consider TAA or a proper downsample kernel
      for >1.5×; watch VRAM/frame-time at 2×.

**Weather / rain (bonus follow-through)**
- [ ] **Night-scene lamp tuning** (RFI G-P1-1 follow-up) — lamp intensity/falloff for a proper wet night.
- [ ] **Light-coupled rain streaks** (RFI R-P1-2) — streaks brighten/tint near lamps.
- [ ] **Windshield lens optics** (RFI R-P1-3) — refracted-ray displacement, TIR, chromatic aberration.

**Software robustness (deferred RFI)**
- [ ] Exception-safe subsystem destructors (S-P0-2); std140 `static_assert`s; delete dead `glslc_test`
      stub; `VK_CHECK_LOG` for teardown paths.

---

# Roadmap — rain realism · LIE extension · night-scene visual realism (2026-06-30)

Research-driven plan. Grounded in two cited briefs (AI-assisted research artifacts):
[`docs/research-rain-rendering.md`](../docs/research-rain-rendering.md) and
[`docs/research-night-scene-realism.md`](../docs/research-night-scene-realism.md). Each item links the
technique that motivates it. Ordered by **visual-impact-per-effort** within each track. Nothing here is
started yet — this is the plan to verify before implementation.

> Current baseline (committed this pass): rain = 2-layer parallax streaks + Heartfelt-style refractive
> windshield drops (small/dense beads) + persistent wetness map + visible wiper; road = 4.22 km LIE,
> nearest-N lamp lighting (`MAX_POINT_LIGHTS=32`); composite already does ACES tone-map + bloom; car =
> 911 Turbo S physics.

---

## Track A — Rain realism (next steps)

- [ ] **A1. Light-source-dependent streaks** — make falling-streak brightness/tint depend on the
      nearest deferred point lights so streaks *ignite* near streetlamps (Garg–Nayar photometric model;
      Halder ICCV 2019, rain brief §1). Feed the streak frag the same nearest-N lamp list `CameraUniforms`
      already builds. **Highest rain ROI.**
- [ ] **A2. Impact splashes / road spray** — short-lived splash sprites where streaks "hit" the road
      plane + a faint up-spray near the car (R4 collision splashes, rain brief §3). Adaptive density
      near the cockpit only (LOD).
- [ ] **A3. Lens raindrops as a final overlay** — distinct from the windshield pass: screen-space drops
      on the *virtual camera lens*, NOT cleared by wipers (Heartfelt as a lens pass; night brief §6).
      Subtle, gated low so it doesn't fight the windshield drops.
- [ ] **A4. Validate windshield drops vs Heartfelt reference** — confirm trail-cutting-through-fog and
      normal→UV-offset refraction match the canonical behaviour; check the von Bernuth contact-angle
      drop shape $h=\tan(\theta/2)\cdot d$ ($\theta\approx 87°$) for bead bulge (rain brief §2).
- [ ] **A5. Wetness-map mip blur** — the snapshot is single-LOD; wire `screenAndRefr.z` (already
      reserved) to a wetness-driven blur once the snapshot generates mips (rain/README "future work").

## Track B — Extend & enrich the Long Island Expressway

- [ ] **B1. Curved centerline** — replace the straight road with gentle arcs/clothoids so it reads as
      real I-495 (OpenDRIVE reference-line + $(s,t)$ model; Galin curvature constraints; night brief §5).
      Express lanes, lamps, signs as offsets along arc-length $s$ — one 1-D parameter drives the layout.
- [ ] **B2. Distance LOD + billboard impostors** for lamps/signs/guardrail posts marching to the
      horizon; geomorph/alpha-fade transitions, select by projected screen coverage (RTR4 Ch.19, night
      brief §5). Needed before extending further than 4.22 km without a frame-time hit.
- [ ] **B3. Roadside enrichment** — guardrails swept along the spline, more sign sets, an overpass or
      two, lane-wear/tire decals via deferred screen-space decals against the G-buffer (Wronski 2015,
      night brief §5).
- [ ] **B4. Network-first refactor** — treat the baked centerline as the single source of truth and
      *derive* lane stripes, lamp spacing, sign placement, and the exit-ramp split from it (Parish &
      Müller "network drives everything", night brief §5), instead of hand-placed Z positions.
- [ ] **B5. Optional further extension** — only after B2; with LOD in place, length becomes a config
      number again (`road.toml`).

## Track C — Night-scene visual realism (biggest "looks AAA" wins)

- [ ] **C1. Wet-asphalt BRDF** — single-pass dry↔wet lerp on diffuse/roughness/normal driven by
      `WetLevel` (reuse `RainSystem::get_wetness()`); porosity-masked so lane paint/metal barely change
      (Lagarde 3a/3b; Nakamae 1990 drive-sim wet-road; night brief §2). **Highest night-scene ROI** —
      turns the road from "dim grey" to "wet & reflective", reusing existing point lights for the
      stretched specular streaks.
- [ ] **C2. Bloom firefly suppression / quality pass** — confirm the existing bloom uses a
      downsample/upsample mip pyramid with Karis-average firefly suppression (bright lamps vs dark sky
      flicker without it) (Jimenez 2014, night brief §1). Upgrade if it's a naive blur.
- [ ] **C3. Verify/upgrade tone map** — composite is ACES per docs; confirm + add log-average
      **auto-exposure** so the dark road doesn't wash out or clip headlights (Hable/Narkowicz/Reinhard,
      night brief §3).
- [ ] **C4. Froxel volumetric fog** — view-aligned froxel volume, inject in-scattering from each lamp +
      headlight cone with a forward Henyey–Greenstein phase ($g\approx0.2$–$0.6$); composites as one
      texture lookup, cost independent of light count (Wronski 2014, Hillaire 2015, night brief §4).
      The defining rainy-night atmosphere. *(Cheap interim: Mitchell screen-space god-rays for the one
      hero on-screen lamp, night brief §4.)*
- [ ] **C5. SSR on the wet road** — half-res stochastic SSR (McGuire–Mara tracer + Stachowiak BRDF
      sampling + temporal/spatial filter) so wet asphalt reflects on-screen lamps; reuses C1's wet
      material (night brief §2). Planar-reflection RTT as the cheaper artifact-free fallback.
- [ ] **C6. Clustered light culling** — froxel light-assignment compute pass so dozens–hundreds of
      lamps down the road don't blow up the per-fragment light loop (Olsson 2012; UPenn CIS 565). The
      scalability backbone for B5 + C4. Pairs with **IES / range-windowed point lights** in physical
      units (Lagarde & de Rousiers 2014, night brief §1) for correct falloff + tight bounding spheres.
- [ ] **C7. Camera motion blur** — reconstruction filter from a reprojection velocity buffer; strongest
      per-frame speed cue (McGuire 2012). Budget speed cues across FOV/shake/streaks — the Disney study
      shows blur alone doesn't sell speed (Sharan 2013, night brief §6).
- [ ] **C8. Mesopic night look** — subtle rod/cone desaturation + Purkinje blue-shift on dark regions so
      the road *reads* as night, not just dim (Jensen 2000; Kirk & O'Brien 2011, night brief §3).
- [ ] **C9. Circular-bokeh DOF** (optional/cinematic) — keep dash in focus, distant headlights bloom
      into clean bokeh discs (Garcia GDC 2018, night brief §6).

## Sequencing notes
- **Do C1 + C2 + C3 first** — three mostly-shader changes that transform the look fast and underpin
  everything else (wet road needs the tone map/bloom to show).
- **C6 (clustered) gates B5 + C4** — get many-lights scalability before adding more lamps or per-light
  fog.
- **A1 reuses** the nearest-N lamp list already in `CameraUniforms` — cheap, high-impact, do early.
- Per the visual-feature lesson ([`lessons.md`](lessons.md)): "clean build ≠ done" — every item here is
  visually verified in-app (screenshot workflow) before being checked off, at rain extremes 0 and 1.0.

---

# Three-task pass — rain bugs/realism · 911 Turbo perf · +2 mi road (2026-06-29)

Plan: `~/.claude/plans/three-agents-to-work-encapsulated-stroustrup.md`. Three agents on disjoint
file sets (physics + road in parallel; rain last since it shares CarEntity/SceneTypes regions).

## Agent 2 — Porsche 911 Turbo performance ✅
- [x] Root cause: drag trap (`kAccel/kDragCoeff = 18000/2.5 ≈ 7200 WU/s ≈ 16 mph`), not the 30k clamp
- [x] `kDragCoeff 2.5→0.12`, `kMaxForwardSpeed 30000→92000` (205 mph), `kAccel 18000→12000` (0–60 ≈2.6 s), `kBrakeAccel 24000→36000`
- [x] Variable steering ratio `kSteerRefSpeed/(kSteerRefSpeed+|v|)` → stable at top speed, sharp when parking

## Agent 3 — Extend LIE +2 miles ✅
- [x] `road.toml` length_m 1000→4218.688; auto-baked via CMake `bake_configs`
- [x] Exit ramp + 6 signs re-expressed relative to `z_far` (ramp ~75%, signs 10–90%)
- [x] Lamp gen cap removed; nearest-N lamp selection in `CameraUniforms`; `MAX_POINT_LIGHTS 16→32` + arrays in lighting/basic.frag

## Agent 1 — Rain realism + bugs ✅
- [x] "Up at rest" was windshield mesh-UV gravity sign (NOT the mod wrap) → flipped gravity/aero Y
- [x] Blobs→streaks (less speckle, `kStreakLen 1200→3200`); animated gust wind
- [x] Visible wiper blade SDF + wider swept-sector clear + slower re-wet (manual V)
- [x] Whole-cabin light-gray wash via gbuffer alpha-sentinel + `is_interior` tag (world untouched)
- [x] Tuning round: fixed heavy-rain opaque veil → see-through beads; fixed near-white cabin → light gray

## Review
- **Verified in-app** (screencapture + CGEvent driving): build clean; app runs validation-clean; car
  loads (bbox 4.57×1.29×2.03 m). Car drives (was drag-capped); heavy rain road visible (veil fixed);
  interior reads light gray (white-out fixed); wiper blade sweeps + clears; road extends to horizon.
- **Two regressions caught in QA and fixed by a second Agent-1 pass**: heavy-rain windshield went
  opaque (the project's known "too thick" mode) and interior washed near-white. Lesson: always test
  rain at the **intensity extremes** (0 and 1.0), not just mid.
- **Tests:** 32/34 (2 pre-existing `RoadScene zero-*-config` failures, unrelated to these tasks).
- **Pending live taste-check by user**: heavy-rain bead *density* is conservative (kept usable); the
  windshield-drop "down at rest / up at speed" crawl is a deterministic sign flip — best confirmed by
  watching live motion.

---

# Realistic Rain Overhaul — parallax + retinal streaks + windshield trails + halos (2026-06-29)

## Context
Four disjoint improvements to the two rain systems. Agent 2 kept in RainSystem.cpp/.h
(two-draw far layer, no shader edit) so rain.frag stays exclusive to Agent 3 — all four
workstreams are parallel-safe. "premons" → full implementation.

## Plan
- [x] Agent 1 — `shaders/windshield_rain.frag`: lower l1 threshold + reduce l0 so clinging→
      sliding trails show at light/medium rain; verify trail/fog read.
- [x] Agent 2 — `RainSystem.cpp/.h`: second "far" parallax layer via 2nd draw + 2nd UBO
      (halfExt×2, dropSpeed×0.7, intensity×0.55, streakLen×0.8, time+kFarTimePhase); double
      descriptor pool size; cleanup far buffers. No shader/pipeline/Renderer/CMake change.
- [x] Agent 3 — `shaders/rain.frag`: average N=7 samples along the oscillation outline for
      multi-highlight speckled streaks (Rousseau). Kept additive + head/tail fade.
- [x] Agent 4 — `shaders/lighting.frag`: subtle additive halo around point lights scaled by
      wetness (rainy streetlight glow), in the point-light loop.
- [x] `./scripts/format.sh`, `make build` (clean — all 3 shaders compile, swish+tests link).
- [x] `make test` — 32/34 pass; 2 failures (`RoadScene zero-*-config`) are PRE-EXISTING and
      unrelated (working-tree RoadScene.cpp changes predating this work; no rain/lighting refs).
- [x] **Visual verification done in-app** (forced m_rainIntensity=1.0, screenshot, iterate, revert).
- [x] **Realism tuning pass** (windshield drops were ring outlines): folded trail into the height
      field (rivulets now refract), per-drop sky glint (was using macro normal → no per-bead glint),
      darkened lens body + cut haze; density 60→85, refraction 0.065→0.080; brighter falling streaks.
- [x] Docs: CHANGELOG.md (2 entries 2026-06-29), docs/rain/README.md (Part 1 + tunables incl. lens model).
- [x] **Realism round 2 — "too thick" → thin/translucent/delicate** (user feedback, grounded in
      Garg & Nayar / Rousseau / in-repo examples/DownPour). Windshield: combine drop layers with
      `max` not sum (the key fix), semi-transparent alpha cap (≤0.62), smaller drops, thinner
      rivulets, lighter haze/glint, refraction 0.080→0.070. Streaks: thinner width (3–8), lower
      opacity, thin core + head taper, drop the ×1.6 boost→×1.15, deeper internal speckle.
      Verified in-app at heavy AND light rain (overshot to near-clear, dialed back to delicate).
      CHANGELOG + docs/rain tunables updated. Build clean; same 2 pre-existing RoadScene failures.

## Review
- **Disjoint-files conflict resolved:** the task framed Agent 2 + Agent 3 as both touching
  `rain.frag`. Implemented Agent 2 entirely in `RainSystem.cpp/.h` (two-draw far layer, no shader
  edit) so `rain.frag` stayed exclusive to Agent 3 — all four workstreams ran as truly parallel
  subagents on disjoint files.
- **Agent 2 (parallax):** validated "two-draw" approach (Plan agent). One render pass, one pipeline,
  same instance buffer; the second draw binds a far UBO with scaled params. The one correctness
  pitfall — doubling the descriptor pool `descriptorCount` + `maxSets` to `2·MAX_FRAMES_IN_FLIGHT` —
  is in. Verified by reading `createDescriptors`/`update`/`record_draws`/`cleanup` post-build.
- **Compile/link verified:** `make build` clean; lighting.frag, rain.frag, windshield_rain.frag all
  recompiled via glslc; RainSystem.cpp relinked. `clang-format` clean.
- **Not yet verified:** on-screen appearance (needs interactive `make run`). Tuning constants
  (windshield weights, kHaloStrength, far-layer scales, N) are documented and easy to iterate.

---

# Persistent wetness map — real wipe-off + speed flow (2026-06-28)

## Done
- [x] New fullscreen wetness pass `windshield_wetness.frag` (registered in CMake): accumulate rain +
      semi-Lagrangian advection along flow + wiper clear + evaporation; R16F ping-pong A/B + copy.
- [x] `WindshieldRainPass`: wetness images/views/FB/render pass/pipeline/descriptors, `record_wetness_update`,
      CPU flow + advect params, one-time clear; rain frag gates drops by sampled `wetMap` (binding 2);
      analytic wiper removed from rain frag (now persistent in the wetness pass).
- [x] **Fixed unreachable speed crossover**: car terminal speedFactor ≈ 0.24 (drag-limited), so
      `smoothstep(0.1,0.6)` never went up → retuned to `smoothstep(0.05,0.20)` in both CPU + frag.
- [x] Renderer calls `record_wetness_update` before the snapshot.
- [x] Build green (swish + tests); validation-clean at runtime (ping-pong + barriers + binding-2 sampler).
- [x] Docs: CHANGELOG entry, docs/rain/README updated (Part 3 = wetness map), diagram text updated.

## Review
- Rain now accumulates and the wiper **genuinely wipes water off** — visually confirmed: a cleared
  swath persists and rebuilds as rain re-wets (wfinal*.png). The drops are gated by the wetness map.
- Water flows down at rest / up at speed; the real blocker was the crossover being above the car's
  drag-limited top speed — fixed. Up-direction visual not cleanly screenshotted this session (an
  unrelated foreground app, ComfyUI, occluded the swish window); confirm live by driving (hold ↑) with rain on.
- Architecture: screen-space map (cockpit view is fixed; mesh UVs are degenerate). Ping-pong A/B + a
  B→A copy keeps the rain descriptor's wetness binding fixed (no per-frame churn).

---

# Refractive windshield rain + wiper (2026-06-28)

## Plan (multi-agent per prompt.md → approved plan)
- [x] ModelManager: `isWindshield` excludes `WindowInside_Geo` (outer pane only)
- [x] PostProcessManager: HDR image gains `TRANSFER_SRC` usage
- [x] WindshieldRainPass.h/.cpp: 4-Vec4 UBO, refraction-source image + sampler, descriptor
      binding 1, `record_scene_snapshot` (transfer barriers + copy), wiper state, alpha+back-cull pipeline
- [x] windshield_rain.vert: glass-space `fragUV` + object-space normal
- [x] windshield_rain.frag: layered Voronoi drops, stick-slip, finite-diff normal, scene
      refraction, Fresnel rim + glint, front-normal mask, analytic wiper clear
- [x] Renderer.h/.cpp: snapshot call (glass→windshield), `set_wiper_enabled`, wiper threaded into update
- [x] App.h/.cpp: `V` key edge-detected wiper toggle
- [x] Docs: CHANGELOG, shaders/README, src/renderer/README, docs/rain/*.md
- [x] Build green (swish + tests); shaders compile; app runs validation-clean; `windshield: 1`
- [x] **Visual verification DONE** (permissions restored): drops are small refractive beads (not
      blobs), confined to the front windshield (no cabin/side-glass rain), wiper sweeps a clear streak.
      Real path confirmed: default-off → `R` rain → `V` wiper (held CGEvent keys). See
      `docs/images/windshield-rain-fixed.png`.
- [x] Tuning applied: cull `FRONT` (BACK culled the cabin-facing surface — drops were invisible);
      density 60, refractStrength 0.030, Fresnel rim 0.15; wiper moved to **screen space** (windshield
      mesh UV is ~constant near (1,1), so glass-UV pivot missed the glass), pivot (0.5,0.92), band 0.12.

## Review
- Root causes from `issue.md` addressed structurally: additive→refraction (alpha blend +
  HDR snapshot), screen-space→glass-space drop field, low→high density, inner-pane untagged +
  single-sided cull + forward-normal mask, and a `V`-toggle analytic wiper.
- **Feedback-loop avoided:** the pass renders into HDR, so it samples a per-frame *copy* of HDR
  (`vkCmdCopyImage`) made after the glass pass; explicit transfer barriers (insertImageBarrier
  doesn't cover TRANSFER layouts).
- **Verified:** clean build incl. tests; shaders compile; app runs many frames validation-clean
  (snapshot + binding-1 sampler + barriers exercised); loader logs exactly 1 windshield submesh.
- **Not verified (env limitation):** on-screen appearance — Screen Recording + Accessibility are
  denied here, so screenshots/synthetic keystrokes fail. The remaining `[ ]` is pure visual tuning
  with the tunables listed above; everything is parameterized for quick iteration.
- **Side fix:** `swish_tests` failed to link (`_glfwGetKey` in `Camera.cpp`) — pre-existing, unrelated
  to rain; fixed by linking `glfw` into the test target.

---

# Car-on-road correctness + documentation

## Context
Car system (Entity/CarEntity, ModelManager::load_car, dynamic geometry path,
App wiring) already existed and built clean. GLB inspection + live screenshots
revealed two placement bugs: the mesh origin is at axle height (car sank 36cm
into the asphalt) and the mesh nose points mesh +Z while the old yaw convention
put it at world −X (car drove sideways, and visual yaw mirrored the physics
heading in Z).

## Plan
- [x] Add mutable `getVertices()` to MeshData (SceneTypes.h)
- [x] Normalize mesh in ModelManager::load_car: rotate +90° about Y so nose
      +Z → +X (positions, normals, tangents), then ground min.y → 0; log bbox
      (gate: 4.57 × 1.66 × 2.03 m ✓, grounded by 0.362 m)
- [x] Make physics match the visual convention: forward = R_y(yaw)·(+X)
      = (cos, 0, −sin); yaw −= ω·dt (positive steer = right) — CarEntity.cpp
- [x] Spawn yaw −90° → +90° (nose down −Z) — App.cpp
- [x] Rebuild + tests: 32/32 pass
- [x] Visual verification via screenshots + synthetic CGEvent key input:
      rear view from camera ✓, tires on asphalt ✓, throttle drives down the
      lane ✓, left steer curves left with matching body yaw ✓, X clamp holds
      car on roadway ✓
- [x] docs/car_system.md: new convention section + load-normalization mermaid
      flowchart; fixed spawn snippet and yaw table row
- [x] architecture.excalidraw: updated loader box, driving-model box, and
      asset-facts box with implemented normalization + convention

## Round 2: cockpit camera + full interior rendering (2026-06-10)

- [x] Node-walk loader: bake inverse(W_RootNode)·W_node into vertices
      (normals inv-transpose, tangents mat3) — 1665/1667 mesh nodes had
      non-identity transforms; steering wheel + interior now render
- [x] Skip alphaMode==BLEND prims (3 glass) so cockpit can see out
      (interim until Phase 5 forward pass)
- [x] Cockpit camera in App: default mode, C toggles free-fly; eye =
      M_car · kSeatEye(−0.32, 1.05, −0.34 m); yaw = −heading + look;
      mouse look clamped ±150°/±60°
- [x] Verified via screenshots: full dash/wheel/gauges render, road
      visible through windshield, camera rides car while driving,
      free-fly detaches, exterior intact, no stray geometry
      (white road marks = pre-existing lane markings)
- [x] Bbox gate after baking: 4.57 × 1.29 × 2.03 m (1.29 = real 911
      height; old 1.66 was misplaced geometry), grounded by ~0.001
- [x] Docs: car_system.md node-walk + cockpit sections; excalidraw
      loader/cockpit/glass boxes updated

## Review
- The "drive on the road correctly" bug was two independent convention
  mismatches (vertical origin, heading axis), both fixed at the loader level
  so entity transforms keep clean semantics: position.y = 0 means tires on
  road; R_y(yaw)·(+X) is both the nose direction and the velocity direction.
- Verified live: app runs with validation layers clean; bbox gate logged at
  load. Screenshots: /tmp/swish_fix2_zoom.png (grounded, rear view),
  /tmp/swish_drive1.png (drove down lane), /tmp/swish_steer2_zoom.png
  (left turn, body yawed with the curve).
- Deferred (per plan/car_system_port.md): config-driven tunables, cockpit
  camera, FSM physics upgrade, articulated steering wheel, glass pass.
