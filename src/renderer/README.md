# src/renderer

All Vulkan rendering subsystems. Each lives in its own subdirectory and follows the two-phase init/cleanup pattern described in [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Directory Layout

| Directory | Class | Responsibility |
|-----------|-------|----------------|
| `Renderer/` | `Renderer` | Top-level facade; owns all subsystems as `unique_ptr`; drives per-frame recording |
| `VulkanContext/` | `VulkanContext` | Instance, debug messenger, surface |
| `Pipeline/Device/` | `Device` | Physical + logical device selection; GPU vendor scoring via `SWISH_BACKEND` |
| `Pipeline/` | `Pipeline` | Graphics-pipeline builder + `ScopedShaderModule` RAII helper |
| `Swapchain/` | `Swapchain` | Swapchain, image views, present mode negotiation, recreation |
| `RenderPass/` | `RenderPass` | `VkRenderPass` construction helpers |
| `DepthBuffer/` | `DepthBuffer` | Depth image + view allocation |
| `CommandManager/` | `CommandManager` | Command pool; `MAX_FRAMES_IN_FLIGHT` command buffers |
| `SyncObjects/` | `SyncObjects` | `inFlight[frame]` fences; `imageAvailable[frame]` + `renderFinished[imageIndex]` semaphores |
| `PostProcessManager/` | `PostProcessManager` | All offscreen images, render passes, framebuffers, post-process pipelines |
| `ScenePipeline/` | `ScenePipeline` | Graphics pipeline for the G-buffer pass (basic.vert + gbuffer.frag) |
| `DeferredLightingPipeline/` | `DeferredLightingPipeline` | Lighting, bloom extract/blur, composite pipelines |
| `CameraUniforms/` | `CameraUniforms` | UBO (view, proj, lights) + descriptor set 0; dirty-flag upload |
| `MaterialDescriptors/` | `MaterialDescriptors` | Per-material descriptor sets (set 1); batched `vkUpdateDescriptorSets` |
| `SceneGeometry/` | `SceneGeometry` | Device-local vertex + index buffers; staging upload; draw-call recording; exposes `get_vertex_buffer()` / `get_index_buffer()` for passes that share the car buffer |
| `ResourceManager/` | `ResourceManager` | `createBuffer`, `createImage`, `insertImageBarrier`, one-time commands |
| `TextureManager/` | `TextureManager` | stb_image → device-local `VkImage`; name-keyed cache; shared sampler |
| `RainSystem/` | `RainSystem` | GPU rain streaks (instanced billboard quads, additive blend); owns its own render pass (LOAD_OP_LOAD on HDR), per-frame UBOs, and wetness accumulation |
| `GlassPass/` | `GlassPass` | Forward transparent pass for `alphaMode=BLEND` car glass; alpha blending, depth-test read-only; Fresnel tint + sun specular; reuses the car shared VBO/IBO |
| `WindshieldRainPass/` | `WindshieldRainPass` | Refractive rain drops on the front windshield: snapshots the HDR scene (`record_scene_snapshot`) and treats drops as lenses distorting it (alpha blend, single-sided cull); layered Voronoi drops in glass space, forward-normal confinement, analytic wiper; per-frame `WindshieldRainUBO` + refraction-source image |

---

## Subsystem Init Order

```
VulkanContext → Device → Swapchain
  → CameraUniforms → MaterialDescriptors          (descriptor set layouts)
  → CommandManager → SyncObjects
  → PostProcessManager                            (images + passes sized to swapchain)
  → RainSystem                                    (HDR image views from PostProcessManager)
  → GlassPass                                     (HDR + depth views; must follow RainSystem)
  → WindshieldRainPass                            (HDR + depth views; must follow GlassPass)
  → ScenePipeline → DeferredLightingPipeline      (pipeline objects)
  → TextureManager → SceneGeometry                (on scene load)
```

Cleanup runs in **reverse order**. `vkDeviceWaitIdle` must be called before any cleanup begins.

<details>
<summary><strong>Why this order?</strong></summary>

- `CameraUniforms` and `MaterialDescriptors` create descriptor set **layouts** that `ScenePipeline` and `DeferredLightingPipeline` reference — layouts must exist before pipeline creation.
- `PostProcessManager` creates render passes and framebuffers at swapchain extent — must come after `Swapchain`.
- `ScenePipeline` needs the G-buffer render pass from `PostProcessManager` — must come after it.
- `CommandManager` and `SyncObjects` have no dependencies beyond `Device`, but are placed late because `PostProcessManager` may issue one-time commands during init (e.g. priming the AO texture).

</details>

---

## Per-Frame Recording

`Renderer::drawFrame()` runs the following sequence each frame:

```
1. waitForFence(frame)               — stall until GPU done with this slot
2. vkAcquireNextImageKHR             — get swapchain image index
3. resetFence(frame)
4. vkResetCommandBuffer(frame)
5. vkBeginCommandBuffer
6.   G-buffer pass (ScenePipeline::record_draws)
7.   barrier → SHADER_READ_ONLY
8.   Lighting pass
9.   Rain pass (additive streaks over HDR, LOAD_OP_LOAD — no barrier between lighting and rain)
10.  Glass pass (LOAD_OP_LOAD — forward transparent car glass, alpha blend, depth read-only)
10a. Scene snapshot (copy HDR → refraction-source image; transfer barriers) — feeds the windshield refraction
11.  Windshield rain pass (LOAD_OP_LOAD — refractive water drops on the front windshield, alpha blend)
12.  barrier → SHADER_READ_ONLY
13.  Bloom extract → blur H → blur V
14.  Composite pass → swapchain image
15. vkEndCommandBuffer
16. vkQueueSubmit  (wait: imageAvailable[frame], signal: renderFinished[imageIndex], fence: inFlight[frame])
17. vkQueuePresentKHR  (wait: renderFinished[imageIndex])
```

<details>
<summary><strong>Semaphore indexing — VUID-00067</strong></summary>

`renderFinished` is indexed by swapchain **image** index, not frame slot. With 3 swapchain images and 2 frames-in-flight, there are 3 `renderFinished` semaphores and 2 `inFlight` fences.

The spec (VUID-00067) forbids re-signaling a semaphore while it is still pending in a `vkQueuePresentKHR` operation. If `renderFinished` were indexed by frame slot (size 2), then with 3 images it would be possible for frame slot 0 to submit and signal semaphore[0], then before the present completes, frame slot 0 is reused and tries to signal semaphore[0] again — undefined behaviour.

Indexing by image index (size ≥ image count) guarantees each semaphore maps to at most one in-flight present at a time.

</details>

---

## RendererServices Bundle

Short-lived struct of Vulkan handles passed to subsystem `init()` and `upload()` calls:

```cpp
struct RendererServices {
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          graphicsQueue   = VK_NULL_HANDLE;
    VkExtent2D       swapchainExtent = {0, 0};   // invalidated on every resize
};
```

Never store `RendererServices` as a member — `swapchainExtent` becomes stale after every swapchain recreation. Copy out only the handles your subsystem needs for long-term storage. Full field reference: [docs/data-types.md](../../docs/data-types.md).

---

## Adding a New Subsystem

See [CONTRIBUTING.md § Adding a new subsystem](../../CONTRIBUTING.md).

## Adding a New Render Pass

See [CONTRIBUTING.md § Adding a new shader pass](../../CONTRIBUTING.md) and [docs/render-pipeline.md](../../docs/render-pipeline.md).

---

## Diagrams

- [docs/diagrams/system-overview.excalidraw](../../docs/diagrams/system-overview.excalidraw) — component ownership
- [docs/diagrams/render-pipeline.excalidraw](../../docs/diagrams/render-pipeline.excalidraw) — 6-pass pipeline
- [docs/diagrams/vulkan-sync.excalidraw](../../docs/diagrams/vulkan-sync.excalidraw) — frame synchronization (fence + semaphore timing)
- [docs/diagrams/scene-data-flow.excalidraw](../../docs/diagrams/scene-data-flow.excalidraw) — asset → GPU → draw calls
- [docs/diagrams/descriptor-sets.excalidraw](../../docs/diagrams/descriptor-sets.excalidraw) — set 0 / set 1 bindings + push constants
- [docs/diagrams/data-layout.excalidraw](../../docs/diagrams/data-layout.excalidraw) — Vertex byte layout + std140 UBO layout

See also [docs/data-types.md](../../docs/data-types.md) for every struct's exact layout.
