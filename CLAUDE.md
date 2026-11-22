# Swish — project instructions

Swish is a **Vulkan 1.3 / MoltenVK real-time driving simulator on macOS**: a glossy Porsche 911 on a
procedural Long Island Expressway, rendered through a deferred PBR pipeline with a realism suite and a
Dear ImGui live-tuning UI. C++20, CMake. Rules before writing code: [`docs/CODING_RULES.md`](docs/CODING_RULES.md) (`make rules`); new features: [`docs/FEATURE_RULES.md`](docs/FEATURE_RULES.md) (`make feature-rules`). Where things live: [`docs/REPO_MAP.md`](docs/REPO_MAP.md) and
[`docs/DIRECTORY.md`](docs/DIRECTORY.md). How to add X: [`docs/RECIPES.md`](docs/RECIPES.md). Tidy-tree rules:
[`docs/KEEPING_IT_CLEAN.md`](docs/KEEPING_IT_CLEAN.md). The 2026-09-29 clean-up: [`docs/CLEANUP_2026-09-29.md`](docs/CLEANUP_2026-09-29.md).

## Current state — do not trust this file for status

Status claims in prose go stale. Check the live sources, in this order: (1) [`docs/user-goals/CURRENT.md`](docs/user-goals/CURRENT.md),
what the user wants now; (2) [`tasks/todo.md`](tasks/todo.md), the active plan (finished work is in `tasks/archive/`);
(3) `git log --oneline -20` and [`CHANGELOG.md`](CHANGELOG.md), what actually shipped. Verify any "not started / paused /
disabled" claim against `git log` and the code before acting on it. **Platform reality:** macOS/MoltenVK has no
`VK_KHR_ray_tracing_pipeline` / `VK_KHR_acceleration_structure`: no RT reflections or denoisers; reflections are IBL + SSR only.

## Build & run

| Command | Does |
|---------|------|
| `make run` | Builds release (`-DSWISH_DEBUG_UI=OFF`, `build/`), then runs `build/swish`. |
| `make debug` | Builds `-DSWISH_DEBUG_UI=ON` (ImGui UI, RelWithDebInfo) into `build-debug/` and runs. Backtick toggles edit vs drive mode. |
| `make build` / `make test` / `make test-debug` | Configure+build release / plus `ctest` on `build/` / same for `build-debug/`. Keep the suite green. |
| `make check` | `scripts/check_repo.sh`: changelog index, stray root files, ignored-but-tracked, source globs (hard); file sizes, format (report-only; `CHECK_ARGS=--strict`). |
| `make format` / `make format-check` | clang-format in place / dry run (exit 1 on any diff). |
| `make new-class NAME=Foo DIR=scene` | Scaffolds `src/scene/Foo/Foo.{h,cpp}` (sources are globbed: no CMake edit). `make install-hooks` opts in to the pre-commit `check_repo.sh --quick`. |

Kill the running app before rebuilding (it loads `.spv` at startup). Working branch is `debug-ui`. New art
assets come from the Arch machine over ssh. Debug presets live in `$SWISH_PRESETS_DIR` or `~/.config/swish/presets/`.
**Traps:** `glslc` is found via `$VULKAN_SDK/bin` first; with `VULKAN_SDK` unset (GUI-launched IDE) Homebrew's
newer glslc wins and `tests/golden/release_spv.sha256` will not match. Validation layers are on only in
`-DCMAKE_BUILD_TYPE=Debug` (`make run` and `make debug` both define `NDEBUG`); `SWISH_CLASSIC_VULKAN=1` forces the
classic (non-dynamic-rendering) path, and keep first-party warnings at 0 (`-DSWISH_WERROR=ON`; the full `-Wall -Wextra
-Wpedantic` set applies on AppleClang too).

## Conventions that MUST be followed

- **CHANGELOG per non-trivial change.** Add the entry to the current month file `docs/changelog/YYYY-MM.md`
  (create it if the month is new), then run `python3 scripts/changelog_index.py` to refresh the index in
  `CHANGELOG.md`. Entry format: dated `### YYYY-MM-DD — <title>`, a one-line past-tense summary, then a
  collapsible `<details><summary>Technical summary</summary>` with root cause/motivation, the fix (with
  `[path](path)` links), a **file-change table** of every touched file, and LaTeX (`$$…$$`) / mermaid where it
  clarifies math or data flow. Skip only for pure formatting/comment edits. Never skip it for bug fixes,
  tuning changes or refactors.
- **Commit only when the user asks. Push only when the user asks.** Never either unprompted.
- **`#ifdef SWISH_DEBUG_UI` gating everywhere.** All debug-UI / live-tuning code is gated so the release
  build (`SWISH_DEBUG_UI=OFF`) output is **byte-identical** to before, or intentionally changed and
  verified. Identity defaults (identity correction, no override table, fixed exposure) must reproduce
  prior behaviour exactly.
