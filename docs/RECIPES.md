---
verified: 2026-09-29
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD; the 2026-09-29 deep clean and follow-ups (R1-R3) are not committed yet
owner: docs-scribe
covers: [src/debug/**, src/renderer/**, src/scene/**, src/utils/**, shaders/**, tests/**, config/**, CMakeLists.txt, scripts/**, Makefile]
---

# Recipes: how to add X

> **Label:** Largely AI-generated (Claude + subagents); verified by automated builds, tests and screenshots; please review.
> Paths and symbols verified against the tree on 2026-09-29 (after the follow-ups: pass registry, dynamic rendering); recipes not yet dry-run end
> to end. Every recipe ends with a **Verify with** line. Line numbers drift, so files are named by symbol; use `rg`.
> Map of the tree: [REPO_MAP.md](REPO_MAP.md) / [DIRECTORY.md](DIRECTORY.md). Rules: [../CLAUDE.md](../CLAUDE.md).
> Upkeep rules (sizes, root files, hooks): [KEEPING_IT_CLEAN.md](KEEPING_IT_CLEAN.md).

Common to every recipe: includes are rooted at `src/` (`#include "utils/math/Smoothing.h"`, same-directory files use
the bare name, never `../`); units come from `utils/units/Units.h`; run `make format`, `make test`, `make check`; a
non-trivial change ends with an entry in `docs/changelog/YYYY-MM.md` + `python3 scripts/changelog_index.py`, and with
the docs that name the touched contract. **Never regenerate a golden to make a refactor pass** (see add-a-test).

## add-a-tunable

**When:** a value should be live-adjustable in `make debug` and keep a literal constant in release.

**A. Group already converted to the registry** (`ssr` 5, `reflection` 3, `wet` 4: porosity, roughness, puddles enabled, puddle coverage; 12 tunables):

1. [`src/debug/Tunables.def`](../src/debug/Tunables.def): add **one line** (`TUNE_F(name, "group", "key", def, lo, hi,
   label, fmt, dumpPrecision, PUSH(member) | NOPUSH, rel)`, where `rel` is `NOREL` or the release value). That generates the `DebugParams` field
   and default, the TOML save/load, the slider, the dump line, and (for `PUSH`) the push-constant copy in
   [`SsrPass.cpp`](../src/renderer/Passes/SsrPass.cpp) (`SsrPass::describe`). Read the header
   comment of the file first: it is the authoritative recipe.
2. `PUSH(member)`: add the member to the C++ push struct in
   [`PostProcessManager.h`](../src/renderer/PostProcessManager/PostProcessManager.h) and the GLSL block, keep the
   block a **16-byte multiple** (adjust the `static_assert` size). `NOPUSH` rows that go to the set-4 UBO are packed by
   hand in [`SceneParamsUniform.cpp`](../src/debug/SceneParamsUniform.cpp) (`update`).
3. Shader: read the value under `#ifdef SWISH_DEBUG_UI`, literal `SP_*` constant otherwise
   (debug builds compile shaders with `-DSWISH_DEBUG_UI`). Keep `lo <= def <= hi`; never rename an existing TOML key.

**B. Any other group** (still hand-written, 3 sites + consumer):

1. [`src/debug/DebugParams.h`](../src/debug/DebugParams.h): add the field with the **identity default** (must
   reproduce release output). Example: `godrayIntensity`, `flakeDensityMul`.
2. [`src/debug/DebugParamsIO.cpp`](../src/debug/DebugParamsIO.cpp): add to the write table and the read table
   (`tbl["group"]["key"].value_or(p.field)`); see `flake_density_mul`.
3. [`src/debug/DebugUI.cpp`](../src/debug/DebugUI.cpp): add the slider in the right group
   (see `Flake density mul`, `Intensity##godrays`).
4. Consumer: **push constants** (copy in the owning pass's `describe()` / `record()`, e.g. `gp.tune` in
   [`GodRaysPass.cpp`](../src/renderer/Passes/GodRaysPass.cpp), read from `FrameCtx::tune`; 16-byte block) or
   the **set-4 UBO** (add a `vec4` slot in [`SceneParamsUniform.h`](../src/debug/SceneParamsUniform.h) and `.cpp`, update
   `static_assert(sizeof(SceneParamsUBO) == 11 * 16)` and the `SceneParamsUBO` block in
   [`shaders/lighting.frag`](../shaders/lighting.frag), the only shader that reads it).
5. [`data-types.md`](data-types.md) and [`debug-ui.md`](debug-ui.md) if a UBO layout changed.

**Verify with:** `make debug`, drag to both extremes, window-only screenshot; `make build` release must render
identically at the default (33/33 `.spv` hashes unless the shader change is intended); `make test-debug`
(`swish_debug_tests` diffs the registry against the GLSL release macros).

## add-a-shader

1. Create `shaders/<name>.{vert,frag,comp}`. No `sampler2DShadow` (MoltenVK).
2. **Shared code goes in [`shaders/common/`](../shaders/common)** (`camera`, `lights`, `math`, `hash`, `hash11`,
   `oct`, `color`, `puddle`, `bindings`): `#include "math.glsl"`. Set/binding numbers come from `bindings.glsl`
   (`SET_IBL`, `BIND_GB_ALBEDO`, ...). Do not paste a `CameraUBO` or hash function into a shader; opt into the
   `camera.glsl` variants with `CAMERA_UBO_WEATHER` / `CAMERA_UBO_CSM` before the include.
3. Discovery: `CMakeLists.txt` globs `shaders/*.vert|*.frag|*.comp` (`SHADER_SOURCES`, `CONFIGURE_DEPENDS`), so **no
   list edit**. glslc gets `-I shaders/common` and a depfile, so editing an include rebuilds exactly its users.
4. Load `<name>.spv`: set `cfg.vertShaderPath` / `fragShaderPath = std::string(SHADER_DIR) + "<name>.frag.spv"` in the
   owning pass's `PipelineConfig` (pattern: [`DofPass.cpp`](../src/renderer/DofPass/DofPass.cpp)); `pipeline::create_graphics`
   ([`Pipeline.h`](../src/renderer/Pipeline/Pipeline.h)) reads them with `util::io::read_binary` and wraps them in
   `vkx::make_shader_module`; the `VkPipelineCache` comes from `RendererServices::pipelineCache`.
5. Screen-space passes reuse `fullscreen.vert.spv`.
6. Add the row to [REPO_MAP.md](REPO_MAP.md) section 2 and [`shaders/README.md`](../shaders/README.md). A new shader
   adds a row to `tests/golden/release_spv.sha256` only when you intend to extend the hash baseline.

**Verify with:** `bash scripts/glslc_test.sh`, then `make build`. Kill the running app first (`.spv` load at startup).
Keep the file under the 400-line GLSL limit that `make check` reports.

## add-a-render-pass

A pass is **two files**: its `.cpp` and one line in the ordered list. (Before the pass registry a self-contained pass
touched 5 C++ files: `Foo.h`, `Foo.cpp`, `Renderer.h`, `Renderer.cpp` in five places, `Renderer_Record.cpp`.) Background:
[`src/renderer/README.md`](../src/renderer/README.md) "Pass registry".

1. **The pass:** `src/renderer/Passes/FooPass.cpp` (globbed, no CMake edit). An anonymous-namespace class plus a factory:

   ```cpp
   namespace swish {
   namespace {
   class FooPass final : public IRenderPass {          // or FullscreenPass<FooPush> for a fullscreen post pass
   public:
       const char* name() const override { return "Foo"; }
       void init(const PassInitCtx& c) override;        // pipelines, own images; read views via c.images.views(Img::Hdr)
       void onResize(const PassInitCtx& c) override;    // framebuffers / descriptor writes after a recreate
       void update(const FrameUpdate& u) override;      // optional: uniform upload, simulation (u.state = blackboard)
       void record(const FrameCtx& c) override;         // commands; barriers are EAGER, placed where they always were
       void cleanup(VkDevice d) override;
   };
   }  // namespace
   std::unique_ptr<IRenderPass> make_foo_pass() { return std::make_unique<FooPass>(); }
   }  // namespace swish
   ```

   For a fullscreen post pass derive `FullscreenPass<Push>` instead and implement only `describe()` (fill the
   `FullscreenDraw` and the push block) plus `before()` / `after()` for barriers; see
   [`SsrPass.cpp`](../src/renderer/Passes/SsrPass.cpp) and [`BloomPasses.cpp`](../src/renderer/Passes/BloomPasses.cpp).
   A pass that owns a folder (`src/renderer/FooPass/FooPass.{h,cpp}` via `make new-class`) is fine when other code needs its
   header; the factory then lives in its `.cpp`.
2. **The list:** [`src/renderer/Passes/PassList.def`](../src/renderer/Passes/PassList.def): one line
   `PASS(Foo, make_foo_pass)` (or `PASS_DEBUG(...)` for a `SWISH_DEBUG_UI`-only pass, whole file inside `#ifdef`) in the
   place of the frame where it records. **Line order is recording order**; the test
   `tests/utils/test_renderer_PassRegistry.cpp` pins it, so update the expected list in the same change.
3. Own GPU objects with `GpuImage` / `GpuBuffer` ([`vma-memory.md`](vma-memory.md)); build render passes with
   `vkx::make_color_pass`, descriptor layouts / pools / sets with `utils/vulkan/Descriptors.h`, samplers with
   `vkx::make_sampler`, views with `vkx::make_image_view2d`. A forward pass that draws onto the lit HDR with read-only
   depth uses `make_forward_hdr_pass` ([`ForwardPass.h`](../src/renderer/Passes/ForwardPass.h)); push ranges with
   `vkx::push_range<T>()` (static-asserts the 16-byte rule); `vkx::RenderScope` for begin/end of a render pass.
4. Images other passes read: add a name to `enum class Img` (`ImageRegistry.h`) and register it in `init` / `onResize`
   (see `VelocityPass`). CPU values later passes need go into `FrameState` (see `RainSystem::beginFrame`).
5. Barriers: use `swish::vkx::image_barrier` ([`utils/vulkan/Barrier.h`](../src/utils/vulkan/Barrier.h)); it throws on
   layout pairs it has no row for, so add the row (with a test in `tests/utils/test_vulkan_Barrier.cpp`). State in the
   pass header which layout each image is left in: the next pass in the list relies on it. Set indices 0-4:
   [REPO_MAP.md](REPO_MAP.md) section 3; binding numbers shared with a shader live in `shaders/common/bindings.glsl`
   (`renderer/Bindings.h`).
6. Optional: a pass that wants Vulkan 1.3 dynamic rendering follows [`src/renderer/README.md`](../src/renderer/README.md) "Vulkan 1.3 path"
   (`RendererServices::dynamicRendering`; keep the classic path, leave images in the same layouts on both).
7. Shader per add-a-shader; debug toggle per add-a-tunable. A new **composite input** = one `#define` in
   `bindings.glsl`, one line in `kCompositeInputs` ([`PassInputs.h`](../src/renderer/Passes/PassInputs.h)), the sampler in
   `composite.frag`.
8. Docs: [`render-pipeline.md`](render-pipeline.md) or [`realism-features.md`](realism-features.md),
   [`../src/renderer/README.md`](../src/renderer/README.md) (frame-order paragraph), REPO_MAP pass table.

**Verify with:** `make build` and `make debug`; `make test` (the pass-order test); 33/33 `.spv` hashes unless the shader
change is intended; a first-frame release screenshot pixel-identical to the previous build; a `SWISH_CLASSIC_VULKAN=1`
run if the pass has a dynamic-rendering path; 0 first-party warnings with `-DSWISH_WERROR=ON`; for validation layers
configure a plain `-DCMAKE_BUILD_TYPE=Debug` build (`make run` and `make debug` define `NDEBUG`, so validation is OFF
there) and run with `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1`; resize the window, exit cleanly (no VMA leak report).

## add-a-road-prop

1. Declare a generator in [`src/scene/RoadScene/RoadScene.h`](../src/scene/RoadScene/RoadScene.h)
   (pattern: `generate_sign_posts`).
2. Implement it in the topic file that fits: [`RoadScene_Surfaces.cpp`](../src/scene/RoadScene/RoadScene_Surfaces.cpp)
   (ground-level surfaces), [`RoadScene_Furniture.cpp`](../src/scene/RoadScene/RoadScene_Furniture.cpp) (barriers,
   signs, lamps, trees), [`RoadScene_Ramps.cpp`](../src/scene/RoadScene/RoadScene_Ramps.cpp) (service roads, ramps,
   interchanges). Emit through `MeshBuilder` ([`MeshBuilder.h`](../src/scene/RoadScene/MeshBuilder.h)).
3. **Register it**: add a row to `intro_elements()` and/or `chunk_elements()` in
   [`RoadScene.cpp`](../src/scene/RoadScene/RoadScene.cpp). The table order is the emit order and is pinned by the order
   test in [`tests/test_road_scene.cpp`](../tests/test_road_scene.cpp): update that expectation deliberately.
4. Dimensions: shared cross-section values in [`RoadParams.h`](../src/scene/RoadScene/RoadParams.h); authored values in
   [`config/road.toml`](../config/road.toml) via [`RoadConfig.h`](../src/scene/RoadScene/RoadConfig.h) and
   `tools/toml_baker.cpp`. Use `util::units::ft()` / `m()`; never `1000` or `0.3048`.
5. Props go through the per-draw frustum + distance culling path (`DrawCall` bounding sphere).
6. Docs: [`glen-cove-lie.md`](glen-cove-lie.md) or [`endless-road.md`](endless-road.md).

**Verify with:** `make test`. New geometry changes the mesh hashes in `tests/golden/road_mesh.txt`; that is an
**intended** behaviour change, so review the diff, regenerate with `SWISH_UPDATE_GOLDENS=1 build/tests/swish_tests
"[golden]"` and say so in the changelog. Then drive 1 km and screenshot. Note the intro mesh hashes are bound to Release +
Clang codegen. RoadScene is not the active road (`CorridorDriver` is), so also check the corridor path if it applies.

## add-a-test

1. **Engine tests:** `tests/test_<topic>.cpp` (Catch2 v3, no Vulkan / GLFW window), listed in
   `add_executable(swish_tests ...)` in [`tests/CMakeLists.txt`](../tests/CMakeLists.txt). Implementation `.cpp` files it
   needs go in the same list (`Camera.cpp`, `CarEntity.cpp`, `CorridorAlignment.cpp`, ...); `src/scene/RoadScene/*.cpp` is
   globbed; header-only code needs nothing.
2. **Util tests:** `tests/utils/test_<topic>_<Header>.cpp`, listed in
   [`tests/utils/CMakeLists.txt`](../tests/utils/CMakeLists.txt) (target `swish_utils_tests`: Catch2 + GLM only, **no
   GLFW**). See [`src/utils/README.md`](../src/utils/README.md).
3. **Debug-UI-only tests:** `swish_debug_tests` (built only with `-DSWISH_DEBUG_UI=ON`; `test_debug_params_io.cpp`,
   `utils/test_debug_TomlGlm.cpp`).
4. Keep `,` `;` `[` `]` out of `TEST_CASE` names (Catch/CTest discovery splits on them). `catch_discover_tests` picks up
   the cases.
5. Golden tests (`[golden]`) are the refactor safety net: if one fails after a refactor, **fix the code, do not
   regenerate the fixture**. Mesh hashes are Release + Clang bound (they fail at `-O0` and under GCC); `.spv` hashes need
   the SDK glslc. See [`tests/golden/README.md`](../tests/golden/README.md).

**Verify with:** `make test` and `make test-debug` (`ctest --output-on-failure`); CI runs the same.

## add-a-source-file

`make new-class NAME=Foo DIR=scene` (or `scripts/create_object.sh scene Foo`) writes `src/scene/Foo/Foo.{h,cpp}`
(PascalCase name, refuses to overwrite). **No CMake edit:** `CMakeLists.txt` globs `src/**/*.cpp` with
`CONFIGURE_DEPENDS` (`src/debug/` only when `SWISH_DEBUG_UI=ON`). Tests only link what `tests/CMakeLists.txt` lists.
A new **util header** does not use this recipe: follow [`src/utils/README.md`](../src/utils/README.md) (topic folder,
doc block, `swish::util::<topic>`, test in `tests/utils/`).

**Verify with:** `make build`; `make check` (CI also runs `bash scripts/test_source_registry.sh`).

## add-a-util

Follow [`src/utils/README.md`](../src/utils/README.md) "How to add a util": `src/utils/<topic>/Name.h` with the doc
block and `swish::util::<topic>` (std + GLM only), rooted include `"utils/<topic>/Name.h"`, test
`tests/utils/test_<topic>_Name.cpp` + line in `tests/utils/CMakeLists.txt`, move call sites keeping float expression
order, row in the README table, changelog entry. A constant that already exists (`kWuPerMetre`, `kPi`) must be used, not
re-typed.

**Verify with:** `make test` and `make test-debug`; release build with `-DSWISH_WERROR=ON` stays warning-free.

## add-a-config-key

1. Key in [`config/road.toml`](../config/road.toml). `config/lie_corridor.toml` drives `tools/lie_corridor.py`:
   edit it only together with a re-bake of `assets/corridor/lie_corridor.bin`.
2. Field in [`src/scene/RoadScene/RoadConfig.h`](../src/scene/RoadScene/RoadConfig.h) (bump `VERSION`, update
   `static_assert(sizeof(RoadConfig) == 292)`) and the read in [`tools/toml_baker.cpp`](../tools/toml_baker.cpp);
   the baker shares that header, so the layout cannot drift.
3. `make build`: the CMake custom command re-bakes `build/config/road.bin` when the toml changes.

**Verify with:** `make build` plus a test in `tests/test_road_scene.cpp` / `tests/test_road_params.cpp` or a debug print.
Not the same as debug presets (below).

## add-a-car-material

1. Material id in [`src/scene/SceneTypes.h`](../src/scene/SceneTypes.h) (`MaterialId`; car slots
   `MAT_CAR_0..19`).
2. Defaults `default_clearcoat_strength` / `default_flake_density` in the same header; consumed in
   [`src/renderer/SceneGeometry/SceneGeometry.cpp`](../src/renderer/SceneGeometry/SceneGeometry.cpp).
3. `ModelManager::load_car` assigns `MAT_CAR_*` by raw glTF index with no semantic tag; changing that is a
   loader change ([`car-asset-pipeline.md`](car-asset-pipeline.md), `src/scene/ModelManager/ModelManager.cpp`).
4. Shader side: `shaders/gbuffer.frag` writes `outClearcoatFlake` (location 3); `shaders/lighting.frag`
   reads set 1 binding 4 (`gbClearcoatFlake`, `BIND_GB_CLEARCOAT_FLAKE` in `shaders/common/bindings.glsl`). Design:
   [`adr/0001-separate-clearcoat-and-velocity-gbuffer-targets.md`](adr/0001-separate-clearcoat-and-velocity-gbuffer-targets.md).

**Verify with:** screenshot at flake 0 and 0.6 with clearcoat 1.0.

## add-a-debug-preset

Presets are TOML files at `<presets_dir()>/<name>.toml`, where `presets_dir()` is `$SWISH_PRESETS_DIR`, else
`$XDG_CONFIG_HOME/swish/presets/`, else `~/.config/swish/presets/` (`presets_dir`, `save_preset`, `load_preset`,
`list_presets` in [`src/debug/DebugParamsIO.h`](../src/debug/DebugParamsIO.h)). The old `build/config/presets/` is still
read as a fallback and listed. Save one from the debug UI. A tunable is preset-visible automatically for `Tunables.def`
groups; for hand-written groups only if add-a-tunable step B2 (write and read) was done. Missing keys keep their value,
so old presets stay valid.

**Verify with:** save, restart, load, compare screenshots; load an old preset and confirm unrelated fields do not change.
