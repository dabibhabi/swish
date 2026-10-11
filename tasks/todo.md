# Active plan

> Reorganised 2026-09-29. Finished and shipped sections were moved **verbatim** to
> [`archive/2026-06.md`](archive/2026-06.md), [`archive/2026-07.md`](archive/2026-07.md) and
> [`archive/2026-08.md`](archive/2026-08.md); open items from them are carried below.
> Checkbox state inside the archive is a **snapshot** and some open boxes there shipped later — the
> "Verify before starting" note at the bottom lists the ones known to be stale. Ground truth for what
> shipped: `git log` and [`CHANGELOG.md`](../CHANGELOG.md). Current user goal:
> [`docs/user-goals/CURRENT.md`](../docs/user-goals/CURRENT.md).

## Car lights: headlights, tail/brake lights, rain on the lenses (started 2026-10-10)

Branch `feature/car-lights` (worktree `.worktrees/car-lights`, off `debug-ui`). Asset: `assets/blend/porsche_rigged.blend`
→ `porsche_rigged.glb` (Blender rig with named nodes; real scale, wheelbase 2.259 m). Test with
`SWISH_CAR=<abs path>/assets/blend/porsche_rigged.glb make debug`.

### Feature: car lights
- **Player sees:** at night or in rain the Porsche's headlights throw two warm beams down the road; red tail
  lights glow at the back and brighten when braking; in rain the beams show a wet halo and the lenses bead with water.
- **Done when:** debug build shows headlight pools on the asphalt and red brake glow behind the car (window-only
  screenshots, rain 0 and 1.0, day and night); toggles off = today's image exactly; release 33/33 `.spv` hashes and
  goldens unchanged; fps within 2% of before with lights on.
- **Builds:** debug-only first (`#ifdef SWISH_DEBUG_UI`); release enable is a separate, later step.
- **Default:** `car_lights.enabled = false` → identical output to today.
- **Cost:** ≤ 8 extra spot lights in the existing set-0 lights UBO (debug layout only); one extra loop in `lighting.frag`.
- **Touches:** car asset (`ModelManager` loader), `scene/Entity/CarLights.h` (new), `CarEntity`, `SceneTypes.h`
  (`LightsUBO`), `shaders/common/lights.glsl`, `shaders/lighting.frag`, `CameraUniforms`, `Renderer`, `App`,
  `debug/Tunables.def` + one `draw_tunable_group` line.
- **Owner:** `car-asset-engineer` (loader) → `lighting-materials-engineer` (spots) → `rain-weather-engineer` (lenses).
- **Out of scope (v1):** headlight shadows, volumetric beams, turn signals / fog lamps (still in the bumper mesh),
  per-light emissive materials (the 20-slot car material table has 1 free slot), the original `porsche.glb`
  (no light nodes; gets no car lights).
- **Math:** spot cone $c = \mathrm{smoothstep}(\cos\theta_o, \cos\theta_i, -\hat L\cdot\hat d)$; distance falloff = the point
  lights' windowed bounded inverse-square, $att = \mathrm{win}(d/R)\,\frac{d_{ref}^2}{d_{ref}^2+d^2}$, $d_{ref}=0.15R$;
  radiance $= C\,I\,att\,c$; wet halo $= C\,I\,att^2\,c\,w\,k_{halo}$ (same $k_{halo}$ as street lamps).

- [x] S0 Asset: named light nodes (`Headlight_L/R`, `TailLight_L/R`, `BrakeLight_L/R`, `RearLightBar` + `_Lens`,
      `_Housing`, `_Emitter`, `_Lamp` children), root scale fixed to real size (2026-10-10, Blender)
- [x] S1 (2026-10-10) Loader + CPU logic: `ModelManager::load_car` reads light anchors by exact node name into car space;
      `CarLights.h` turns anchors + car model matrix + state into world spot lights; Catch2 test (GPU-free)
