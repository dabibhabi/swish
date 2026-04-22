# Swish

A real-time driving rain simulation on the Long Island Expressway (I-495), built from scratch with Vulkan.

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
| [stb_image](https://github.com/nothings/stb) | Image loading (JPG/PNG) | Public Domain / MIT |
| [GLFW](https://www.glfw.org/) | Windowing + input | Zlib |
| [GLM](https://github.com/g-truc/glm) | Math (vectors, matrices) | MIT |
| [Vulkan SDK](https://vulkan.lunarg.com/) | Graphics API | Apache 2.0 |

## Investigations

In-tree notes on non-obvious behavior found during development. See [`docs/investigations/`](docs/investigations/).

| Date | Topic |
|------|-------|
| 2026-04-21 | [Vulkan validation: `pSignalSemaphores-00067` and dormant-SSAO image layout](docs/investigations/2026-04-21-vulkan-validation-errors.md) |
