# Contributing to Swish

Conventions you need to add code without breaking what is there. Read three things first:

- [docs/KEEPING_IT_CLEAN.md](docs/KEEPING_IT_CLEAN.md): where each kind of thing goes, the rules the tree was
  cleaned to, the edit / verify / commit loop and the pre-commit checklist.
- [docs/RECIPES.md](docs/RECIPES.md): step-by-step "how do I add X" (tunable, shader, render pass, road prop,
  test, config key, ...). Adding a subsystem, a shader pass or a test is in there, not here.
- [docs/REPO_MAP.md](docs/REPO_MAP.md): where things live.

Agent rules (build commands, MoltenVK gotchas, changelog format) are in [CLAUDE.md](CLAUDE.md).

*Labelled AI-assisted: revised 2026-09-29 during the deep clean ([docs/CLEANUP_2026-09-29.md](docs/CLEANUP_2026-09-29.md)); not yet reviewed by the owner.*

---

## Daily commands

| Command | Does |
|---------|------|
| `make build` / `make test` | release tree (`build/`), then ctest |
| `make debug` / `make test-debug` | debug-UI tree (`build-debug/`), then ctest |
| `make format` / `make format-check` | clang-format in place / dry run |
| `make check` | repo hygiene (changelog index, stray root files, sizes, source globs) |
| `make new-class NAME=Foo DIR=scene` | scaffold `src/scene/Foo/Foo.{h,cpp}` |
| `make install-hooks` | opt-in pre-commit hook (runs the quick `make check`) |

Commit and push only when asked. Every non-trivial change gets a changelog entry in
`docs/changelog/YYYY-MM.md` followed by `python3 scripts/changelog_index.py`.

---

<details>
<summary><strong>Naming and layout</strong></summary>

| Entity | Style | Examples |
|--------|-------|---------|
| Classes, structs, enums | `PascalCase` | `SceneGeometry`, `DrawCall`, `MaterialId` |
| New functions, utils, free functions | `snake_case` | `read_binary()`, `record_draws()` |
| Existing Vulkan-core getters | keep `camelCase`; never mix styles inside one class | `getDevice()`, `getExtent()` |
| Member variables | `m_` + `snake_case` | `m_position`, `m_vertexBuffer` |
| Constants | `k` + `PascalCase` | `kWuPerFt`, `kCrownSlope` |
| Enum values | `UPPER_SNAKE` | `MAT_ASPHALT` |
| Data-struct fields (DTOs) | bare `camelCase`, no `m_` | `indexOffset`, `positionRadius` |
| Namespaces | `swish`; `swish::util::<topic>` for utils; `swish::vkx` for Vulkan helpers | `swish::util::units` |
| Files | match the class, `PascalCase`; a topic suffix when a class is split | `Camera.h`, `RoadScene_Ramps.cpp` |

One class per directory, directory named after the class: `src/renderer/SceneGeometry/SceneGeometry.{h,cpp}`.
File-size limits: 800 lines C++, 400 lines GLSL (`make check` reports them; split by topic).

</details>

---

<details>
<summary><strong>Includes and header structure</strong></summary>

Project includes are **rooted at `src/`**; same-directory headers use the bare name; **never `../`**.

```cpp
#pragma once

// 1. Vulkan / GLFW: only when the full type is needed in the header
#include <vulkan/vulkan.h>

// 2. Standard library
#include <memory>
#include <vector>

// 3. Third-party
#include <glm/glm.hpp>

// 4. Project headers, rooted at src/
#include "utils/types/Types.h"
#include "renderer/SomeSubsystem/SomeSubsystem.h"

namespace swish {

class Renderer;   // forward-declare types used only as pointers or references

class MyClass {
public:
    void init(const RendererServices& s);
    void cleanup(VkDevice device);
    VkPipelineLayout get_layout() const { return m_layout; }   // trivial getters inline

private:
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

}  // namespace swish
```

Inline only for trivial one-liners that return a stored value. Anything that changes state or calls Vulkan goes in
the `.cpp`. The full utils rules and layout: [src/utils/README.md](src/utils/README.md).

</details>

---

<details>
<summary><strong>Units and constants</strong></summary>

`1 scene-metre = 1000 world units (WU)`. Convert only through [`src/utils/units/Units.h`](src/utils/units/Units.h)
(`kWuPerMetre`, `m()`, `ft()`, `to_m()`, `to_ft()`, `kPi`). No literal `1000`, `0.3048` or pi for a conversion.
Road dimensions live in [`RoadParams.h`](src/scene/RoadScene/RoadParams.h) and `config/road.toml`, never as a second
copy in C++.

</details>

---

<details>
<summary><strong>Vulkan resource conventions</strong></summary>

- Initialise raw handles to `VK_NULL_HANDLE`, never `nullptr`.
- Wrap every fallible call in `VK_CHECK` (`#include "utils/vulkan/Check.h"`); it throws `std::runtime_error` with the
  symbolic error, file and line.
- **Swapchain result codes are not errors.** Route them through `swish::vkx::swapchain_needs_recreation(r)`
  (`utils/vulkan/Init.h`), and `VK_CHECK(r)` anything else.
- Use the `vkx` helpers for barriers (`vkx::image_barrier`), shader modules (`vkx::make_shader_module`) and push
  blocks (`push_range<T>()`): [src/utils/vulkan/README.md](src/utils/vulkan/README.md). Do not hand-roll
  `VkImageMemoryBarrier` and do not add a "default to TOP/BOTTOM_OF_PIPE" fallback.
