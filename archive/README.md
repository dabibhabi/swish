# Archive

**Label:** Index largely AI-generated (Claude) on 2026-09-29 from a staleness audit of every doc; please review.

Things that no longer describe the code as it is, or are not code at all, but are worth keeping. Nothing here is
built, tested or kept up to date. **Read it for history; do not build on it.** For the current state start at
[../docs/README.md](../docs/README.md), [../docs/CODING_RULES.md](../docs/CODING_RULES.md) and
[../tasks/todo.md](../tasks/todo.md).

**Paths mirror where each file used to live:** `docs/wheels/phase-b-spec.md` became `archive/docs/wheels/phase-b-spec.md`.
To find an old link, put `archive/` in front of it.

| Part | Tracked in git? | What is in it |
|------|-----------------|---------------|
| `archive/docs/`, `archive/src/`, `archive/tasks/` | Yes (moved with `git mv`, history kept) | Finished plans, shipped specs, research, resolved investigations, old notes, stale diagrams |
| `archive/local/` | **No** (gitignored, this machine only) | Third-party reference code, raw downloads, old build trees, the clean-up review |

**Adding to it:** `git mv <path> archive/<path>`, add a row below, repoint inbound links (code comments too), and
note it in the changelog. Drafts and scratch work go in `scratch/`, not here.

---

## Tracked: docs that were finished or went stale

### Shipped plans and specs

| File | What it was | Why archived | Current source of truth |
|------|-------------|--------------|-------------------------|
| [docs/phase-1-physics-speed-accuracy.md](docs/phase-1-physics-speed-accuracy.md) | Phase 1 physics and speed accuracy (911 Turbo S retarget) | Shipped 2026-08-23; pre-split paths | [car_system.md](../docs/car_system.md), `src/scene/Entity/` |
| [docs/phase-2-hydroplane-spinout.md](docs/phase-2-hydroplane-spinout.md) | Phase 2 hydroplane spin-out design + as-built notes | Shipped 2026-08-23; open follow-up is in `tasks/todo.md` | `src/scene/Entity/{Longitudinal,Bicycle}.h` |
| [docs/wheels/phase-b-spec.md](docs/wheels/phase-b-spec.md) | Wheel Phase B (ride height / quarter-car) spec | Shipped 2026-08-15 | [docs/wheels/README.md](../docs/wheels/README.md), `WheelKinematics.h` |
| [src/core/App/REFACTOR.md](src/core/App/REFACTOR.md) | `App::run` split plan and as-built notes | Marked DONE 2026-09-29; its "what remains" list moved to `tasks/todo.md` (Core / App) | [src/core/README.md](../src/core/README.md) |
| [tasks/glen-cove-lie-selfguide.md](tasks/glen-cove-lie-selfguide.md) | Offline self-execution handoff for Glen Cove detail | Garbled title, pre-split line refs, old test counts | [docs/glen-cove-lie.md](../docs/glen-cove-lie.md) |

### Research (superseded by what was built)

| File | What it was | Why archived |
|------|-------------|--------------|
| [docs/wheels/research/01-physics-theory.md](docs/wheels/research/01-physics-theory.md) | Rolling kinematics, slip, quarter-car theory | Research only (owner chose to archive); still the theory behind open Phase C |
| [docs/wheels/research/02-graphics-vulkan.md](docs/wheels/research/02-graphics-vulkan.md) | Wheel rendering research | Phase A shipped 2026-07-24 |
| [docs/wheels/research/03-implementation-plan.md](docs/wheels/research/03-implementation-plan.md) | Wheel Phases A/B implementation plan | Carried out; superseded by the Phase B spec |
| [docs/research-rain-rendering.md](docs/research-rain-rendering.md) | Cited survey of rain-rendering techniques | Mostly shipped; kept as a reading list |
| [docs/research-night-scene-realism.md](docs/research-night-scene-realism.md) | Cited survey of night-scene realism | Kept as a reading list for future night work |
| [docs/rain/implementation-plan.md](docs/rain/implementation-plan.md) | Rain implementation plan (as-built record) | Current doc is [docs/rain/README.md](../docs/rain/README.md) |
| [docs/rain/codebase-map.md](docs/rain/codebase-map.md) | Pre-implementation map of rain code | Superseded by rain/README |
| [docs/rain/technique-refraction.md](docs/rain/technique-refraction.md) | Windshield refraction research | Shipped |
| [docs/rain/technique-wiper.md](docs/rain/technique-wiper.md) | Wiper research | Shipped (analytic blade) |

### Investigations and notes

