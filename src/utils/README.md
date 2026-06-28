# src/utils

Header-only helpers and shared type aliases used across every layer. No subsystem state lives here — these are pure utilities.

## Files

| File | Provides |
|------|----------|
| `Types.h` | GLM type aliases, global constants |
| `VulkanCheck.h` | `VK_CHECK` macro + `vkResultString()` |
| `VulkanInit.h` | `sType`-prefilled struct factories + swapchain result predicates |
| `FileIO/FileIO.{h,cpp}` | Binary file reading (SPIR-V, configs) |
| `StbImage.cpp` | Single translation unit that compiles `stb_image` |

---

## Types.h

```cpp
using Vec2 = glm::vec2;  using Vec3 = glm::vec3;
using Vec4 = glm::vec4;  using Mat4 = glm::mat4;  using Quat = glm::quat;

static constexpr float    WORLD_SCALE          = 1000.0f;  // 1000 world units = 1 metre
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
```

`WORLD_SCALE` is the project-wide unit convention: **1000 world units (WU) per metre.** All physics constants (car speed, wheelbase) and road dimensions are expressed in WU. `RoadScene::kFt = 0.3048f * WORLD_SCALE` converts feet → WU.

---

## VulkanCheck.h

```cpp
VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer));
```

Evaluates the expression once; if the result is not `VK_SUCCESS`, throws `std::runtime_error` with file, line, and the symbolic result name:

```
/path/SceneGeometry.cpp:84 Vulkan error: VK_ERROR_OUT_OF_DEVICE_MEMORY
```

`vkResultString(VkResult)` maps the common result codes to their names; unknown codes return `"VK_UNKNOWN_ERROR"`.

<details>
<summary><strong>When not to use VK_CHECK</strong></summary>

`VK_CHECK` treats anything ≠ `VK_SUCCESS` as fatal. Swapchain codes (`VK_SUBOPTIMAL_KHR`, `VK_ERROR_OUT_OF_DATE_KHR`) are **expected** and must be routed through the predicates in `VulkanInit.h` instead — see [CONTRIBUTING.md § Vulkan Resource Conventions](../../CONTRIBUTING.md).

</details>

---

## VulkanInit.h

Two groups of helpers in `namespace swish::vk`:

**Struct factories** — return a zero-initialized struct with `sType` already set, eliminating a common source of validation errors:

```cpp
auto si = swish::vk::makeSubmitInfo();        // VK_STRUCTURE_TYPE_SUBMIT_INFO
auto pi = swish::vk::makePresentInfo();       // VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
// also: makeRenderPassBeginInfo, makeImageViewCreateInfo, makeSamplerCreateInfo
```

**Swapchain result predicates** — classify a `VkResult` from acquire/present:

| Predicate | True when |
|-----------|-----------|
| `is_success(r)` | `r == VK_SUCCESS` |
| `is_suboptimal(r)` | `r == VK_SUBOPTIMAL_KHR` |
| `is_out_of_date(r)` | `r == VK_ERROR_OUT_OF_DATE_KHR` |
| `is_presentable(r)` | success or suboptimal |
| `swapchain_needs_recreation(r)` | out-of-date or suboptimal |

Usage in the present path:

```cpp
VkResult r = vkQueuePresentKHR(queue, &presentInfo);
if (swish::vk::swapchain_needs_recreation(r)) recreateSwapchain();
else VK_CHECK(r);
```

---

## FileIO

```cpp
[[nodiscard]] std::vector<char> FileIO::readBinaryFile(const std::string& path);
```

Reads an entire binary file (SPIR-V shaders, baked configs). Throws `std::runtime_error` if the file cannot be opened, and verifies `gcount()` matches the expected size after reading — a short read is an error, not a silent truncation.

---

## StbImage.cpp

Contains the single `#define STB_IMAGE_IMPLEMENTATION` so `stb_image.h` is compiled exactly once. Do not define the implementation macro anywhere else — it will cause duplicate-symbol link errors. `TextureManager` includes the header normally (declaration only).
