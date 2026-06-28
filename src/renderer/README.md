# src/renderer

All Vulkan rendering subsystems. Each lives in its own subdirectory and follows the two-phase init/cleanup pattern described in [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Directory Layout

| Directory | Class | Responsibility |
|-----------|-------|----------------|
| `Renderer/` | `Renderer` | Top-level facade; owns all subsystems as `unique_ptr`; drives per-frame recording |
| `VulkanContext/` | `VulkanContext` | Instance, debug messenger, surface |
| `Pipeline/Device/` | `Device` | Physical + logical device selection; GPU vendor scoring; queue families |
| `Pipeline/Swapchain/` | `Swapchain` | Swapchain, image views, recreation on resize |
| `CommandManager/` | `CommandManager` | Command pool; one command buffer per frame-in-flight |
| `SyncObjects/` | `SyncObjects` | Fences (`inFlight[frame]`), semaphores (`imageAvailable[frame]`, `renderFinished[imageIndex]`) |
| `PostProcessManager/` | `PostProcessManager` | G-buffer + HDR + bloom + SSAO images; render passes and framebuffers |
| `ScenePipeline/` | `ScenePipeline` | Graphics pipeline for the G-buffer pass |
| `DeferredLightingPipeline/` | `DeferredLightingPipeline` | Full-screen lighting, bloom extract/blur, composite pipelines |
| `CameraUniforms/` | `CameraUniforms` | UBO (view, proj, lights) + descriptor set 0 |
| `MaterialDescriptors/` | `MaterialDescriptors` | Per-material descriptor sets (set 1); PBR texture bindings |
| `SceneGeometry/` | `SceneGeometry` | Device-local vertex + index buffers; staging upload; draw-call recording |
| `ResourceManager/` | `ResourceManager` | Buffer + image allocation helpers |
| `TextureManager/` | `TextureManager` | Image loading, staging upload, sampler; registered by App |

## Subsystem Init Order

```
VulkanContext → Device → Swapchain → CameraUniforms → MaterialDescriptors
→ CommandManager → SyncObjects → PostProcessManager → ScenePipeline
→ DeferredLightingPipeline → (SceneGeometry / TextureManager on scene load)
```

Cleanup runs in **reverse order**. Call `cleanup(VkDevice)` on each subsystem before destroying the device.

## RendererServices Bundle

Short-lived struct passed into subsystem `init()` and `upload()` calls:

```cpp
struct RendererServices {
    VkDevice         device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool    commandPool;
    VkQueue          graphicsQueue;
    VkExtent2D       extent;
};
```

Do not store it across frames — `extent` is invalidated on resize.

## Adding a New Subsystem

See the checklist in [CONTRIBUTING.md § Adding a new subsystem](../../CONTRIBUTING.md).

## Adding a New Render Pass

See [CONTRIBUTING.md § Adding a new shader pass](../../CONTRIBUTING.md) and [docs/render-pipeline.md](../../docs/render-pipeline.md).

## Diagrams

- [docs/diagrams/system-overview.excalidraw](../../docs/diagrams/system-overview.excalidraw) — component ownership
- [docs/diagrams/render-pipeline.excalidraw](../../docs/diagrams/render-pipeline.excalidraw) — 6-pass pipeline
- [docs/diagrams/vulkan-sync.excalidraw](../../docs/diagrams/vulkan-sync.excalidraw) — frame synchronization
