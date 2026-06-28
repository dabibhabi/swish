# Contributing to Swish

This guide covers the conventions, patterns, and checklists you need to add code to Swish without breaking what's already there. Read it before opening a PR.

---

<details>
<summary><strong>Naming Conventions</strong></summary>

| Entity | Style | Examples |
|--------|-------|---------|
| Classes, structs, enums | `PascalCase` | `SceneGeometry`, `DrawCall`, `MaterialId` |
| Public methods (scene/app layer) | `snake_case` | `get_forward()`, `record_draws()`, `upload()` |
| Public getters (Vulkan subsystems) | `camelCase` | `getDevice()`, `getExtent()`, `getSwapchain()` |
| Member variables | `m_` + `snake_case` | `m_position`, `m_vertexBuffer`, `m_currentFrame` |
| Static/constexpr constants | `k` + `PascalCase` | `kFt`, `kCrownSlope`, `kSpeedDeadZone` |
| CMake-defined macros | `UPPER_SNAKE` | `MAX_FRAMES_IN_FLIGHT`, `MAX_POINT_LIGHTS` |
| Enum values | `UPPER_SNAKE` | `MAT_ASPHALT`, `MAT_GRASS`, `MAT_DEFAULT` |
| Data-struct fields (DTOs) | bare `camelCase`, no `m_` | `indexOffset`, `positionRadius`, `sunDir` |
| Files | Match class name exactly | `Camera.h` / `Camera.cpp` |

One class per directory. The directory name matches the class name:

```
src/renderer/SceneGeometry/
  SceneGeometry.h
  SceneGeometry.cpp
```

The `camelCase` vs `snake_case` split exists for historical reasons: the Vulkan Core layer (`Device`, `Swapchain`, etc.) followed Vulkan SDK convention; the Scene layer followed STL convention. Do not mix them within a new class.

</details>

---

<details>
<summary><strong>Header Structure</strong></summary>

Enforce this include order inside every header and `.cpp`:

```cpp
#pragma once

// 1. Vulkan / GLFW — only when the full type is needed in the header
#include <vulkan/vulkan.h>

// 2. Standard library
#include <vector>
#include <memory>
#include <string>

// 3. Third-party
#include <glm/glm.hpp>

// 4. Relative project includes (closest first)
#include "../../utils/Types.h"
#include "../SomeSubsystem/SomeSubsystem.h"

namespace swish {

// Forward-declare types used only as pointers or references.
// Never include a header just to get a T* or T& member.
class Renderer;
class TextureManager;

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

Inline methods are acceptable **only** for trivial one-liners that return a stored value. Everything that modifies state, calls Vulkan, or exceeds one line belongs in the `.cpp`.

</details>

---

<details>
<summary><strong>Vulkan Resource Conventions</strong></summary>

### Handle initialization

Always initialize raw Vulkan handles to `VK_NULL_HANDLE`, never `nullptr`. `VK_NULL_HANDLE` is `0` on all platforms but communicates intent clearly:

```cpp
VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
VkDescriptorPool m_pool             = VK_NULL_HANDLE;
```

### Error checking

Wrap every fallible Vulkan call in `VK_CHECK`. It throws `std::runtime_error` with the symbolic error name, file, and line:

```cpp
VK_CHECK(vkCreateBuffer(device, &info, nullptr, &m_buffer));
VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &m_memory));
```

**Exception — swapchain result codes are not errors.** Handle them explicitly in the present path:

```cpp
VkResult r = vkQueuePresentKHR(queue, &presentInfo);
if (swish::vk::swapchain_needs_recreation(r)) {
    recreateSwapchain();
} else {
    VK_CHECK(r);   // re-throw anything else
}
```

See `src/utils/VulkanCheck.h` for `VK_CHECK` and `src/utils/VulkanInit.h` for `swish::vk::*` predicates.

### Cleanup guard

Check `!= VK_NULL_HANDLE` before every `vkDestroy*` / `vkFree*` call. Reset to `VK_NULL_HANDLE` after to make double-cleanup safe:

```cpp
void SceneGeometry::cleanup(VkDevice device) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBuffer       = VK_NULL_HANDLE;
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}
```

### Cleanup order

A subsystem destroys exactly what it created, in reverse order. `Renderer::cleanup()` drives the teardown sequence:

```cpp
void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());
    // reverse of init() order:
    m_deferredLighting->cleanup(m_device->getDevice());
    m_scenePipeline->cleanup(m_device->getDevice());
    m_postProcess->cleanup();
    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    m_materialDescriptors->cleanup(m_device->getDevice());
    m_cameraUniforms->cleanup(m_device->getDevice());
    m_swapchain->cleanup(m_device->getDevice());
    m_device->cleanup();
    m_context->cleanup();
}
```

</details>

---

<details>
<summary><strong>Ownership Model</strong></summary>

| Situation | Pattern |
|-----------|---------|
| Renderer owns a subsystem it creates | `std::unique_ptr<SubsystemName> m_name;` |
| App owns a manager; Renderer uses it | Raw pointer `TextureManager* m_textureManager = nullptr;` registered via `register_*()` |
| Subsystem needs Vulkan handles at init/upload | Pass `RendererServices` bundle; never store it long-term |

### unique_ptr for subsystems

All 7 core Renderer subsystems are `unique_ptr` members. This means:
- Destructor is `= default` (no manual delete)
- Construction is `std::make_unique<T>()` in the initializer list
- `cleanup()` is still called explicitly before destruction to ensure correct Vulkan teardown order

```cpp
// Renderer.h
std::unique_ptr<VulkanContext>  m_context;
std::unique_ptr<Device>         m_device;
std::unique_ptr<SyncObjects>    m_syncObjects;

