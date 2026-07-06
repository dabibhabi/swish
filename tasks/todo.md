# 🎬 Wow-factor weighting — calibrated to the UE reference (2026-07-02)

Reference: four Unreal Engine cinematic stills the user supplied as the visual bar (black 911 GT3 R,
city / wet-industrial streets). **Weight = pure visual wow toward *that* look (1–10), NOT effort.**
Effort + status sit alongside so ROI ≈ weight ÷ effort is legible. Cues that recur across all four
frames, strongest first: **DOF/bokeh → mirror-gloss paint reflecting a real environment → wet puddle
reflections → motion blur → dense environment + GI → atmosphere/haze + volumetric shafts → bloom +
cinematic grade/vignette/grain → clean temporal AA.**

**Honest read:** these shots are ~half *cinematic camera + post* (DOF, motion blur, bloom, grade) and
~half *environment richness + GI*. Swish's **materials** are already close (wet PBR · IBL · SSR landed);
the biggest deltas to this bar are **DOF, motion blur + TAA, real HDRI reflections, environment density +
GI, and the post stack** — NOT another BRDF tweak. So the weights below deliberately float **C9 DOF and
HDRI IBL to the top**, and rate environment/GI (B3 + new H1) high — a bare road with lamps will never read
as a dense city block on shaders alone.

| W | Item (code) | Reference cue it delivers | Effort | Status |
|---|---|---|---|---|
| 10 | **Real HDRI IBL** | mirror paint reflects a *real* environment (buildings/trees) | L | deferred |
| 10 | **Circular-bokeh DOF** (C9) | the #1 visible cue — foreground+background melt in all 4 | M | planned |
| 9 | **Puddles + reflections** (deferred) | standing water throwing the scene back | L | deferred |
| 9 | **Motion blur + velocity buffer** (C7 / deferred) | the chase-frame signature | L | deferred |
| 8 | **TAA** (temporal AA) | shimmer-free AAA edges; substrate for clean SSR/SSAO | L | deferred |
| 8 | **GI approximation** (H1, NEW) | bounce light, filled shadows, dappled tree light | L | NEW |
| 8 | **Environment density + decals** (B3) | trees / clutter / props → "a real place" | M | planned |
| 8 | **Froxel volumetric fog + headlight cones** (C4) | backlit shafts, rainy-night air | L | planned |
| 8 | **SSR on wet road** (C5) | on-screen lamps/geo mirrored in asphalt | — | ✓ landed |
| 8 | **Layered wet BRDF** (E3) | asphalt reads genuinely wet, not just shiny | M | planned |
| 7 | **God-rays / light shafts** (deferred) | backlit sun/fire shafts (frame 1) | M | deferred |
| 7 | **Karis bloom pyramid** (C2) | glowing taillights / fire / bright sky | S | planned |
| 7 | **Height fog** (D1) | aerial perspective down the street | S | planned |
| 7 | **Vignette + film grain** (F2) | "shot on a lens" cinematic layer | S | planned |
| 7 | **Soft shadows / PCSS** (shadow polish) | soft grounded contact shadows | M | planned |
| 6 | **Sun-tinted airlight** (D2) | directional glow through haze | S | planned |
| 6 | **FOV kick + camera shake** (F1) | speed *feel* in motion | S | planned |
| 6 | **LOD impostors** (B2) | dense skyline without a frame-time hit | M | planned |
| 6 | **Puddle ripple normals** (E4) | animated concentric ripples on puddles | S | planned |
| 5 | **Filmic grade / auto-exposure** | exposure balance sky ↔ street | — | ✓ landed |
| 5 | **Analytic sky** Preetham/Hošek (D3) | coupled sky+sun+fog colour from one knob | M | planned |
| 5 | **Fresnel-bounded sheen** (E2) | fixes the over-exposed far road | S | planned |
| 5 | **Multiscatter GGX** (E1) | rough wet keeps energy, not muddy | S | planned |
| 5 | **Curved centerline** (B1) | road geometry reads as a real place | M | planned |
| 5 | **Wet BRDF (C1) · SSAO · CSM** | wet road · contact AO · crisp shadows | — | ✓ landed |
| 4 | **Blue-noise dithering** (G1) | de-banded gradients / clean screen-space | S | planned |
| 4 | **Mesopic night look** (C8) | night read (refs are mostly day) | S | planned |
| 4 | **Light-coupled streaks (A1) · windshield optics (R-P1-3)** | rain interacts with lamps | M | planned |
| 3 | **Clustered light culling** (C6) | invisible enabler for many lamps | L | planned |
| 3 | **Halton (G2) · temporal upsample (G3) · network refactor (B4)** | sampling + perf plumbing | var | planned |
| 2 | **Smoothing helper** (F3) | infra for F1 / weather / wiper transitions | S | planned |

