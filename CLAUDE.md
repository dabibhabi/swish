# Swish — project instructions

## Project

Swish is a **Vulkan 1.3 / MoltenVK real-time driving simulator on macOS**. It renders a glossy
Porsche 911 Turbo driving down an **endless procedural Long Island Expressway** (a hand-authored
4.2 km intro, then treadmilled chunks with service roads and drivable elevated interchanges) through
a **deferred pipeline** (CSM → G-buffer → deferred PBR lighting → SSAO → SSR → god-rays → DoF →
forward/transparent → TAA → bloom → composite with AgX tonemap). Landed realism suite: split-sum IBL
baked from the procedural sky, CSM sun shadows, SSAO, SSR, god-rays, puddles, GPU-compute road spray,
DoF, TAA + motion blur, SSAA, auto-exposure, rain/wetness/wipers, reverse-Z depth, per-draw frustum +
distance culling, and kinematic wheel spin/steer — plus a full Dear ImGui live-tuning UI. C++20, CMake.

## ⏭️ Current state — READ THIS FIRST

**Platform reality:** this is macOS/MoltenVK. `VK_KHR_ray_tracing_pipeline` and
`VK_KHR_acceleration_structure` are **not available** — ray-traced reflections and RT-denoiser work
(SVGF etc.) are off the table for this codebase. Reflections are IBL + SSR only.

The four big GPU features that were paused on 2026-07-02 **all shipped on 2026-07-03** — god-rays,
split-sum IBL (baked procedural sky; the user chose this over shipping an `.hdr` asset), puddles +
GPU spray, and TAA + motion blur. Do **not** re-plan them as new work. Their as-built docs are in
[`docs/realism-features.md`](docs/realism-features.md); status detail in
[`tasks/todo.md`](tasks/todo.md).

**Genuinely open, roughly by ROI** (full weighted table in `tasks/todo.md`):

1. **Clearcoat + flake car paint** — a second specular lobe over the GGX base + stochastic normal
   perturbation. Nothing in the tree does this yet; biggest remaining delta on the hero car.
2. **Real HDRI IBL** — the bake chain already exists in `IBLManager`; swapping the env-cube source
   from the procedural sky to a loaded `.hdr` equirect is the only new work. **Open question:** ship
   an `.hdr` asset or keep baking procedurally. Decide with the user first.
3. **Per-object velocity buffer** — TAA/motion blur currently derive motion from depth + prev/cur VP,
   so dynamic objects reproject wrong. A velocity G-buffer target fixes that and would let TAA
   replace SSAA in release (today TAA is debug-gated; SSAA is the release default).
4. **Endless-road Layer 5 polish** — exit signage, network lights, junction UX, haze/far-plane tuning.
5. **Wheel Phase B/C** — ride height + quarter-car suspension, then slip/lockup visuals + rain-μ.
6. Lower priority: reflection↔ambient energy rebalance, froxel volumetrics (C4), height fog (D1),
   PCSS shadow polish, 3D-LUT grading, POM road, decals, night lamp tuning.

## Build & run

