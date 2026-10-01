---
verified: 2026-09-29
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD; SceneParamsUBO (11 rows) and push-block sizes added
owner: docs-scribe
covers: [src/scene/SceneTypes.h, src/renderer/Vertex.h, src/debug/SceneParamsUniform.h, src/renderer/PostProcessManager/PostProcessManager.h]
---

# Data Types Reference

Swish moves data across layer boundaries with plain data structures — what enterprise codebases call **DTOs** (Data Transfer Objects). We don't use that term in the code (the C++ idiom is *aggregate* / *POD* / *value type*), but the role is real: these types carry data and have no behavior. This page catalogs every one, with exact field layouts.

There are **two families**, and the distinction matters:

| Family | Examples | Rule |
|--------|----------|------|
| **CPU value types** | `DrawCall`, `Submesh`, `LightDesc`, `RendererServices`, `RoadLayout` | Plain `struct`, public fields, no `m_` prefix, aggregate-initializable, no methods |
| **GPU-layout types** | `Vertex`, `CameraUBO`, `LightsUBO`, `PointLightData`, `PushConstantData` | Field-for-field identical to a GLSL block; subject to alignment rules below |

> **Note:** `MeshData` is **not** a DTO — it is a class with a controlled-mutation invariant (vertices and indices only grow through `addVertex`/`addIndex` so callers always get correct base indices). Keep it that way; do not flatten it into a bare struct.

---

## Why GPU-Layout Types Use `Vec4` Everywhere

The single most important convention here: **GPU-layout structs use only `Vec4` and `Mat4`, never `Vec3` or bare scalars between vectors.** This is a direct consequence of the std140 layout rules the GPU uses for uniform buffers.

<details>
<summary><strong>std140 alignment rules (the footgun)</strong></summary>

A uniform block laid out with std140 aligns each member to the following base alignments:

| Type | Base alignment |
|------|---------------|
| `float`, `int`, `uint` | 4 bytes |
| `vec2` | 8 bytes |
| `vec3`, `vec4` | **16 bytes** |
| `mat4` | 16 bytes (4 × `vec4`) |
| struct / array element | round up to **16 bytes** |

The trap: a `vec3` consumes 12 bytes but is aligned to 16, so the next scalar does **not** pack into the trailing 4 bytes — it jumps to the next 16-byte boundary. A C++ struct using `glm::vec3` followed by `float` will therefore **not** match the GLSL block, and the GPU reads garbage.

```
// BROKEN — C++ vec3 + float packs to 16 bytes,
// but std140 puts 'intensity' at offset 16, not 12.
struct Light { glm::vec3 position; float intensity; };

// CORRECT — pack into one vec4 lane.
struct Light { glm::vec4 positionIntensity; };  // xyz = pos, w = intensity
```

By using only `Vec4`/`Mat4` (all 16-byte aligned, all 16-byte sized multiples) the C++ layout is **trivially identical** to std140 with zero padding. Every packed-scalar comment (`// w = radius`) exists to document which lane holds what.

</details>

---

## GPU-Layout Types

<details>
<summary><strong>Vertex</strong> — <code>src/renderer/Vertex.h</code></summary>

The vertex buffer element. Packed tightly (C++ natural layout, described to Vulkan via `VkVertexInputAttributeDescription` using `offsetof`).

| Field | Type | Offset | Size | Shader location |
|-------|------|--------|------|-----------------|
| `position` | `vec3` | 0 | 12 | `location = 0` (`R32G32B32_SFLOAT`) |
| `normal` | `vec3` | 12 | 12 | `location = 1` (`R32G32B32_SFLOAT`) |
| `uv` | `vec2` | 24 | 8 | `location = 2` (`R32G32_SFLOAT`) |
| `tangent` | `vec4` | 32 | 16 | `location = 3` (`R32G32B32A32_SFLOAT`) |

**Stride: 48 bytes.** `tangent.w` is the bitangent sign (±1), used to reconstruct the TBN basis in `basic.vert`:

$$\mathbf{B} = \mathbf{t}_w \cdot (\mathbf{N} \times \mathbf{t}_{xyz})$$

Vertex buffers are not UBOs, so std140 does not apply — only the `offsetof`-derived attribute offsets matter.

</details>

<details>
<summary><strong>CameraUBO</strong> — <code>src/scene/SceneTypes.h</code> (set 0, binding 0)</summary>

```cpp
struct CameraUBO {
    Mat4 view;      // offset   0
    Mat4 proj;      // offset  64
    Vec4 camPos;    // offset 128   xyz = camera world position, w = unused
    Vec4 sunDir;    // offset 144   xyz = normalized sun direction, w = intensity
    Vec4 sunColor;  // offset 160   rgb = sun color, a = ambient strength
};                  // total  176 bytes — no padding (all members 16-aligned)
```

Updated per frame by `CameraUniforms::update()`. Mirrored in `lighting.frag` and `basic.vert` as a `uniform CameraUBO` block — keep field order identical.

</details>

<details>
<summary><strong>PointLightData / LightsUBO</strong> — <code>src/scene/SceneTypes.h</code> (set 0, binding 1)</summary>

