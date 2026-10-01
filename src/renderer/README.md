# src/renderer

All Vulkan rendering subsystems. Each lives in its own subdirectory and follows the two-phase init/cleanup pattern described in [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Directory Layout

| Directory | Class | Responsibility |
|-----------|-------|----------------|
| `Renderer/` | `Renderer` | Top-level facade; owns the Vulkan core, the shared subsystems and the pass registry; drives the frame. Split across `Renderer.cpp` (construction/init/cleanup/swapchain recreation, `init_passes`), `Renderer_Frame.cpp` (`drawFrame`: acquire, per-frame CPU work through the registry, record, submit, present), `Renderer_Record.cpp` (`recordCommandBuffer`: begin, `PassRegistry::record_all`, end) and `Renderer_State.cpp` (setters, debug-param apply, scene-geometry delegations). `RendererServices.h` is the short-lived handle bundle |
| `Passes/` | `IRenderPass`, `PassRegistry`, `ImageRegistry`, `FullscreenPass<Push>` | The render-pass registry (see "Pass registry" below): `PassList.def` (the ordered list), `IRenderPass.h` (`FrameCtx`, `FrameUpdate`, `FrameState`, `PassResources`, `PassInitCtx`), `ImageRegistry.h` (`enum class Img`), `PassInputs.h` (declared sampler inputs, binding numbers from `bindings.glsl`), `FullscreenPass.h`, `ForwardPass.h`, and one `.cpp` per pass that has no folder of its own (`BloomPasses`, `CompositePass`, `GodRaysPass`, `SsrPass`, `SsaoPasses`, `LightingPass`, `HdrFinalizePass`, `DebugOverlayPass`) |
| `GeometryProducer/` | `GeometryProducer` | The geometry producer (R4): owns the scene geometry (intro, road chunks, interchanges, corridor segments, the car), the G-buffer and depth-only pipelines and the CSM cascade fit. Its `record()` writes the shadow atlas, then the G-buffer, then the G-buffer -> lighting barriers. First entry of the pass list, but not an ordinary pass: the Renderer keeps its scene-upload API |
| `WeatherController/` | `WeatherController` | Sun / clarity / HDRI look of the weather preset and the dirty-checked IBL rebake (R5); binds to `CameraUniforms`, `IBLManager` and (debug) `DebugParams` |
| `Bindings.h` | `swish::bind` | Descriptor set / binding constants. Includes `shaders/common/bindings.glsl` (the single source, R7) and re-exports each number as a `constexpr` |
| `VulkanContext/` | `VulkanContext` | Instance, debug messenger, surface |
| `Pipeline/Device/` | `Device` | Physical + logical device selection; owns the `VkPipelineCache` (loaded/saved from `config/pipeline_cache.bin`, exposed via `getPipelineCache()`); GPU vendor scoring via `SWISH_BACKEND`. Queries `VkPhysicalDeviceVulkan13Features` (`vkGetPhysicalDeviceFeatures2`) and enables `dynamicRendering` + `synchronization2` when the device supports them (`supportsDynamicRendering()` / `supportsSynchronization2()`, copied into `RendererServices`); logs the active path once at startup. `SWISH_CLASSIC_VULKAN=1` forces the classic path for testing. Unsupported is never an error |
| `Pipeline/` | `PipelineConfig`, `pipeline::` | Free functions `pipeline::create_graphics` / `create_compute` / `create_layout` (no class, no global state; was an all-static `Pipeline` class). The `VkPipelineCache` is owned by `Device`, published as `RendererServices::pipelineCache` and passed in explicitly (`DepthOnlyPipeline` builds its own pipeline and stays uncached, as before). Config-driven builder (`PipelineConfig::dynamicRendering` + `dynamicColorFormats` chain a `VkPipelineRenderingCreateInfo` and ignore the `VkRenderPass` argument); shader modules are RAII-owned via `swish::vkx::make_shader_module` (`utils/vulkan/ShaderModule.h`) |
| `Swapchain/` | `Swapchain` | Swapchain, image views, present mode negotiation, recreation |
| `RendererConfig.h` | `MAX_FRAMES_IN_FLIGHT` | Renderer-wide compile-time constants (moved out of `utils/` on 2026-09-29) |
| `DepthOnlyPipeline/` | `DepthOnlyPipeline` | Depth-only pipeline for the CSM shadow cascades (`depth_only.{vert,frag}`); owned by `GeometryProducer` |
| `GpuResource/` | `GpuImage`, `GpuBuffer` | Header-only RAII VMA handles ([docs/vma-memory.md](../../docs/vma-memory.md)); `VmaUsage.cpp` holds the VMA implementation |
| `CommandManager/` | `CommandManager` | Command pool; `MAX_FRAMES_IN_FLIGHT` command buffers |
| `SyncObjects/` | `SyncObjects` | `inFlight[frame]` fences; `imageAvailable[frame]` + `renderFinished[imageIndex]` semaphores |
| `PostProcessManager/` | `PostProcessManager` | All offscreen images, render passes, framebuffers, post-process pipelines and descriptor sets. Owns the GPU objects the `FullscreenPass` passes draw with (their recording moved to `Passes/`); publishes its images to the `ImageRegistry` (`register_images`) and builds the composite / G-buffer sets from the declared input lists. Split by topic: `PostProcessManager.cpp` (init/cleanup/recreate, samplers, extent scaling, named-image lookup), `_Passes.cpp` (render passes), `_Images.cpp` (images, framebuffers, AO/SSR prime), `_Descriptors.cpp` (set layouts, pipeline layouts, pool, sets), `_Pipelines.cpp` (fullscreen pipelines) |
| `ScenePipeline/` | `ScenePipeline` | Graphics pipeline for the G-buffer pass (basic.vert + gbuffer.frag); owned by `GeometryProducer` |
| `DeferredLightingPipeline/` | `DeferredLightingPipeline` | The lighting pipeline + five-set layout (`lighting.frag`); owned by `LightingPass` (`Passes/LightingPass.cpp`) |
| `CameraUniforms/` | `CameraUniforms` | UBO (view, proj, lights) + descriptor set 0; dirty-flag upload |
| `MaterialDescriptors/` | `MaterialDescriptors` | Per-material descriptor sets (set 1); batched `vkUpdateDescriptorSets` |
| `SceneGeometry/` | `SceneGeometry` | Device-local vertex + index buffers; staging upload; draw-call recording; exposes `get_vertex_buffer()` / `get_index_buffer()` for passes that share the car buffer |
| `ResourceManager/` | `swish::resource` (namespace) | Free functions, no class (was all-static): `transitionImageLayout` / `copyBufferToImage` one-time commands, `findDepthFormat`; the legacy `insertImageBarrier` was deleted (2026-09-29, no callers); inline transitions are `swish::vkx::image_barrier` / `image_barrier2` (`utils/vulkan/Barrier.h`) |
| `TextureManager/` | `TextureManager` | stb_image → device-local `VkImage`; name-keyed cache; shared sampler |
| `RainSystem/` | `RainSystem` | GPU rain streaks (instanced billboard quads, additive blend); registry pass `Rain`. Steps its simulation (gusting wind, integrated wetness) in `beginFrame` and publishes `FrameState::wetness` / `rainIntensity`; owns its render pass (LOAD on HDR) and per-frame UBOs |
| `GlassPass/` | `GlassPass` | Forward transparent pass for `alphaMode=BLEND` car glass; registry pass `Glass`; alpha blending, depth-test read-only; Fresnel tint + sun specular; draws the car buffers from `GeometryProducer::dynamic()` |
| `IBLManager/` | `IBLManager` | Image-based lighting bake: procedural sky or HDRI equirect to cube, irradiance, GGX prefilter, BRDF LUT (`ibl_*.frag`); feeds descriptor set 3 |
| `DofPass/` | `DofPass` | Depth-of-field pass (`dof.frag`); registry pass `Dof`, debug-gated |
| `SpraySystem/` | `SpraySystem` | GPU road spray: `spray_sim.comp` particle SSBO simulation + additive billboards (`spray.{vert,frag}`). Two registry entries: `SpraySim` (this class, compute, before rain) and `SprayDraw` (a proxy in `SpraySystem.cpp`, after rain) |
| `TaaPass/` | `TaaPass` | Temporal AA + motion blur (`taa.frag`); registry pass `Taa`, debug-gated (`SWISH_DEBUG_UI`), SSAA is the release default. Owns its ping-pong history, the previous view-projection and the Halton jitter counter (`beginFrame` sets the camera jitter). First pass on the Vulkan 1.3 path: with `RendererServices::dynamicRendering` it records `vkCmdBeginRendering` (pipeline built with `VkPipelineRenderingCreateInfo`, no `VkRenderPass`/`VkFramebuffer`) and, with `synchronization2`, `vkCmdPipelineBarrier2`; otherwise the classic render pass. Same output image state on both paths |
| `VelocityPass/` | `VelocityPass` | Per-object velocity target for the car body and wheels (`velocity.{vert,frag}`); registry pass `Velocity`, debug-gated; registers `Img::Velocity` (TaaPass samples it) and keeps its own previous-VP history |
| `WindshieldRainPass/` | `WindshieldRainPass` | Refractive rain drops on the front windshield; registry pass `WindshieldRain`: `update` advances the wiper and UBO, `record` steps the wetness map, snapshots the HDR scene and draws the drops (alpha blend, single-sided cull); layered Voronoi drops in glass space, forward-normal confinement, analytic wiper |

## Vulkan 1.3 path (dynamic rendering + synchronization2)

Added 2026-09-29 (refactor R2). `Device::init` enables both features when the physical device reports them and
publishes the result as two `bool`s (`RendererServices::dynamicRendering`, `::synchronization2`). A pass opts in by
reading those flags; anything that does not stays on `VkRenderPass` + classic barriers, so a device without the
features (or `SWISH_CLASSIC_VULKAN=1`) still runs everything.

| Pass | Dynamic rendering | sync2 barriers | Notes |
|------|-------------------|----------------|-------|
| `TaaPass` | yes (flag) | yes (flag) | classic render pass + framebuffers are only created on the classic path; both resize through `recreate()` |
| everything else | not yet | not yet | still `VkRenderPass`; barriers go through `vkx::image_barrier` (classic) |

Rules for a dynamic-rendering pass: transition each attachment explicitly (no render pass `initial/finalLayout`),
leave images in the layout the classic render pass would have, and build the pipeline with
`PipelineConfig::dynamicRendering`. Both paths must compile. Details and header docs: `src/utils/vulkan/Rendering.h`,
`src/utils/vulkan/Barrier.h`. Local MoltenVK (1.4.321 SDK, Apple M3 Max) reports both features `true`; NVIDIA on Arch is
expected to but is not verified.

---

## Pass registry (R3)

The frame is a single ordered list, `Passes/PassList.def`, and that line order **is** the command-buffer recording
order. `make_pass_registry` (`Passes/PassList.cpp`) expands it into factory calls; the `Renderer` owns the resulting
`PassRegistry` and drives the lifecycle:

```
init_all(PassInitCtx)  ->  [per frame] begin_frame(FrameUpdate) -> compute cascades -> camera UBO
                           -> update(FrameUpdate) -> record_all(FrameCtx)  ->  cleanup_all (reverse order)
resize_all(PassInitCtx) after every swapchain / SSAA recreate (PostProcessManager images are re-registered first)
```

Frame order (release; debug-only passes in brackets, listed where they record):

`Geometry` (shadow atlas, G-buffer, G-buffer -> lighting barriers) -> `Lighting` -> [`Ssao`, `AoBlur`, `Velocity`] ->
`Ssr` -> `GodRays` -> [`Dof`] -> `SpraySim` (compute) -> `Rain` -> `SprayDraw` -> `Glass` -> `WindshieldRain` ->
[`Taa`] -> `HdrFinalize` (luminance pyramid when auto-exposure is on, else the HDR -> SHADER_READ barrier) ->
`BloomExtract` -> `BloomBlurH` -> `BloomBlurV` -> `Composite` -> [`DebugOverlay`].

| Type | Kind | Role |
|------|------|------|
| `IRenderPass` | class | `name / init / onResize / beginFrame / update / record / cleanup` |
| `FrameCtx` | struct | what `record` may read: `cmd`, frame/image index, render/swap extent, `ImageRegistry`, `PassResources`, camera, `FrameState`, `tune`, sun direction |
| `FrameUpdate` | struct | what `beginFrame` / `update` read and write: dt, camera (non-const, for TAA jitter), car state, rain target, wiper, `FrameState&` |
| `FrameState` | struct | CPU values a pass publishes for later passes: `wetness`, `rainIntensity`, `exposure` (a blackboard, not a service) |
| `PassResources` | struct | non-owning pointers: `PostProcessManager`, `CameraUniforms`, `IBLManager`, `GeometryProducer`, `MaterialDescriptors` (+ debug: `SceneParamsUniform`, `DebugUI`) |
| `Img` / `ImageRegistry` | enum / class | typo-safe names for the images passes exchange; owners register (again after every recreate), consumers look up per frame |
| `FullscreenPass<Push>` | class template | the shared fullscreen shape: `describe` fills the draw + push block, `before`/`after` place the barriers; records exactly the calls the hand-written passes did |

`FrameCtx::tune` is the live `DebugParams` in debug builds and a `DebugParams{}` defaults instance in release: the
"release keeps the shipped look" idiom is written once, in `Renderer::tune()`.

Rules (each is checked by review, and most by the golden identity run):

- **Barriers are eager.** The pass that leaves an image in a layout records the barrier, at the same point of the command
  stream as before the registry. No tracker, no reordering, no lazy transitions.
- **History is private.** TAA ping-pong images and previous view-projection, velocity's previous view-projection and the
  auto-exposure adapted luminance live inside their pass. A pass never reaches into another pass; it reads named images
  and `FrameState`.
- **Debug passes** are `PASS_DEBUG` lines: they are not compiled into the release list, so release output is unchanged.
- **CSM / shadow is a geometry producer, not a pass.** `GeometryProducer` is the registry's first entry but is created by
  the Renderer (it owns the scene-upload API); every other entry comes from a factory.

### Add a pass in 2 files

1. `src/renderer/Passes/FooPass.cpp`: an anonymous-namespace class deriving `IRenderPass` (or `FullscreenPass<Push>` for a
   fullscreen post pass) and a factory `std::unique_ptr<IRenderPass> make_foo_pass()`. Sources are globbed: no CMake edit.
2. `src/renderer/Passes/PassList.def`: one `PASS(Foo, make_foo_pass)` (or `PASS_DEBUG`) line at the place of the frame
   where it records.

| | Before the registry (self-contained pass like `DofPass`) | After |
|---|---|---|
| New C++ files | `FooPass.h`, `FooPass.cpp` | `FooPass.cpp` |
| Edited C++ files | `Renderer.h` (forward decl + member), `Renderer.cpp` (include, create, cleanup, recreate), `Renderer_Record.cpp` (record) | `PassList.def` (1 line) |
| C++ files touched | 5 | **2** |
| Per-frame CPU work (uniform upload, simulation) | `Renderer_Frame.cpp` | `update()` / `beginFrame()` in the pass |
| Recreate wiring | hand-written array plumbing in `recreateSwapchain` | `onResize()` reads the `ImageRegistry` |

A shader is separate (`shaders/foo.frag`, globbed). A pass with live tunables still touches the three `src/debug/` files
(see `docs/RECIPES.md`, add-a-tunable). A new composite input is one `#define` in `bindings.glsl`, one line in
`PassInputs.h::kCompositeInputs` and the sampler in `composite.frag`.

### What is not migrated yet

- `PostProcessManager` still owns the GPU objects (images, render passes, framebuffers, pipelines, sets) of bloom,
  god-rays, SSAO, SSR and composite; the `FullscreenPass` classes only own the recording. Moving each pass's objects into
  its own class is the next step and would shrink `PostProcessManager` from dozens of getters to a thin image owner.
- The G-buffer (4 MRT) and shadow render passes are still built by hand in `PostProcessManager_Passes.cpp` (not the
  `vkx::make_color_pass` shape).
- `ScopedLabel` (debug labels around every `record`) is not wired: the label function pointers are not loaded yet.

---

## Subsystem Init Order

```
VulkanContext -> Device -> Swapchain
  -> CameraUniforms -> MaterialDescriptors        (descriptor set layouts)
  -> CommandManager -> SyncObjects
  -> PostProcessManager                           (images + passes sized to swapchain; images registered)
  -> IBLManager, SceneParamsUniform (debug)       (set 3 / set 4 layouts the lighting pipeline needs)
  -> PassRegistry::init_all                       (list order: Geometry pipelines, Lighting pipeline, ...)
  -> WeatherController::set_preset                (first HDRI bake)
  -> TextureManager -> SceneGeometry              (on scene load)
```

Cleanup runs in **reverse order**. `vkDeviceWaitIdle` must be called before any cleanup begins.

<details>
<summary><strong>Why this order?</strong></summary>

- `CameraUniforms` and `MaterialDescriptors` create descriptor set **layouts** that the Geometry and Lighting pipelines reference: layouts must exist before pipeline creation.
- `PostProcessManager` creates render passes and framebuffers at swapchain extent: it must come after `Swapchain`, and its images must be in the `ImageRegistry` before any pass `init` reads them.
- `IBLManager` (and the debug `SceneParamsUniform`) create the set 3 / set 4 layouts that `LightingPass::init` builds its pipeline layout from.
- `CommandManager` and `SyncObjects` have no dependencies beyond `Device`, but are placed late because `PostProcessManager` may issue one-time commands during init (e.g. priming the AO texture).

</details>

---

## Per-Frame Recording

`Renderer::drawFrame()` runs the following sequence each frame:

```
1. waitForFence(frame)               - stall until GPU done with this slot
2. vkAcquireNextImageKHR             - get swapchain image index
3. resetFence(frame), resetBuffer
4. (debug) DebugUI::begin_frame + WeatherController::apply_debug
5. PassRegistry::begin_frame         - TAA jitter, auto-exposure readback, rain simulation (publishes FrameState)
6. GeometryProducer::compute_cascades, CameraUniforms::set_cascades + update, (debug) SceneParamsUniform::update
7. PassRegistry::update              - spray sim UBO, windshield wiper/UBO; then CameraUniforms::set_wetness
8. vkBeginCommandBuffer
9.   PassRegistry::record_all        - the ordered list above
10. vkEndCommandBuffer
11. vkQueueSubmit  (wait: imageAvailable[frame], signal: renderFinished[imageIndex], fence: inFlight[frame])
12. vkQueuePresentKHR  (wait: renderFinished[imageIndex])
```

<details>
<summary><strong>Semaphore indexing - VUID-00067</strong></summary>

`renderFinished` is indexed by swapchain **image** index, not frame slot. With 3 swapchain images and 2 frames-in-flight, there are 3 `renderFinished` semaphores and 2 `inFlight` fences.

The spec (VUID-00067) forbids re-signaling a semaphore while it is still pending in a `vkQueuePresentKHR` operation. If `renderFinished` were indexed by frame slot (size 2), then with 3 images it would be possible for frame slot 0 to submit and signal semaphore[0], then before the present completes, frame slot 0 is reused and tries to signal semaphore[0] again - undefined behaviour.

Indexing by image index (size >= image count) guarantees each semaphore maps to at most one in-flight present at a time.

</details>

---

## RendererServices Bundle

Short-lived struct of Vulkan handles (and the device-feature flags) passed to subsystem `init()` and `upload()` calls:

```cpp
struct RendererServices {
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          graphicsQueue   = VK_NULL_HANDLE;
    VkExtent2D       swapchainExtent = {0, 0};   // invalidated on every resize
    VmaAllocator     allocator       = nullptr;  // the single VMA allocator
    bool             dynamicRendering = false;   // Vulkan 1.3 features enabled on the device
    bool             synchronization2 = false;
    VkPipelineCache  pipelineCache   = VK_NULL_HANDLE;  // Device-owned, passed to pipeline::create_*
};
```

Never store `RendererServices` as a member — `swapchainExtent` becomes stale after every swapchain recreation. Copy out only the handles your subsystem needs for long-term storage. Full field reference: [docs/data-types.md](../../docs/data-types.md).

---

## Adding a New Subsystem

See [CONTRIBUTING.md § Adding a new subsystem](../../CONTRIBUTING.md).

## Adding a New Render Pass

See "Add a pass in 2 files" above, [docs/RECIPES.md § add-a-render-pass](../../docs/RECIPES.md#add-a-render-pass) and [docs/render-pipeline.md](../../docs/render-pipeline.md).

---

## Diagrams

- [docs/diagrams/system-overview.excalidraw](../../docs/diagrams/system-overview.excalidraw) — component ownership
- [docs/diagrams/render-pipeline.excalidraw](../../docs/diagrams/render-pipeline.excalidraw) — 6-pass pipeline
- [docs/diagrams/vulkan-sync.excalidraw](../../docs/diagrams/vulkan-sync.excalidraw) — frame synchronization (fence + semaphore timing)
- [docs/diagrams/scene-data-flow.excalidraw](../../docs/diagrams/scene-data-flow.excalidraw) — asset → GPU → draw calls
- [docs/diagrams/descriptor-sets.excalidraw](../../docs/diagrams/descriptor-sets.excalidraw) — set 0 / set 1 bindings + push constants
- [docs/diagrams/data-layout.excalidraw](../../docs/diagrams/data-layout.excalidraw) — Vertex byte layout + std140 UBO layout

See also [docs/data-types.md](../../docs/data-types.md) for every struct's exact layout.

## Push-constant blocks (16-byte rule)

Every push block is a 16-byte multiple (MoltenVK misbehaves otherwise). Ranges are built with `vkx::push_range<T>(stages)`
(`utils/vulkan/PushConstants.h`), which `static_assert`s the rule, so a new block that is not padded fails to compile.
`PostProcessParams` is 13 shader floats (52 B) padded on the host to 64 B (`_pad3.._pad5`); the shader block in
`composite.frag` is unchanged and never reads the tail. The model+color prefix range (`kPushConstantModelColorSize`, glass
and windshield) is asserted in `scene/SceneTypes.h`.