- **Golden tests are the refactor safety net: never regenerate a golden to make a refactor pass.**
  `SWISH_UPDATE_GOLDENS=1` is only for an intended behaviour change, with a changelog entry. The intro mesh
  hashes are bound to Release + Clang codegen (they fail at `-O0` and under GCC); `.spv` hashes are bound to the
  SDK glslc. See [`tests/golden/README.md`](tests/golden/README.md).
- **Verify visual features by looking, not just building** (see `tasks/lessons.md`). Definition of done for
  anything a human sees is to run it and view the output. Workflow: temp-force a default to an extreme,
  build, **window-only** screenshot (`screencapture -R <window bounds>`, never the full display), judge,
  **revert the temp force**, repeat. Test rain at **both extremes (0 and 1.0)**. Suspect **structural**
  bugs (a computed-but-unused term, a constant normal) before retuning constants.
- **Code style:** clang-format (`make format`) after editing C++; `#pragma once`; all code in `namespace swish`
  (utils in `swish::util::<topic>`, Vulkan glue in `swish::vkx`); one class per `Name/Name.{h,cpp}` pair.
  **Includes are rooted at `src/`** (`#include "utils/math/Smoothing.h"`); same-directory includes use the bare
  name; **never `../`**. Vocabulary types (`Vec3`, `Mat4`, `u32`) come from `utils/types/Types.h`.
- **`class` vs `struct` (owner rule):** `class` = a large component (owns resources/behaviour, private state,
  invariants — e.g. `Renderer`, `RoadScene`, `CarEntity`). `struct` = a sub-component of a class (plain grouped
  data, public fields, no invariants — e.g. `SsrParams`, `RoadParams`, `CarParams`). Creational/structural
  patterns (factories, builders, registries): see [`docs/DESIGN_PATTERNS.md`](docs/DESIGN_PATTERNS.md).
- **Layering:** `utils/` depends only on std + GLM (+ Vulkan headers). Scene code never calls Vulkan (go through
  the renderer managers). Renderer subsystems are owned by `Renderer` / `RendererServices`, no parallel global
  singletons. Scene parameters live in `config/*.toml` and `scene/RoadScene/RoadParams.h`, not hard-coded. Do not
  edit vendored headers (`include/`) unless asked. New helper: follow [`src/utils/README.md`](src/utils/README.md).
- **Units:** 1 scene-metre = 1000 world units (WU); physics, geometry and lights are in WU. Use
  `src/utils/units/Units.h` (`kWuPerMetre`, `m()`, `ft()`, `kPi`); **never re-type `1000`, `0.3048` or pi**.
- **One source of truth:** live tunables go in `src/debug/Tunables.def` (one line for converted groups);
  shared GLSL goes in `shaders/common/*.glsl` (`#include`); road dimensions in `RoadParams.h`.
- **Pass registry:** the frame is the ordered list `src/renderer/Passes/PassList.def` (`IRenderPass`, `PassRegistry`).
  Add a pass = `Passes/FooPass.cpp` + one list line (RECIPES add-a-render-pass); barriers are eager; a test pins the order.
- **`vkx` builders:** use `src/utils/vulkan/` (`make_color_pass`, `Descriptors.h`, `Samplers.h`, `ImageView.h`, `RenderScope.h`,
  `push_range<T>`) instead of raw `vkCreate*` structs; closed variant sets use tag dispatch (`utils/meta/Tags.h`).

## MoltenVK / Vulkan gotchas

- **No comparison samplers.** No `sampler2DShadow`: use a plain `sampler2D` + **manual PCF** compare.
- **Push-constant blocks must be 16-byte multiples** (`vec4`-aligned); pad the struct or MoltenVK silently
  misbehaves (`utils/vulkan/PushConstants.h` static-asserts it).
- **Match formats across blits** (the luminance/auto-exposure chain uses the HDR format).
- **Image barriers:** use `swish::vkx::image_barrier` (`utils/vulkan/Barrier.h`), which fails loudly on
  unsupported layout pairs.
- **Descriptor set indices** (`shaders/lighting.frag`):
  `0` = camera UBO (b0) + lights UBO (b1) · `1` = G-buffer (b0 albedo, b1 normal, b2 material, b3 depth,
  b4 `gbClearcoatFlake`) · `2` = shadow map (CSM atlas) · `3` = IBL (irradiance cube, prefiltered cube,
  BRDF LUT) · `4` = scene-params / debug live-tunables UBO (`SceneParamsUBO`, 11 vec4 rows).

## Docs map
[`docs/README.md`](docs/README.md) is the full index (architecture, render-pipeline, realism-features, endless-road, debug-ui,
rain, wheels, vma-memory, data-types, GLOSSARY, DESIGN_PATTERNS, diagrams); [`tasks/lessons.md`](tasks/lessons.md) has hard-won lessons.