- Cleanup: check `!= VK_NULL_HANDLE` before every `vkDestroy*` / `vkFree*` and reset to `VK_NULL_HANDLE` after.
  A subsystem destroys exactly what it created, in reverse order. Prefer `GpuImage` / `GpuBuffer`
  ([docs/vma-memory.md](docs/vma-memory.md)).
- Push-constant blocks are multiples of 16 bytes (MoltenVK). No comparison samplers (manual PCF).
- Scene code never calls Vulkan directly; go through the renderer managers.

</details>

---

<details>
<summary><strong>Ownership and lifecycle</strong></summary>

| Situation | Pattern |
|-----------|---------|
| `Renderer` owns a subsystem it creates | `std::unique_ptr<Name> m_name;`, `std::make_unique` in the constructor, destructor `= default` |
| `App` owns a manager the renderer uses | raw non-owning pointer handed over via a `register_*()` call (today `Renderer::register_texture_manager`) |
| Subsystem needs Vulkan handles at init or upload | take `RendererServices` ([`src/renderer/Renderer/RendererServices.h`](src/renderer/Renderer/RendererServices.h)); copy out the handles you need, never store the struct, never cache `swapchainExtent` (it changes on resize) |

Every Vulkan subsystem is two-phase: a trivial constructor, `init(...)` allocates, `cleanup(device)` destroys in
reverse creation order, then the destructor runs. `Renderer::cleanup()` drives teardown in reverse of `init()`.
No parallel global singletons. Steps to add a subsystem: RECIPES, add-a-render-pass.

</details>

---

<details>
<summary><strong>Data structures (DTOs) and std140</strong></summary>

| Family | Examples | Rules |
|--------|----------|-------|
| CPU value types | `DrawCall`, `Submesh`, `LightDesc`, `RendererServices` | plain `struct`, bare `camelCase` fields, defaults so aggregate init works, no methods |
| GPU-layout types | `Vertex`, `CameraUBO`, `LightsUBO`, `PointLightData`, push blocks | field-for-field identical to the GLSL block; **only `Vec4` / `Mat4`** (never `Vec3` or a trailing scalar) |

The `Vec4`-only rule is required, not stylistic: in std140 a `vec3` aligns to 16 bytes but a following scalar does
not pack into its trailing 4 bytes, so `glm::vec3` + `float` does not match the GLSL block. Pack scalars into
`vec4` lanes (`positionRadius.w = radius`), comment each lane, and `static_assert(sizeof(...))` the struct. Shader
side, the shared blocks come from `shaders/common/*.glsl`, never a pasted copy. A type that protects an invariant
(`MeshData`) stays a class. Full catalogue: [docs/data-types.md](docs/data-types.md).

</details>

---

<details>
<summary><strong>Tests</strong></summary>

Catch2 v3, fetched by CMake. **Tests never link Vulkan or GLFW windows**: they cover pure math, I/O, geometry
generation and physics. Add the file to the list in `tests/CMakeLists.txt` (or `tests/utils/CMakeLists.txt` for
utils); steps in RECIPES, add-a-test. Keep `, ; [ ]` out of `TEST_CASE` names. Build helpers for valid test objects
instead of constructing them inline in each case, and test boundaries and degenerate input, not just the nominal
value.

**Goldens** ([tests/golden/README.md](tests/golden/README.md)) record known-good output. A golden diff after a
refactor means behaviour changed: investigate, never regenerate to make it pass. Regenerate with
`SWISH_UPDATE_GOLDENS=1 build/tests/swish_tests "[golden]"` only for an intentional change, and review the diff.
Mesh hashes are bound to the shipped Release Clang build.

```bash
make test                                       # release tree
ctest --test-dir build-debug --output-on-failure   # or: make test-debug
./build/tests/swish_tests "[golden]"            # filter by tag
cmake -B build -DENABLE_SANITIZERS=ON           # ASan + UBSan
```

</details>

---

<details>
<summary><strong>Error handling and comments</strong></summary>

| Situation | Pattern |
|-----------|---------|
| Unrecoverable failure (bad GPU, missing file, bad `.spv`) | `throw std::runtime_error("subsystem: what failed")` (or `std::invalid_argument` for bad input) |
| Input validation the caller can handle | return `bool`; the caller decides |
| File I/O | verify size and magic/version fields |
| Swapchain out-of-date / suboptimal | handle explicitly in the present path |

Never call `std::exit()` in a utility (it skips unwinding and leaks Vulkan resources). Never silently swallow an
exception: re-throw or add context.

Comment only when the **why** is non-obvious (a hidden constraint, an invariant, a spec workaround). Do not write
"keep in sync with X" comments: make the two places one (KEEPING_IT_CLEAN, golden rule 1). Track open work in
`tasks/todo.md`, not `TODO:` comments. State the coordinate system in a class header when it is non-obvious.

</details>

---

## PR checklist

The full list is in [docs/KEEPING_IT_CLEAN.md](docs/KEEPING_IT_CLEAN.md#15-before-you-commit). Minimum:

- [ ] `make test` and `make test-debug` green; `make check` OK
- [ ] rooted includes (no `../`), no new unit/pi literals, no file over 800 (C++) / 400 (GLSL) lines
- [ ] all Vulkan calls through `VK_CHECK` / `vkx`; handles start as `VK_NULL_HANDLE`; `cleanup()` mirrors `init()`
- [ ] shader change: release `.spv` hashes match (pure refactor) or were deliberately re-recorded
- [ ] debug-only code inside `#ifdef SWISH_DEBUG_UI`; release output unchanged or the change is stated
- [ ] no golden regenerated to hide a diff; new pure logic has a test
- [ ] READMEs, `docs/REPO_MAP.md`, `docs/RECIPES.md` updated if paths changed
- [ ] changelog entry added and `python3 scripts/changelog_index.py` run
