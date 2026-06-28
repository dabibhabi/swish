# Documentation Index

Everything you need to understand, extend, and contribute to Swish.

## Start Here

| If you want to… | Read |
|-----------------|------|
| Look up what a term/abbreviation/class means | [**GLOSSARY.md**](GLOSSARY.md) |
| Build and run the project | [Root README — Quick Start](../README.md) |
| Understand the whole system | [architecture.md](architecture.md) |
| Add code without breaking conventions | [CONTRIBUTING.md](../CONTRIBUTING.md) |
| Know what a struct's fields mean | [data-types.md](data-types.md) |

## Reference Docs

| Doc | Covers |
|-----|--------|
| [GLOSSARY.md](GLOSSARY.md) | One-line lookup of terms, abbreviations, classes, conventions, coordinate spaces, controls |
| [architecture.md](architecture.md) | The 5-layer stack, startup sequence, RendererServices, swapchain recreation |
| [rain/README.md](rain/README.md) | Rain system architecture — falling rain, refractive windshield drops, wetness map + wiper |
| [render-pipeline.md](render-pipeline.md) | The 6-pass deferred pipeline, per-pass I/O, all shader math |
| [data-types.md](data-types.md) | Every data struct (DTO) and UBO, with exact layouts and std140 rules |
| [car_system.md](car_system.md) | GLB loading and car-mesh normalization algorithm |
| [investigations/](investigations/) | Debugging write-ups (e.g. the VUID-00067 semaphore fix) |

## Component READMEs

| Directory | README |
|-----------|--------|
| `src/core/` | [Platform layer — App, Window, controls](../src/core/README.md) |
| `src/renderer/` | [Vulkan subsystems — directory map, init order, per-frame recording](../src/renderer/README.md) |
| `src/scene/` | [Scene management, entity physics, procedural road](../src/scene/README.md) |
| `src/utils/` | [Shared helpers — VK_CHECK, init helpers, FileIO, type aliases](../src/utils/README.md) |
| `shaders/` | [Shader pipeline, descriptor layout, per-shader math](../shaders/README.md) |
| `tests/` | [Catch2 testing guide](../tests/README.md) |
| `tools/` | [toml_baker and car_analyzer](../tools/README.md) |

## Diagrams

All diagrams are valid [Excalidraw](https://excalidraw.com) JSON (version 2). Open them at excalidraw.com (paste the file) or via the VS Code Excalidraw extension.

| Diagram | Shows |
|---------|-------|
| [system-overview](diagrams/system-overview.excalidraw) | Component ownership hierarchy |
| [render-pipeline](diagrams/render-pipeline.excalidraw) | The 6 render passes and barriers |
| [vulkan-sync](diagrams/vulkan-sync.excalidraw) | Fence + semaphore timing across frames |
| [scene-data-flow](diagrams/scene-data-flow.excalidraw) | Asset → GPU → draw call pipeline |
| [descriptor-sets](diagrams/descriptor-sets.excalidraw) | Set 0 / set 1 bindings and push constants |
| [data-layout](diagrams/data-layout.excalidraw) | Vertex byte layout and std140 UBO layout |
