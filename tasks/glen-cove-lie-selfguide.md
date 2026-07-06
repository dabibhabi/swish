# Glen Cove-LIE detail — self-execution guidehow can i build realistic jersey barriers for my how can i build realistic jersey barriers for my 

Handoff so you can continue offline. Each item lists **what to do (files + the pattern to copy)**,
the **concept to understand**, and a **resource** to learn it. Nothing here needs the internet except
downloading a couple of books/papers once.

## Where things stand (committed this session)

- `04f08a7` extended draw distance (far plane 2M→4.3M) + always-on aerial haze
- `b7bc273` reverse-Z depth (kills far z-fighting) — **note: depth is now near→1, far→0, sky = depth 0**
- `91822bf` sky reworked (overcast grey-white / clear HDR blue) + cabin ambient lift
- `7af9581` spray = turbulent mist · `63e5cab` camera-relative rendering (crest precision) ·
  `c809944` AgX EOTF (washed-out fix) · `a208b51` SSR on wet road
- Docs the agent wrote: **`docs/glen-cove-lie.md`** (the pipeline + generator map — read this first) and
  **`docs/diagrams/roadside-detail.excalidraw`** (open in Excalidraw). Plan: `~/.claude/plans/twinkly-mixing-parasol.md`.

## The iterate-and-verify loop (how to work solo)