Effort: **S** = shader/post tweak · **M** = new pass/system · **L** = multi-pass / asset / compute.

### If the goal is literally *these four frames*, build in this order
**C9 DOF → HDRI IBL → C7 motion blur + TAA → C4 volumetrics / D1 height fog → C2 bloom + F2 vignette/grain
→ B3 + H1 environment/GI.** This front-loads the cues the eye reads first (DOF, reflections, motion,
atmosphere, post) before the subtler PBR/energy items — which is where "quality and how it looks" in the
reference actually comes from. (This table is the authoritative per-item weight — every roadmap item
below is a row here; the deferred features and the newest Tracks D–H are additionally tagged inline as `[W#]`.)

## ▶ Recommended next batch — cheap-wow-first (2026-07-02)

Curated from the weight table, tiered by ROI. **Bank the cheap 50% before the expensive 50%.** The math
+ algorithms behind each are in [`docs/learning-path.md`](../docs/learning-path.md).

**Tier 1 — very achievable now (cheap, high wow):**
- [ ] **DOF / bokeh** (C9, W10) — pure composite pass; the single biggest visible win. The blurred
      foreground / soft skyline *is* the reference look.
- [ ] **Bloom pyramid + vignette + film grain** (C2 / F2, W7) — shader/post only; the "shot on a lens" layer.
- [ ] **Height fog + sun-tinted airlight** (D1 / D2, W6–7) — one closed-form eval in `lighting.frag`; instant atmospheric depth.
- [ ] **Fresnel-bounded sheen** (E2) — small fix that *also* cures the over-exposed far road.

**Tier 2 — worth it, but real work:**
- [ ] **Real HDRI IBL** (W10) — reuse the existing IBL hooks; paint reflects a real place instead of a gradient. Medium effort, huge payoff.
- [ ] **Motion blur + TAA** (C7, W9) — needs a velocity buffer + history. Do after DOF.
- [ ] **Froxel volumetrics** (C4, W8) — the rainy-night shafts; wants clustered light culling first.

---

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
- [x] **4. Supersampling (SSAA)** — render offscreen chain at scale×, composite downsamples.

Rules: full-execution (user-authorized for Swish); CHANGELOG per feature; verify each (build, ctest
52/52, validation-clean) before next; update Obsidian RFI note (G-P1-2/G-P0-3/G-P1-3) when landed.

## Deferred — big GPU features (paused 2026-07-02, user requested, do 1 per turn)

All debug-UI tunable + release-safe like the landed realism features. Order = ROI/effort.
- [x] **[W7] God-rays / volumetric light shafts** — sun screen-pos → occlusion-masked radial-blur pass,
      composited additively; later froxel volumetric fog. Debug density/decay/weight sliders. (Medium.)
      **✓ LANDED 2026-07-03** — `shaders/godrays.frag` half-res radial blur (Mitchell), sky-gated occlusion,
      added at composite (binding 4). Ships in release (un-gated, defaults density 0.9 / decay 0.95 / weight 0.35 /
      intensity 0.04) + debug sliders. Build clean both configs · ctest 52/52 · validation-clean · verified.
      Froxel volumetric fog remains the future upgrade. **The other 3 GPU features below stay paused.**
- [x] **[W10] Real HDRI IBL** — load an `.hdr` equirect → cubemap → irradiance convolution + specular
      prefilter mips + BRDF LUT → sample via the existing `skyIrradiance`/reflection hooks in
      `lighting.frag`. Needs an `.hdr` asset (or bake the procedural sky into the cubemap). (Large.)
      **✓ LANDED 2026-07-03** — user chose to **bake the procedural sky** (no `.hdr` asset). New `IBLManager`
      (env→irradiance→GGX-prefilter→BRDF LUT), sampled in `lighting.frag` set 3 (scene-params UBO → set 4).
      Baked at init + re-baked on weather change. Ships in release. Build clean both configs · ctest 52/52 ·
      validation-clean · reflections verified (overcast grey + clear-day blue, coherent). A real `.hdr` asset
      could later feed the same cubemap hooks. **Puddles+spray and motion-vectors→TAA remain paused.**