```cpp
struct PointLightData {
    Vec4 positionRadius;  // xyz = world position, w = influence radius
    Vec4 colorIntensity;  // rgb = color,          a = intensity multiplier
};                        // 32 bytes

struct LightsUBO {
    PointLightData pointLights[MAX_POINT_LIGHTS];  // MAX_POINT_LIGHTS = 16 → 512 bytes
    glm::uvec4     numPointLights;                 // x = count, yzw = 0 → 16 bytes
};                                                 // total 528 bytes
```

The two-`vec4` packing of `PointLightData` is the std140 workaround in action: position+radius and color+intensity each fit one 16-byte lane, so the array packs with no inter-element padding. `numPointLights` is a `uvec4` (not a bare `uint`) for the same reason — a trailing scalar would be 16-aligned anyway, so we use the full lane and document it.

</details>

<details>
<summary><strong>PushConstantData</strong> — <code>src/scene/SceneTypes.h</code></summary>

```cpp
struct PushConstantData {
    Mat4 model;  // offset  0
    Vec4 color;  // offset 64
};               // 80 bytes — well under the 128-byte guaranteed push-constant minimum
```

Pushed per draw call in the G-buffer pass. Mirrored in `gbuffer.frag` / `basic.vert` as `layout(push_constant) uniform PushConstants`. The deferred-lighting pass uses a *different* push block (`invView`, `invProj`) — push constants are not shared across pipelines.

</details>

---

## CPU Value Types

<details>
<summary><strong>DrawCall</strong> — <code>src/scene/SceneTypes.h</code></summary>

One draw call = one material applied to a contiguous index range.

```cpp
struct DrawCall {
    uint32_t   indexOffset;  // first index into the shared index buffer
    uint32_t   indexCount;   // number of indices to draw
    Vec4       color;        // tint, copied into PushConstantData.color
    Mat4       model;        // model-to-world, copied into PushConstantData.model
    MaterialId material;     // selects the set-1 descriptor (texture triplet)
};
```

Produced by `RoadScene::generate()` and `ModelEntity::get_draw_calls()`. Consumed by `SceneGeometry::record_draws()`, which binds the material descriptor set, pushes `{model, color}`, and calls `vkCmdDrawIndexed(indexCount, 1, indexOffset, 0, 0)`.

</details>

<details>
<summary><strong>Submesh</strong> — <code>src/scene/SceneTypes.h</code></summary>

A `DrawCall` *without* a model matrix — the matrix is injected at draw-call generation time from the owning entity's current transform. This is how the car's submeshes follow the car each frame.

```cpp
struct Submesh {
    uint32_t   indexOffset;
    uint32_t   indexCount;
    MaterialId material = MAT_DEFAULT;
    Vec4       color    = Vec4(1.f);
    bool       is_steering_wheel = false;   // animated separately
    Mat4       sw_pivot_frame    = Mat4(1.f);  // steering-wheel pivot (normalized frame)
};
```

When `is_steering_wheel` is set, `CarEntity::get_draw_calls()` composes an extra rotation about the pivot so the wheel turns with steering input.

</details>

<details>
<summary><strong>LightDesc</strong> — <code>src/scene/SceneTypes.h</code></summary>

The CPU-friendly point-light description. Unpacked (separate `position`, `color`, scalars) for ergonomic authoring; converted to the packed `PointLightData` when uploaded to the `LightsUBO`.

```cpp
struct LightDesc {
    Vec3  position;
    Vec3  color;
    float intensity;
    float radius;
};
```

Produced by `RoadScene` (street lamps) → passed via `CameraUniforms::set_lights()`.

</details>

<details>
<summary><strong>RendererServices</strong> — <code>src/renderer/Renderer/RendererServices.h</code></summary>

The transient handle bundle passed to subsystems at init/upload time. **Never stored** — `swapchainExtent` is invalidated on every resize.

```cpp
struct RendererServices {
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          graphicsQueue   = VK_NULL_HANDLE;
    VkExtent2D       swapchainExtent = {0, 0};
    VmaAllocator     allocator       = nullptr;          // single VMA sub-allocator
    bool             dynamicRendering = false;           // Vulkan 1.3 features enabled on the device
    bool             synchronization2 = false;
    VkPipelineCache  pipelineCache   = VK_NULL_HANDLE;   // Device-owned; passed to pipeline::create_*
};
```

