<div align="center">

<img src="assets/sim_logo.png" alt="Swish logo" width="220" />

# Swish

**A real-time driving simulator built from scratch in Vulkan.**

Cruise a glossy Porsche down a procedurally generated Long Island Expressway (I-495) through a
full deferred + forward rendering pipeline — G-buffer → PBR lighting → GPU rain → glass →
windshield rain trails → HDR bloom → AgX tone mapping.

<br />

<img src="assets/swish_demo.gif" alt="Swish gameplay demo" width="720" />

[**▶ Watch the full demo on YouTube**](https://youtu.be/Krco1xFme2A)

</div>

> [!WARNING]
> **On Linux, do _not_ run the simulator in full-screen mode.** There is a known full-screen bug
> that is still being fixed. Run in windowed mode instead.

---

## What it's built with

| Tool | Version | What it's for |
|------|---------|---------------|
| [Vulkan SDK](https://vulkan.lunarg.com/) | ≥ 1.3 | Graphics API + `glslc` shader compiler |
| [CMake](https://cmake.org/) | ≥ 3.20 | Build system |
| C++ compiler | C++20 | Apple clang / Clang (the reference toolchain) or a recent GCC; the intro-mesh golden hashes only match Release + Clang |
| [GLFW](https://www.glfw.org/) | ≥ 3.3 | Windowing + input |
| [GLM](https://github.com/g-truc/glm) | any | Vector / matrix math |
| [tinygltf](https://github.com/syoyo/tinygltf) | bundled | GLB / glTF model loading |
| [toml++](https://github.com/marzer/tomlplusplus) | bundled | TOML scene config |
| [stb_image](https://github.com/nothings/stb) | bundled | Texture loading |
| [Catch2](https://github.com/catchorg/Catch2) | bundled | Unit tests |

The bundled libraries come with the repo — you only need to install the top five.

---

## Get it running

### 1. Install what you need to download

<details open>
<summary><b>Linux (Arch)</b></summary>

```bash
sudo pacman -S vulkan-devel glfw-x11 glm cmake
```
</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install glfw glm cmake
# Then grab the Vulkan SDK (MoltenVK): https://vulkan.lunarg.com/sdk/home
```
</details>

> Windows is no longer a supported backend (dropped 2026-09-29): macOS (MoltenVK) and Linux only.

### 2. Build & run

```bash
make run          # release build into build/, then runs build/swish
make debug        # Dear ImGui live-tuning build into build-debug/ (backtick toggles edit/drive)
make test         # release build + ctest
make help         # every target (check, format, new-class, ...)
# or, without make:
cmake -B build -DCMAKE_BUILD_TYPE=Release   # auto-detects your platform
cmake --build build -j
./build/swish
```

> Shaders compile automatically during the build via `glslc`. The first build writes all SPIR-V
> binaries into `build/shaders/`. Keep `VULKAN_SDK` set (or pass `-DGLSLC=...`): CMake looks for `glslc`
> in `$VULKAN_SDK/bin` first, and a different `glslc` version changes the SPIR-V bytes (the golden shader hashes in
> `tests/golden/` assume the SDK's). Validation layers are on only in a plain `-DCMAKE_BUILD_TYPE=Debug` build.

---

## Controls

| Key | Action |
|-----|--------|
| `↑` `↓` | Throttle / brake |
| `←` `→` | Steer left / right |
| `C` | Cycle camera (cockpit, chase, hood, bumper, roof, side, helicopter, free-fly) |
| `W A S D` | Move camera (free-fly) |
| `Q` `E` | Camera down / up |
| `Shift` | Sprint (free-fly speed ×2) |
| `R` | Cycle rain: off → light → heavy |
| `V` | Cycle the wipers: off → intermittent → low → high |
| `G` | Cycle the weather HDRI preset |
| `ESC` | Toggle mouse capture |
| `` ` `` | Debug build only: toggle edit (ImGui) vs drive mode |

---

## Platform config

Swish auto-detects the graphics backend from your OS. Override with `-DSWISH_BACKEND=<value>`:

| Value | Platform | GPU preference |
|-------|----------|----------------|
| `AUTO` | Detect from OS | — |
| `LINUX_VULKAN` | Linux | AMD or NVIDIA |
| `APPLE_METAL` | macOS (MoltenVK) | Apple GPU |

```bash
cmake -B build -DSWISH_BACKEND=LINUX_VULKAN   # force a backend
```

Discrete GPUs are preferred over integrated, and a matching vendor over a mismatched one.

---

## Architecture

Swish is a 5-layer stack: platform (App/Window) → Vulkan core → rendering subsystems → scene
management → GLSL shaders. The `Renderer` is a facade that owns every Vulkan
subsystem and exposes a `RendererServices` bundle so scene code can upload geometry without holding
raw handles.

See [`docs/architecture.md`](docs/architecture.md) for the full design and startup sequence.

---

## Documentation

Start at the [**documentation index**](docs/README.md). Highlights:

| Document | Description |
|----------|-------------|
| [`docs/CODING_RULES.md`](docs/CODING_RULES.md) | **Read before writing code** — 12 rules, where each thing goes, what connects to what in plain English (`make rules`) |
| [`docs/FEATURE_RULES.md`](docs/FEATURE_RULES.md) | **Read before planning a feature** — spec template, feature shapes and owners, build dark, prove it, turn it on (`make feature-rules`) |
| [`docs/GLOSSARY.md`](docs/GLOSSARY.md) | **Quick lookup** — every term, class, and convention, one line each |
| [`docs/REPO_MAP.md`](docs/REPO_MAP.md) · [`docs/DIRECTORY.md`](docs/DIRECTORY.md) | Where everything lives (task-oriented map / full annotated tree) |
| [`docs/RECIPES.md`](docs/RECIPES.md) | How to add a tunable, shader, render pass, road prop, test, util, config key |
| [`docs/KEEPING_IT_CLEAN.md`](docs/KEEPING_IT_CLEAN.md) · [`docs/CLEANUP_2026-09-29.md`](docs/CLEANUP_2026-09-29.md) | Upkeep rules (`make check`) and what the 2026-09-29 deep clean changed |
| [`docs/architecture.md`](docs/architecture.md) | 5-layer architecture, startup sequence, design patterns |
| [`docs/render-pipeline.md`](docs/render-pipeline.md) | Per-frame 6-pass deferred pipeline + shader math |
| [`docs/data-types.md`](docs/data-types.md) | Every struct/UBO with exact byte layouts and std140 rules |
| [`docs/car_system.md`](docs/car_system.md) | Car physics, mesh normalization, cockpit camera |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Coding conventions, Vulkan patterns, subsystem guides |

<details>
<summary><b>Component READMEs</b></summary>

| Directory | README |
|-----------|--------|
| `src/core/` | [`App`, `CameraController` and `Window`](src/core/README.md) |
| `src/renderer/` | [Renderer subsystems](src/renderer/README.md) |
| `src/scene/` | [Scene management](src/scene/README.md) |
| `src/utils/` | [Shared helpers by topic (`units`, `math`, `io`, `vulkan`, type aliases, ...)](src/utils/README.md) |
| `shaders/` | [Shader pipeline](shaders/README.md) |
| `tests/` | [Testing guide](tests/README.md) |
| `tools/` | [Developer tools](tools/README.md) |
</details>

<details>
<summary><b>Diagrams</b> (open <code>.excalidraw</code> at <a href="https://excalidraw.com">excalidraw.com</a>)</summary>

| Diagram | Description |
|---------|-------------|
| [`system-overview`](docs/diagrams/system-overview.excalidraw) | Component ownership hierarchy |
| [`render-pipeline`](docs/diagrams/render-pipeline.excalidraw) | 6-pass rendering pipeline |
| [`vulkan-sync`](docs/diagrams/vulkan-sync.excalidraw) | Frame synchronization (fences + semaphores) |
| [`scene-data-flow`](docs/diagrams/scene-data-flow.excalidraw) | Asset → GPU → draw calls |
| [`descriptor-sets`](docs/diagrams/descriptor-sets.excalidraw) | Descriptor set bindings + push constants |
| [`data-layout`](docs/diagrams/data-layout.excalidraw) | Vertex byte layout + std140 UBO layout |

More diagrams (and which ones predate the 2026-09-29 refactor) are indexed in [`docs/README.md`](docs/README.md#diagrams).
</details>

---

## Credits

<details>
<summary><b>Textures</b> — all CC0 1.0 (Public Domain)</summary>

| Material | Source | Author |
|----------|--------|--------|
| Asphalt (road) | [ambientCG — Asphalt012](https://ambientcg.com/view?id=Asphalt012) | ambientCG |
| Grass (shoulders) | [ambientCG — Grass004](https://ambientcg.com/view?id=Grass004) | ambientCG |
| Concrete (barrier) | [ambientCG — Concrete012](https://ambientcg.com/view?id=Concrete012) | ambientCG |
| Galvanized Steel (guardrail) | [cgbookcase — GalvanizedSteel01](https://www.cgbookcase.com/textures/galvanized-steel-01) | cgbookcase |
</details>

<details>
<summary><b>Third-party libraries</b></summary>

| Library | Purpose | License |
|---------|---------|---------|
| [GLFW](https://www.glfw.org/) | Windowing + input | Zlib |
| [GLM](https://github.com/g-truc/glm) | Math | MIT |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Graphics API | Apache 2.0 |
| [tinygltf](https://github.com/syoyo/tinygltf) | GLB/glTF loading | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | TOML config | MIT |
| [stb_image](https://github.com/nothings/stb) | Image loading | Public Domain / MIT |
| [Catch2](https://github.com/catchorg/Catch2) | Unit testing | BSL-1.0 |
</details>

---

## Investigations

In-tree notes on non-obvious behavior found during development. See [`archive/docs/investigations/`](archive/docs/investigations/).

| Date | Topic |
|------|-------|
| 2026-04-21 | [Vulkan validation: `pSignalSemaphores-00067` and dormant-SSAO image layout](archive/docs/investigations/2026-04-21-vulkan-validation-errors.md) |