- [x] **[W9] Puddles + road spray** — screen-space puddle mask reflecting through the existing SSR; GPU
      particle spray/mist behind the car (compute). (Large.)
      **✓ LANDED 2026-07-03** — puddles: road tag in `gbMaterial.a` + world-space procedural pool mask in
      `lighting.frag` (rides the wet model → IBL sky mirror) and the `ssr.frag` reflectivity gate (debug).
      Spray: the renderer's **first compute pipeline** — `Pipeline::createCompute`, a 4096-particle SSBO
      simulated by `spray_sim.comp`, drawn as additive billboards (`SpraySystem`). Both wetness-gated →
      dry release byte-identical. Validation-clean, 52/52, spray billboards verified. **Only TAA remains.**
- [x] **[W9] Motion vectors → TAA + motion blur** — velocity G-buffer target (prev vs cur clip pos),
      history buffer + reproject/neighborhood-clamp TAA (replacing SSAA), per-pixel motion blur. (Largest.)
      **✓ LANDED 2026-07-03 (debug-gated)** — reprojection TAA (Halton jitter → depth-reproject → 3×3
      neighborhood-clamp blend) + per-pixel motion blur, as a self-contained `TaaPass` that copies back
      into HDR (bloom/composite untouched). Motion vectors are derived from depth + prev/cur VP (no
      velocity G-buffer / push-constant change — MoltenVK-safe). Per user decision **SSAA stays the
      release default**; TAA is a debug toggle to trial in motion, then flip `taaEnabled` + drop SSAA to
      1.0 to promote it. Validation-clean, 52/52, static TAA + motion blur verified. **ALL 4 DEFERRED
      GPU FEATURES NOW COMPLETE.** Follow-up (optional): jittered TAA fully replacing SSAA in release
      once validated in motion; per-object velocity for correct dynamic-object motion vectors.

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

# Wow-factor additions — analytic atmosphere · PBR energy · perceptual polish (2026-07-02)

Brainstorm triage (planning conversation, **nothing started**). Split by *realistic* (physically-based
light transport) vs *magical* (perceptual tricks that exploit how the eye reads a scene). Cross-refs to
existing codes are noted so we never double-add: froxel volumetrics = **C4**, clustered culling = **C6**,
motion blur/velocity buffer = **C7** + Deferred:TAA, Purkinje/mesopic = **C8**, bokeh DOF = **C9**,
Karis bloom pyramid = **C2**, windshield chromatic aberration = **R-P1-3**, full HDRI IBL = Deferred.
Everything below is genuinely NEW or an upgrade to a landed feature. All debug-UI tunable + release-safe.

## Track D — Analytic atmosphere & sky (NEW; highest cheap-realism ROI)
- [ ] **[W7] D1. Height fog** — extend the existing Beer–Lambert fog in `lighting.frag` ($1-e^{-\beta d}$) with
      exponential altitude falloff so mist pools on the low road and thins upward. Closed-form, one eval/px:
      $\;\text{fog}=\dfrac{\rho_0\, e^{-k\,h_{cam}}}{k\cos\theta}\bigl(1-e^{-k\,d\cos\theta}\bigr)$. Debug: $\rho_0$, $k$. **Do first — biggest realism-per-line.**
- [ ] **[W6] D2. Sun-tinted airlight (Henyey–Greenstein)** — tint the fog airlight by $\hat v\!\cdot\!\hat s$ through the
      HG phase ($g\approx0.3$) so looking toward the sun through mist glows, away stays flat. Cheap analytic
      precursor to the **C4** froxel volumetrics — makes fog read as volumetric, not a painted veil.
- [ ] **[W5] D3. Analytic sky (Preetham / Hošek–Wilkie)** — replace the hand-tuned sky gradient with a
      turbidity-driven model so sky + sun aureole + fog colour all shift *together* from one knob. Debug:
      turbidity slider. Physically couples with D1/D2 and feeds the existing IBL `skyIrradiance` hook.

## Track E — PBR energy & wet-surface upgrades (fixes the look AND the known over-exposure)
- [ ] **[W5] E1. Energy-preserving multiscatter GGX (Kulla–Conty)** — a compensation term on the existing GGX
      spec so rough wet surfaces keep energy and stop going muddy.
- [ ] **[W5] E2. Fresnel-bounded grazing sheen** — feed the wet grazing sheen through a proper Fresnel-weighted
      reflectance so horizon brightening is physically bounded, not an unbounded add. **Fixes the
      over-exposed far road.** Pairs with "reflection ↔ ambient energy rebalance" (Next-plans, above).
- [ ] **[W8] E3. Layered wet BRDF (water-film clear-coat)** — model wet asphalt as a thin smooth water layer over
      the rough diffuse base (clear-coat lobe + refracted diffuse) instead of a roughness lerp; darkening +
      sharp specular fall out of ONE physical param (film thickness). Upgrade to landed wet BRDF / **C1**.
