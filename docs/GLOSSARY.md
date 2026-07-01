# Glossary

A fast lookup of what each term, abbreviation, class, and convention means **in the Swish codebase**.
Skim the section you need — every entry is one line.

**Jump to:** [Conventions](#project-conventions) · [Abbreviations](#abbreviations) · [Render pipeline & passes](#render-pipeline--passes) · [Coordinate spaces](#coordinate-spaces) · [Vulkan objects](#vulkan-objects-as-used-here) · [GPU memory (VMA + RAII)](#gpu-memory-vma--raii) · [Rain system](#rain-system) · [Components / classes](#components--classes) · [Key globals & files](#key-globals--files) · [Controls](#controls)

---

## Project conventions

| Term | What it means in Swish |
|------|------------------------|
| `namespace swish` | Everything lives here. No global singletons. |
| One class per folder | `ComponentName/ComponentName.{h,cpp}` — folder name == class name. |
| Two-phase init/cleanup | Objects aren't fully constructed in the ctor; you call `init(...)` then `cleanup(device)` explicitly (Vulkan lifetime is manual). |
| Register through `Renderer` | New passes/managers are owned by `Renderer` and handed Vulkan handles via `RendererServices` — never a parallel global. |
| `RendererServices` | Small bundle of raw Vulkan handles (`device`, `physicalDevice`, `commandPool`, `graphicsQueue`, `swapchainExtent`) passed to subsystems at init. |
| Vec4-only UBOs | Uniform buffer structs use only `Vec4` rows so std140 packing is unambiguous (e.g. `WindshieldRainUBO`). |
| `VK_CHECK(...)` | Macro asserting a `VkResult` is `VK_SUCCESS`; the standard error-handling pattern. |
| DTO | "Data Transfer Object" — a plain struct (e.g. `DrawCall`); its fields are bare `camelCase` (no `m_`). |
| `m_` prefix | Member variable (`m_position`). `k` prefix = `constexpr` constant (`kMaxSpeed`). |
| `camelCase` vs `snake_case` | Vulkan-core layer uses `camelCase` getters (`getDevice()`); scene/app layer uses `snake_case` (`get_speed()`). |
| Shader registration | A new shader must be added to `SHADER_SOURCES` in `CMakeLists.txt`; the `shaders` target compiles `.vert/.frag` → `.spv` with `glslc`. |

---

## Abbreviations

| Abbr. | Expansion / meaning here |
|-------|--------------------------|
| **HDR** | High Dynamic Range — the `R16G16B16A16_SFLOAT` scene-colour target the scene is lit into before tone-mapping. |
| **G-buffer** | Geometry buffer — the deferred-rendering attachments (albedo, normal, material, depth) written first. |
| **UBO** | Uniform Buffer Object — CPU→GPU constants (camera, rain params, …). |
| **PBR** | Physically-Based Rendering — the material model (metallic/roughness). |
| **SSAO / AO** | Screen-Space Ambient Occlusion / Ambient Occlusion — contact-shadow darkening. |
| **ACES** | Filmic tone-mapping curve applied in the composite pass. |
| **BLEND** | A glTF `alphaMode` marking transparent glass meshes (windows). |
| **SDF** | Signed Distance Field — distance to a shape's surface (negative inside); used for drops and the wiper blade. |
| **LOD** | Level Of Detail — here, a mipmap level (`textureLod`). |
| **NDC** | Normalized Device Coordinates — clip space ÷ w, range `[-1,1]`. |
| **WU** | World Units — Swish's world scale (`WORLD_SCALE = 1000`); e.g. car top speed ≈ 30000 WU/s ≈ 108 km/h. |
| **SPIR-V / `.spv`** | Compiled shader bytecode Vulkan consumes (output of `glslc`). |
| **glslc** | The GLSL→SPIR-V compiler (from the Vulkan SDK). |
| **FoV** | Field of View. |
| **VMA** | [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) — AMD's allocator library. Swish sub-allocates every GPU buffer/image from a few big device blocks through one `VmaAllocator`, instead of one `vkAllocateMemory` per resource. See [GPU memory](#gpu-memory-vma--raii). |
| **RAII** | *Resource Acquisition Is Initialization* — the C++ idiom where an object releases its resource in its destructor. Here `GpuBuffer`/`GpuImage` free their VMA sub-allocation automatically, so there are no manual `vkDestroy*`/`vkFreeMemory` pairs. |

---

## Render pipeline & passes

The per-frame order (see [`Renderer::recordCommandBuffer`](../src/renderer/Renderer/Renderer.cpp) and [`render-pipeline.md`](render-pipeline.md)):

| Stage | What it does |
|-------|--------------|
| **G-buffer pass** | Rasterizes scene geometry into albedo/normal/material/depth. |
| **Lighting pass** | Deferred lighting reads the G-buffer → writes the **HDR** target. |
| **Rain pass** | `RainSystem` draws falling streaks additively onto HDR (`LOAD_OP_LOAD`). |
| **Glass pass** | `GlassPass` draws transparent BLEND windows (alpha + Fresnel) onto HDR. |
| **Wetness update** | Fullscreen pass steps the windshield wetness map (rain/advect/wipe). |
| **HDR snapshot** | Copies HDR → a sampleable image so the windshield rain can refract it. |
| **Windshield rain pass** | Refractive drops on the windshield, gated by the wetness map. |
| **Bloom** | Extract bright pixels → blur → (added in composite). |
| **Composite** | HDR + bloom + AO → ACES tone-map → swapchain image. |
| **Render pass** (Vulkan) | A set of attachments + subpass(es); each pass above is one. |
| **Framebuffer** | The concrete image views bound to a render pass for a frame. |
| **Barrier** | A `vkCmdPipelineBarrier` that transitions an image's layout / orders GPU access between passes. |
| **`LOAD_OP_LOAD`** | Render-pass setting that preserves existing attachment contents (forward passes draw *over* HDR). |
| **Swapchain** | The set of images presented to the window. |

---

## Coordinate spaces

| Space | Range / origin | Used for |
|-------|----------------|----------|
| **World space** | WU, world origin | physics, geometry, lights. |
| **View space** | camera at origin | lighting, the falling-rain streak orientation. |
| **Clip / NDC** | `[-1,1]` after ÷w | rasterization. |
| **Screen UV** | `gl_FragCoord.xy / size`, `[0,1]` | refraction lookup, wetness map, wiper (cockpit view is fixed). |
| **Glass / mesh UV** (`fragUV`) | mesh `inUV`, `[0,1]`-ish | the windshield drop field (drops "stick" to the pane). |
| **Object / local space** (`fragLocalNormal`) | car-local, nose = `+X` | the front-windshield normal mask (stable under car yaw). |

---

## Vulkan objects (as used here)

| Object | Role in Swish |
|--------|---------------|
| `VkImage` / `VkImageView` | A GPU texture and a typed "window" into it (HDR, G-buffer, refraction source, wetness map). |
| **Image layout** | The image's current usage state (`COLOR_ATTACHMENT_OPTIMAL`, `SHADER_READ_ONLY_OPTIMAL`, `TRANSFER_SRC/DST_OPTIMAL`); changed by barriers. |
| Descriptor set / layout / pool | A bound group of shader resources / its schema / the allocator. **Set 0 = camera+lights, set 1 = material textures** (or a pass's own UBO+samplers). |
| Push constants | Tiny inline shader constants (e.g. per-draw `model` + `color`, or the wetness-pass params). |
| Pipeline / pipeline layout | The compiled GPU state (shaders + blend + cull + depth) / its descriptor & push-constant signature. Built via `Pipeline::create` + `PipelineConfig`. |
| Command buffer | Recorded GPU commands for a frame (`CommandManager`). |
| Fence / semaphore | CPU↔GPU / GPU↔GPU sync (`SyncObjects`). |
| **Frames in flight** | `MAX_FRAMES_IN_FLIGHT = 2` — how many frames are recorded/executing concurrently; most per-frame resources are arrays of this size. |
| Sampler | How a shader filters a texture (linear, clamp-to-edge here). |
| Ping-pong | Two images alternated read/write because a shader can't read+write the same image (used by the wetness map). |
| `VmaAllocator` | The one VMA sub-allocator for the whole renderer, owned by `Device` (`getAllocator()`), created at `VK_API_VERSION_1_3`. Handed to subsystems via `RendererServices::allocator`; must outlive every `GpuBuffer`/`GpuImage`. |
| `GpuBuffer` / `GpuImage` | Move-only RAII wrappers ([`GpuResource.h`](../src/renderer/GpuResource/GpuResource.h)) around a VMA allocation. `.handle()` → the `VkBuffer`/`VkImage`; `.mapped()` → the persistent CPU pointer (host-visible buffers); destructor / `.reset()` frees the sub-allocation. Replace the old raw `VkImage`+`VkDeviceMemory` (or `VkBuffer`+`VkDeviceMemory`) pairs. |

---

## GPU memory (VMA + RAII)

How Swish allocates and frees GPU memory. Full write-up (diagrams + math): [`docs/vma-memory.md`](vma-memory.md).

| Term | Meaning in Swish |
|------|------------------|
| **Sub-allocation** | Carving many resources out of a few large device memory blocks. VMA does this so the live-allocation count stays far below the driver's `maxMemoryAllocationCount` cap. |
| **`maxMemoryAllocationCount`** | The Vulkan limit on **simultaneous** `vkAllocateMemory` calls — as low as **4096** on some drivers. One-allocation-per-resource scales with scene size and can hit it; VMA sub-allocation collapses it to a handful. |
| **Device-local** | Memory that lives in VRAM, fastest for the GPU. Used for attachments/textures/vertex+index buffers (via `gpu::deviceLocalImage` / `deviceLocalBuffer`). Written from the CPU only through a **staging** copy. |
| **Host-visible** | Memory the CPU can map and write directly. Used for per-frame UBOs (`gpu::hostVisibleBuffer`). |
| **Persistently mapped** | A host-visible buffer whose CPU pointer (`.mapped()`) is obtained once at creation and reused every frame — no `vkMapMemory`/`vkUnmapMemory` per write. |
| **Staging buffer** | A temporary host-visible buffer the CPU writes, then a `vkCmdCopyBuffer/Image` uploads it into a device-local resource. |
| **`gpu::` factory helpers** | `deviceLocalBuffer`, `hostVisibleBuffer`, `deviceLocalImage` — one-liners that fill the VMA create-info for the three common patterns and return a `GpuBuffer`/`GpuImage`. |
| **Teardown order** | Every subsystem's `GpuBuffer`/`GpuImage` must `reset()` (or be destroyed) **before** `Device` calls `vmaDestroyAllocator`, which itself runs **before** `vkDestroyDevice`. |

---

## Rain system

See [`docs/rain/README.md`](rain/README.md) for the full architecture.

| Term | Meaning |
|------|---------|
| **Falling rain** | `RainSystem` — 8192 GPU-instanced billboard streaks, additive over HDR; accumulates a scalar **wetness**. |
| **Windshield rain** | `WindshieldRainPass` — refractive water drops on the front windshield + the wiper. |
| **Refraction (drops as lenses)** | Sampling the scene *behind* a drop, offset by the drop's normal — instead of adding glowing colour. |
| **Scene snapshot / `refrSrc`** | A per-frame copy of the HDR scene the windshield shader samples (avoids a read/write feedback loop on HDR). |
| **Drop height field** `h(uv)` | Procedural layered Voronoi field whose value is "drop thickness" at a point. |
| **Voronoi / cell noise** | Nearest-feature-point partition; one jittered drop per cell. |
| **Finite-difference normal** | Drop surface normal from the gradient of `h`: `n = normalize(grad h, 1)`. |
| **Stick-slip** | Drop motion model: cling, then suddenly slide (a sawtooth), like real beads. |
| **Fresnel rim** | Edge brightening `(1 − n_z)^p` that gives drops a glassy rim. |
| **Front-normal mask** | Discards drops whose object normal isn't forward-facing → confines rain to the windshield, not side/rear glass. |
| **Wetness map** | Persistent screen-space R16F field: rain adds, it advects along the flow, the wiper clears it, it evaporates; gates where drops appear. |
| **Advection (semi-Lagrangian)** | Moving the wetness by sampling *upstream* (`uv − flow·dt`) so water streams in the flow direction. |
| **Wiper** | Analytic rotating blade SDF (screen space) that subtracts wetness along its sweep; toggled by `V`. |
| **Flow / aero crossover** | Flow direction blends gravity (down) → aerodynamic (up) by speed: `smoothstep(0.05, 0.20, speedFactor)`. |
| **`speedFactor`** | `carSpeed / 30000`, clamped `[0,1]`; drag limits it to ≈0.24 in practice. |

---

## Components / classes

One class per folder under `src/`. (`camelCase` getters = Vulkan-core layer; `snake_case` = scene/app layer.)

| Class | Folder | Role |
|-------|--------|------|
| `App` | `core/App` | Top-level app: owns the window, main loop, and keyboard input. |
| `Window` | `core/Window` | GLFW window + surface creation, resize flag. |
| `VulkanContext` | `renderer/VulkanContext` | `VkInstance`, validation/debug messenger, surface. |
| `Device` | `renderer/Pipeline/Device` | Physical + logical device selection, queues. |
| `Swapchain` | `renderer/Swapchain` | Swapchain images/views + clean recreate-on-resize. |
| `CommandManager` | `renderer/CommandManager` | Command pool + per-frame command buffers. |
| `SyncObjects` | `renderer/SyncObjects` | Per-frame fences + image-available/render-finished semaphores. |
| `Renderer` | `renderer/Renderer` | Orchestrates the per-frame pipeline; central manager registry. |
| `PostProcessManager` | `renderer/PostProcessManager` | Owns offscreen resources: G-buffer, HDR, bloom, SSAO, composite. |
| `CameraUniforms` | `renderer/CameraUniforms` | **Set 0** — camera + lights UBOs. |
| `MaterialDescriptors` | `renderer/MaterialDescriptors` | **Set 1** — PBR material textures. |
| `ScenePipeline` | `renderer/ScenePipeline` | The deferred G-buffer pipeline. |
| `DeferredLightingPipeline` | `renderer/DeferredLightingPipeline` | The lighting pipeline + its layout. |
| `SceneGeometry` | `renderer/SceneGeometry` | Vertex/index buffers + draw-call recording. |
| `ResourceManager` | `renderer/ResourceManager` | Static helpers: create buffers/images, copies, layout barriers, depth-format pick. |
| `Pipeline` | `renderer/Pipeline` | `PipelineConfig` struct + the pipeline/layout factory. |
| `RainSystem` | `renderer/RainSystem` | Falling-rain streaks + wetness accumulation. |
| `GlassPass` | `renderer/GlassPass` | Forward transparent pass for BLEND car glass. |
| `WindshieldRainPass` | `renderer/WindshieldRainPass` | Refractive windshield rain + wetness map + wiper. |
| `TextureManager` | `renderer/TextureManager` | Owns all GPU textures (`Texture` = one image + view + sampler). |
| `DepthBuffer` | `renderer/DepthBuffer` | Depth image + view helper. |
| `RenderPass` | `renderer/RenderPass` | Render-pass wrapper helper. |
| `Camera` | `scene/Camera` | POV camera: WASD free-fly + mouse look + cockpit mode. |
| `ModelManager` | `scene/ModelManager` | Loads GLB/glTF cars; mesh normalization + glass tagging. |
| `SceneManager` | `scene/SceneManager` | `Scene` = a lambda that populates the world; switching. |
| `RoadScene` | `scene/RoadScene` | The road/world scene (`MeshBuilder` builds its geometry). |
| `Entity` / `CarEntity` | `scene/Entity` | Base scene object with a transform; `CarEntity` = drivable car physics. |

---

## Key globals & files

| Name | Where | Meaning |
|------|-------|---------|
| `Vec2/3/4`, `Mat4` | [`src/utils/Types.h`](../src/utils/Types.h) | `glm` aliases — use these, not raw `glm::`. |
| `MAX_FRAMES_IN_FLIGHT` | `src/utils/Types.h` | `2` — frames recorded concurrently. |
| `WORLD_SCALE` | config / `Types` | `1000.0f` — scene-unit ↔ world-unit factor. |
| `SceneTypes.h` | `src/scene/` | Shared DTOs: `Vertex`, `DrawCall`, `Submesh`, `PushConstantData`, `MeshData`. |
| `SHADER_DIR` / `ASSET_DIR` / `TEXTURE_DIR` / `CONFIG_DIR` | CMake-defined macros | Absolute paths so the binary runs from anywhere. |
| `config/*.toml` | repo root `config/` | Scene params baked to binary at build (`toml_baker`). |

---

## Controls

| Key | Action |
|-----|--------|
| ↑ / ↓ | Drive forward / brake-reverse |
| ← / → | Steer |
| `R` | Cycle rain intensity (off → light → heavy) |
| `V` | Toggle the windshield wiper |
| `C` | Toggle cockpit ↔ free-fly camera |
| `W` `A` `S` `D` `Q` `E` + Shift | Free-fly camera move (when not in cockpit) |
| `Esc` | Toggle mouse-look capture |

---

*Add a term here when you introduce a new abbreviation, pass, or convention — it's the first place a new reader (or future you) will look.*