- [x] S2 (2026-10-10) GPU: `SpotLightData` array in `LightsUBO` + `lights.glsl`, spot loop in `lighting.frag`, all `#ifdef SWISH_DEBUG_UI`
- [x] S3 (2026-10-10) Wiring + tunables: `car_lights` group in `Tunables.def`; brake lamps follow `m_brake`; App pushes spots each frame
- [ ] S4 Verify (night chase screenshots, tests, 33/33 .spv done 2026-10-10; rain 0/1.0 pair + fps numbers open): debug + release builds, `make test` / `make test-debug`, 33/33 `.spv`, screenshots night/day × rain 0/1.0
- [ ] S5 Lens glow: headlight / tail / brake lens submeshes tagged by node name and lit emissive when on
- [ ] S6 Rain on the lenses: wet-lens beading + streak on `*_Lens` submeshes (reuse windshield-rain height field idea)
- [ ] S7 Changelog + `docs/car_system.md` + `docs/car-asset-pipeline.md` (light node convention)
- [ ] Review: what shipped, what did not, lessons

## Typed C++ system refactor + tutorial (started 2026-10-01)

Mirrors the approved plan (`~/.claude/plans/im-figuring-out-with-sunny-unicorn.md`). Each tutorial chapter
(`docs/tutorials/typed-cpp/NN-*.md`) is the spec for one phase. Owner implements chapters; Claude scaffolds
(chapter + skeleton + failing tests), reviews and verifies; renderer = owner pilot, Claude rollout. One branch
per phase (`refactor/NN-topic` off `debug-ui`); refactor phases are behaviour-preserving, **B-phases** carry
intended changes. Gate per phase: `make test` + `make test-debug` green, 0 warnings, Arch GCC builds + unit
tests, 33 `.spv` + goldens + pass-order/bindings tests, `make check` + `make format-check`, screenshots for
anything visible, changelog. Commit/merge only when the owner asks. **Arch GCC gate deferred** (owner, 2026-10-01): macOS AppleClang + CI clang gate each phase; Arch is reconciled later in one pass.

### M0 — baseline and hygiene (Claude)
- [x] P0.0 Baseline: release 218/218, debug-UI 241/241, 33/33 `.spv`, `make check` OK — committed `aab1c9c` (2026-10-01)
- [x] P0.1 (B) Quick fixes (2026-10-01, branch `refactor/00-quick-fixes`; tests green, manual key/resize check still open): cursor/edit-mode desync; weather preset desync; resize flag cleared before renderer reads it; fog "km" label; duplicated shadow dump line; double `glfwTerminate`; `EnumCycle` `Count == 0`; dead Treadmill gantry message + stale comments; stale `Window.h` TODOs
- [x] P0.2 (2026-10-01, Apple+debug green; Arch GCC gate deferred by owner until the Arch reconciliation) C++23 bump alone (`CMakeLists.txt:4`, `toml_baker`); all hashes/goldens unchanged, 0 warnings on 3 compilers
- [ ] P0.3 Build: `swish_scene_core` static lib, `swish_utils` INTERFACE, warnings on tests/`toml_baker`, CI builds both test targets (clang Release) + `make check` + format-check, compiler guard on golden mesh test
- [ ] P0.4 Test seam: replace `#define private public` in `test_golden_car.cpp`
- [ ] P0.5 Doc drift fixes (Units.h test path, golden README contradiction, CODING_RULES rule 9, `rel`/`NOREL`, DESIGN_PATTERNS line ref, restore `docs/utils-component-design.md`)
- [x] P0.6 (2026-10-01, branch `docs/typed-cpp-tutorial`; chapters 01-12 written, owner review pending) Tutorial scaffold: `docs/tutorials/typed-cpp/00-index.md` + `docs/README.md` row