- [ ] **[W6] E4. Puddle ripple normals** — superpose a few expanding radial sinusoids (Gerstner-style) into a
      normal map for rain-struck puddles: animated concentric ripples. Sub-item of Deferred:Puddles+spray.

## Track F — Perceptual polish & speed feel (tiny cost, big "feel")
- [ ] **[W6] F1. FOV kick + camera shake on acceleration** — near-zero cost, the strongest speed cue after motion
      blur; the sim feels floaty at 200 mph without it. Uses F3 easing. NEW standalone (C7 only notes *budgeting* speed cues).
- [ ] **[W7] F2. Vignette + animated film grain** — a gentle vignette + subtle animated grain on top of the
      existing AgX → "captured through a lens" cinematic quality. Composite-pass, debug-toggleable.
- [ ] **[W2] F3. Framerate-independent exponential-smoothing helper** — formalise
      $x \mathrel{+}= (x_{target}-x)\,(1-e^{-k\,\Delta t})$ as one shared util for FOV/shake/weather/wiper
      transitions (auto-exposure + wetness already use this shape). "Polished vs janky" is mostly this.

## Track G — Sampling quality (supporting; makes the above clean)
- [ ] **[W4] G1. Blue-noise dithering** — swap white-noise sample offsets for blue noise in SSAO/SSR and the
      sky/fog gradients; visibly de-noises for ~free.
- [ ] **[W3] G2. Halton / low-discrepancy sequences** — TAA jitter + SSAO hemisphere sampling; better coverage
      than random. Pairs with Deferred:TAA.
- [ ] **[W3] G3. Halton-jittered temporal upsampling** — render the expensive passes (C4 volumetrics, SSR) at
      lower res and reconstruct with the TAA history; buys back the perf the volumetrics cost.

## Track H — the honest gaps the reference reveals (NEW)
- [ ] **[W8] H1. Global-illumination approximation** — the reference's filled shadows, dappled tree light,
      and bounce onto the car are *GI*, not direct light. Rising cost: (a) stronger IBL-driven ambient + AO
      (cheap, reuses landed IBL/SSAO), (b) screen-space GI (SSGI, one bounce), (c) DDGI probe volumes
      (Majercik 2019). Hardest item — content + lighting, not one pass; a bare road with lamps won't read as
      a dense city block on shaders alone. Pairs with **B3** (environment density). *No shader-only shortcut.*

### "Magic five" for *this* project (rainy, night-capable, wet-PBR already landed)
1. **D1 height fog + D2 sun-tinted airlight** — best realism-per-line; instant atmosphere on the wet road.
2. **C4 froxel volumetrics + headlight cones** *(already coded)* — the defining rainy-night image.
3. **Deferred TAA + C7 velocity motion blur**, amplified by **F1 FOV/shake** — clean *and* fast-feeling.
4. **E2 Fresnel-bounded sheen + E3 layered wet BRDF** — makes wet look wet *and* kills the over-exposure.
5. **Perceptual stack: F2 vignette/grain + C8 Purkinje + G1 blue-noise + C2 Karis bloom** — the
   "demo → magical" delta; each tiny, collectively decisive.

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

---

## 🛣️ Endless LIE epic — streaming corridor + service roads + drivable interchanges (2026-07-05)

User goal: make the drive feel 20–30 miles on a believable LIE corridor. Decisions locked with the user:
endless **treadmill** (recycle chunks + origin-rebase → precision solved for free); keep the authored
4.2 km as the **intro**, procedural beyond; **elevated diamond/cloverleaf** interchanges (graded ramps +
car pitch on grade); **service roads both sides, continuous**; **guided network** (bounds follow the
current surface — no free-roam). Full design: `~/.claude/plans/resilient-bubbling-honey.md`.

- [x] **Layer 0 — Per-draw distance + frustum culling.** `DrawCall` bounding sphere (−1 = never-cull
      sentinel); `CullParams`/`CullStats` + cull filter in `SceneGeometry::record_draws` (frustum+dist)
      and `record_depth` (dist-from-camera only). Gribb–Hartmann planes in `Renderer`. Debug "Culling"
      panel. Ships in release with image-identical defaults. **Verified:** default 6000/6046 drawn
      (unchanged look); 250 m stress → 388/6046 (93.6%) with no visible wall. 52/52 tests; both configs
      build. (CHANGELOG 2026-07-05.)