| Command | Does |
|---------|------|
| `make run` | Release-style run (builds `-DSWISH_DEBUG_UI=OFF`, then runs `build/swish`). |
| `make debug` | Builds `-DSWISH_DEBUG_UI=ON` (Dear ImGui live-tuning UI) and runs. Backtick (`` ` ``) toggles edit vs drive mode. |
| `make build` | Configure + build only (debug-UI OFF). |
| `make test` | Builds, then `ctest --test-dir build --output-on-failure`. **56/56 passing** — keep it green. |
| `make format` | `scripts/format.sh` (clang-format). |

- Current working branch: **`debug-ui`** (not pushed).
- The app loads `.spv` shaders at startup — **kill the running app before rebuilding** when iterating.

## Conventions that MUST be followed

- **CHANGELOG per non-trivial change.** Append to `CHANGELOG.md`: dated `### YYYY-MM-DD — <title>`,
  a one-line past-tense summary, then a collapsible `<details><summary>Technical summary</summary>`
  with root cause/motivation, the fix (with `[path](path)` links), a **file-change table** of every
  touched file, and LaTeX (`$$…$$`) / mermaid where it clarifies math or data flow. Skip only for
  pure formatting/comment edits.
- **Commit only when the user asks. Push only when the user asks.** Do not do either unprompted.
- **`#ifdef SWISH_DEBUG_UI` gating everywhere.** All debug-UI / live-tuning code must be gated so the
  **release build (`SWISH_DEBUG_UI=OFF`) output is byte-identical** to before — or intentionally
  changed and verified. Identity defaults (identity correction, no override table, fixed exposure)
  must reproduce prior behaviour exactly.
- **Verify visual features by looking, not just building** (see `tasks/lessons.md`). Definition of done
  for anything a human *sees* = run it and view the output. Workflow: temp-force a default to an
  extreme → build → **window-only** screenshot (`screencapture -R <window bounds>`, never the full
  display) → judge → **revert the temp force** → repeat. Test rain at **both extremes (0 and 1.0)**.
  Suspect **structural** bugs (a computed-but-unused term, a constant normal) before retuning constants.

## MoltenVK / Vulkan gotchas

- **No comparison samplers.** MoltenVK has no `sampler2DShadow`/compare path — use a plain `sampler2D`
  + **manual PCF** compare (this is why the shadow map is a plain depth texture).
- **Push-constant blocks must be 16-byte multiples** (`vec4`-aligned). A non-multiple size silently
  misbehaves on MoltenVK — pad the struct.
- **Match formats across blits** (e.g. the luminance/auto-exposure blit chain uses the HDR format so
  no conversion happens). Mismatched blit formats break on MoltenVK.
- **Descriptor set indices** (in `lighting.frag`):
  `0` = camera UBO + lights UBO · `1` = G-buffer (albedo/normal/material/depth) ·
  `2` = shadow map (CSM atlas) · `3` = IBL (irradiance cube, prefiltered cube, BRDF LUT) ·
  `4` = scene-params / debug live-tunables UBO.
  *(IBL took set 3 when it landed 2026-07-03; scene-params moved 3 → 4.)*
- **Units:** `WORLD_SCALE = 1000`, i.e. **1 scene-metre = 1000 world units (WU)**. Physics/geometry/
  lights are in WU.

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — the 5-layer stack, startup sequence,
  `RendererServices`, swapchain recreation.
- [`docs/render-pipeline.md`](docs/render-pipeline.md) — the **base** deferred passes (G-buffer,
  lighting, bloom, composite), per-pass I/O, shader math. The realism passes that interleave with
  these live in `realism-features.md`.
- [`docs/debug-ui.md`](docs/debug-ui.md) — the Dear ImGui live-tuning UI, presets, gizmos.
- [`docs/realism-features.md`](docs/realism-features.md) — the full realism suite as built: SSAO, CSM,
  split-sum IBL, SSR, SSAA, auto-exposure, AgX, god-rays, puddles, GPU spray, DoF, TAA + motion blur.
- [`docs/endless-road.md`](docs/endless-road.md) — the endless LIE corridor: culling, chunk treadmill
  + origin rebase, service roads, ribbons, elevated interchanges, the ribbon-follower.
- [`docs/rain/README.md`](docs/rain/README.md) — falling rain, refractive windshield drops, wetness map.
- [`docs/wheels/README.md`](docs/wheels/README.md) — wheel/tire articulation (Phase A shipped;
  B/C planned).
- [`docs/glen-cove-lie.md`](docs/glen-cove-lie.md) — roadside detail: signage, guardrail, lamps,
  overpasses.
- [`docs/vma-memory.md`](docs/vma-memory.md) — VMA allocation + RAII handle conventions.
- [`docs/data-types.md`](docs/data-types.md) · [`docs/GLOSSARY.md`](docs/GLOSSARY.md) — struct/UBO
  layouts (std140) and term lookup.
- [`docs/diagrams/`](docs/diagrams/) — Excalidraw diagrams (pipeline, sync, descriptor sets, data flow).
- [`docs/README.md`](docs/README.md) — full documentation index.
- [`tasks/todo.md`](tasks/todo.md) · [`tasks/lessons.md`](tasks/lessons.md) — live plan and hard-won lessons.