### M1 — foundations, core, car (owner implements)
- [ ] Ch01 `static_assert` / constexpr validators (GearboxParams, MAX_FRAMES_IN_FLIGHT)
- [ ] Ch02 type_traits / `if constexpr` / concepts (`utils/meta/Concepts.h`)
- [ ] Ch03 Strong types: `Quantity<Tag>`, `Gear`, `Corner` + `PerCorner<T>`, `Direction` (car golden bit-identical)
- [ ] Ch04 `swish::fsm` engine (`src/utils/fsm/Fsm.h`), test-first, constexpr table validation
- [ ] Ch05 Gearbox on the FSM (equivalence sweep vs `gearbox_shift_step`)
- [ ] Ch06 `std::variant` event queue (`src/core/Events/`)
- [ ] Ch07 `Commands.def` + `InputSampler`/`InputFrame`; remove 5 GLFW polling sites
- [ ] Ch08 `InputMode` FSM, actions-as-data; ImGui pushes `SetEditMode`
- [ ] Ch09 `WeatherState` reducer, `RainLevel`
- [ ] Ch10 `CameraController` variant-of-states
- [ ] Ch11 RAII/ownership: `GlfwContext`, `unique_ptr<Camera>`, `RoadSurface`, `CorridorDriver` value result
- [ ] Ch12 Pure step split of `CarEntity::update`; `utils/math/Angle.h`

### M2 — debug, config, data
- [ ] Ch13 Tunables: `TUNE_V`; owner converts sky, Claude the other 9 groups; split `DebugUI.cpp`
- [ ] B2 Persist the 14 unsaved sliders
- [ ] Ch14 `RoadSchema.def` + `std::expected` validation (`road.bin` byte-identical)
- [ ] B3 Fix road-config narrowing + fallbacks
- [ ] Ch15 `std::expected` across loaders
- [ ] Cleanup: `io/Paths.h`, `FrameClock`, `puddle_hash` move, split `test_car_physics.cpp`, format 20 files, missing utils tests

### M3 — renderer (owner pilot, Claude rollout)
- [ ] Ch16 RAII handles (stable `services()` first; pilot IBLManager)
- [ ] Ch17 `OneTimeSubmit`, `RenderScope` rollout, `record_car_draws()`
- [ ] Ch18 `enum class Set`, typed binding builder
- [ ] Ch19 `PushBlock<T, Stages>` + `PushConstantBlock` concept
- [ ] Ch20 Swapchain/frame FSM, `RendererCommand` queue
- [ ] Ch21 `TrackedImage` + declarative pass reads/writes
- [ ] B4 Drop redundant SSR/GodRays HDR round trip
- [ ] Ch22 TAA history FSM, IBL `SkySource`
- [ ] Ch23 `BlendMode` + depth bias in `PipelineConfig`
- [ ] Ch24 PostProcessManager façade breakup + small ownership fixes

### M4 — wrap-up
- [ ] Ch25 Patterns recap, ADRs, doc sync, CLAUDE.md conventions, archive finished items

## Cleanup program (started 2026-09-29)

Mirrors the approved plan (`~/.claude/plans/i-just-answered-my-typed-shore.md`). The main session ticks
boxes when work is verified. **Order is fixed: 0 → 1 → 2 → 3;** a divergent hash or screenshot means stop
and re-plan. Release build (`SWISH_DEBUG_UI=OFF`) stays byte-identical unless deliberately changed.

