---
verified: 2026-09-29
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD
owner: docs-scribe
---

# Documentation Index

Everything you need to understand, extend, and contribute to Swish.

## Start Here

| If you want to… | Read |
|-----------------|------|
| **Know the rules before writing code** (where things go, what connects to what, in plain English) | [**CODING_RULES.md**](CODING_RULES.md) · `make rules` |
| **Plan a new feature** (spec, what shape it is, build it dark, prove it, turn it on) | [**FEATURE_RULES.md**](FEATURE_RULES.md) · `make feature-rules` |
| Look up what a term/abbreviation/class means | [**GLOSSARY.md**](GLOSSARY.md) |
| Build and run the project | [Root README — Quick Start](../README.md) |
| Find where things live | [**REPO_MAP.md**](REPO_MAP.md) (task-oriented) · [**DIRECTORY.md**](DIRECTORY.md) (full annotated tree) |
| Add a tunable / shader / pass / prop / test / util / config key / car material / preset | [**RECIPES.md**](RECIPES.md) |
| Edit without re-creating the mess (sizes, root files, hooks, goldens) | [**KEEPING_IT_CLEAN.md**](KEEPING_IT_CLEAN.md) |
| See what the 2026-09-29 deep clean (and the same-day follow-ups) changed | [**CLEANUP_2026-09-29.md**](CLEANUP_2026-09-29.md) |
| Choose a pattern (factory, builder, registry, tag dispatch) or check class vs struct | [**DESIGN_PATTERNS.md**](DESIGN_PATTERNS.md) (status per row: what is built, what is not) |
| Add a render pass (2 files: `Passes/FooPass.cpp` + one `PassList.def` line) | [RECIPES.md](RECIPES.md) add-a-render-pass · [../src/renderer/README.md](../src/renderer/README.md) "Pass registry" |
| Understand the whole system | [architecture.md](architecture.md) |
| Add code without breaking conventions | [CONTRIBUTING.md](../CONTRIBUTING.md) · [../CLAUDE.md](../CLAUDE.md) |
| Know what a struct's fields mean | [data-types.md](data-types.md) |
| Find out what shipped and when | [../CHANGELOG.md](../CHANGELOG.md) (generated index into [changelog/](changelog/)) |

## Repo hygiene and the 2026-09-29 clean-up

| Doc | Covers |
|-----|--------|
| [DIRECTORY.md](DIRECTORY.md) | The annotated directory layout (top level, the five `src/` layers, `shaders/`, `tests/`, `tools/`, `scripts/`, `docs/`) and a "where do I put X" table. Visual version: [diagrams/directory-layout.excalidraw](diagrams/directory-layout.excalidraw) |
| [KEEPING_IT_CLEAN.md](KEEPING_IT_CLEAN.md) | Ongoing rules that keep the tree tidy: `make check` / `make format-check`, the pre-commit hook, size limits, root-file allowlist, golden-test rules |
| [CLEANUP_2026-09-29.md](CLEANUP_2026-09-29.md) | What the deep clean removed, moved, unified and verified, plus behaviour changes, a correction about warning flags, the same-day follow-ups (pass registry, dynamic rendering, validation clean) and items still waiting on the owner |
| [utils-component-design.md](utils-component-design.md) | The 2026-09-29 plan behind the topic-organised `src/utils/` (inventory, conventions, `Units.h` design, move table); historical rationale, restored from the local archive |
| [DESIGN_PATTERNS.md](DESIGN_PATTERNS.md) | class vs struct audit (applied), factory / builder / registry / tag-dispatch decisions with **done** / not-done status, DRY hotspots, rules for new code |
| [changelog/](changelog/) | Monthly changelog files `YYYY-MM.md`; `CHANGELOG.md` at the root is a generated index (`python3 scripts/changelog_index.py`) |

Local-only: `archive/local/review_2026-09-29/` (the audit and plan documents behind the clean-up) is gitignored and exists only on the
machine that produced it. Tracked docs must not link into it; if you need it and it is missing, it was not shared.

## Archive

Finished plans, shipped specs, resolved investigations, old notes, research surveys and stale diagrams are in
[`../archive/`](../archive/README.md), under the same paths they had (e.g. `docs/wheels/phase-b-spec.md` is now
`archive/docs/wheels/phase-b-spec.md`). Local-only bulk (reference code, raw downloads, old builds) is in `archive/local/`.

## Reference Docs