1. Edit → `make debug` (builds `-DSWISH_DEBUG_UI=ON`, runs; backtick `` ` `` toggles edit/drive mode).
   `make build` = release (must stay byte-identical when a feature is "off", or be an intentional+verified change).
2. **Look at it.** Kill the app before rebuilding (loads `.spv` at startup). Screenshot the window only.
3. `make test` must stay **52/52**. Add a `CHANGELOG.md` entry per non-trivial change. Commit per feature.
4. Gotchas: MoltenVK has **no comparison samplers** (shadows use manual PCF); **push-constant blocks must be
   16-byte multiples**; the car uses **camera-relative rendering** (double-precision model rebase) so keep
   world-pos reconstruction consistent; road-dimension constants live in **both** `RoadConfig.h` and
   `toml_baker.cpp` (edit in lockstep) — quick per-prop tints/sizes can stay as locals in the generator.

---

## Phase 2a — overhead green sign gantries + Glen Cove signs  (IN PROGRESS)

The gantry **geometry already exists** and is good: `RoadScene::generate_sign_posts` ([RoadScene.cpp:758](../src/scene/RoadScene/RoadScene.cpp)) builds steel-truss beams, chords, web members, X-braced posts, and a hanging **textured** panel. The gap is the **textures**.

**TODO**
- [ ] Rewrite the sign content in `tools/sign_generator.py` to **Glen Cove-area** destinations: e.g. `EXIT 39 / Glen Cove Rd`, `Northern Blvd`, a two-destination gantry (`S Oyster Bay Rd / Syosset / Bethpage`), speed 55, mile ~39. Keep the FHWA green `(0,105,62)` + white border + `FHWASeriesEF.otf`.
- [ ] Make them read like the reference photos: add an **exit tab** (small green tab on a top corner with `EXIT 39`), a **↗ arrow** for exit-only, and multi-line destinations (left-justify destinations, arrow on the right). Optionally draw the **I-495 shield** as a simple graphic instead of text.
- [ ] Fill the empty slot: generate `sign_07.png` (+ `_normal`/`_roughness` if you want — else it falls back to flat) so `MAT_SIGN_7` (already wired in `MaterialDescriptors.cpp`) shows a real sign; add it to a gantry in `generate_sign_posts` if you want more signage.
- [ ] Run `python3 tools/sign_generator.py` (PIL 12 is installed), then `make debug` and check the gantries.
- **Concept:** UV-mapped textured quads; FHWA sign layout (color, spacing, arrow/exit-tab conventions).
- **Resource:** FHWA **MUTCD** + "Standard Highway Signs" (free PDFs, fhwa.dot.gov) for exact layouts; Pillow docs for `ImageDraw` (text/polygon/rounded-rectangle).

## Phase 2b — guardrails + median + lane markings

Enrich `generate_guardrail` ([RoadScene.cpp:469](../src/scene/RoadScene/RoadScene.cpp)) into a real **W-beam** guardrail (evenly-spaced posts + a corrugated horizontal beam — extrude a W cross-section along Z), and tidy `generate_jersey_barrier` ([:424]) into a proper NJ **F-shape** profile. Confirm the HOV diamond + edge lines (`generate_hov_diamonds` [:693], `generate_solid_markings` [:502]) read at distance.
- **Concept:** building meshes by extruding a 2D cross-section along a path; `MeshBuilder::addVerticalFace`/`addSlopedQuad` are your primitives.
- **Resource:** any modeling-by-extrusion tutorial; **Real-Time Rendering (RTR4) Ch.16** (polygon meshes). AASHTO Roadside Design Guide for W-beam dimensions (or just eyeball from photos).

## Phase 2c — light poles + overpasses

Improve `generate_street_lamps` ([RoadScene.cpp:1338](../src/scene/RoadScene/RoadScene.cpp)) into **davit / mast-arm** poles (tall pole + curved arm + fixture); the point-lights are already budget-managed to the 32 nearest. Make `generate_overpass` ([:929]) decks/piers read as local-road bridges.
- **Concept:** same extrusion/quad approach; a curved arm = a few short segments approximating an arc.
- **Resource:** photos of LIE davit poles; RTR4 Ch.16 again.

## Phase 3 — richer clear-blue sky  (quick; values only)

Edit `SP_SKY_HORIZON_CLEAR`/`SP_SKY_ZENITH_CLEAR` ([lighting.frag:109,111](../shaders/lighting.frag)) and the matching `skyHorizonClear`/`skyZenithClear` ([DebugParams.h:41,43](../src/debug/DebugParams.h)) toward a slightly more saturated blue. It's already HDR (>1) — nudge zenith toward e.g. `(0.35, 0.55, 1.4)` and test at clarity=1 (`G` key toggles clear day). No plumbing.
- **Concept:** HDR sky values + AgX tone-mapping (a saturated color needs HDR headroom to read bright).
- **Resource:** Troy Sobotka's **AgX** notes; Filament docs §"Imaging pipeline".

## Real .glb trees + hero props  (deferred — you wanted real trees, not billboards)

Add a `ModelManager::load_prop(path, baseMatSlot)` beside `load_car` ([ModelManager.cpp:133](../src/scene/ModelManager/ModelManager.cpp)) — reuse `upload_gltf_image` + the mesh/index loop (~254-335) but drop the Porsche node-name/steering/axis hacks. Source/author low-poly tree `.glb`s (check licenses). Place many instances along the embankments; upload via `upload_scene_geometry` so they cast shadows/light for free.
- **Watch:** there is **no frustum/LOD culling and no mesh instancing** (only rain instances). A tree every ~20 ft over 4.2 km = a lot of individual draw calls. So this phase likely needs **instancing** (a new vertex-input-rate-instance pipeline like `RainSystem`) and/or **distance culling** first.
- **Concept:** glTF import; GPU **instancing**; frustum/distance **culling**; LOD/impostors.
- **Resource:** glTF 2.0 spec + **tinygltf** README (github.com/syoyo/tinygltf); RTR4 **Ch.19** (acceleration/culling/LOD); Sascha Willems Vulkan `instancing` example. Free tree assets: Poly Haven / Quaternius / Kenney (check each license).

## Phase 4 — PCSS soft shadows

All in `lighting.frag` shadow block (~273-311). Three steps: (1) **blocker search** — before the PCF loop, sample the shadow map over a small radius, average the depths of taps that are *closer than the fragment* (`occluder < sc.z - bias`); early-out to fully lit if none. (2) **penumbra estimate** — `penumbra = (sc.z - avgBlocker) / avgBlocker * lightSize` (add a small `SP_LIGHT_SIZE` uniform to the SP_* macros + `SceneParamsUBO` + `DebugParams`). (3) **variable-radius PCF** — replace the fixed 3×3 taps with a Poisson-disk kernel whose radius = penumbra. **Keep the per-cascade `uMin/uMax` clamp** so taps never bleed across the horizontal shadow atlas. Remember depth is **reverse-Z** now, and the sampler is a plain (non-compare) `sampler2D`.
- **Concept:** PCF → blocker search → variable penumbra (contact-hardening soft shadows).
- **Resource:** Randima Fernando, **"Percentage-Closer Soft Shadows"** (NVIDIA, SIGGRAPH 2005 — the original 4-page paper; download the PDF). **RTR4 Ch.7** (shadows). NVIDIA's PCSS sample code.

## Phase 5 — depth of field  (your #1 cinematic cue)

Copy the **god-rays pass** end-to-end as the template: `Renderer::recordGodRaysPass` ([Renderer.cpp:903-955](../src/renderer/Renderer/Renderer.cpp)) + its resources in `PostProcessManager` (image ~434, FB ~555, layout ~678, sets ~756/802, pipeline ~954 via `makeFullscreenPipeline`). Add a new `shaders/dof.frag`: compute **circle of confusion** from depth (reconstruct view-space Z via `invProj` — auto-correct under reverse-Z), then blur the HDR by CoC (start with a separable Gaussian; upgrade to a disk/bokeh gather later). Insert `recordDofPass` right after `recordGodRaysPass` (HDR fully lit, depth still readable). Debug-gate it off by default (like TAA) so release is unchanged until enabled; push a focus-distance/aperture/maxCoC block (like `GodRaysParams`).
- **Concept:** thin-lens camera / circle of confusion; separable vs. gather (bokeh) blur; near/far CoC split.
- **Resource:** GPU Gems 3 **Ch.28 "Practical Post-Process Depth of Field"** (free online); Kleber Garcia, **"Circular Separable Convolution DOF"** (SIGGRAPH 2017, Frostbite — free slides); Bart Wronski's DOF blog posts (bartwronski.com); **RTR4 Ch.12** (image-space effects). PBR-Book Ch.5 for the thin-lens model.

---

## Books & core references (buy/download once, use offline)

- **Real-Time Rendering, 4th ed.** — Akenine-Möller, Haines, Hoffman. *The* reference for all of the above
  (shadows Ch.7, image effects/DOF/bloom/tone-map Ch.12, atmosphere Ch.14, meshes Ch.16, culling/LOD Ch.19).
- **Physically Based Rendering (PBR-Book), 4th ed.** — Pharr, Jakob, Humphreys. Free at **pbr-book.org** (thin-lens/DOF, sampling, radiometry).
- **Google Filament** design doc — google.github.io/filament (downloadable PDF). Best concise real-time PBR + IBL + tone-mapping.
- **Foundations of Game Engine Development, Vols 1–2** — Eric Lengyel (math + rendering).
- **Game Engine Architecture, 3rd ed.** — Jason Gregory (engine systems, resource/asset pipelines).
- **Vulkan:** vulkan-tutorial.com · vkguide.dev · Sascha Willems examples (github.com/SaschaWillems/Vulkan) ·
  the Vulkan spec (registry.khronos.org) · "Vulkan Programming Guide" (Sellers & Kessenich).
- **Papers/slides (free PDFs):** PCSS (Fernando 2005) · GPU Gems 3 Ch.28 DOF · Frostbite Circular-DOF (Garcia 2017) ·
  Hillaire "Scalable Sky & Atmosphere" (EGSR 2020) · Schneider HZD volumetric clouds (SIGGRAPH 2015) ·
  Reed "Depth Precision Visualized" (reverse-Z context).
- **Signs:** FHWA **MUTCD** + "Standard Highway Signs" (fhwa.dot.gov, free) for green-guide-sign layout.
- **Assets (check licenses):** Poly Haven, Quaternius, Kenney (trees/props/HDRIs).

## Suggested self-order

Finish **2a** (signs) → **3** (clear-blue, 5 min) → **2b/2c** (guardrails/poles/overpasses) →
**4 PCSS** (shadows) → **5 DOF** (biggest cinematic win) → real **.glb trees** (needs instancing/culling first).
Do PCSS and DOF when you have a solid block of time — they're the two that most repay careful reading.
