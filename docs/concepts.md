# Concepts to Learn — a Study Aid

A map from the design ideas and GPU techniques used on the `debug-ui` branch to the exact
place in this repo where each one lives. Use it as a study guide: read the one-line "why it
matters here", then open the file and trace it.

Companion docs: [debug-ui.md](debug-ui.md) (the tuning UI) and
[realism-features.md](realism-features.md) (the GPU effects).

> **Diagram:** the object-ownership graph is in
> [`docs/diagrams/ood-ownership.excalidraw`](diagrams/ood-ownership.excalidraw).

## OOP / OOD patterns

| Concept | Why it matters here | Where it lives |
|---------|---------------------|----------------|
| **RAII GPU handles** | GPU memory is a manual `alloc`/`free` API with an ordering footgun; wrapping `VkBuffer`/`VkImage` in move-only handles whose destructor frees makes leaks and double-frees structurally impossible. Every new subsystem (SceneParams UBO, luminance/SSR/SSAO images) just holds a handle and gets correct teardown for free. | [`GpuResource.h`](../src/renderer/GpuResource/GpuResource.h) (`GpuBuffer` / `GpuImage`); see [vma-memory.md](vma-memory.md) |
| **`unique_ptr` subsystem ownership** | `App` owns its subsystems by `unique_ptr`, so destruction order is deterministic and reversed-construction — the allocator outlives the buffers that free through it, the device outlives the allocator. This is what makes "validation-clean teardown" achievable. | [`App.cpp`](../src/core/App/App.cpp); [architecture.md](architecture.md) |
| **Single struct of tunables** | One `DebugParams` object is the sole source of truth for every knob. The UI mutates it, the renderer reads it, TOML serializes it — no scattered config globals, and adding a knob is adding one field. | [`DebugParams.h`](../src/debug/DebugParams.h); see [debug-ui.md](debug-ui.md#the-debugparams-single-struct-pattern) |
| **Descriptor-set separation as an interface boundary** | Sets 0/1/2/3 = camera / textures / shadow / debug-scene-params. Each set is a stable "interface" a pipeline binds to; adding the debug UI's live constants as a *new* set 3 means the existing sets — and the release build that omits set 3 — are untouched. | [`SceneParamsUniform.h`](../src/debug/SceneParamsUniform.h); [debug-ui.md](debug-ui.md#the-scene-params-ubo-descriptor-set-3) |
| **Tail-append struct evolution** | CSM data was appended to the *end* of `CameraUBO` after the vertex-shader prefix, so every vertex shader that declares only the prefix stays binary-compatible while `lighting.frag` reads the tail. A discipline for evolving a shared GPU struct without breaking every consumer. | [`SceneTypes.h`](../src/scene/SceneTypes.h) (`CameraUBO`) |
| **Compile-time feature gating (`#ifdef`) as zero-cost-in-release design** | The whole debug UI is `#ifdef SWISH_DEBUG_UI`. In release the code doesn't exist — not "disabled at runtime", *absent* — and the shader `SP_*` macros expand to literals so the release `.spv` is provably identical. Zero binary cost, zero runtime branch. | [debug-ui.md](debug-ui.md#release-safety); [`lighting.frag`](../shaders/lighting.frag) macro block |
| **Move-only + rule-of-zero** | RAII handles delete copy, default move; owning types then need no hand-written destructor. Read one handle to see how ownership transfer replaces manual cleanup. | [`GpuResource.h`](../src/renderer/GpuResource/GpuResource.h) |

## GPU / rendering concepts

| Concept | Why it matters here | Where it lives |
|---------|---------------------|----------------|
| **Deferred shading** | Geometry is rasterized once into a G-buffer, then lit in a single fullscreen pass — decoupling shading cost from geometry count and enabling all the screen-space passes that read the G-buffer. | [render-pipeline.md](render-pipeline.md); [`gbuffer.frag`](../shaders/gbuffer.frag) + [`lighting.frag`](../shaders/lighting.frag) |
| **G-buffer channel packing** | Albedo / normal / material(metal, rough, wettable) / depth are packed into MRT attachments; the material channels are what SSR and the wet-road BRDF read to gate their effects. | [`gbuffer.frag`](../shaders/gbuffer.frag) (`outMaterial = vec4(metal, rough, wettable, 1)`) |
| **Descriptor sets & push constants** | Sets bind resources by frequency of change (per-frame UBO vs per-material texture); push constants carry tiny per-draw data with no descriptor. Knowing the split explains why SSAO needed its own layout (its push exceeded the shared 32-B range). | [debug-ui.md](debug-ui.md#per-frame-live-parameter-flow); [realism-features.md](realism-features.md#ssao--screen-space-ambient-occlusion) |
| **PCF shadow filtering** | A hard depth-compare aliases into stair-stepped shadow edges; averaging a 3×3 neighbourhood of comparisons softens them. Note the slice-clamp so taps stay in one cascade. | [`lighting.frag`](../shaders/lighting.frag) (PCF loop) |
| **Cascaded shadow maps (CSM)** | One shadow map can't cover a 4.2 km road with crisp near shadows; splitting the frustum by depth into cascades — packed here into one atlas — puts resolution where it's visible. Learn the practical split scheme and the bounding-sphere stable fit. | [realism-features.md](realism-features.md#csm--cascaded-shadow-maps-3-cascades); `computeCascades` in [`Renderer.cpp`](../src/renderer/Renderer/Renderer.cpp) |
| **Screen-space techniques (SSAO / SSR) and their failure modes** | Marching the depth/HDR buffers gives cheap contact AO and reflections, but only for on-screen geometry — so silhouettes-against-sky, off-screen rays, and thickness misses are the failure modes you must handle (sky skips, edge fades, IBL fallback). | [realism-features.md](realism-features.md#ssr--screen-space-reflections); [`ssao.frag`](../shaders/ssao.frag), [`ssr.frag`](../shaders/ssr.frag) |
| **Split-sum IBL** | The environment-lighting integral factors into "prefiltered environment × environment-BRDF"; Swish evaluates both **analytically** (sky-blend irradiance + Karis env-BRDF) so no cubemap or LUT is needed. The cleanest doorway into physically-based ambient. | [realism-features.md](realism-features.md#split-sum-ibl--environment-lighting-from-the-procedural-sky); `skyIrradiance` / `envBRDFApprox` in [`lighting.frag`](../shaders/lighting.frag) |
| **Tone mapping (AgX) + auto-exposure** | HDR must map to a displayable range without hue shifts (AgX), and the exposure that feeds it can be driven automatically from measured scene luminance (eye adaptation) — including the prev-frame-readback trick that avoids a GPU stall. | [realism-features.md](realism-features.md#auto-exposure--eye--camera-adaptation); [`composite.frag`](../shaders/composite.frag) (`AgX`), `updateAutoExposure` in [`Renderer.cpp`](../src/renderer/Renderer/Renderer.cpp) |
| **Barriers & image-layout transitions** | Every screen-space pass reuses the same HDR/depth images as attachment *and* as sampled texture within one frame, so it must transition layouts (`COLOR_ATTACHMENT` ↔ `SHADER_READ` ↔ `TRANSFER_SRC`) and insert memory barriers at exactly the right points. This is the single most error-prone part of the branch. | [realism-features.md](realism-features.md#where-the-passes-sit); barrier code in [`Renderer.cpp`](../src/renderer/Renderer/Renderer.cpp) (`recordSsaoPasses`, `recordSsrPass`, `recordLuminancePyramid`) |
| **std140 / MoltenVK portability** | Uniform-buffer layout rules bite hardest on the Vulkan-on-Metal portability subset; the branch packs every UBO member as a `vec4` to sidestep `vec3` padding, and 16-byte-aligns push constants. | [debug-ui.md](debug-ui.md#the-scene-params-ubo-descriptor-set-3); [data-types.md](data-types.md) |
| **Prev-frame GPU→CPU readback** | Reading GPU results on the CPU normally stalls the pipeline; reading a buffer written a frame or two ago (fine when the consumer adapts slowly) hides the latency. The auto-exposure loop is the textbook example. | [realism-features.md](realism-features.md#auto-exposure--eye--camera-adaptation) |
| **Supersampling (SSAA) as a resolve** | Rendering the offscreen chain larger than the swapchain and downsampling once with a bilinear filter *is* anti-aliasing — no separate resolve shader. Learn how one factor and a clamp against `maxImageDimension2D` implement it. | [realism-features.md](realism-features.md#ssaa--internal-supersampling) |

## Suggested reading order

1. [architecture.md](architecture.md) — the 5-layer stack and who owns what.
2. [render-pipeline.md](render-pipeline.md) — the base deferred passes.
3. [debug-ui.md](debug-ui.md) — how tuning is plumbed and gated.
4. [realism-features.md](realism-features.md) — the GPU effects, with the math.
5. The shaders themselves ([`lighting.frag`](../shaders/lighting.frag) first) — the ground truth.
