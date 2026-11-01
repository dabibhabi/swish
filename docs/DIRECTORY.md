# Directory layout

**Label:** Largely AI-generated (Claude + subagents); verified by automated builds, tests and screenshots; please review.

*Generated from the tree on 2026-09-29 (commit `c843ebe` + uncommitted refactor and follow-ups). Trees were script-generated, then hand-annotated.*

---

## TL;DR

- **Five layers** — `core` wires `scene`, `renderer`, `debug`; all use `utils`.
- **Scene has no Vulkan** — it hands mesh data and draw calls to the renderer.
- **Rooted includes** — from `src/`; same-directory bare name; never `../`.
- **Globbed sources** — new `.cpp` and shaders need no CMake edit.
- **Debug code gated** — by `SWISH_DEBUG_UI`, so release stays byte-identical.
- **Not in the root** — drafts go to `scratch/`; finished or stale docs to `archive/` ([archive/README.md](../archive/README.md)).

## Contents

- [Where do I put...?](#where-do-i-put)
- [Top level](#top-level)
- [`src/`](#src-five-layers): [core](#srccore) · [scene](#srcscene) · [renderer](#srcrenderer) · [debug](#srcdebug) · [utils](#srcutils)
- [`shaders/`](#shaders) · [`tests/`](#tests) · [`tools/`, `scripts/`, `config/`](#tools-scripts-config) · [`docs/`, `tasks/`, `archive/`](#docs-tasks-archive)
- [Dependency direction](#dependency-direction)
- [What changed today](#what-changed-today)

**Related**

- **Visual version** — [`diagrams/directory-layout.excalidraw`](diagrams/directory-layout.excalidraw) (excalidraw.com or the Obsidian Excalidraw plugin).
- **Task-oriented map** — [REPO_MAP.md](REPO_MAP.md)
- **How-tos** — [RECIPES.md](RECIPES.md)
- **Today's refactor** — [CLEANUP_2026-09-29.md](CLEANUP_2026-09-29.md)
- **Upkeep rules** — [KEEPING_IT_CLEAN.md](KEEPING_IT_CLEAN.md)

---

## Where do I put...?

| I am adding... | Put it in | Then |
|----------------|-----------|------|
| A new class / subsystem | `src/<layer>/<Name>/<Name>.{h,cpp}` (`make new-class NAME=Foo DIR=scene`) | Add a row to that layer's README |
| A new Vulkan helper | `src/utils/vulkan/Name.h` (header-only, doc block, `swish::vkx`) | Add `tests/utils/test_vulkan_Name.cpp`, list it in `tests/utils/CMakeLists.txt`, add a row to `src/utils/README.md` |
| A new tag type (compile-time overload pick) | `src/utils/meta/Tags.h` (`swish::tag`, `<name>_t`) | Covered by `tests/utils/test_meta_Tags.cpp` |
| A generic helper (no state) | `src/utils/<topic>/Name.h`, doc block, `swish::util::<topic>` | Add `tests/utils/test_<topic>_Name.cpp`, list it in `tests/utils/CMakeLists.txt`, add a row to `src/utils/README.md` |
| A shader include | `shaders/common/<name>.glsl` | `#include` it; glslc `-I` and depfile are wired |
| A new shader | `shaders/<name>.{vert,frag,comp}` | Keep under 400 lines; update `tests/golden/release_spv.sha256` only if intended |
| A live tunable | One line in `src/debug/Tunables.def` (converted groups) | Else hand-edit `DebugParams.h` / `DebugParamsIO.cpp` / `DebugUI.cpp`; gate with `SWISH_DEBUG_UI` |
| A render pass | `src/renderer/Passes/FooPass.cpp` (class + factory) + ONE line in `src/renderer/Passes/PassList.def` | Line order = recording order; `PASS_DEBUG` for debug-only; barriers via `utils/vulkan/Barrier.h`; push constants 16-byte multiples; see `RECIPES.md` |
| A named image passes share | `Img` entry in `src/renderer/Passes/ImageRegistry.h` | The owner registers it (again after swapchain recreate) |
| A descriptor binding shared with GLSL | `#define` in `shaders/common/bindings.glsl` + constexpr in `src/renderer/Bindings.h` | Never re-type `set = N` / `binding = M` in host code |
| A road element | Builder in `src/scene/RoadScene/RoadScene_*.cpp`; sizes in `RoadParams.h` | Add to `intro_elements()` / `chunk_elements()` in order; order test guards it |
| A physics term | Topic header in `src/scene/Entity/`; include from `CarPhysics.h` | Golden car test must still pass |
| A test | `tests/test_<thing>.cpp` (or `tests/utils/test_<topic>_<Header>.cpp`) | Add to that `CMakeLists.txt` |
| A doc / design note | `docs/<topic>.md`; drafts in `scratch/`; stale ones to `archive/docs/` | Link from `docs/README.md` (or `archive/README.md`) |
| A diagram | `docs/diagrams/<name>.excalidraw` | Link from the doc it explains |
| A changelog entry | `docs/changelog/YYYY-MM.md` | Run `python3 scripts/changelog_index.py` |
| Private scratch notes | `scratch/` (gitignored) | Nothing tracked may link into it |
| Weather / sun / HDRI look logic | `src/renderer/WeatherController/` | Not in `Renderer_State.cpp` |

---

## Top level

```
swish/
├── src/            C++20 engine
├── shaders/        GLSL + common/ includes
├── tests/          Catch2 unit + golden tests
├── tools/          Offline baker, car analyzer
├── scripts/        Hygiene, format, Linux run, hooks
├── config/         Road/corridor TOML inputs
├── assets/         Models, PBR textures, fonts, corridor .bin
├── textures/       Road/terrain textures (directory-scanned)
├── docs/           Guides, rules, changelog, diagrams, user-goals/
├── tasks/          Live plan, lessons, archive
├── archive/        Stale docs (tracked) + archive/local/ (gitignored bulk)
├── include/        Vendored stb_image.h
├── CMakeLists.txt  Build; globs src/ + shaders/
├── CMakePresets.json  release and debug-ui presets
├── Makefile        run / debug / test / check / format
├── CLAUDE.md       Agent instructions
├── README.md, CONTRIBUTING.md  Human entry points
├── CHANGELOG.md    Generated index of docs/changelog/
└── .github/workflows/ci.yml   CI: Linux tests + matrix
```

- **Local only, not in git** (see `.gitignore`)
  - **Assets** — `assets/Sky/`, `assets/Charger/`, `assets/cars/` (GLB car models), `data/osm/`.
  - **Archive** — `archive/local/`: reference code, raw downloads, Blender builder, old build tree, the clean-up review ([archive/README.md](../archive/README.md)).
  - **Build and review output** — `build*/`, `review_*/`, `*.spv`.
  - **Per machine** — `scratch/`, `.claude/` (incl. old `worktrees/`, now removed), `.mcp.json`, `config/presets/`, `imgui.ini`.
  - **Vault** — `.obsidian/` (vault settings); untracked 2026-09-29.

---

## `src/`: five layers

```
src/
├── main.cpp     Entry point: constructs App
├── core/        Application shell
├── scene/       World data + physics (no Vulkan)
├── renderer/    All Vulkan code
├── debug/       Live tuning (SWISH_DEBUG_UI only)
└── utils/       Generic helpers by topic
```

- **Includes** — rooted at `src/` (`"utils/math/Smoothing.h"`); same-directory bare name; never `../`.

### `src/core/`

```
core/
├── README.md
├── App/
│   ├── App.h, App.cpp     Main loop, update, teardown
│   ├── AppInit.cpp        Startup: window, renderer, scene, corridor
│   ├── AppInput.cpp       Key table (C camera, R rain, V wiper, G weather)
│   └── REFACTOR.md        Split notes (done)
├── CameraController/      Camera modes, follow/cockpit motion
└── Window/                GLFW window wrapper
```

### `src/scene/`

```
scene/
├── README.md
├── SceneTypes.h, CameraMode.h, WeatherPreset.h   Shared enums/structs
├── Camera/                   View/projection, reverse-Z
├── SceneManager/             Registry; re-setup after swapchain rebuild
├── ModelManager/             glTF/GLB load into mesh data
├── Entity/
│   ├── Entity.h/.cpp         Base entity
│   ├── CarEntity.h/.cpp      The Porsche: state + update()
│   ├── CarPhysics.h          Umbrella for topic headers below
│   ├── CarParams.h, Powertrain.h, Drivetrain.h, Longitudinal.h
│   ├── Tire.h, Bicycle.h, WetGrip.h    Physics topics (header-only)
│   └── WheelKinematics.h     Wheel spin/steer
├── RoadScene/
│   ├── RoadScene.h/.cpp      Entry: generate, generate_chunk, interchange
│   ├── RoadScene_Surfaces.cpp, _Furniture.cpp, _Ramps.cpp   Split builders
│   ├── MeshBuilder.h/.cpp    Vertex/index/draw-call helpers
│   ├── RoadParams.h          constexpr road sizes (single source)
│   └── RoadConfig.h          Binary layout shared with toml_baker
├── RoadGeometry/             Ribbon/lane math
├── Corridor/                 Endless LIE: Alignment, Asset, Bounds, Driver, Scene
└── Treadmill/                Vestigial; CorridorDriver is active
```

### `src/renderer/`

```
renderer/
├── README.md, Vertex.h, VmaUsage.cpp, RendererConfig.h   MAX_FRAMES_IN_FLIGHT
├── Bindings.h                Set/binding numbers; includes shaders/common/bindings.glsl
├── Renderer/                 Core: facade + frame loop (smaller now)
│   ├── Renderer.h/.cpp       Facade: construct/init/cleanup/recreate
│   ├── Renderer_Frame.cpp    drawFrame
│   ├── Renderer_Record.cpp   Brackets the pass list, builds FrameCtx
│   ├── Renderer_State.cpp    Setters, debug apply, scene-geometry delegation
│   └── RendererServices.h    Shared device/allocator handles
├── Passes/                   The frame as an ordered list of passes
│   ├── IRenderPass.h         Pass interface + FrameCtx/FrameUpdate/FrameState
│   ├── PassRegistry.h/.cpp   Ordered list; drives pass lifecycle
│   ├── PassList.def, PassList.cpp   ONE line per pass = recording order
│   ├── ImageRegistry.h/.cpp  Named images passes exchange (enum Img)
│   ├── FullscreenPass.h, ForwardPass.h, PassInputs.h   Shared pass shapes, sampler inputs
│   └── LightingPass, SsaoPasses, SsrPass, GodRaysPass, HdrFinalizePass,
│       BloomPasses, CompositePass, DebugOverlayPass   .cpp, one per pass
├── GeometryProducer/         Shadow atlas + G-buffer; owns scene geometry
├── WeatherController/        Weather preset, sun/clarity, IBL rebake trigger
├── PostProcessManager/       Offscreen images + post pipelines (split below)
│   ├── PostProcessManager.h/.cpp
│   └── _Passes, _Images, _Descriptors, _Pipelines .cpp
├── Core Vulkan:  VulkanContext/  Pipeline/ (+ Device/)  Swapchain/  CommandManager/  SyncObjects/
│                 GpuResource/ (RAII VMA)  ResourceManager/  TextureManager/  SceneGeometry/
│                 (Pipeline is namespace swish::pipeline, ResourceManager swish::resource)
├── Descriptors:  CameraUniforms/  MaterialDescriptors/
├── Pipelines:    ScenePipeline/  DepthOnlyPipeline/  DeferredLightingPipeline/
├── Effect passes: DofPass/  TaaPass/  VelocityPass/  GlassPass/  WindshieldRainPass/
├── Lighting:     IBLManager/ (sky/HDRI bake)
└── Weather FX:   RainSystem/ (+ RainPhysics.h)  SpraySystem/ (compute)
```

- **Adding a pass** — write `Passes/FooPass.cpp` (class + factory), add one line to `Passes/PassList.def`.
- **Frame order** — `PassList.def`: Geometry, Lighting, Ssao, AoBlur, Velocity, Ssr, GodRays, Dof, Spray, Rain, Glass, WindshieldRain, Taa, HdrFinalize, Bloom x3, Composite, DebugOverlay.
- **Subsystem shape** — most dirs are `<Name>/<Name>.h` + `<Name>.cpp`, with two-phase init/cleanup; `Passes/` is the exception (many small `.cpp`).

### `src/debug/`

Compiled only with `SWISH_DEBUG_UI=ON`.

```
debug/
├── Tunables.def              One line per converted tunable
├── Tunables.h                Expands registry (field, save, load, UI, push)
├── DebugParams.h             All tunable fields + defaults
├── DebugParamsIO.h/.cpp      TOML preset save/load
├── DebugUI.h/.cpp            Dear ImGui panels
└── SceneParamsUniform.h/.cpp Set-4 UBO for lighting.frag
```

### `src/utils/`

Namespace `swish::util::<topic>` (Vulkan glue: `swish::vkx`). Full table: [src/utils/README.md](../src/utils/README.md).

```
utils/
├── types/       Types.h (umbrella), GlmTypes.h, StdTypes.h, GlfwTypes.h
├── units/       Units.h            1 m = 1000 WU, feet, pi
├── math/        Smoothing.h, Noise.h, Hash.h, Sequences.h
├── geometry/    Frustum.h, Polyline.h
├── color/       Color.h
├── container/   VectorOps.h
├── meta/        EnumCycle.h, Tags.h   Tag dispatch (swish::tag)
├── io/          File.h/.cpp (read_binary), StbImage.cpp
├── vulkan/      Check.h, Init.h, Barrier.h, DebugLabel.h, DeviceHandle.h,
│                ShaderModule.h, PushConstants.h, Rendering.h, RenderScope.h,
│                RenderPass.h, Descriptors.h, Samplers.h, ImageView.h, README.md
└── debug/       TomlGlm.h          Debug-UI only
```

---

## `shaders/`

```
shaders/
├── README.md
├── common/      Shared includes: bindings (set/binding numbers), camera, lights, math, hash, hash11, oct, color, puddle (.glsl)
├── basic.*, gbuffer.frag, depth_only.*, velocity.*    Geometry passes
├── lighting.frag, ssao.frag, ao_blur.frag, ssr.frag, godrays.frag   Lighting + screen-space
├── ibl_*.frag   IBL bake (sky, equirect, irradiance, prefilter, brdf)
├── dof.frag, taa.frag, bloom_*.frag, composite.frag, fullscreen.vert   Post chain
└── rain.*, windshield_*.*, glass.*, spray*.*, spray_sim.comp   Weather + glass
```

- **`.spv` output** — built into `build/`, never committed.
- **Size limit** — 400 lines per GLSL file (`make check`).

---

## `tests/`

```
tests/
├── CMakeLists.txt, README.md
├── test_*.cpp            camera, car_physics, corridor, debug_params_io, pbr, rain, road_params, road_scene
├── test_golden_car.cpp, test_golden_mesh.cpp, golden_util.h   Exact regressions
├── golden/               car_trajectory.csv, road_mesh.txt, release_spv.sha256, README.md
└── utils/                test_<topic>_<Header>.cpp per util (+ CMakeLists.txt)
    ├── test_vulkan_*     Barrier, Check, Descriptors, ImageView, Init, PushConstants, Rendering, RenderPass, Samplers
    ├── test_meta_*       EnumCycle, Tags
    ├── test_renderer_*   Bindings, PassRegistry (host-only logic, no GPU)
    └── test_math_ / geometry_ / types_ / units_ / color_ / container_ / io_ / debug_   one per header
```

- **Targets** — `swish_tests`, `swish_debug_tests` (debug-UI only), `swish_utils_tests` (no GLFW).
- **Goldens** — regenerate with `SWISH_UPDATE_GOLDENS=1`.

---

## `tools/`, `scripts/`, `config/`

```
tools/
├── README.md
├── toml_baker.cpp        road.toml -> road.bin (CMake builds it)
├── lie_corridor.py       OSM/SUMO -> lie_corridor.bin
├── sign_generator.py     Highway sign textures
└── car_analyzer/         Python (uv) GLB inspector

scripts/
├── check_repo.sh         `make check` driver (see below)
├── check_root.sh         Allowed repo-root files
├── check_file_sizes.sh   800 C++ / 400 GLSL report
├── format.sh             clang-format (`--check` for CI)
├── changelog_index.py    Rebuilds CHANGELOG.md index (`--check`)
├── hooks/pre-commit      Via `make install-hooks`
├── run_linux.sh          Linux launch helper
├── create_object.sh      `make new-class NAME=Foo DIR=scene`
├── glslc_test.sh         Compile every shader (temp dir)
├── prune_sources.sh      No-op kept for old docs
├── test_source_registry.sh   Asserts CMake still globs sources

config/
├── road.toml, lie_corridor.toml   Road + corridor inputs
└── (presets/ is local and gitignored; default location is now ~/.config/swish/presets/)
```

- **`tools/lie_corridor.py` output** — `assets/corridor/lie_corridor.bin`.
- **`toml_baker` input** — `config/road.toml`.
- **`check_repo.sh` runs** — changelog index, stray root, ignored-but-tracked, registry, sizes, format.
- **Sources are globbed** — so `prune_sources.sh` does nothing.

---

## `docs/`, `tasks/`, `archive/`

```
docs/
├── README.md             Index of everything below
├── CODING_RULES.md, FEATURE_RULES.md   Read before coding / before planning a feature
├── REPO_MAP.md, RECIPES.md, DIRECTORY.md (this file)
├── CLEANUP_2026-09-29.md, KEEPING_IT_CLEAN.md   Refactor + upkeep rules
├── architecture.md, render-pipeline.md, realism-features.md, endless-road.md, debug-ui.md
├── car_system.md, car-asset-pipeline.md, environment-asset-pipeline.md, glen-cove-lie.md, vma-memory.md
├── data-types.md, GLOSSARY.md, concepts.md, learning-path.md, phase-3-osm-lie-corridor.md
├── changelog/            2026-06.md ... 2026-09.md
├── user-goals/           CURRENT.md (active goal), history/, README.md
├── diagrams/             24 Excalidraw diagrams (incl. this layout)
├── adr/                  Decision records (0001-...)
├── rain/                 Rain system (README)
├── wheels/               Wheel articulation plan (README)
├── lessons-from-chrono/  Project Chrono notes (00-index...09)
└── images/               PNGs used by docs

tasks/     todo.md (live plan), lessons.md, archive/2026-0{6,7,8}.md, bench specs
archive/   docs/, src/, tasks/ (stale docs under their old paths), local/ (gitignored), README.md (index)
```

- **Changelog index** — `CHANGELOG.md` indexes the monthly files.
- **Archive index** — [archive/README.md](../archive/README.md) lists every archived file and why.

---

## Dependency direction

```mermaid
graph TD
    main[main.cpp] --> core
    core --> scene
    core --> renderer
    core --> debug
    scene --> utils
    renderer --> utils
    debug --> utils
    renderer -.loads .spv.-> shaders
    debug -.tunables reach.-> renderer
    utils --> ext[std + GLM + Vulkan headers]
```

**Rules**

- **`utils/`** — never includes `core/`, `renderer/`, `scene/` or project `debug/` headers.
- **`scene/`** — never calls Vulkan; it hands mesh data and draw calls to the renderer.
- **`core/`** — the only layer that wires the others together.
- **Debug code** — gated by `SWISH_DEBUG_UI`, so release output stays byte-identical.

---

## What changed today

2026-09-29, including the follow-up refactors.

| Before | After |
|--------|-------|
| `Renderer.cpp` 1718 lines, then `Renderer*.cpp` + `RendererDetail.h` + `Renderer_PostPasses.cpp` | Small `Renderer/` core (`Renderer`, `_Frame`, `_Record`, `_State`, `RendererServices.h`); `RendererDetail.h` and `Renderer_PostPasses.cpp` deleted |
| Hand-ordered pass calls in `Renderer_Record.cpp` | `src/renderer/Passes/`: `IRenderPass`, `PassRegistry`, `ImageRegistry`, one `.cpp` per pass, order in `PassList.def` |
| Scene draws inside `Renderer` | `src/renderer/GeometryProducer/` (shadow atlas + G-buffer) |
| Weather / IBL rebake inside `Renderer_State.cpp` | `src/renderer/WeatherController/` |
| `PostProcessManager.cpp` one file | `PostProcessManager{,_Passes,_Images,_Descriptors,_Pipelines}.cpp` |
| `ResourceManager` static class, `Pipeline` helpers | `namespace swish::resource`, `namespace swish::pipeline` (file names unchanged) |
| Descriptor set/binding literals in host and GLSL | `shaders/common/bindings.glsl` + `src/renderer/Bindings.h` |
| Raw Vulkan boilerplate per pass | `utils/vulkan/` gained `Rendering.h`, `RenderScope.h`, `RenderPass.h`, `Descriptors.h`, `Samplers.h`, `ImageView.h` |
| `if (kind == X)` ladders | `utils/meta/Tags.h` (tag dispatch) |
| `RoadScene.cpp` 2139 lines | `RoadScene.cpp` + `_Surfaces` / `_Furniture` / `_Ramps` + `MeshBuilder` |
| `CarPhysics.h` 955 lines | Umbrella + 7 topic headers in `Entity/` |
| `App.cpp` 715 lines | `App.cpp` + `AppInit.cpp` + `AppInput.cpp`; new `CameraController/` |
| Flat `utils/Types.h`, `Smoothing.h`, `FileIO/`, `VulkanCheck.h` | Topic dirs: `types/ math/ io/ vulkan/ units/ ...` |
| Duplicate `WORLD_SCALE`, pi, road constants | `utils/units/Units.h`, `RoadParams.h` |
| Duplicated GLSL helpers | `shaders/common/*.glsl` |
| Hand-written tunable plumbing | `src/debug/Tunables.def` (12 tunables so far: ssr, reflection, wet incl. puddles) |
| Stray root notes (`issue.md`, `plan/`, `architecture.excalidraw`, ...) | `archive/docs/notes/`, `docs/diagrams/` |
| `CHANGELOG.md` monolith, 1031-line `todo.md` | `docs/changelog/YYYY-MM.md` + index; `tasks/archive/` |
| Tracked `.obsidian/` and `References/` | Untracked, gitignored, local only; `.claude/worktrees/` removed |
| New util tests | `test_meta_Tags`, `test_vulkan_{Rendering,RenderPass,Descriptors,Samplers,ImageView}`, `test_renderer_{Bindings,PassRegistry}` |
| No guardrails | `make check`, pre-commit hook, `check_root.sh`, size limits |

- **Details and held items** — [CLEANUP_2026-09-29.md](CLEANUP_2026-09-29.md).
- **Ongoing rules** — [KEEPING_IT_CLEAN.md](KEEPING_IT_CLEAN.md).