| Doc | Covers |
|-----|--------|
| [GLOSSARY.md](GLOSSARY.md) | One-line lookup of terms, abbreviations, classes, conventions, coordinate spaces, controls |
| [architecture.md](architecture.md) | The 5-layer stack, startup sequence, RendererServices, the pass registry and frame flow, swapchain recreation |
| [rain/README.md](rain/README.md) | Rain system architecture — falling rain, refractive windshield drops, wetness map + wiper |
| [render-pipeline.md](render-pipeline.md) | The **base** deferred chain (G-buffer → lighting → bloom → composite), per-pass I/O, all shader math. The realism passes that interleave with it live in the next row. |
| [realism-features.md](realism-features.md) | The GPU realism suite as built — SSAO, CSM, split-sum IBL, SSR, god-rays, puddles, GPU spray, DoF, TAA + motion blur, SSAA, auto-exposure, AgX — with math, release/debug gating, and MoltenVK notes |
| [endless-road.md](endless-road.md) | The endless LIE corridor — per-draw culling, the 300 m chunk treadmill + origin rebase, service roads, the ribbon primitive, elevated diamond interchanges, and the ribbon-follower that makes ramps drivable |
| [glen-cove-lie.md](glen-cove-lie.md) | The Glen Cove LIE roadside-detail effort — goal, phased plan/status (Phase 1 done, 2a in progress), and the procedural roadside-prop generation pipeline |
| [phase-3-osm-lie-corridor.md](phase-3-osm-lie-corridor.md) | Phase 3: the real OpenStreetMap + SUMO LIE corridor that replaced the fictional road as the active one |
| [vma-memory.md](vma-memory.md) | VMA allocation and the RAII handle conventions (`GpuImage` / `GpuBuffer`) |
| [debug-ui.md](debug-ui.md) | The in-engine live-tuning UI (`make debug`) — DebugParams, `Tunables.def`, set-4 UBO, gizmos, TOML presets (`$SWISH_PRESETS_DIR` or `~/.config/swish/presets/`), and the release-safety strategy |
| [concepts.md](concepts.md) | Study aid — OOP/OOD patterns and GPU concepts, each mapped to the file in this repo where it lives |
| [learning-path.md](learning-path.md) | Study map — graphics algorithms, GPU methods, and the math (ODEs, SH, splines, volumetrics) behind rain · car · road · light, with resources and questions |
| [data-types.md](data-types.md) | Every data struct (DTO) and UBO, with exact layouts and std140 rules |
| [car_system.md](car_system.md) | GLB loading and car-mesh normalization algorithm |
| [car-rig.md](car-rig.md) | The named-node car rig as built (2026-10-12): `Wheel_*` / `Caliper_*` / `SteeringWheel` / `Needle_*` / `Wiper_*` convention, how to rig and export a car in Blender, wheel spin + Ackermann, wiper motion + rain clear, dial calibration, `config/car.toml` keys, `CarProfile` (992 / 993 RS) |
| [car-asset-pipeline.md](car-asset-pipeline.md) | Plan (not built) for generating your own car models with AI tools — mesh-gen tool survey, a modular node/pivot naming convention (body/wheels/steering wheel/seats/suspension/engine/wiper) so every car shares one rig, and a PBR clearcoat + flake texturing spec feeding the planned clearcoat shader |
| [environment-asset-pipeline.md](environment-asset-pipeline.md) | Plan (not built) for roadside assets on the LIE corridor — why trees/leaves need a procedural-trunk + leaf-card pipeline instead of AI mesh-gen, why guardrails/lamps/signage should stay procedural C++ geometry, and where AI PBR texturing (not mesh-gen) actually helps |
| [wheels/README.md](wheels/README.md) | Wheel/tire articulation plan — spin/steer/suspension phases A–C, decision log (4 open questions), risk register; synthesizes three expert research docs; Phase B shipped; its spec and the research docs are archived ([archive/README.md](../archive/README.md)) |
| [lessons-from-chrono/](lessons-from-chrono/00-index.md) | 10-part deep dive into Project Chrono's vehicle dynamics (`archive/local/examples/chrono`) — tire models, non-smooth contact as measure theory, CCP solvers, Lie-group rotations, numerics toolbox — each mapped to a swish seam; culminates in the wet-road model plan (07) and the prioritized swish map (09). AI-assisted |
| [tutorials/typed-cpp/](tutorials/typed-cpp/00-index.md) | Build-it-yourself course in typed C++ (`static_assert`, concepts, strong types, `std::variant`, X-macros, state machines, RAII) where each chapter is the spec for one refactor phase of Swish. Chapters 01-12 (M1) written; 13-25 to follow. AI-assisted |
| [adr/](adr/0001-separate-clearcoat-and-velocity-gbuffer-targets.md) | Architecture decision records (0001: separate clearcoat/flake and velocity G-buffer targets) |

Docs that carry `verified:` front matter (`verified: YYYY-MM-DD`, `against: <short sha>`) are checked against the tree
on that date; the freshness convention is warn-only. Status claims in prose still go stale: prefer
[../tasks/todo.md](../tasks/todo.md), `git log` and the changelog.

## Component READMEs