### Phase 0 — safety net (must be green before any refactor)
- [x] 1. Flip project to C++20 (`CMakeLists.txt:4` and `:198`, `.clang-format:9`); both configs build, 137/137 tests, 33 `.spv` hashes identical to the C++17 baseline (2026-09-29)
- [~] 2. Baselines: 33 release `.spv` hashes saved in `tests/golden/release_spv.sha256` (Vulkan SDK glslc 1.4.321.0, macOS) — DONE; window-only release screenshots at rain 0 and 1.0 — STILL OPEN (needs a human-visible run)
- [x] 3. Golden tests in `tests/golden/`: trajectories against the real `CarEntity::update`; meshes for `generate()`, `generate_chunk()`, `generate_interchange_scene()` (done 2026-09-29: release 141 / debug-UI 149 tests pass; see `tests/golden/README.md`)
- [x] 4. `DebugParamsIO` round-trip / unknown-key / range tests; `static_assert`s on `SsrParams`, `GodRaysParams`, `SsaoParams`
- [x] 5. Investigated (report only, nothing changed): **C2 is a real mismatch** — `Renderer.cpp` forces SSR `puddleCoverage` to 0 in release (its comment says release is puddle-free) but `lighting.frag` `SP_PUDDLE_COVERAGE` is 0.501 in release, so the lit pass draws puddles that SSR never reflects in; **needs an owner decision**. R8: `LUMA_601` (0.299/0.587/0.114) is only the wet-albedo desaturation in `lighting.frag:322`, present since the first commit; keep as a separate named constant
- [x] 6. MoltenVK on Apple M3 Max reports `maxPushConstantsSize = 4096` (device API 1.3.313); the 224 B SSR block is far below it. Still to copy into the docs

### Phase 1 — hygiene and docs
- [x] `.gitignore` rewritten (large asset dirs, `data/osm`, `review_*/`, `build-debug/`, presets outside `build/`); corridor `.bin`/`.toml` left unignored (still untracked — `git add` them when committing)
- [x] `git rm --cached References/` (3 PDFs) — done in the owner-approved follow-ups (index deletions staged); files now in `archive/local/References/`
- [~] Docs: CLAUDE.md slim + AGENTS.md stub, REPO_MAP, RECIPES, stale-fact fixes, `.cursorrules`/`.cursor/` removed (backup in scratchpad), monthly CHANGELOG split + generated index (`scripts/changelog_index.py`, all 103 entries verified preserved) — done 2026-09-29; still open: freshness front-matter on the remaining core docs
- [x] Agent merge 20 -> 12 (local `.claude/`, `traffic-engineer` kept; backup in scratchpad), dispatch depth <= 1 rule in `.claude/README.md`
- [x] Prune `.claude/worktrees/thirsty-darwin` — done in the owner-approved follow-ups (backup in scratchpad)
- [x] CI (unproven on Linux — not runnable here): Linux job building `swish` + debug-UI config; `make help`; `make run` builds first; `build-debug/` + `CMakePresets.json`; drop Windows backend

### Deep clean (owner request 2026-09-29, running while owner is away)
- [x] Wave A (parallel, split by directory; verified: release 149/149, debug-UI 168/168, 33/33 `.spv`, 0 warnings with `-Werror`, runtime first frame pixel-identical to the pre-refactor binary, 0 validation messages): A1 scene (RoadScene.cpp split, delete dead `generate_trees`, element registry, CarPhysics.h topic headers); A2 renderer (Renderer.cpp split, migrate the 6 identical-mask barrier pairs to `vkx`, shader-module dedupe; 2 differing pairs held for sync validation); A3 core/debug (App::run split + CameraController, presets out of `build/`, more registry groups); A4 hygiene (root clutter -> `archive/docs/notes/` or backup, script portability, `make check` + size guard + pre-commit sample)
- [x] Wave B (serial, touches includes everywhere; verified: release 181/181, debug-UI 203/203, 33/33 `.spv`, 0 warnings, smoke test identical): `src/utils/<topic>/` layout + `swish::util` namespaces + include-path cleanup
- [x] Wave C: docs — cleanup report (what was cleaned / reorganized), "keeping it clean" guide, directory README + Excalidraw of the new tree, REPO_MAP/RECIPES/CLAUDE.md refresh, changelog entries
- [ ] Final: main session verifies both configs, 33 `.spv` hashes, goldens, runtime smoke test under validation + window-only screenshots
- Follow-up found by verification: golden mesh intro hashes are bit-exact to Release/Clang codegen and fail at `-O0`, with `-ffp-contract=off`, and under GCC — make them tolerance-based outside optimized Clang before Arch/GCC CI can be green
- Held for the owner: C2 puddle mismatch, `References/` untrack, `.claude/worktrees/thirsty-darwin`, the 2 differing barrier pairs, V5 dynamic rendering, pass registry, Arch reconciliation (plans in `archive/local/review_2026-09-29/docs/arch-plan-*.md`), any commit/push

