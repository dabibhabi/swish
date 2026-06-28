# Contributing to Swish

This guide covers the conventions, patterns, and checklists you need to add code to Swish without breaking what's already there. Read it before opening a PR.

---

## Naming Conventions

| Entity | Style | Examples |
|--------|-------|---------|
| Classes, structs, enums | `PascalCase` | `SceneGeometry`, `DrawCall`, `MaterialId` |
| Public methods (scene/app layer) | `snake_case` | `get_forward()`, `record_draws()`, `upload()` |
| Public getters (Vulkan subsystems) | `camelCase` | `getDevice()`, `getExtent()`, `getSwapchain()` |
| Member variables | `m_` + `snake_case` | `m_position`, `m_vertexBuffer`, `m_currentFrame` |
| Static/constexpr constants | `k` + `PascalCase` | `kFt`, `kCrownSlope`, `kSpeedDeadZone` |
| CMake-defined macros | `UPPER_SNAKE` | `MAX_FRAMES_IN_FLIGHT`, `MAX_POINT_LIGHTS` |
| Enum values | `UPPER_SNAKE` | `MAT_ASPHALT`, `MAT_GRASS`, `MAT_DEFAULT` |
| Files | Match class name exactly | `Camera.h` / `Camera.cpp` |

One class per directory. The directory name matches the class name:
```
src/renderer/SceneGeometry/
  SceneGeometry.h
  SceneGeometry.cpp
```

---

## Header Structure

Enforce this include order inside every header:

```cpp
#pragma once

// 1. Vulkan / GLFW (only when the full type is needed in the header)
#include <vulkan/vulkan.h>

// 2. Standard library
#include <vector>
#include <memory>

// 3. Third-party
#include <glm/glm.hpp>

// 4. Relative project includes
#include "../../utils/Types.h"

namespace swish {

// Forward-declare types used only as pointers or references.
// Never include a header just because you have a T* member.
class Renderer;
class TextureManager;

class MyClass {
    // ...
};

}  // namespace swish
```

Inline methods are acceptable for trivial one-liners:
```cpp
VkPipelineLayout get_layout() const { return m_layout; }
bool has_geometry() const { return m_indexBuffer != VK_NULL_HANDLE; }
```

Everything that modifies state or calls Vulkan belongs in the `.cpp`.

---

## Vulkan Resource Conventions

### Handle initialization
Always initialize raw Vulkan handles to `VK_NULL_HANDLE`, never `nullptr`:
```cpp
VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
```

### Error checking
Wrap every fallible Vulkan call in `VK_CHECK`. It throws `std::runtime_error` with the file, line, and symbolic error name:
```cpp
VK_CHECK(vkCreateBuffer(device, &info, nullptr, &m_buffer));
```

**Exception:** swapchain result codes are not errors — handle them explicitly:
```cpp
VkResult r = vkQueuePresentKHR(queue, &presentInfo);
if (swish::vk::swapchain_needs_recreation(r)) {
    recreateSwapchain();
} else if (!swish::vk::is_success(r)) {
    throw std::runtime_error("present failed");
}
```

See `src/utils/VulkanCheck.h` for `VK_CHECK` and `src/utils/VulkanInit.h` for the `swish::vk::*` predicates.

### Cleanup order
A subsystem destroys exactly what it created, in reverse creation order. Callers (`Renderer::cleanup()`) drive the teardown sequence:

```cpp
void Renderer::cleanup() {
    vkDeviceWaitIdle(m_device->getDevice());
    // reverse of init() order:
    m_postProcess->cleanup();
    m_materialDescriptors->cleanup(m_device->getDevice());
    m_cameraUniforms->cleanup(m_device->getDevice());
    m_syncObjects->cleanup(m_device->getDevice());
    m_commandManager->cleanup(m_device->getDevice());
    m_swapchain->cleanup(m_device->getDevice());
    m_device->cleanup();
    m_context->cleanup();
}
```

---

## Ownership Model

| Situation | Pattern |
|-----------|---------|
| Renderer owns a subsystem it creates | `std::unique_ptr<SubsystemName> m_name;` |
| App owns a manager; Renderer just uses it | Raw pointer `TextureManager* m_textureManager = nullptr;` |
| Subsystem needs Vulkan handles during init/upload | `RendererServices` bundle (see below) |

### RendererServices bundle

`RendererServices` (`src/renderer/Renderer/RendererServices.h`) is a lightweight struct of the Vulkan handles subsystems need at init / upload time:

```cpp
struct RendererServices {
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       swapchainExtent;  // invalidated on every resize
};
```

Retrieve it from `Renderer::services()`. **Never store it long-term** — the swapchain extent changes on every resize, and stale extents cause framebuffer mismatches.

---

## Two-Phase Init / Cleanup Pattern

All Vulkan subsystems follow the same lifecycle:

```cpp
// Constructor — lightweight, no Vulkan calls
SceneGeometry::SceneGeometry() = default;
SceneGeometry::~SceneGeometry() = default;

// init() — allocates all Vulkan resources
void SceneGeometry::upload(const RendererServices& s, const MeshData& mesh,
                           const std::vector<DrawCall>& draws) { ... }

// cleanup() — releases all owned Vulkan resources
void SceneGeometry::cleanup(VkDevice device) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
}
```

The pattern keeps constructors trivial (useful for value members inside other classes) and makes cleanup explicit and predictable.

---

## Adding a New Rendering Subsystem

1. **Create files** in `src/renderer/<Name>/<Name>.h` and `<Name>.cpp`
2. **Add `.cpp`** to `add_executable(swish ...)` in root `CMakeLists.txt`
3. **Forward-declare** `class Name;` in `Renderer.h`
4. **Add member** `std::unique_ptr<Name> m_name;` to `Renderer`'s private section
5. **Construct** in `Renderer::Renderer()`: `m_name(std::make_unique<Name>())`
6. **Initialize** in `Renderer::init()` — after the subsystems it depends on
7. **Clean up** in `Renderer::cleanup()` — before the subsystems it depends on
8. **Update** `src/renderer/README.md` with the new entry in the directory table

---

## Adding a New Shader Pass

1. **Add source** to `shaders/` (e.g. `shaders/mypass.frag`)
   - Add the path to `SHADER_SOURCES` in root `CMakeLists.txt` so glslc picks it up
   - Fullscreen passes reuse `shaders/fullscreen.vert` — no vertex input needed
2. **Load at runtime** in your subsystem's `init()`:
   ```cpp
   auto fragCode = FileIO::readBinaryFile(std::string(SHADER_DIR) + "mypass.frag.spv");
   ```
3. **Wrap shader modules** in `ScopedShaderModule` (from `src/renderer/Pipeline/Pipeline.cpp`) to prevent leaks if pipeline creation fails
4. **Draw** fullscreen quads with `vkCmdDraw(cmd, 3, 1, 0, 0)` — the vertex shader generates a fullscreen triangle from `gl_VertexIndex`
5. **Update** `shaders/README.md`

---

## Testing

**Framework:** Catch2 v3 (fetched automatically by CMake).

**Rule:** tests never link Vulkan SDK or GLFW. They cover pure math and I/O — geometry generation, camera matrices, file parsing.

### Running tests
```bash
cmake --build build --target swish_tests
cd build && ctest --output-on-failure
# or run directly with a tag filter:
./build/swish_tests [camera]
```

Enable sanitizers:
```bash
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build --target swish_tests
```

### Writing a new test

1. Create `tests/test_<feature>.cpp`
2. Add it to `add_executable(swish_tests ...)` in `tests/CMakeLists.txt`
3. Follow the pattern from any existing test file:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

static constexpr float kEps = 1e-4f;

// Helper that builds a valid test object
static RoadConfig make_test_config() {
    RoadConfig cfg{};
    cfg.magic   = RoadConfig::MAGIC;
    cfg.version = RoadConfig::VERSION;
    // populate fields ...
    return cfg;
}

TEST_CASE("Describe what is being tested", "[tag]") {
    // Floating-point comparisons:
    REQUIRE_THAT(value, Catch::Matchers::WithinAbs(expected, kEps));

    // Expected throws:
    REQUIRE_THROWS(FileIO::readBinaryFile("/nonexistent/file"));
}
```

---

## Error Handling

| Situation | Pattern |
|-----------|---------|
| Unrecoverable failure | `throw std::runtime_error("subsystem: message")` |
| Input validation (caller can handle) | Return `bool`; caller throws if needed |
| File I/O | Check `gcount()` after binary reads; verify magic + version |
| Swapchain out-of-date | Handle in present path; never via `VK_CHECK` |

Never call `std::exit()` from inside a utility function — it prevents stack unwinding and leaks resources.

---

## Comment Style

Write comments only when the **why** is non-obvious (hidden constraint, subtle invariant, workaround for a specific spec requirement). Never comment what the code already says.

**Section banners** for logical blocks inside a `.cpp`:
```cpp
// ── Camera uniforms ───────────────────────────────────────
```

**Coordinate system** — always document when a class uses one:
```cpp
// Coordinate system:
//   +X = right, +Y = up, -Z = forward (into screen)
//   Yaw 0 = looking along -Z, Yaw 90 = looking along -X
```

Track open work in `tasks/todo.md`, not in source comments.

---

## PR Checklist

Before opening a pull request:

- [ ] New `.cpp` added to `CMakeLists.txt`
- [ ] Follows naming conventions (class, method, member, constant)
- [ ] All Vulkan calls wrapped in `VK_CHECK` or handled explicitly
- [ ] Handles initialized to `VK_NULL_HANDLE`
- [ ] `cleanup()` releases everything `init()` allocated
- [ ] No `std::exit()` calls
- [ ] If a new shader: added to `SHADER_SOURCES` in CMakeLists.txt
- [ ] If a new subsystem: `src/renderer/README.md` updated
- [ ] Tests pass: `ctest --output-on-failure`
- [ ] New pure-math or I/O logic covered by a test in `tests/`