| File | What it was | Why archived |
|------|-------------|--------------|
| [docs/investigations/2026-04-21-vulkan-validation-errors.md](docs/investigations/2026-04-21-vulkan-validation-errors.md) | Linux validation errors (VUID-00067 semaphore, dormant SSAO layout) | Fixed |
| [docs/notes/README.md](docs/notes/README.md) | Index of the loose notes moved out of the repo root | The whole folder was already "history, not maintained" |
| [docs/notes/issue.md](docs/notes/issue.md) | Rain defect write-up | Fixed (status line says "Open" but it is not) |
| [docs/notes/prompt.md](docs/notes/prompt.md) | One-off orchestration prompt for that fix | One-off; says C++17 |
| [docs/notes/metal_debugging.md](docs/notes/metal_debugging.md) | MoltenVK / Metal GPU-capture how-to | Owner chose to archive; still useful, but its `cd` path is stale |
| [docs/notes/obsidian_latex_mermaid.md](docs/notes/obsidian_latex_mermaid.md) | Obsidian LaTeX/mermaid cheat-sheet | `.obsidian/` was removed |
| [docs/notes/plan/](docs/notes/plan/README.md) | DownPour car-system port plan | 13 of 18 paths it names no longer exist |
| [docs/notes/call_hierarchies/rain_hierchy.md](docs/notes/call_hierarchies/rain_hierchy.md) | Rain call chain | Names `App::processInput` and the pre-split `Renderer.cpp` |

### Diagrams (draw deleted classes)

| File | Stale because it shows |
|------|------------------------|
| [docs/diagrams/architecture.excalidraw](docs/diagrams/architecture.excalidraw) | `DepthBuffer`, `WORLD_SCALE` |
| [docs/diagrams/renderer_architecture.excalidraw](docs/diagrams/renderer_architecture.excalidraw) | `DepthBuffer`, `utils/Types.h`, `WORLD_SCALE` |
| [docs/diagrams/roadside-detail.excalidraw](docs/diagrams/roadside-detail.excalidraw) | the deleted `generate_trees` |
| [docs/diagrams/debug-ui-dataflow.excalidraw](docs/diagrams/debug-ui-dataflow.excalidraw) | scene-params UBO as "set 3, 10 vec4" (now set 4, 11 vec4) |
| [docs/diagrams/descriptor-sets-scene-params.excalidraw](docs/diagrams/descriptor-sets-scene-params.excalidraw) | the layout before IBL took set 3 |
| [docs/diagrams/phase3-osm-pipeline-small.excalidraw](docs/diagrams/phase3-osm-pipeline-small.excalidraw) | `WORLD_SCALE` |

Current diagrams: [../docs/diagrams/](../docs/diagrams/) (index in [../docs/README.md](../docs/README.md#diagrams)).

---

## Local only: `archive/local/` (gitignored)

These exist only on the machine that has them. They are not on GitHub and links to them from tracked docs only work
locally. Sizes measured 2026-09-29.

| Folder | Size | What it is | Still referenced by |
|--------|------|------------|---------------------|
| `examples/` | 3.1 GB | Third-party reference source: Project Chrono, DownPour, rind (~4,900 C/C++ files; never compiled here) | [docs/lessons-from-chrono/](../docs/lessons-from-chrono/00-index.md) (links repointed) |
| `blender_builder/` | 577 MB | Blender build scripts, logs and plans for the Hellcat model | nothing in the build |
| `zip_files/` | 140 MB | Raw downloads: asphalt/gravel textures, FHWA highway fonts, a tree `.blend` | nothing (the unpacked textures live in `textures/`) |
| `build-xcode/` | 48 MB | Old Xcode build tree (last built April 2026); regenerate instead of reusing | nothing |
| `new_assets/` | 30 MB | Raw HDRI downloads (`*_2k.hdr`) | nothing (runtime HDRIs go in `assets/Sky/`) |
| `References/` | 5.1 MB | Rain-rendering PDFs (Tatarchuk, GPU rainfall, ACM paper) | [docs/rain/README.md](../docs/rain/README.md) |
| `durango/` | 472 KB | Dodge Durango reference photos | nothing |
| `review_2026-09-29/` | 396 KB | Audit and plan documents behind the 2026-09-29 clean-up | a few design notes cite it by path |
| `car_internals/` | 20 KB | Empty stub (`main.cpp` placeholder, empty folders, `.mcp.json`) | nothing |
| `imgui.ini`, `.conductor/`, `.dist/` | tiny | Old ImGui layout file (the debug build writes a fresh one), empty tool folders | nothing |

**Want something back?** `mv archive/local/<name> .` from the repo root. The root-file check still allows these names,
and `.gitignore` still ignores them.