See [architecture.md § RendererServices Bundle](architecture.md#rendererservices-bundle).

</details>

<details>
<summary><strong>RoadLayout</strong> — private to <code>RoadScene</code></summary>

Per-`generate()` precomputed offsets, threaded to every section generator so none of them recompute road widths independently.

```cpp
struct RoadLayout {
    float eb_start;    // eastbound start X (barrier + clearance)
    float road_width;  // total road width (lane_count * lane_width)
    float wb_inner;    // westbound inner edge (-road_width)
};
```

</details>

<details>
<summary><strong>RoadScene::SceneData</strong> — <code>src/scene/RoadScene/RoadScene.h</code></summary>

The bundle `generate()` returns — everything needed to upload a scene.

```cpp
struct SceneData {
    MeshData               meshData;   // vertex + index buffers
    std::vector<DrawCall>  drawCalls;  // one per material range
    std::vector<LightDesc> lights;     // street lamps
};
```

</details>

<details>
<summary><strong>SceneParamsUBO</strong> — <code>src/debug/SceneParamsUniform.h</code> (set 4, binding 0; debug builds only)</summary>

Eleven `vec4` rows, `static_assert(sizeof(SceneParamsUBO) == 11 * 16)` = **176 B**. Mirrored by the `SceneParamsUBO` block in
`shaders/lighting.frag` (the only reader). Set/binding numbers come from `shaders/common/bindings.glsl`.

| Row | Lanes |
|-----|-------|
| `skyHorizonOvercast`, `skyHorizonClear`, `skyZenithOvercast`, `skyZenithClear` | rgb |
| `sunDisc` | expMin, expMax, strMin, strMax |
| `fogColor` | rgb, **w = haze max** |
| `fogParams` | dist63, max, envGlossExp, **w = haze dist63** |
| `shadowParams` | bias, floor, lightSize |
| `wetParams` | porosity, roughness collapse, puddle coverage |
| `iblParams` | diffuse, specular |
| `paintParams` | clearcoat roughness, flake roughness |

Adding a lane: use a free `.w` / `.z` first; a new row means updating the assert, the host struct, the GLSL block and
`SceneParamsUniform.cpp::update`. See [debug-ui.md](debug-ui.md#scene-params-ubo) and [RECIPES.md](RECIPES.md#add-a-tunable).

</details>

<details>
<summary><strong>Post-process push blocks</strong> — <code>src/renderer/PostProcessManager/PostProcessManager.h</code></summary>

Push-constant blocks must be **16-byte multiples** on MoltenVK (`utils/vulkan/PushConstants.h`: every push range goes
through `vkx::push_range<T>`, which static-asserts it); each of these also carries its own `static_assert`.

| Struct | Size | Layout | Shader |
|--------|------|--------|--------|
| `PostProcessParams` | 64 B | 13 shader floats (52 B) + 3 host-only pad floats; bloom, composite and rain-haze controls | `composite.frag`, `bloom_*.frag` |
| `SsaoParams` | 144 B | `mat4 invProjection`, `mat4 projection`, `float radius, bias, intensity, _pad0` | `ssao.frag` |
| `SsrParams` | 224 B | `mat4 proj, invProj, invView`, `float maxDist, thickness, stride, intensity, wetness, puddleCoverage, _pad1, _pad2` | `ssr.frag` |
| `GodRaysParams` | 32 B | `vec4 sunUV` (xy sun, z visibility), `vec4 tune` (density, decay, weight, intensity) | `godrays.frag` |

</details>

---

## Enums & Config

<details>
<summary><strong>MaterialId</strong> — <code>src/scene/SceneTypes.h</code></summary>

A `uint32_t` enum indexing into the material descriptor table. Slots 0–15 are scene materials; 16–35 are car material slots loaded from the GLB (one per glTF material).

```
MAT_ASPHALT=0  MAT_GRASS=1  MAT_CONCRETE=2  MAT_METAL=3  MAT_DEFAULT=4
MAT_RUMBLE=5   MAT_DIRT=6   MAT_TREE=7      MAT_SIGN_0..7 = 8..15
MAT_CAR_0..19 = 16..35      MAT_COUNT = 36
```

`MAT_COUNT` sizes the descriptor array — add scene materials before `MAT_CAR_0`, never in the middle.

</details>

<details>
<summary><strong>RoadConfig</strong> — <code>src/scene/RoadScene/RoadConfig.h</code></summary>

The binary-serializable road configuration. `#pragma pack(push, 1)` — **byte-packed, no padding** — so `toml_baker`'s output and the engine's `fread` agree exactly.

- `MAGIC = 0x53575243` ("SWRC"), `VERSION = 3` — both validated on load; mismatch throws.
- `static_assert(std::is_trivially_copyable_v<RoadConfig>)` guards binary I/O safety.
- `kCrownSlope` (= `road::kCrownSlope` = 0.02f, in `RoadParams.h`) is a `static constexpr` (2% cross-slope for drainage) — **not** a serialized field.
- `static_assert(sizeof(RoadConfig) == 292)` pins the size; `toml_baker` includes this same header, so the baked layout cannot drift.

See [tools/README.md](../tools/README.md) for the full field list and the baker workflow.

</details>

---

## Adding a New Data Type

1. **CPU value type?** Plain `struct` in the nearest `*Types.h` (`SceneTypes.h` for scene data). Public fields, no `m_`, no methods, give sensible defaults so aggregate init works.
2. **Mirrors a GLSL block?** Use only `Vec4`/`Mat4`. Pack scalars into `.w`/`.a` lanes and document each lane. Keep field order identical to the shader. Verify total size is a multiple of 16.
3. **Has an invariant to protect?** Make it a class with controlled access (like `MeshData`) — that's no longer a DTO, and that's fine.
4. Add it to the relevant table on this page.

See also [CONTRIBUTING.md § Data Structures](../CONTRIBUTING.md).
