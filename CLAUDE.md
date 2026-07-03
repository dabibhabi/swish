# Swish — project instructions

## Project

Swish is a **Vulkan 1.3 / MoltenVK real-time driving simulator on macOS**. It renders a glossy
Porsche 911 Turbo driving down a 4.2 km procedural Long Island Expressway through a **deferred
pipeline** (G-buffer → deferred PBR lighting → forward/transparent → bloom → composite with AgX
tonemap). Recent work is a "Blender-look" realism pass: sky reflections (IBL-lite), sun/CSM shadows,
SSAO, SSR, SSAA, auto-exposure, rain/wetness, plus a full Dear ImGui live-tuning UI. C++20, CMake.

## ⏭️ Upcoming / deferred tasks — READ THIS FIRST

Four big GPU features were **explicitly paused by the user** (2026-07-02). They are **not** started.
**Do NOT begin any of them without the user's confirmation.** When resumed: do **one per turn**, each
debug-UI-tunable and release-safe like the landed realism features. Full detail + ordering (by
ROI/effort) is in [`tasks/todo.md`](tasks/todo.md) under "Deferred — big GPU features".

1. **God-rays / volumetric light shafts** — sun screen-pos → occlusion-masked radial-blur pass,
   composited additively; later froxel volumetric fog. Debug density/decay/weight sliders. (Medium.)
2. **Real HDRI IBL** — load an `.hdr` equirect → cubemap → irradiance convolution + specular
   prefilter mips + BRDF LUT → sample via the existing `skyIrradiance`/reflection hooks in
   `lighting.frag`. (Large.)
   - **Open question:** ship an `.hdr` asset **or** bake the existing procedural sky into the cubemap.
     Decide with the user before implementing.
3. **Puddles + GPU road spray** — screen-space puddle mask reflecting through the existing SSR; GPU
   particle spray/mist behind the car (compute). (Large.)
4. **Motion vectors → TAA + motion blur** — velocity G-buffer target (prev vs cur clip pos), history
   buffer + reproject/neighborhood-clamp TAA (replacing SSAA), per-pixel motion blur. (Largest.)

Also open (not paused, lower priority): reflection↔ambient energy rebalance, live/auto exposure polish,
SSR on wet road, shadow polish/PCSS, night-scene lamp tuning, CSM long-range. See `tasks/todo.md`.

## Build & run

| Command | Does |
|---------|------|
| `make run` | Release-style run (builds `-DSWISH_DEBUG_UI=OFF`, then runs `build/swish`). |
| `make debug` | Builds `-DSWISH_DEBUG_UI=ON` (Dear ImGui live-tuning UI) and runs. Backtick (`` ` ``) toggles edit vs drive mode. |
| `make build` | Configure + build only (debug-UI OFF). |
| `make test` | Builds, then `ctest --test-dir build --output-on-failure`. **52/52 passing** — keep it green. |
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
  `2` = shadow map · `3` = scene-params / debug live-tunables UBO.
- **Units:** `WORLD_SCALE = 1000`, i.e. **1 scene-metre = 1000 world units (WU)**. Physics/geometry/
  lights are in WU.

## Docs map

- [`docs/architecture.md`](docs/architecture.md) — the 5-layer stack, startup sequence,
  `RendererServices`, swapchain recreation.
- [`docs/render-pipeline.md`](docs/render-pipeline.md) — the deferred passes, per-pass I/O, shader math.
- [`docs/debug-ui.md`](docs/debug-ui.md) — the Dear ImGui live-tuning UI, presets, gizmos *(being
  authored now by a parallel agent; reference by this path)*.
- [`docs/realism-features.md`](docs/realism-features.md) — sky reflections, shadows, SSAO/SSR/SSAA,
  auto-exposure *(being authored now; reference by this path)*.
- [`docs/rain/README.md`](docs/rain/README.md) — falling rain, refractive windshield drops, wetness map.
- [`docs/data-types.md`](docs/data-types.md) · [`docs/GLOSSARY.md`](docs/GLOSSARY.md) — struct/UBO
  layouts (std140) and term lookup.
- [`docs/diagrams/`](docs/diagrams/) — Excalidraw diagrams (pipeline, sync, descriptor sets, data flow).
- [`docs/README.md`](docs/README.md) — full documentation index.
- [`tasks/todo.md`](tasks/todo.md) · [`tasks/lessons.md`](tasks/lessons.md) — live plan and hard-won lessons.
