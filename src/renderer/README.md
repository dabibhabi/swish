# src/renderer

All Vulkan rendering subsystems. Each lives in its own subdirectory and follows the two-phase init/cleanup pattern described in [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Directory Layout

| Directory | Class | Responsibility |
|-----------|-------|----------------|
| `Renderer/` | `Renderer` | Top-level facade; owns all subsystems as `unique_ptr`; drives per-frame recording |
| `VulkanContext/` | `VulkanContext` | Instance, debug messenger, surface |
| `Pipeline/Device/` | `Device` | Physical + logical device selection; GPU vendor scoring via `SWISH_BACKEND` |
| `Pipeline/Swapchain/` | `Swapchain` | Swapchain, image views, present mode negotiation, recreation |
| `CommandManager/` | `CommandManager` | Command pool; `MAX_FRAMES_IN_FLIGHT` command buffers |
| `SyncObjects/` | `SyncObjects` | `inFlight[frame]` fences; `imageAvailable[frame]` + `renderFinished[imageIndex]` semaphores |
| `PostProcessManager/` | `PostProcessManager` | All offscreen images, render passes, framebuffers, post-process pipelines |
| `ScenePipeline/` | `ScenePipeline` | Graphics pipeline for the G-buffer pass (basic.vert + gbuffer.frag) |
| `DeferredLightingPipeline/` | `DeferredLightingPipeline` | Lighting, bloom extract/blur, composite pipelines |
| `CameraUniforms/` | `CameraUniforms` | UBO (view, proj, lights) + descriptor set 0; dirty-flag upload |
| `MaterialDescriptors/` | `MaterialDescriptors` | Per-material descriptor sets (set 1); batched `vkUpdateDescriptorSets` |
| `SceneGeometry/` | `SceneGeometry` | Device-local vertex + index buffers; staging upload; draw-call recording |
| `ResourceManager/` | `ResourceManager` | `createBuffer`, `createImage`, `insertImageBarrier`, one-time commands |
| `TextureManager/` | `TextureManager` | stb_image → device-local `VkImage`; name-keyed cache; shared sampler |

---

## Subsystem Init Order

```
VulkanContext → Device → Swapchain
  → CameraUniforms → MaterialDescriptors          (descriptor set layouts)
  → CommandManager → SyncObjects
  → PostProcessManager                            (images + passes sized to swapchain)
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
9.   barrier → SHADER_READ_ONLY
10.  Bloom extract → blur H → blur V
11.  Composite pass → swapchain image
12. vkEndCommandBuffer
13. vkQueueSubmit  (wait: imageAvailable[frame], signal: renderFinished[imageIndex], fence: inFlight[frame])
14. vkQueuePresentKHR  (wait: renderFinished[imageIndex])
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
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       extent;            // invalidated on every resize
};
```

Never store `RendererServices` as a member — `extent` becomes stale after every swapchain recreation. Copy out only the handles your subsystem needs for long-term storage.

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
