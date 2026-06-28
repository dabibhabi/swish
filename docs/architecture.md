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

## Layer 1 — Platform

### App (`src/core/App/`)

The top-level orchestrator. `App::run()` is the main loop:

```
delta_time = min(now - last_frame, 1/15 s)   ← cap prevents physics explosion on frame spikes
pollEvents()
handle input (ESC cursor toggle, C cockpit toggle, WASD camera, arrow keys car)
car.update(delta_time)
update cockpit camera from car transform (if cockpit mode)
renderer.drawFrame()
```

App owns `Window`, `Renderer`, `TextureManager`, `SceneManager`, and `ModelManager`. It registers the last three with Renderer so render-time code can access them through the registry without holding raw handles.

### Window (`src/core/Window/`)

A thin GLFW wrapper. Responsibilities:
- Create a Vulkan surface–compatible window (`GLFW_NO_API` — no OpenGL context)
- Set a framebuffer resize callback that sets `m_resized = true`
- Expose `pollEvents()` / `waitEvents()` / `getFramebufferSize()`
- `waitEvents()` is used in the minimize spin-loop (`recreateSwapchain`) so the CPU blocks instead of spinning at 100%

---

## Layer 2 — Vulkan Core

These five subsystems form the Vulkan infrastructure. They must be initialized in this order and cleaned up in reverse:

### VulkanContext (`src/renderer/VulkanContext/`)

Creates the `VkInstance`, debug messenger (debug builds only), and `VkSurfaceKHR` via `glfwCreateWindowSurface`. On `SWISH_BACKEND_APPLE` it adds `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` and the `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` flag for MoltenVK compatibility.

### Device (`src/renderer/Pipeline/Device/`)

Selects the best `VkPhysicalDevice` using a scoring function: discrete GPU +1000, integrated +100, matching vendor +500 (AMD/NVIDIA on Linux; Apple GPU on macOS). Creates the `VkDevice` with one queue per unique family (graphics == present → single entry; separate families → two entries, as required by the spec).

After init, call `getQueueFamilies()` to retrieve the cached `QueueFamilyIndices` — do not call `findQueueFamilies()` again.

### Swapchain (`src/renderer/Swapchain/`)