- [x] **Layer 1 — Endless road: tiled chunks + origin-rebase treadmill.** Chose a UNIFORM canonical
      tile (one `RoadScene::generate_chunk(300k)` mesh instanced per frame at a sliding window of world
      offsets via a per-draw `originOffset`) instead of the full per-chunk upload/fence/deferred-destroy
      machinery — that's only needed once chunks VARY (Layer 4). Double origin rebase in whole-chunk
      steps keeps float32 bounded; `renderZ = trueZ + m_originShift`. Far plane 4.3M→2.4M. **Verified:**
      intro start unchanged; 6 km out tiles seamlessly + survives a 6M rebase; ~110 fps; 52/52; both
      configs build. (CHANGELOG 2026-07-05.) Follow-ups: post-spacing seam hiccup, intro→chunk UV seam,
      per-slot chunk lights, spray/TAA rebase — see CHANGELOG "Known follow-ups".
- [x] **Layer 2 — Continuous service roads, both sides.** `RoadScene::generate_service_roads` — 2-lane
      frontage road each side beyond the sound barriers (double-yellow centre + white edges, all
      full-length spans → seamless), lifted 6 WU above grass to avoid coplanar z-fight. Called from
      `generate_chunk` only (endless region); intro + its exit-ramp marginal road left untouched.
      **Verified** from a drone view: both frontage roads parallel to horizon, clean markings, no
      z-fight; intro start unchanged. 52/52; both configs. (CHANGELOG 2026-07-05.) Deferred: curbs,
      service-road lamps, drivability (Layer 3).
- [x] **Layer 3 — Ribbon primitive + guided EB↔WB crossover.** `MeshBuilder::emit_ribbon` (3D
      centreline → up-facing strip, arc-length UVs — building block for curved/graded ramps);
      `generate_crossover` (smoothstep S-curve connector across a jersey-barrier gap, per chunk);
      `drivable_bounds` in App feeds the car per-frame lateral bounds that open across the median at
      crossover windows (else EB/WB by side); symmetric `road_surface_y`. Car physics untouched (free
      bicycle + existing X-clamp; only the bounds/Y SOURCE generalized → EB byte-identical).
      **Verified:** ribbon renders (winding-checked w/ temp colour); bounds open [-20526,20526] at a
      crossover; car drives WB with [-20526,-1828]; intro unchanged; 52/52; both configs.
      (CHANGELOG 2026-07-05.) Follow-ups: grade (L4), one-maneuver U-turn tuning, per-chunk demo cadence,
      full (ribbonId,s,t) tracking for overlapping decks (L4).
- [x] **Layer 4 — Elevated diamond interchanges (geometry).** `generate_interchange` — over-the-mainline
      cross-street deck (19.5 ft) on piers + FOUR graded on/off ramps via `emit_ribbon` (smoothstep 3D
      centreline climbing y:6→deck_top, ~4.6% grade). Instanced as a 2nd canonical geometry SPARSELY
      (~1.5 mi = 8 chunks apart) via the treadmill (reuses chunk cull/rebase/originOffset path).
      **Verified** from a close drone view: deck + piers + curved rising ramps render (temp-red/orange
      checked); intro unchanged; 52/52; both configs. (CHANGELOG 2026-07-05.) NOT drivable yet — ramps/deck
      need the car to track a ribbon (Y/pitch from the graded surface; overlapping decks defeat (x,z)→y).
      "Drive the other direction" already works via the L3 median crossover. Deferred: ramp markings,
      embankment fill, wire ramps into the guided surface-network, drivable grade physics.
- [x] **Layer 4b — Drivable interchanges + ramp to frontage road.** `RoadScene::interchange_ribbons()`
      exposes the 4 drivable centrelines (shared by geometry + physics). App ribbon-follower: car attaches
      to an on-ramp, then Y/pitch/heading come from the ribbon (arc-length s by speed, lateral t by steer,
      pitch→rotation.z since nose is +X); junctions via next/branch; exits to free driving. Frontage road
      made drivable (road_surface_y + drivable_bounds window) so the service off-ramp lands there.
      **Verified by driving:** climb 6→5944 (pitch→3.9°), deck flat, descend 5944→16 (pitch→−3.9°) → WB
      opposite direction; steer at top → service ramp → frontage road. 52/52; both configs; intro/EB
      unchanged. (CHANGELOG 2026-07-05.) Follow-ups: one-way service road (no on-ramp back), attach/junction
      UX basic, deck/ramp markings.
- [ ] **Layer 5 — Polish** (exit signage, network lights, junction UX, haze/far-plane tuning).