// Renderer.cpp
Renderer::Renderer()
    : m_context(std::make_unique<VulkanContext>())
    , m_device(std::make_unique<Device>())
    , m_syncObjects(std::make_unique<SyncObjects>())
{}

Renderer::~Renderer() = default;
```

### RendererServices bundle

`RendererServices` (`src/renderer/Renderer/RendererServices.h`) is a transient struct of Vulkan handles:

```cpp
struct RendererServices {
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       swapchainExtent;   // invalid after every resize
};
```

Rules:
- Retrieve via `Renderer::services()` at the moment you need it
- Copy out only the specific handles you need; do not store the struct
- Never cache `swapchainExtent` — it changes on every resize and causes framebuffer dimension mismatches

</details>

---

<details>
<summary><strong>Data Structures (DTOs)</strong></summary>

Swish passes data across layer boundaries with plain data structures — the role enterprise codebases call a **DTO** (Data Transfer Object). We do **not** use that term in the code: the C++ idiom is *aggregate* / *POD* / *value type*. But the discipline is real and worth following.

**Two families, two rule sets:**

| Family | Examples | Rules |
|--------|----------|-------|
| CPU value types | `DrawCall`, `Submesh`, `LightDesc`, `RendererServices`, `RoadLayout` | Plain `struct`; public fields in bare `camelCase` (no `m_`); give defaults so aggregate init works; **no methods** |
| GPU-layout types | `Vertex`, `CameraUBO`, `LightsUBO`, `PointLightData`, `PushConstantData` | Field-for-field identical to a GLSL block; **use only `Vec4`/`Mat4`** (never `Vec3` or trailing scalars) |

**The `Vec4`-only rule for UBOs is not stylistic — it is required.** In std140 a `vec3` aligns to 16 bytes but a following scalar will not pack into its trailing 4 bytes, so a C++ `glm::vec3` + `float` does *not* match the GLSL block and the GPU reads garbage. Packing scalars into `vec4` lanes (`positionRadius.w = radius`) keeps the C++ layout byte-identical to std140 with zero padding. Document each packed lane with a comment.

```cpp
// CPU value type — plain aggregate, no behavior
struct DrawCall {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    Vec4       color;
    Mat4       model;
    MaterialId material;
};

// GPU-layout type — vec4-only, mirrors the GLSL uniform block exactly
struct PointLightData {
    Vec4 positionRadius;  // xyz = position, w = radius
    Vec4 colorIntensity;  // rgb = color,    a = intensity
};
```

**When *not* to make a DTO:** if the type must protect an invariant, make it a class with controlled access. `MeshData` is the canonical example — vertices/indices only grow through `addVertex`/`addIndex` so callers always get correct base indices. Do not flatten such a type into a bare struct.

The full catalog (every struct, exact byte offsets, std140 rules) lives in [docs/data-types.md](docs/data-types.md).

</details>

---

<details>
<summary><strong>Two-Phase Init / Cleanup Pattern</strong></summary>

Every Vulkan subsystem follows the same lifecycle. This keeps constructors trivial and makes resource lifetimes explicit:

```
Constructor      lightweight — just make_unique calls or default-initializes members
init(...)        allocates all Vulkan resources; takes RendererServices or specific handles
cleanup(device)  destroys all owned Vk objects in reverse creation order
Destructor       = default (unique_ptr handles memory after cleanup freed Vk resources)
```

### Example

```cpp
// SceneGeometry.h
class SceneGeometry {
public:
    SceneGeometry()  = default;
    ~SceneGeometry() = default;