### Phase 2 — single sources of truth
- [ ] 1. `src/utils/units/Units.h` replacing the 4-6 duplicated unit constants; `toml_baker` includes shared `RoadConfig.h`
- [ ] 2. `src/utils/<topic>/` layout per `docs/utils-component-design.md`
- [ ] 3. Tunable registry (`src/debug/Tunables.def`), start with the `ssr` group
- [ ] 4. `shaders/common/*.glsl` in verified batches (`.spv` hashes must match)
- [ ] 5. `utils/vulkan/` (`vkx`), starting with `Barrier.h`

### Phase 3 — structural (only after 0-2 are green)
- [ ] V5 gate: Vulkan 1.3 dynamic rendering + sync2, TAA first
- [ ] Pass architecture (`IRenderPass`, `FrameCtx`, `PassRegistry`, `enum class Img`, ...)
- [ ] Pure file splits: `RoadScene.cpp`, `Renderer.cpp`, `CarPhysics.h`; delete dead `generate_trees`
- [ ] Build/module split; split `App::run()` (~515 lines today; see `archive/src/core/App/REFACTOR.md`)

### Owner-approved follow-ups (2026-09-29, in progress)
- [x] Delete stale `.claude/worktrees/thirsty-darwin` (backup `scratchpad/deepclean-backup/thirsty-darwin-worktree.tgz`); branch `claude/thirsty-darwin` left for the owner
- [x] Untrack `.obsidian/` and `References/` (index-only, 12 deletions STAGED; files still on disk; gitignored)
- [x] C2 puddle fix: SSR uses the same puddle coverage as the lit pass (0.501) from one registry entry; test pins `lighting.frag`'s literal. Verified: dry and fully-wet frames pixel-identical, 0 validation messages
- [x] C2 visual proof (2026-09-29): at forced wetness 0.3, old (SSR coverage 0) vs new (0.501) differ in ~300 px/frame by ≤6/255 in cockpit/chase/hood views — correct but subtle: puddles become more reflective (~0.11 -> ~0.33) yet SSR can only reflect on-screen geometry and road reflections from these cameras mostly hit the off-screen sky (IBL fallback, unchanged). Would show more with a low camera across puddles toward objects
- [x] DRY: puddle coverage single C++ source `swish::road::kPuddleCoverage` (`src/scene/RoadScene/RoadParams.h`); physics aliases it; tests pin registry default == scene constant and `lighting.frag` literal == registry
- [x] R1: PostProcessManager split (1063 -> 5 files, pure move; runtime check pixel-identical)
- [x] R2: dynamic rendering + sync2 (TAA first) + all legacy barrier helpers removed; fixed 14 pre-existing barrier/blit sync hazards (luminance chain) that the original code produced with TAA+auto-exposure on
- [x] R2 follow-up: 0 validation messages in all 4 configs (debug-UI TAA off / TAA+AE dynamic / TAA+AE classic / release-look). Fixes: ImGui render pass external dependency now includes COLOR_ATTACHMENT_READ (pre-existing), VelocityPass colour subpass dependency + vertex input trimmed to location 0 (new), per-frame bloom-extract descriptor sets (pre-existing cross-frame race: odd frames sampled frame 0's HDR). Release first frame pixel-identical inside the window
- [x] R3: pass registry (`src/renderer/Passes/`: IRenderPass, PassRegistry, PassList.def, FullscreenPass; GeometryProducer, WeatherController; vkx builders). Add a pass = 2 files. Verified: 218/241 tests, 33/33 .spv, release first frame pixel-identical, 0 validation messages in 4 configs
- [x] Tag dispatch for tunable value kinds (`utils/meta/Tags.h`, `tunables::visit`) + MeshBuilder `push_quad`/`add_quad` (mesh dump byte-identical at -O3 and -O0)
- [x] class/struct audit applied (ResourceManager -> `swish::resource`, Pipeline -> `swish::pipeline` with Device-owned cache, CorridorScene/Scene/MeshData fixed); PostProcessParams padded to 64 B; every push range uses `vkx::push_range<T>`

- [x] GCC 16 sweep clean for Arch (DebugUI sign-compare, VmaUsage pragmas, test unused fn); GCC runs all tests except the known Clang-bound golden-mesh hash
- [x] Warnings: `-Wall -Wextra -Wpedantic` now also applies on AppleClang (was GNU/Clang only — earlier macOS "0 warnings" claims lacked these flags); VMA + `include/` marked SYSTEM; 4 real warnings fixed; 0 first-party warnings with `-Werror`
- [x] Final docs pass: changelog entry, CLEANUP "Later updates", DIRECTORY.md + Excalidraw for `Passes/`, stale refs fixed across docs
- [ ] Owner decisions left: delete branch `claude/thirsty-darwin`?; commit plan (see `archive/local/review_2026-09-29/docs/arch-plan-git-reconciliation.md`; corridor `.bin`/`.toml` need `git add`; moved notes need `git add -A`); Arch reconciliation incl. swapchain fix `6d9d920` (must be platform-gated); golden-mesh portability for GCC/Arch
- [ ] Still over the size limit / not clang-formatted: see `make check` (report-only)

### Archive reorganisation (owner request 2026-09-29)
Owner chose: every local-only folder into `archive/local/` (gitignored); archive the 19 stale docs + 3 borderline
(wheels research 01, both research surveys, metal_debugging) + 6 stale diagrams into `archive/` with their folder
structure; fix every inbound link. Nothing deleted.
- [x] Baselines: 25 pre-existing broken links, 33/33 `.spv` OK
- [x] Carry `archive/src/core/App/REFACTOR.md` "What remains" into Open items (below) before archiving it
- [x] Move local-only dirs: examples, blender_builder, zip_files, new_assets, durango, References, review_2026-09-29, car_internals, build-xcode, .conductor, .dist, imgui.ini
- [x] `git mv` / `mv` the stale docs and diagrams into `archive/`, `archive/README.md` index
- [x] Repoint inbound links (docs, code comments, shader comment) + the moved files' own outbound links
- [x] `.gitignore` (`archive/local/`), `scripts/check_root.sh` (`archive`), notes rule -> `scratch/`, docs index / REPO_MAP / DIRECTORY / CODING_RULES
- [x] Verify: broken links <= baseline, 33/33 `.spv`, `make test`, `make check`; changelog entry
- **Review:** 29 docs/diagrams to `archive/`, 12 local folders to `archive/local/`, links 25 -> 13 broken (13 = placeholders + changelog history), 218/218 + 241/241 tests, 33/33 `.spv`, `make check` OK. Adapted to the owner's own moves (`docs/user-goals/`, `assets/cars/` now gitignored, `AGENTS.md` removed). Lesson: a plain-text path rewrite must skip text inside links it just rewrote, and must not touch paths that name git-index or other-worktree state.

### CI fixes (deferred by owner 2026-09-29 — do later)

**Why it's needed:** today's refactor changed the test layout, but `.github/workflows/ci.yml` was not updated
(an agent's edit was blocked by permissions and the owner chose to defer). As written, CI will most likely fail:

- **Job 1 builds only `swish_tests`** (`cmake --build build --target swish_tests`) but then runs `ctest` over the
  whole build dir — the new `swish_utils_tests` target is never built, so ctest reports it as `NOT_BUILT` → failure.
- **Job 1 uses Ubuntu's default GCC.** The golden-mesh intro hashes (`generate.vertex_hash`,
  `generate.drawcall_hash` in `tests/test_golden_mesh.cpp`) are bit-exact to optimized Apple-Clang codegen; they were
  proven to fail at `-O0`, with `-ffp-contract=off`, and under GCC 16. Linux Clang on x86_64 is also unverified.
- **No repo-hygiene gate:** `make check` / `scripts/check_repo.sh` exist but CI never runs them, so clutter,
  stale changelog index or ignored-but-tracked files can slip in.
- **Stale header comment** in `ci.yml` still mentions `FileIO` (removed in the refactor).
- **Nothing has run on Actions yet** — the Linux matrix job (release + debug-ui, clang, `-DSWISH_WERROR=ON`) added
  today is unproven.

**Checklist:**
- [ ] Job 1: build all test targets (`cmake --build build` or `--target swish_tests swish_utils_tests`), or scope
      ctest to built targets
- [ ] Make the golden-mesh hash checks portable: assert exact hashes only in optimized Clang builds; elsewhere check
      counts/bounds/tolerances (keep `make test` on macOS strict)
- [ ] Add a `bash scripts/check_repo.sh --quick` step (hard checks only)
- [ ] Fix the stale `FileIO` comment in `ci.yml`
- [ ] Decide the `.spv` hash policy for CI (the golden file is tied to macOS Vulkan SDK glslc 1.4.321.0; Linux glslc
      differs — per-platform baseline or skip in CI; see `archive/local/review_2026-09-29/docs/arch-plan-toolchain-build-assets.md`)
- [ ] Push to a branch and confirm both CI jobs go green (release + debug-ui matrix, `-Werror` on first-party code)
- [ ] Optional: add `make check --strict` once the tree is clang-formatted and no file exceeds the size limits

## Open feature work (roughly by ROI; verify each against `git log` before starting)

| # | Item | Notes |
|---|------|-------|
| 1 | Endless-road **Layer 5 polish** | exit signage, network lights, junction UX, haze/far-plane tuning |
| 2 | Wheel **Phase C** | slip/lockup visuals + rain-mu tie-in; verify rain at 0 and 1.0; effort S-M |
| 3 | Phase 3 corridor **3b / 3c** | 3b: real interchange ramps + service-road connections, then delete unwired `generate_interchange` / `generate_service_roads` / `generate_chunk`; 3c: exit-number signage + HOV markings from OSM tags |
| 4 | **Per-wheel spray emitters** | needs a `CarEntity` wheel-matrix accessor, `App.cpp` wiring, `Renderer` setter, 4 spawn points in `SpraySimUBO` |
| 5 | **TAA as release default** vs SSAA | user decision pending; TAA and `VelocityPass` are debug-gated today |
| 6 | Reflection / ambient energy rebalance; Fresnel-bounded sheen (E2) | stops bright panels stacking past AgX's shoulder; also cures over-exposed far road |
| 7 | Height fog (D1) + sun-tinted airlight (D2); froxel volumetrics (C4) | C4 wants clustered light culling (C6) first |

## Open items by area (carried verbatim in meaning from the archives)

**Vehicle**
- [ ] Squat / dive / roll load transfer (Phase B2; needs stable `ax`/`ay`) — `archive/2026-08.md`, `archive/2026-07.md`
- [ ] Wheel Phase B spec section 11 debug sliders (`debug-ui-engineer`)
- [ ] Hydroplane: violent spin needs per-side water sampling or combined-slip tire (cheapest step: first-order lag on `filmOverTread`); dry-road spin stays gated (decision 2)
- [ ] `puddleAmount` exists in three hand-synced copies (`lighting.frag`, `ssr.frag`, `CarPhysics.h`); the planned shared GLSL include (Phase 2 step 4) removes this
- [ ] Promote the debug Telemetry speed readout to an always-compiled release overlay (user chose debug-gated first)

**Core / App** (from the archived `REFACTOR.md`, 2026-09-29)
- [ ] Input injection: `Camera::process_keyboard` and `CarEntity::handle_input` still read GLFW directly; gather input into a plain struct in `core/`
- [ ] Weather presets as data files: `WeatherPreset` is still an enum in `src/scene/WeatherPreset.h`
- [ ] `Subsystems` struct for the `unique_ptr`s + scene-lambda wiring in `App` (only if a second scene appears)
- [ ] No automated test drives `App` (needs a window); verify with a scripted screenshot run

**Rendering / lighting**
- [ ] `glass.frag` sun disc has no `#ifdef SWISH_DEBUG_UI` set-4 branch, so debug sun-disc sliders do not move the windshield sky (bind set 4 in `GlassPass.cpp`)
- [ ] Composite `Params` push-constant block was measured at 52 bytes (not a 16-byte multiple); re-measure and pad if still true
- [ ] Shadow polish: PCSS soft shadows, tighter frustum fit, bias auto-tune
- [ ] SSAA polish (render-scale as key/config; better downsample kernel above 1.5x)
- [ ] Bloom pyramid with Karis firefly suppression (C2) + vignette + film grain (F2)
- [ ] Analytic sky (D3); multiscatter GGX (E1); layered wet BRDF (E3); puddle ripple normals (E4)
- [ ] Blue-noise dithering (G1); Halton sequences (G2); Halton temporal upsampling (G3)
- [ ] GI approximation (H1); environment density + decals (B3); clustered light culling (C6); mesopic night look (C8)
- [ ] Night-scene lamp tuning; light-coupled rain streaks (A1); windshield lens optics (R-P1-3)
- [ ] Software robustness: exception-safe subsystem destructors (S-P0-2); `VK_CHECK_LOG` on teardown paths; delete the 0-byte `scripts/glslc_test`

**Rain (from the 2026-06-30 roadmap)**
- [ ] A2 impact splashes; A3 lens raindrops overlay; A4 validate windshield drops vs Heartfelt; A5 wetness-map mip blur

**Road / LIE (from the 2026-06-30 roadmap)**
- [ ] B1 curved centerline (largely superseded by the OSM corridor, Phase 3a); B2 distance LOD + billboard impostors; B4 network-first refactor; B5 further extension after B2

**Car and environment assets (planned, nothing built; `docs/car-asset-pipeline.md`, `docs/environment-asset-pipeline.md`)**
- [ ] A/B Tripo3D vs Hunyuan3D on an exterior shell; budget real Blender retopo
- [ ] Interior shapes (steering wheel + dashboard) to the naming convention in `docs/car-asset-pipeline.md` section 2
- [ ] GLB export from Blender, name-based loader path with heuristic fallback (additive)
- [ ] Hand-model wiper blade (two materials, WORLD_SCALE standoff gap)
- [ ] Leaf-card texture atlases (species-referenced) before any tree LOD; B2 = nearer mesh LOD geomorphing into the billboard
- [ ] Note: clearcoat + flake shader (formerly "open item 1") **shipped 2026-08-23**, so AI-paint texturing can target real shader inputs

## Verify before starting (archive checkboxes known to be stale)

These read as open in the archives but shipped later (evidence in `CHANGELOG.md` / code): real HDRI IBL,
SSR, SSAO, CSM, auto-exposure, DoF (`src/renderer/DofPass/`), god-rays, puddles + spray, TAA + motion blur
(debug-gated), FOV kick + shake (F1), `Smoothing.h` (F3), wet-asphalt BRDF (C1), clearcoat + flake,
per-object velocity buffer, Wheel Phase B, ADR `docs/adr/0001-*` (the "ADR candidate" box). The C++ standard
is now 20. Re-check any other older box before treating it as open.
