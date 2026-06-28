# Architecture

Swish is organized as a 5-layer stack. Each layer depends only on the layers below it.

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 1 — Platform          App · Window · GLFW                │
├─────────────────────────────────────────────────────────────────┤
│  Layer 2 — Vulkan Core       VulkanContext · Device · Swapchain │
│                              CommandManager · SyncObjects        │
├─────────────────────────────────────────────────────────────────┤
│  Layer 3 — Render Systems    PostProcessManager · ScenePipeline │
│                              DeferredLightingPipeline           │
│                              CameraUniforms · MaterialDescriptors│
│                              TextureManager · ResourceManager    │
├─────────────────────────────────────────────────────────────────┤
│  Layer 4 — Scene             SceneManager · RoadScene · Camera  │
│                              Entity · CarEntity · ModelManager   │
├─────────────────────────────────────────────────────────────────┤
│  Layer 5 — Shaders           GLSL 450 → glslc → SPIR-V          │
└─────────────────────────────────────────────────────────────────┘
```

See [`docs/diagrams/system-overview.excalidraw`](diagrams/system-overview.excalidraw) for the component ownership diagram.

---

<details>
<summary><strong>Layer 1 — Platform</strong></summary>

### App (`src/core/App/`)

The top-level orchestrator. `App::run()` is the main loop:

```
delta_time = min(now − last_frame, 1/15 s)
```

$$dt_\text{eff} = \min\!\left(t_\text{now} - t_\text{last},\; \tfrac{1}{15}\ \text{s}\right)$$

The cap prevents physics blow-up after focus loss or debugger pauses — without it, a 5-second stall would integrate 5 seconds of car acceleration in one step.

**Per-frame loop:**

```
pollEvents()
handle input (ESC edge-detect, C cockpit toggle, WASD free-fly, arrow keys car)
car.update(delta_time)
if cockpit mode: override camera position + yaw from car model matrix
renderer.drawFrame()
```

**Input edge detection:** the Escape key uses `m_esc_key_prev` to detect the rising edge of the key press. Without this, cursor-lock would toggle every frame (~60×/s) while the key is held.

**Ownership:** App owns `Window`, `Renderer`, `TextureManager`, `SceneManager`, `ModelManager`. The last three are registered with Renderer via `register_*()` so render-time code can access them through the registry without holding raw handles.

### Window (`src/core/Window/`)

A thin GLFW wrapper. Created with `GLFW_CLIENT_API = GLFW_NO_API` — no OpenGL context.

| Method | Behavior |
|--------|---------|
| `pollEvents()` | `glfwPollEvents()` — non-blocking, returns immediately |
| `waitEvents()` | `glfwWaitEvents()` — blocks CPU until any event arrives |
| `mark_resized()` | Called from GLFW resize callback; sets `m_resized = true` |
| `wasResized()` | Returns and **clears** `m_resized` — consume once per frame |

`waitEvents()` is used in the minimize spin-loop so the CPU blocks at ~0% instead of spinning at 100% checking a 0×0 framebuffer.

</details>

---

<details>
<summary><strong>Layer 2 — Vulkan Core</strong></summary>

These five subsystems form the Vulkan infrastructure. They must be initialized in order and cleaned up in reverse.

### VulkanContext (`src/renderer/VulkanContext/`)

Creates the `VkInstance`, debug messenger (validation layers in debug builds), and `VkSurfaceKHR` via `glfwCreateWindowSurface`.

On `SWISH_BACKEND_APPLE`, two additional instance extensions are required for MoltenVK:
```cpp
VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME   // list portability devices
VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR // flag
```

Validation layer enablement uses `constexpr bool kEnableValidationLayers` so the compiler eliminates the debug path entirely in release builds.

### Device (`src/renderer/Pipeline/Device/`)

Selects the best `VkPhysicalDevice` using a score:

| Criterion | Score |
|-----------|-------|
| Discrete GPU | +1000 |
| Integrated GPU | +100 |
| Matching vendor (AMD/NVIDIA on Linux, Apple on macOS) | +500 |
| Required extensions missing | Disqualified |
| Queue families incomplete | Disqualified |

Vendor IDs: `0x1002` = AMD, `0x10DE` = NVIDIA, `0x106B` = Apple. The `SWISH_BACKEND_LINUX` guard enables AMD+NVIDIA preference; `SWISH_BACKEND_APPLE` enables Apple GPU preference.

After selecting a physical device, logical device creation uses `std::set<uint32_t>` to deduplicate queue family indices — graphics and present may share one family, in which case only one `VkDeviceQueueCreateInfo` is created (as required by the spec).

Call `getQueueFamilies()` to retrieve the cached `QueueFamilyIndices` — do not call `findQueueFamilies()` again after init.

### Swapchain (`src/renderer/Swapchain/`)

Negotiates:
- **Image count:** `min(minImageCount + 1, maxImageCount)`
- **Surface format:** prefers `VK_FORMAT_B8G8R8A8_SRGB` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
- **Present mode:** prefers `VK_PRESENT_MODE_MAILBOX_KHR` (low-latency triple-buffer), falls back to `FIFO`

Must be fully recreated on resize — see [Swapchain Recreation](#swapchain-recreation).

### CommandManager (`src/renderer/CommandManager/`)

Allocates one `VkCommandPool` on the graphics queue family. Allocates `MAX_FRAMES_IN_FLIGHT` (2) primary command buffers — one per frame-in-flight slot. Each frame calls `vkResetCommandBuffer` then re-records.

### SyncObjects (`src/renderer/SyncObjects/`)

Owns two semaphore pools with **different sizes**:

| Name | Size | Signaled by | Waited by |
|------|------|-------------|-----------|
| `imageAvailable[frame]` | `MAX_FRAMES_IN_FLIGHT` | `vkAcquireNextImageKHR` | Queue submit |
| `renderFinished[imageIndex]` | swapchain image count | Queue submit | `vkQueuePresentKHR` |

**Why different sizes?** VUID-00067 prohibits re-signaling a semaphore that is still pending in a present operation. Indexing `renderFinished` by swapchain image index (not frame slot) ensures each semaphore maps to one in-flight image, so it cannot be re-signaled while still being waited on by the previous present of that image.

See [`docs/diagrams/vulkan-sync.excalidraw`](diagrams/vulkan-sync.excalidraw) and [`docs/investigations/2026-04-21-vulkan-validation-errors.md`](investigations/2026-04-21-vulkan-validation-errors.md).

</details>

---

<details>
<summary><strong>Layer 3 — Rendering Subsystems</strong></summary>

All subsystems are `std::unique_ptr` members of `Renderer`. They receive raw Vulkan handles via the short-lived `RendererServices` bundle — never via long-lived stored references.

### PostProcessManager

The largest subsystem. Owns all offscreen render targets:

| Image | Count | Size | Format |
|-------|-------|------|--------|
| G-buffer: albedo | per frame-in-flight | Full | `R8G8B8A8_SRGB` |
| G-buffer: normal | per frame-in-flight | Full | `R16G16B16A16_SFLOAT` |
| G-buffer: material | per frame-in-flight | Full | `R8G8B8A8_UNORM` |
| G-buffer: depth | per frame-in-flight | Full | `D32_SFLOAT` |
| HDR color | per frame-in-flight | Full | `R16G16B16A16_SFLOAT` |
| Bloom extract | shared | 1/4 | `R16G16B16A16_SFLOAT` |
| Bloom blur H | shared | 1/4 | `R16G16B16A16_SFLOAT` |
| Bloom blur V | shared | 1/4 | `R16G16B16A16_SFLOAT` |
| AO (disabled) | shared | 1/2 | `R8_UNORM` |

Also owns all render passes, framebuffers, descriptor sets, and post-process pipelines (bloom extract, blur H, blur V, composite).

On swapchain recreate, only extent-dependent images and framebuffers are rebuilt; pipelines persist.

### ScenePipeline

G-buffer generation pipeline. `basic.vert` + `gbuffer.frag`. Enables back-face culling (`VK_CULL_MODE_BACK_BIT`). Used for both static road geometry and the dynamic car entity.

### DeferredLightingPipeline

`fullscreen.vert` + `lighting.frag`. Reads the G-buffer to compute PBR lighting. Inverse view and projection matrices are pushed as constants each frame (see [`docs/render-pipeline.md`](render-pipeline.md) for the math).

### CameraUniforms

Per-frame UBOs for descriptor set 0:
- `CameraUBO`: view matrix, proj matrix, camera world position, sun direction + color
- `LightsUBO`: `PointLightData[MAX_POINT_LIGHTS]`

Uses `m_lightsDirty` flag — lights are only re-uploaded on `set_lights()`, not every frame.

### MaterialDescriptors

Descriptor sets (set 1) binding PBR texture triplets (albedo, normal, roughness) per `MaterialId`. All writes batched into a single `vkUpdateDescriptorSets` call via `kTexturesPerMaterial` constant.

### TextureManager / ResourceManager

`TextureManager` loads images (JPG/PNG via stb_image) into device-local `VkImage`s, maintaining a name-keyed cache and a shared sampler. `ResourceManager` provides static helpers: `createBuffer`, `createImage`, `insertImageBarrier`, one-time command recording.

</details>

---

<details>
<summary><strong>Layer 4 — Scene Management</strong></summary>

### SceneManager

Maintains a registry of named scenes. Each scene is a setup lambda invoked at startup and again after swapchain recreation:

```cpp
sceneManager.register_scene("highway", [&]() {
    RoadScene road;
    road.generate(services);
    renderer.upload_scene_geometry(road.getMeshData(), road.getDrawCalls());
    renderer.set_scene_lights(road.getLights());
    renderer.set_camera(new Camera(...));
});
```

### RoadScene

Procedural highway generator. Configuration-driven (see `src/scene/README.md`). Outputs `MeshData` + `DrawCall[]` + `LightDesc[]`. A `RoadLayout` struct computed once at the top of `generate()` is threaded through all sub-generators.

### Camera

FPS-style POV camera. Angles stored as $(yaw, pitch)$ in degrees; view matrix computed from `glm::lookAt`. Supports collision bounds (`Bounds2D`) for cockpit mode. Coordinate system: +X right, +Y up, −Z forward.

### Entity / CarEntity

`Entity` is a base class (position, Euler rotation, scale). `CarEntity` adds bicycle-model physics — exponential drag, clamped steering, yaw rate $\dot\psi = v\tan(\delta)/L$, heading wrapped via `fmod`. See [`src/scene/README.md`](../src/scene/README.md) for full equations.

### ModelManager

Loads GLB files via tinygltf. Node-walk bake collapses all node transforms into a normalized local space. See [`docs/car_system.md`](car_system.md) for the full normalization algorithm.

</details>

---

<details>
<summary><strong>Layer 5 — Shaders</strong></summary>

All shaders are GLSL 4.50. CMake compiles them to SPIR-V via `glslc`, placing binaries in `build/shaders/*.spv`. Runtime loads via `FileIO::readBinaryFile(SHADER_DIR + name)`.

**Descriptor set convention:**

| Set | Content | Bound for |
|-----|---------|-----------|
| 0 | `CameraUBO` + `LightsUBO` | Every draw call |
| 1 | PBR texture triplet (albedo, normal, roughness) | Per material |

Post-process passes bind their own descriptor sets (managed by `PostProcessManager`). See [`shaders/README.md`](../shaders/README.md) for per-shader documentation and math.

</details>

---

## Startup Sequence

```
Window::init(800, 600, "swish")
VulkanContext::init(window)          ← instance, surface, debug messenger
Device::init(instance, surface)      ← pick GPU, logical device, queues
Swapchain::init(device, surface)     ← images, image views, present mode
CameraUniforms::init(services, N)    ← per-frame UBOs + descriptor sets
MaterialDescriptors::init(device)    ← empty descriptor sets + layout
CommandManager::init(device, N)      ← pool + N command buffers
SyncObjects::init(device, N, imgs)   ← semaphores + fences
PostProcessManager::init(services)   ← all offscreen images, passes, pipelines
ScenePipeline::init(device, config)  ← basic.vert + gbuffer.frag
DeferredLightingPipeline::init(...)  ← fullscreen.vert + lighting.frag (+ bloom + composite)
TextureManager::init(services)
  └─ load_directory(TEXTURE_DIR)
ModelManager::load_car(path)         ← GLB → normalized MeshData
RoadScene::generate(services)        ← procedural geometry → upload
renderer.rebuild_material_descriptors()
renderer.set_camera(new Camera(...))
```

---

## RendererServices Bundle

`RendererServices` (`src/renderer/Renderer/RendererServices.h`) is a transient struct of raw Vulkan handles:

```cpp
struct RendererServices {
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       swapchainExtent;   // invalid after every resize
};
```

**Rules:**
- Retrieve from `Renderer::services()` at the moment of use
- Copy out only the specific handles you need; never store the struct
- Never cache `swapchainExtent` — stale extents cause framebuffer dimension mismatches

---

## Swapchain Recreation

Triggered by: resize (`wasResized()` flag) or `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` from acquire or present.

**What gets recreated:**
- `Swapchain` — new extent, possibly new image count
- `SyncObjects::m_renderFinished` — resized to match new image count
- `PostProcessManager` offscreen images + framebuffers — new extent
- `DeferredLightingPipeline` viewport push constant — new extent

**What persists (no recreation needed):**
- All pipeline objects (viewport/scissor are dynamic state)
- Descriptor set layouts and pools
- Render passes (format-independent)
- Vertex/index buffers

During the minimize spin (framebuffer 0×0), the CPU blocks with `glfwWaitEvents()` — not polling — preventing 100% CPU usage while the window is minimized.