| Directory | README |
|-----------|--------|
| `src/core/` | [Platform layer — App, CameraController, Window, controls](../src/core/README.md) |
| `src/renderer/` | [Vulkan subsystems — directory map, Vulkan 1.3 path, pass registry (add a pass in 2 files), init order, per-frame recording](../src/renderer/README.md) |
| `src/scene/` | [Scene management, entity physics, procedural road, corridor](../src/scene/README.md) |
| `src/utils/` | [Shared helpers by topic — layout table, rules, how to add a util](../src/utils/README.md) · [Vulkan glue (`vkx`)](../src/utils/vulkan/README.md) |
| `shaders/` | [Shader pipeline, descriptor layout, `common/` includes, per-shader math](../shaders/README.md) |
| `tests/` | [Catch2 testing guide (three test targets)](../tests/README.md) · [golden fixtures](../tests/golden/README.md) |
| `tools/` | [toml_baker, lie_corridor.py, car_analyzer](../tools/README.md) |

## Diagrams

All diagrams are valid [Excalidraw](https://excalidraw.com) JSON (version 2). Open them at excalidraw.com (paste the file) or via the VS Code Excalidraw extension.

Six diagrams that drew deleted classes (`DepthBuffer`, `WORLD_SCALE`, `generate_trees`, the old set-3 layout) were moved to
`archive/docs/diagrams/` on 2026-09-29. Diagrams that draw hand-wired per-pass recording predate the pass registry: the frame
order is now [`Passes/PassList.def`](../src/renderer/Passes/PassList.def). `rain-architecture` predates the 2026-10-12 wiper
rework (it draws the old screen-space SDF wiper toggled by `V`); for the current wiper, see [car-rig.md](car-rig.md) §3.

| Diagram | Shows |
|---------|-------|
| [directory-layout](diagrams/directory-layout.excalidraw) | Visual version of [DIRECTORY.md](DIRECTORY.md) (current) |
| [system-overview](diagrams/system-overview.excalidraw) | Component ownership hierarchy |
| [render-pipeline](diagrams/render-pipeline.excalidraw) | The 6 base render passes and barriers (pre-realism-suite) |
| [vulkan-sync](diagrams/vulkan-sync.excalidraw) | Fence + semaphore timing across frames |
| [scene-data-flow](diagrams/scene-data-flow.excalidraw) | Asset → GPU → draw call pipeline |
| [descriptor-sets](diagrams/descriptor-sets.excalidraw) | Set 0 / set 1 bindings and push constants |
| [data-layout](diagrams/data-layout.excalidraw) | Vertex byte layout and std140 UBO layout |
| [deferred-pipeline-extended](diagrams/deferred-pipeline-extended.excalidraw) | The deferred pipeline extended with CSM, SSAO, SSR, luminance, and SSAA resolve |
| [csm-cascades](diagrams/csm-cascades.excalidraw) | Frustum split into 3 cascades packed in one shadow atlas |
| [ssr-raymarch](diagrams/ssr-raymarch.excalidraw) | View-space depth ray-march for screen-space reflections |
| [auto-exposure-loop](diagrams/auto-exposure-loop.excalidraw) | Luminance pyramid + prev-frame host readback feedback loop |
| [ibl-split-sum](diagrams/ibl-split-sum.excalidraw) | Split-sum IBL — prefiltered sky × analytic env-BRDF |
| [steering-transform](diagrams/steering-transform.excalidraw) | Steering-wheel pivot-frame + axis-correction composition |
| [ood-ownership](diagrams/ood-ownership.excalidraw) | Subsystem ownership graph (unique_ptr + RAII handles) |
| [architecture-layman](diagrams/architecture-layman.excalidraw) | Plain-language "how one frame becomes the picture" (8 steps) |
| [architecture-technical](diagrams/architecture-technical.excalidraw) | Full per-frame pass pipeline + descriptor sets + compute + the 4 new GPU features |
| [rain-architecture](diagrams/rain-architecture.excalidraw) | Rain system: falling streaks, windshield drops, wetness map, wiper |
| [wheel-tire-plan](diagrams/wheel-tire-plan.excalidraw) | Wheel articulation plan — load-time GLB clustering → wheel groups/pivot frames; per-frame WheelState[4] → 9-group matrix LUT → get_draw_calls → G-buffer/CSM/glass |
| [wheel-articulation-asbuilt](diagrams/wheel-articulation-asbuilt.excalidraw) | Wheel articulation AS BUILT (Phase A, 2026-07-24) — loader ancestry/corner recovery → WheelFrame[4]; per-frame θ wrap + corner LUT → unchanged GPU passes; debug panel + test matrix |
| [phase1-physics-pipeline-large](diagrams/phase1-physics-pipeline-large.excalidraw) · [phase1-awd-torque-small](diagrams/phase1-awd-torque-small.excalidraw) | Phase 1 physics pipeline and AWD torque split |
| [phase2-spinout-dynamics-large](diagrams/phase2-spinout-dynamics-large.excalidraw) · [phase2-spin-trigger-small](diagrams/phase2-spin-trigger-small.excalidraw) | Phase 2 hydroplane spin-out dynamics and trigger |
| [phase3-corridor-map-large](diagrams/phase3-corridor-map-large.excalidraw) | Phase 3 corridor map |