    void upload(const RendererServices& s, const MeshData& mesh,
                const std::vector<DrawCall>& draws);
    void record_draws(VkCommandBuffer cmd) const;
    void cleanup(VkDevice device);

private:
    VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory  = VK_NULL_HANDLE;
    std::vector<DrawCall> m_draws;
};
```

```cpp
// SceneGeometry.cpp
void SceneGeometry::cleanup(VkDevice device) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBuffer       = VK_NULL_HANDLE;
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        vkFreeMemory(device, m_indexBufferMemory, nullptr);
        m_indexBuffer       = VK_NULL_HANDLE;
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
    m_draws.clear();
}
```

</details>

---

<details>
<summary><strong>Adding a New Rendering Subsystem</strong></summary>

1. **Create files** in `src/renderer/<Name>/<Name>.h` and `<Name>.cpp`
2. **Add `.cpp`** to `add_executable(swish ...)` in root `CMakeLists.txt`
3. **Forward-declare** `class Name;` in `Renderer.h`
4. **Add member** `std::unique_ptr<Name> m_name;` to `Renderer`'s private section
5. **Construct** in the `Renderer::Renderer()` initializer list: `m_name(std::make_unique<Name>())`
6. **Initialize** in `Renderer::init()` — strictly after the subsystems it depends on
7. **Clean up** in `Renderer::cleanup()` — strictly before the subsystems it depends on (reverse init order)
8. **Update** `src/renderer/README.md` with the new entry in the directory table

Common dependency rules:
- All subsystems need `Device` → init Device first
- `PostProcessManager` needs `Swapchain` extent → init after Swapchain
- `ScenePipeline` needs descriptor set layouts from `CameraUniforms` + `MaterialDescriptors` → init those first

</details>

---

<details>
<summary><strong>Adding a New Shader Pass</strong></summary>

1. **Add source** to `shaders/` (e.g. `shaders/mypass.frag`)
   - The path must appear in `SHADER_SOURCES` in root `CMakeLists.txt` so `glslc` compiles it
   - Fullscreen passes reuse `shaders/fullscreen.vert` — no vertex input, no vertex buffer
2. **Load at runtime** in your subsystem's `init()`:
   ```cpp
   auto fragCode = FileIO::readBinaryFile(std::string(SHADER_DIR) + "mypass.frag.spv");
   ```
3. **Wrap shader modules** in `ScopedShaderModule` (see `Pipeline.cpp`) to prevent leaks if pipeline creation fails partway through
4. **Draw** fullscreen quads with `vkCmdDraw(cmd, 3, 1, 0, 0)` — the `fullscreen.vert` shader generates the triangle from `gl_VertexIndex`
5. **Image barriers** before reading any attachment written in a previous pass:
   ```cpp
   ResourceManager::insertImageBarrier(cmd, image,
       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
   ```
6. **Update** `shaders/README.md` with the new entry in the file reference table

</details>

---

<details>
<summary><strong>Testing Guide</strong></summary>

**Framework:** Catch2 v3 (fetched automatically by CMake).

**Rule:** tests never link Vulkan SDK or GLFW. They cover pure math and I/O — geometry generation, camera math, file parsing, physics equations.

### Build & run

```bash
cmake --build build --target swish_tests
cd build && ctest --output-on-failure

# filter by tag
./build/swish_tests [camera]
./build/swish_tests [road]

# with sanitizers
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build --target swish_tests && ./build/swish_tests
```

### Writing a new test

1. Create `tests/test_<feature>.cpp`
2. Add the filename to `add_executable(swish_tests ...)` in `tests/CMakeLists.txt`
3. Follow this pattern:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

static constexpr float kEps = 1e-4f;

// Build a valid test object — never construct inline in TEST_CASE
static RoadConfig make_test_config() {
    RoadConfig cfg{};
    cfg.magic   = RoadConfig::MAGIC;
    cfg.version = RoadConfig::VERSION;
    cfg.lane_width  = 3.7f;
    cfg.lane_count  = 3;
    return cfg;
}

TEST_CASE("Road generates non-zero vertex count for valid config", "[road]") {
    auto cfg = make_test_config();
    auto mesh = RoadScene::generate(cfg);
    REQUIRE(mesh.vertices.size() > 0);
    REQUIRE(mesh.vertices.size() <= RoadScene::max_vertex_count(cfg));   // upper-bound check
}

TEST_CASE("Road rejects zero lane count", "[road]") {
    auto cfg = make_test_config();
    cfg.lane_count = 0;
    REQUIRE_THROWS_AS(RoadScene::generate(cfg), std::invalid_argument);
}

TEST_CASE("Camera forward vector matches expected delta", "[camera]") {
    Camera cam;
    cam.set_yaw(0.0f);   // looking along -Z
    cam.set_pitch(0.0f);
    auto fwd = cam.get_forward();
    REQUIRE_THAT(fwd.z, Catch::Matchers::WithinAbs(-1.0f, kEps));
}
```

### What to test

| Type | Must test |
|------|-----------|
| Pure math functions | Nominal values + boundary conditions + degenerate inputs |
| File I/O helpers | Read round-trip, missing file (throws), empty file, corrupted magic/version |
| Geometry generators | Non-zero vertex count, upper-bound count, degenerate config throws |
| Physics step | Single-step values match analytic formula (e.g. drag after 1 s) |

</details>

---

<details>
<summary><strong>Error Handling Rules</strong></summary>

| Situation | Pattern |
|-----------|---------|
| Unrecoverable failure (bad GPU, missing file) | `throw std::runtime_error("subsystem: what failed")` |
| Input validation caller can handle | Return `bool`; caller decides whether to throw |
| File I/O | Check `gcount()` after binary read; verify magic + version fields |
| Swapchain out-of-date / suboptimal | Handle explicitly in present path; never let `VK_CHECK` swallow it |

**Never call `std::exit()`** from inside a utility function — it prevents stack unwinding and leaks all Vulkan resources. Let exceptions propagate to `main()`.

**Never silently swallow exceptions.** If a catch block cannot meaningfully recover, re-throw or convert to `std::runtime_error` with added context.

</details>

---

<details>
<summary><strong>Comment Style</strong></summary>

Write a comment only when the **why** is non-obvious: a hidden constraint, a subtle invariant, or a workaround for a specific spec requirement. Do not comment what the code already says.

```cpp
// ── Camera uniforms ────────────────────────────────────────────
// Section banner for major logical blocks within a .cpp file.

// VUID-00067: renderFinished is indexed by swapchain image, not frame.
// Re-signaling a semaphore pending on a present op is undefined behaviour.
m_renderFinished[imageIndex];   // correct
m_renderFinished[m_currentFrame];   // wrong — do not do this
```

**Coordinate system:** document it in the class header block when a class uses a non-obvious convention:

```cpp
// Coordinate system:
//   +X = right, +Y = up, -Z = forward (into screen)
//   Yaw 0 = looking along -Z, Yaw -90 = looking along +X
```

Track open work in `tasks/todo.md`, not in `TODO:` comments — comments rot, the task list is visible.

</details>

---

## PR Checklist

Before opening a pull request:

- [ ] New `.cpp` added to `add_executable` in `CMakeLists.txt`
- [ ] Follows naming conventions (class PascalCase, method snake_case/camelCase, member `m_`, constant `k`)
- [ ] All Vulkan calls wrapped in `VK_CHECK` or explicitly handled (swapchain codes)
- [ ] All handles initialized to `VK_NULL_HANDLE`; cleanup guards check `!= VK_NULL_HANDLE`
- [ ] `cleanup()` releases everything `init()` allocated, in reverse order
- [ ] No `std::exit()` calls
- [ ] New shader added to `SHADER_SOURCES` in `CMakeLists.txt` and `shaders/README.md`
- [ ] New subsystem: `src/renderer/README.md` directory table updated
- [ ] Tests pass: `cmake --build build --target swish_tests && cd build && ctest --output-on-failure`
- [ ] New pure-math or I/O logic covered by at least one test in `tests/`