Negotiates image count, surface format, and present mode (prefers `VK_PRESENT_MODE_MAILBOX_KHR`). Creates image views. Must be fully recreated on resize — see [Swapchain Recreation](#swapchain-recreation).

### CommandManager (`src/renderer/CommandManager/`)

Allocates one `VkCommandPool` on the graphics queue family, and `MAX_FRAMES_IN_FLIGHT` command buffers (one per frame-in-flight slot). Each frame resets and re-records its buffer.

### SyncObjects (`src/renderer/SyncObjects/`)

Owns two semaphore pools:
- `m_imageAvailable[frame]` — one per frame-in-flight; signaled by `vkAcquireNextImageKHR`, waited by queue submit
- `m_renderFinished[imageIndex]` — **one per swapchain image** (not per frame); signaled by queue submit, waited by present

The two pools have different sizes. Indexing `renderFinished` by swapchain image index (not frame index) is required by Vulkan spec VUID-00067, which prohibits re-signaling a semaphore that is still in use by a pending present operation. See [`docs/investigations/2026-04-21-vulkan-validation-errors.md`](investigations/2026-04-21-vulkan-validation-errors.md).

See [`docs/diagrams/vulkan-sync.excalidraw`](diagrams/vulkan-sync.excalidraw) for the timing diagram.

---

## Layer 3 — Rendering Subsystems

All subsystems are `std::unique_ptr` members of `Renderer`. They receive raw Vulkan handles via the `RendererServices` bundle — never via long-lived stored references.

### PostProcessManager (`src/renderer/PostProcessManager/`)

The largest subsystem. Owns all offscreen render targets:
- G-buffer images (per frame-in-flight): albedo, normal, material, depth
- HDR color image (per frame-in-flight): RGBA16F
- Bloom images (shared, 1/4 resolution): extract, blur-H, blur-V
- AO images (shared, 1/2 resolution): currently disabled
- All render passes, framebuffers, descriptor sets, and post-process pipelines (bloom extract, bloom blur, composite)

On swapchain recreate, only images/framebuffers sized to the swapchain extent are rebuilt; pipelines persist.

### ScenePipeline (`src/renderer/ScenePipeline/`)

The G-buffer generation pipeline. Loads `basic.vert` + `gbuffer.frag`, binds descriptor set layouts (set 0: camera, set 1: material textures), enables back-face culling (`VK_CULL_MODE_BACK_BIT`). Used for both static scene geometry and dynamic geometry (car).

### DeferredLightingPipeline (`src/renderer/DeferredLightingPipeline/`)

The screen-space lighting pipeline. Loads `fullscreen.vert` + `lighting.frag`. Reads the G-buffer to compute PBR lighting (sun + up to 16 point lights), outputting an HDR RGBA16F image. Inverse view and projection matrices are pushed as constants each frame.

### CameraUniforms (`src/renderer/CameraUniforms/`)

Per-frame uniform buffer objects (set 0). Contains:
- `CameraUBO`: view matrix, projection matrix, camera world position, sun direction + color
- `LightsUBO`: array of `PointLightData` (position, color, radius) up to `MAX_POINT_LIGHTS`

Lights are only re-uploaded when `m_lightsDirty` is set (via `set_lights()`), avoiding redundant memcpy every frame.

### MaterialDescriptors (`src/renderer/MaterialDescriptors/`)

Descriptor sets (set 1) binding PBR texture triplets (albedo, normal, roughness) for each `MaterialId` slot. Rebuilt via `rebuild()` whenever the TextureManager loads new textures. All writes are batched into a single `vkUpdateDescriptorSets` call.

### TextureManager (`src/renderer/TextureManager/`)

Loads textures from disk (JPG/PNG via stb_image) or raw pixels. Maintains a name-keyed cache. Provides a shared sampler. Used by MaterialDescriptors to fill texture slots.

### ResourceManager (`src/renderer/ResourceManager/`)

Static utilities: `createBuffer`, `createImage`, `allocateMemory`, `insertImageBarrier`, `beginOneTimeCommands` / `endOneTimeCommands`. Used by SceneGeometry and TextureManager for staging buffer uploads.

---

## Layer 4 — Scene Management

### SceneManager (`src/scene/SceneManager/`)

Maintains a list of named scenes. Each scene is a setup lambda:
```cpp
sceneManager.register_scene("highway", [&]() {
    RoadScene road;
    road.generate();
    renderer.upload_scene_geometry(road.getMeshData(), road.getDrawCalls());
    renderer.set_scene_lights(road.getLights());
    renderer.set_camera(new Camera(...));
});
sceneManager.set_active_scene("highway");
```
Switching scenes calls `vkDeviceWaitIdle`, tears down the current scene's geometry, then runs the new lambda.

### RoadScene (`src/scene/RoadScene/`)

Procedural I-495 highway generator. Reads `RoadConfig` (loaded from `build/config/road.bin`, baked from `config/road.toml` by `toml_baker`). Generates road surfaces, Jersey barriers, guardrails, lane markings, grass shoulders, billboard trees, street lamps, and overpass structure. Outputs `MeshData` + `DrawCall[]` + `LightDesc[]`.

A `RoadLayout` struct is computed once in `generate()` and passed to all sub-generators so no sub-generator recomputes road widths or barrier offsets independently.

### Camera (`src/scene/Camera/`)

FPS-style POV camera. Angles stored as `(yaw, pitch)` degrees; view matrix computed from `glm::lookAt`. Supports WASD movement with optional collision bounds (used to confine the camera to the eastbound lanes). In cockpit mode, `App` overrides position and yaw each frame from `CarEntity::get_model_matrix()`.

Coordinate system: +X right, +Y up, −Z forward. Yaw 0 = looking along −Z; Yaw −90 = looking along +X (default spawn, nose down +X).

### Entity / CarEntity (`src/scene/Entity/`)

`Entity` is a base class (position, Euler rotation, scale, `get_model_matrix()`). `CarEntity` adds bicycle-model physics:
- Forward speed integrated with exponential drag: `speed *= exp(-kDragCoeff * dt)`
- Steering angle rate-limited and clamped before `tan()` to prevent instability
- Yaw rate: `ω = (speed / wheelbase) · tan(steer_angle)`
- Heading wrapped via `fmod` to stay in (−180°, 180°]

Per frame, CarEntity regenerates its draw-call list from the current model matrix and animates the steering wheel submesh rotation.

### ModelManager (`src/scene/ModelManager/`)

Loads GLB files via tinygltf. Walks the node tree, baking `inverse(W_RootNode) · W_node` into vertex positions, normals, and tangents so the final mesh is in a normalized local space: nose along +Z, Y-up, wheels at Y=0. Skips `alphaMode == BLEND` primitives (glass). Extracts embedded textures into TextureManager.

See [`docs/car_system.md`](car_system.md) for the full normalization algorithm.

---

## Layer 5 — Shaders

All shaders are GLSL 450. CMake compiles them to SPIR-V via `glslc` and places the binaries in `build/shaders/*.spv`. Shaders are loaded at runtime by path constant `SHADER_DIR`.

**Descriptor set convention:**
- Set 0: `CameraUBO` (view, proj, camPos, sun) + `LightsUBO` (point lights) — bound for every draw call
- Set 1: PBR texture triplet (albedo, normal, roughness) — bound per material

See [`docs/render-pipeline.md`](render-pipeline.md) for per-shader pass details and [`shaders/README.md`](../shaders/README.md) for the per-file summary.

---

## Startup Sequence

```
Window::init(800, 600, "swish")
VulkanContext::init(glfw_window)         ← instance, surface, debug messenger
Device::init(instance, surface)          ← pick GPU, create logical device
Swapchain::init(device, surface, w, h)   ← image views, present mode
CameraUniforms::init(device, phys, N)    ← per-frame UBOs + descriptor sets
MaterialDescriptors::init(device)        ← empty sets + layout
CommandManager::init(device, family, N)  ← pool + N command buffers
SyncObjects::init(device, N, imgCount)   ← semaphores + fences
PostProcessManager::init(...)            ← all offscreen images/passes/pipelines
ScenePipeline::init(device, config)      ← basic.vert + gbuffer.frag
DeferredLightingPipeline::init(...)      ← fullscreen.vert + lighting.frag
TextureManager::init(services)
  └─ load_directory(TEXTURE_DIR)
ModelManager::load_car(path)
RoadScene::generate()
  └─ renderer.upload_scene_geometry(...)
renderer.rebuild_material_descriptors()
renderer.set_camera(new Camera(...))
```

---

## RendererServices Bundle

`RendererServices` (`src/renderer/Renderer/RendererServices.h`) is a transient struct of raw Vulkan handles passed to subsystems at init or upload time:

```cpp
struct RendererServices {
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       swapchainExtent;  // invalid after resize
};
```

**Rules:**
- Retrieve it from `Renderer::services()` at the moment you need it
- Copy out only the handles you need; never store the struct itself
- Never cache the swapchain extent — it changes on every resize

---

## Swapchain Recreation

Triggered by: window resize (`wasResized()` flag set by GLFW callback) or `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` from acquire/present.

**What gets recreated:**
- `Swapchain` (new extent, possibly new image count)
- `SyncObjects::m_renderFinished` semaphores (size matches image count)
- `PostProcessManager` offscreen images + framebuffers (new extent)
- `DeferredLightingPipeline` viewport extent

**What persists:**
- All pipelines (viewport and scissor are dynamic state)
- Descriptor set layouts
- Render passes (format-independent)

During the minimize spin (framebuffer size = 0×0), the CPU blocks with `glfwWaitEvents()` instead of polling, preventing a busy-wait at 100% CPU.
