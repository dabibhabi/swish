# Swish

A real-time driving simulation on the Long Island Expressway (I-495), built from scratch with Vulkan. Implements a full deferred rendering pipeline: G-buffer → PBR lighting → HDR bloom → ACES tone mapping, with a driveable Porsche on procedurally generated highway geometry.

---

## Quick Start

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.20 | |
| C++ compiler | C++17 | GCC ≥ 9 or Clang ≥ 10 |
| Vulkan SDK | ≥ 1.3 | Includes `glslc` shader compiler |
| GLFW | ≥ 3.3 | System package or Homebrew |
| GLM | any | System package or Homebrew |

**Linux (Arch):**
```bash
sudo pacman -S vulkan-devel glfw-x11 glm cmake
```

**macOS:**
```bash
brew install glfw glm cmake
# Vulkan SDK: download from https://vulkan.lunarg.com/sdk/home
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # auto-detects platform
cmake --build build -j$(nproc)
./build/swish
```

> **Note:** shaders are compiled automatically by CMake via `glslc`. The first build compiles all SPIR-V binaries into `build/shaders/`.

---

## Platform Config

Swish auto-detects the graphics backend from the host OS. Override with `-DSWISH_BACKEND=<value>`:

| Value | Platform | GPU Preference |
|-------|---------|---------------|
| `AUTO` | Detect from OS | — |
| `LINUX_VULKAN` | Linux | AMD (0x1002) or NVIDIA (0x10DE) |
| `APPLE_METAL` | macOS (MoltenVK) | Apple GPU (0x106B) |
| `WINDOWS_VULKAN` | Windows | AMD or NVIDIA |

```bash
# Force Linux backend explicitly
cmake -B build -DSWISH_BACKEND=LINUX_VULKAN
```

GPU vendor scoring: discrete GPU preferred over integrated (+1000 pts); matching vendor preferred over mismatched (+500 pts). The highest-scoring GPU is selected.

---

## Controls

| Key | Action |
|-----|--------|
| `W A S D` | Move camera (free-fly mode) |
| `Q E` | Move camera down / up |
| `↑ ↓` | Throttle / brake |
| `← →` | Steer left / right |
| `C` | Toggle cockpit / free-fly camera |
| `Shift` | Sprint (free-fly speed ×2) |
| `ESC` | Toggle mouse cursor capture |

---

## Architecture

Swish is a 5-layer stack: platform (App/Window) → Vulkan core → rendering subsystems → scene management → GLSL shaders. The `Renderer` acts as a central manager registry — it owns all Vulkan subsystems and exposes a `RendererServices` bundle so scene-level code can upload geometry without holding raw handles permanently.

See [`docs/architecture.md`](docs/architecture.md) for the full system design and startup sequence.

---

## Documentation

Start at the [**documentation index**](docs/README.md) for a full map. Highlights:

| Document | Description |
|----------|-------------|
| [`docs/architecture.md`](docs/architecture.md) | 5-layer system architecture, startup sequence, design patterns |
| [`docs/render-pipeline.md`](docs/render-pipeline.md) | Per-frame 6-pass deferred rendering pipeline + all shader math |
| [`docs/data-types.md`](docs/data-types.md) | Every data struct (DTO) and UBO, with exact byte layouts and std140 rules |
| [`docs/car_system.md`](docs/car_system.md) | Car physics, mesh normalization, cockpit camera |
| [`docs/investigations/`](docs/investigations/) | In-tree root cause analyses |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Coding conventions, Vulkan patterns, data-struct & subsystem guides |

### Component READMEs

| Directory | README |
|-----------|--------|
| `src/core/` | [`App` and `Window`](src/core/README.md) |
| `src/renderer/` | [Renderer subsystems](src/renderer/README.md) |
| `src/scene/` | [Scene management](src/scene/README.md) |
| `src/utils/` | [Shared helpers (`VK_CHECK`, FileIO, type aliases)](src/utils/README.md) |
| `shaders/` | [Shader pipeline](shaders/README.md) |
| `tests/` | [Testing guide](tests/README.md) |
| `tools/` | [Developer tools](tools/README.md) |

### Diagrams

Open `.excalidraw` files at [excalidraw.com](https://excalidraw.com) or with the VS Code Excalidraw extension.

| Diagram | Description |
|---------|-------------|
| [`docs/diagrams/system-overview.excalidraw`](docs/diagrams/system-overview.excalidraw) | Component ownership hierarchy |
| [`docs/diagrams/render-pipeline.excalidraw`](docs/diagrams/render-pipeline.excalidraw) | 6-pass rendering pipeline |
| [`docs/diagrams/vulkan-sync.excalidraw`](docs/diagrams/vulkan-sync.excalidraw) | Frame synchronization (fences + semaphores) |
| [`docs/diagrams/scene-data-flow.excalidraw`](docs/diagrams/scene-data-flow.excalidraw) | Asset → GPU → draw calls |
| [`docs/diagrams/descriptor-sets.excalidraw`](docs/diagrams/descriptor-sets.excalidraw) | Descriptor set 0 / set 1 bindings + push constants |
| [`docs/diagrams/data-layout.excalidraw`](docs/diagrams/data-layout.excalidraw) | Vertex byte layout + std140 UBO layout |

---

## Texture Credits

All textures are **CC0 1.0 (Public Domain)** — free for any use, no attribution required.
We credit the authors here because they deserve it.

| Material | Source | Author |
|----------|--------|--------|
| Asphalt (road surface) | [ambientCG — Asphalt012](https://ambientcg.com/view?id=Asphalt012) | ambientCG |
| Grass (shoulders) | [ambientCG — Grass004](https://ambientcg.com/view?id=Grass004) | ambientCG |
| Concrete (Jersey barrier) | [ambientCG — Concrete012](https://ambientcg.com/view?id=Concrete012) | ambientCG |
| Galvanized Steel (guardrail) | [cgbookcase — GalvanizedSteel01](https://www.cgbookcase.com/textures/galvanized-steel-01) | cgbookcase |

## Third-Party Libraries

| Library | Purpose | License |
|---------|---------|---------|
| [GLFW](https://www.glfw.org/) | Windowing + input | Zlib |
| [GLM](https://github.com/g-truc/glm) | Math (vectors, matrices) | MIT |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Graphics API | Apache 2.0 |
| [tinygltf](https://github.com/syoyo/tinygltf) | GLB/glTF model loading | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | TOML config parsing | MIT |
| [stb_image](https://github.com/nothings/stb) | Image loading (JPG/PNG) | Public Domain / MIT |
| [Catch2](https://github.com/catchorg/Catch2) | Unit testing framework | BSL-1.0 |

---

## Investigations

In-tree notes on non-obvious behavior found during development. See [`docs/investigations/`](docs/investigations/).

| Date | Topic |
|------|-------|
| 2026-04-21 | [Vulkan validation: `pSignalSemaphores-00067` and dormant-SSAO image layout](docs/investigations/2026-04-21-vulkan-validation-errors.md) |
