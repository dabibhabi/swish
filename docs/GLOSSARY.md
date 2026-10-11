---
verified: 2026-09-29
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD; utils paths, set numbers, key controls updated
owner: docs-scribe
---

# Glossary

A fast lookup of what each term, abbreviation, class, and convention means **in the Swish codebase**.
Skim the section you need — every entry is one line.

**Jump to:** [Conventions](#project-conventions) · [Abbreviations](#abbreviations) · [Render pipeline & passes](#render-pipeline--passes) · [Lighting & realism](#lighting--realism) · [Coordinate spaces](#coordinate-spaces) · [Vulkan objects](#vulkan-objects-as-used-here) · [GPU memory (VMA + RAII)](#gpu-memory-vma--raii) · [Rain system](#rain-system) · [Components / classes](#components--classes) · [Debug UI](#debug-ui-live-tuning) · [Key globals & files](#key-globals--files) · [Controls](#controls)

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
| `VK_CHECK(...)` | Macro asserting a `VkResult` is `VK_SUCCESS` (throws `std::runtime_error` with file, line, symbolic name); the standard error-handling pattern. Lives in `src/utils/vulkan/Check.h`. |
| Rooted include | Project headers are included from `src/` (`#include "utils/math/Smoothing.h"`); same-directory includes use the bare name; `../` is never used. |
| `swish::util::<topic>` | Namespace of a `src/utils/<topic>/` helper (`math`, `units`, `geometry`, `color`, `container`, `meta`, `io`, `debug`). |
| `swish::vkx` | The namespace of the Vulkan glue in `src/utils/vulkan/` (`image_barrier`, `make_shader_module`, `push_range<T>`, `DeviceHandle`, `DebugLabel`). Was `swish::vk` before 2026-09-29. |
| `Tunables.def` | `src/debug/Tunables.def` — X-macro registry: one line per live tunable generates the `DebugParams` field, TOML key, slider, dump line and push copy (converted groups only). |
| Golden test | A test that compares output against a committed fixture in `tests/golden/` (car trajectory, road mesh hashes, release `.spv` hashes); the refactor safety net. Never regenerate one to make a refactor pass. |
| `make check` | `scripts/check_repo.sh`: repo hygiene (changelog index, stray root files, ignored-but-tracked, source globs; sizes and format are report-only). See [KEEPING_IT_CLEAN.md](KEEPING_IT_CLEAN.md). |
| DTO | "Data Transfer Object" — a plain struct (e.g. `DrawCall`); its fields are bare `camelCase` (no `m_`). |
| `m_` prefix | Member variable (`m_position`). `k` prefix = `constexpr` constant (`kMaxSpeed`). |
| `camelCase` vs `snake_case` | Vulkan-core layer uses `camelCase` getters (`getDevice()`); scene/app layer uses `snake_case` (`get_speed()`). |
| Shader registration | None: `CMakeLists.txt` globs `shaders/*.{vert,frag,comp}` (`SHADER_SOURCES`, `CONFIGURE_DEPENDS`); the `shaders` target compiles them to `.spv` with `glslc -I shaders/common`. Shared GLSL lives in `shaders/common/*.glsl`. |
| Source registration | None: `src/**/*.cpp` is globbed (`src/debug/` only with `SWISH_DEBUG_UI=ON`). `make new-class NAME=Foo DIR=scene` scaffolds a class. |

---

## Abbreviations

| Abbr. | Expansion / meaning here |
|-------|--------------------------|
| **HDR** | High Dynamic Range — the `R16G16B16A16_SFLOAT` scene-colour target the scene is lit into before tone-mapping. |
| **G-buffer** | Geometry buffer — the deferred-rendering attachments (albedo, normal, material, depth) written first. |
| **UBO** | Uniform Buffer Object — CPU→GPU constants (camera, rain params, …). |
| **PBR** | Physically-Based Rendering — the material model (metallic/roughness). |
| **SSAO / AO** | Screen-Space Ambient Occlusion / Ambient Occlusion — contact-shadow darkening (half-res, bilateral-blurred). |
| **SSR** | Screen-Space Reflections — ray-marches the lit HDR buffer in view space so wet asphalt mirrors on-screen lamps/geometry. |
| **SSAA** | Supersampling Anti-Aliasing — the whole offscreen chain renders at swap×1.5 and the composite downsamples. |
| **CSM** | Cascaded Shadow Maps — the sun shadow map split into `NUM_CASCADES = 3` distance slices (near crisp + long-range). |
| **IBL** | Image-Based Lighting — prefiltered cubemap (diffuse irradiance + GGX-specular roughness mips + split-sum BRDF LUT) baked by `IBLManager` from a Radiance `.hdr` equirect when present (`assets/Sky/…`), else from the procedural sky; sampled in `lighting.frag` (set 3). |
| **PCF** | Percentage-Closer Filtering — manual 3×3 shadow-depth compare (MoltenVK has no comparison samplers). |
| **AgX** | Filmic tone-mapping curve (Blender/Sobotka) applied in the composite pass — replaced the earlier ACES Narkowicz fit. |
| **BLEND** | A glTF `alphaMode` marking transparent glass meshes (windows). |
| **SDF** | Signed Distance Field — distance to a shape's surface (negative inside); used for drops and the wiper blade. |
| **LOD** | Level Of Detail — here, a mipmap level (`textureLod`). |
| **NDC** | Normalized Device Coordinates — clip space ÷ w, range `[-1,1]`. |
| **WU** | World Units — Swish's world scale: 1 scene-metre = 1000 WU (`util::units::kWuPerMetre` in `src/utils/units/Units.h`; the old `WORLD_SCALE` constant was removed 2026-09-29); e.g. car top speed ≈ 30000 WU/s ≈ 108 km/h. |
| **SPIR-V / `.spv`** | Compiled shader bytecode Vulkan consumes (output of `glslc`). |
| **glslc** | The GLSL→SPIR-V compiler (from the Vulkan SDK). |
| **FoV** | Field of View. |
| **Rigid-body chassis (stub)** | Debug-only 6-DOF car model (`RigidChassis.h` on `utils/physics/RigidBody.h`): four spring-damper wheel contacts, pitch/roll/heave as state; sag per wheel $= mg/4k$. Alternative to the bicycle model; see `docs/car_system.md`. |
| **VMA** | [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) — AMD's allocator library. Swish sub-allocates every GPU buffer/image from a few big device blocks through one `VmaAllocator`, instead of one `vkAllocateMemory` per resource. See [GPU memory](#gpu-memory-vma--raii). |
| **RAII** | *Resource Acquisition Is Initialization* — the C++ idiom where an object releases its resource in its destructor. Here `GpuBuffer`/`GpuImage` free their VMA sub-allocation automatically, so there are no manual `vkDestroy*`/`vkFreeMemory` pairs. |

---

## Render pipeline & passes

The per-frame order is the ordered list [`Passes/PassList.def`](../src/renderer/Passes/PassList.def) (`Renderer::recordCommandBuffer` in [`Renderer_Record.cpp`](../src/renderer/Renderer/Renderer_Record.cpp) only brackets `PassRegistry::record_all`); see also [`render-pipeline.md`](render-pipeline.md):

| Stage | What it does |
|-------|--------------|
| **Shadow / CSM pass** | Depth-only render of the scene from the sun's POV into a 3-cascade horizontal atlas (`depth_only.vert`); the lighting pass samples it for manual PCF. Runs before lighting. |
| **G-buffer pass** | Rasterizes scene geometry into albedo/normal/material/depth (at the SSAA render-extent). |
| **SSAO pass** | Half-render-res: 16-sample rotated hemisphere from depth-reconstructed normals → raw ambient occlusion. |
| **AO blur** | Depth-aware bilateral blur of the SSAO target (edge-preserving), consumed at composite. |
| **Lighting pass** | Deferred lighting reads the G-buffer (+ shadow atlas + IBL-lite sky) → writes the **HDR** target. |
| **SSR pass** | View-space depth ray-march of the lit HDR → screen-space reflections (roughness/wetness-gated); sky IBL is the miss fallback. |
| **Rain pass** | `RainSystem` draws falling streaks additively onto HDR (`LOAD_OP_LOAD`). |
| **Glass pass** | `GlassPass` draws transparent BLEND windows (alpha + Fresnel) onto HDR. |
| **Wetness update** | Fullscreen pass steps the windshield wetness map (rain/advect/wipe). |
| **HDR snapshot** | Copies HDR → a sampleable image so the windshield rain can refract it. |
| **Windshield rain pass** | Refractive drops on the windshield, gated by the wetness map. |
| **Luminance pyramid** | Blit HDR down a full mip chain to 1×1 (average luminance) → host readback → CPU auto-exposure feeds the composite `exposure`. |
| **Bloom** | Extract bright pixels → blur → (added in composite). |
| **Composite** | HDR × AO + bloom + SSR, scaled by auto-exposure → **AgX** tone-map → downsampled to the swapchain image (SSAA). |
| **Render pass** (Vulkan) | A set of attachments + subpass(es); each pass above is one. |
| **Framebuffer** | The concrete image views bound to a render pass for a frame. |
| **Barrier** | A `vkCmdPipelineBarrier` (or `vkCmdPipelineBarrier2` with sync2) that transitions an image's layout / orders GPU access between passes; recorded through `swish::vkx::image_barrier`. Barriers are **eager**: the pass that leaves an image in a layout records it. |
| **Pass registry** | The ordered list of frame passes (`Passes/PassList.def`) + `PassRegistry`, which drives every pass's `init` / `onResize` / `beginFrame` / `update` / `record` / `cleanup`. Add a pass = `FooPass.cpp` + one list line. |
| **`IRenderPass`** | The interface every registry pass implements (`Passes/IRenderPass.h`). A `FullscreenPass<Push>` is the shared shape of a fullscreen post pass. |
| **`FrameCtx` / `FrameUpdate` / `FrameState`** | What a pass may read while recording / while doing CPU work / the CPU values passes publish for later passes (wetness, rain intensity, exposure). |
| **`Img` / `ImageRegistry`** | Typo-safe names for the images passes exchange (`Img::Hdr`, `Img::Velocity`, ...); owners register, consumers look up. |
| **Dynamic rendering** | Vulkan 1.3 `vkCmdBeginRendering`: draw without a `VkRenderPass` / `VkFramebuffer`. Enabled when the device supports it; TAA uses it. `SWISH_CLASSIC_VULKAN=1` forces the classic path. |
| **`synchronization2`** | Vulkan 1.3 barrier API (`VkImageMemoryBarrier2`); enabled with dynamic rendering, used by TAA via `image_barrier_auto`. |
| **`LOAD_OP_LOAD`** | Render-pass setting that preserves existing attachment contents (forward passes draw *over* HDR). |
| **Swapchain** | The set of images presented to the window. |

---

## Lighting & realism

The "Blender-look" realism pass — all live-tunable in debug builds (see [Debug UI](#debug-ui-live-tuning)), release-safe by identity defaults. Math + per-pass I/O: [`docs/realism-features.md`](realism-features.md), [`docs/render-pipeline.md`](render-pipeline.md).

| Term | Meaning in Swish |
|------|------------------|
| **Sky reflections / IBL** | Split-sum lighting from a **prefiltered cubemap** (`IBLManager`, set 3), preferably baked from an HDRI equirect: diffuse = `texture(irradianceMap, N)`; specular = `textureLod(prefilteredMap, R, rough·maxMip)` weighted by `texture(brdfLUT, vec2(N·V, rough))`. Visible sky + haze use `textureLod(prefilteredMap, dir, 0)`. HDRI = bake once; procedural fallback re-bakes on weather change. |
| **IBL bake chain** | HDRI equirect (or procedural sky) → env cube (6 faces) → diffuse-irradiance cube → GGX-prefiltered specular cube (roughness mips) → split-sum BRDF LUT. GPU precompute in `IBLManager` (`bakeFromHdri` / `bake`). |
| **Cube-face bake** | Each cube face is rendered from a CPU-supplied basis: `dir = normalize(F + s·R + t·U)`, `s,t ∈ [-1,1]`, matching the hardware `samplerCube` convention. |
| **Split-sum approximation** | Karis' factoring of the IBL specular integral into (pre-filtered environment) × (`envBRDFApprox` scale+bias) so it's one eval per pixel. |
| **`envBRDFApprox`** | Analytic fit of the BRDF-integration LUT (Karis mobile approx) — returns the Fresnel scale+bias for the split-sum. |
| **Sun shadows / CSM** | Directional shadows from a 3-cascade depth atlas; cascade chosen by view-space depth vs `cascadeSplits`. Plain `sampler2D` + manual 3×3 **PCF** (no MoltenVK compare sampler). Set 2 in `lighting.frag`. |
| **Cascade selection** | Pick cascade `i` where view depth < `cascadeSplits[i]`; each cascade has its own `cascadeViewProj[i]` world→`[0,1]` clip. |
| **Shadow floor / bias** | `SP_SHADOW_FLOOR` (min lit fraction ≈0.25) + `SP_SHADOW_BIAS` (depth-compare slop to kill acne); live-tunable. |
| **SSR (screen-space reflections)** | 40-step view-space depth ray-march (geometric stride) of the lit HDR; hit → reflected colour, Fresnel + intensity weighted, added at composite. Sky/matte pixels skipped; wetness collapses roughness toward mirror. |
| **God-rays / light shafts** | Half-res sun-anchored radial blur (Mitchell 2007): march toward the projected sun summing lit-HDR radiance at sky pixels (depth ≈ far) with exponential decay; geometry occludes → dark shafts. Added at composite. **Ships in release** (un-gated) + `density`/`decay`/`weight` debug sliders. Fades out when the sun is off-screen/behind camera. |
| **Puddles** | World-space procedural pool mask on the road (gated by the `gbMaterial.a` **road tag** from `MAT_ASPHALT`), faded in by wetness. Inside a pool the wet model collapses roughness + flattens the normal, so the sky-IBL specular (and, in debug, SSR) resolve as a puddle mirror. `coverage` slider raises the water level; release ships coverage 0 (dry, byte-identical). |
| **Road spray / GPU particles** | The renderer's first **compute** pass: 4096 particles in a shared SSBO advanced in place by `spray_sim.comp` (respawn behind the rear axle → integrate under gravity+drag → age), drawn as additive camera-facing billboards (`SpraySystem`). Emission gated by wetness × speed → dry road/release emits nothing. |
| **TAA (temporal AA)** | Debug-only reprojection TAA (`TaaPass`): per-frame Halton sub-pixel **jitter** → reconstruct world from depth + reproject through the prev-frame VP → **3×3 neighborhood-clamp** blend with history → copy back into HDR (bloom/composite untouched). Converges to supersampled detail. SSAA stays the release default; TAA is a live toggle. |
| **Motion vectors / motion blur** | Two sources, blended in `taa.frag`: (1) depth-reprojection (`uv − prevUV`, pure camera motion) for the world, and (2) a debug-only **per-object velocity G-buffer** (`VelocityPass`, RGBA16F: rg = UV motion, a = validity) that re-draws the car body + wheels a second time using `CarEntity::get_prev_draw_models()` (rigid transform diff, current vs previous frame) — this is what's authoritative wherever it's valid, fixing reprojection ghosting on the moving car/spinning wheels that pure camera reprojection can't represent. `taa.frag` smears the resolved colour along the chosen velocity for per-pixel motion blur. |
| **Auto-exposure / luminance adaptation** | Average scene luminance (1×1 mip of HDR) read back to the CPU, exponentially adapted (`aeKey`, `aeSpeed`, clamped `aeMin`/`aeMax`) → drives the composite `exposure`. Night vs day self-level. |
| **AgX tonemap** | The composite tonemapper (`AgX()`, `kAgXInset`/`kAgXOutset`, `agxCurve`) — Blender/Sobotka; replaced ACES Narkowicz. |
| **SSAA render-extent** | The offscreen chain is sized to `renderExtent = swap × kRenderScale (1.5)`, clamped so the largest dimension ≤ `maxImageDimension2D`; only the composite writes swapchain-sized images → supersample downsample. |
| **Clear-day preset / `weather.x`** | `G` sets `clarity = 1.0` (deep-blue sky, high sun, rain off); clarity rides in **CameraUBO `weather.x`** (0 overcast → 1 clear) and deepens the sky gradient + sharpens the sun disc. |

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
| **Light / cascade space** (`cascadeViewProj[i]`) | world → sun-cascade `[0,1]` clip | CSM shadow lookup + manual PCF. |

---

## Vulkan objects (as used here)

| Object | Role in Swish |
|--------|---------------|
| `VkImage` / `VkImageView` | A GPU texture and a typed "window" into it (HDR, G-buffer, refraction source, wetness map). |
| **Image layout** | The image's current usage state (`COLOR_ATTACHMENT_OPTIMAL`, `SHADER_READ_ONLY_OPTIMAL`, `TRANSFER_SRC/DST_OPTIMAL`); changed by barriers. |
| Descriptor set / layout / pool | A bound group of shader resources / its schema / the allocator. **Scene pass:** set 0 = camera+lights, set 1 = material textures. **Lighting pass:** set 0 = camera+lights, set 1 = G-buffer, set 2 = shadow atlas, set 3 = IBL cubemaps (irradiance + prefiltered + BRDF LUT), set 4 = scene-params / debug UBO (debug builds only). |
| Push constants | Tiny inline shader constants (e.g. per-draw `model` + `color`, or the wetness-pass params). |
| Pipeline / pipeline layout | The compiled GPU state (shaders + blend + cull + depth) / its descriptor & push-constant signature. Built via `pipeline::create_graphics` (free functions, `renderer/Pipeline/Pipeline.h`) + `PipelineConfig`. |
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
| `Device` | `renderer/Pipeline/Device` | Physical + logical device selection, queues; enables Vulkan 1.3 dynamic rendering + sync2 when supported; owns the `VkPipelineCache`. |
| `Swapchain` | `renderer/Swapchain` | Swapchain images/views + clean recreate-on-resize. |
| `CommandManager` | `renderer/CommandManager` | Command pool + per-frame command buffers. |
| `SyncObjects` | `renderer/SyncObjects` | Per-frame fences + image-available/render-finished semaphores. |
| `Renderer` | `renderer/Renderer` | Facade that owns the Vulkan core, the shared subsystems and the `PassRegistry`; split across `Renderer.cpp`, `Renderer_Frame.cpp`, `Renderer_Record.cpp` (brackets `record_all`), `Renderer_State.cpp`. |
| `PassRegistry` / `IRenderPass` / `ImageRegistry` | `renderer/Passes` | The pass registry: ordered list `PassList.def`, lifecycle driver, named images; one `.cpp` per pass (`SsrPass`, `BloomPasses`, `CompositePass`, ...). |
| `GeometryProducer` | `renderer/GeometryProducer` | First registry entry: owns the scene geometry, the G-buffer + depth-only pipelines and the CSM cascade fit; records the shadow atlas, G-buffer and their barriers. |
| `WeatherController` | `renderer/WeatherController` | Weather preset (sun, clarity, HDRI look) and the dirty-checked IBL rebake. |
| `PostProcessManager` | `renderer/PostProcessManager` | Owns offscreen resources and the GPU objects the fullscreen passes draw with: G-buffer, HDR, bloom, SSAO/AO, SSR, shadow atlas, luminance pyramid, composite; sets the SSAA render-extent. Split over 5 files by topic. |
| `IBLManager` | `renderer/IBLManager` | **Set 3** — baked-sky prefiltered-cubemap IBL (env/irradiance/prefiltered cubes + BRDF LUT); bakes at init + on weather change. |
| `SpraySystem` | `renderer/SpraySystem` | GPU road-spray: the first **compute** pipeline — a 4096-particle SSBO simulated by `spray_sim.comp` + additive billboard forward pass. Emission gated by wetness × speed. |
| `TaaPass` | `renderer/TaaPass` | **Debug-only** reprojection TAA + motion-blur resolve; copies its result back into HDR so the composite chain is unchanged. SSAA stays the release AA. |
| `SceneParamsUniform` | `debug/SceneParamsUniform` | **Set 4** (debug) — live look-tunables UBO (sky/sun/fog/shadow/wet/IBL); debug builds only. |
| `DebugUI` | `debug/DebugUI` | Dear ImGui live-tuning panel + ImGuizmo gizmos + toml presets + per-material editor (`#ifdef SWISH_DEBUG_UI`). |
| `CameraUniforms` | `renderer/CameraUniforms` | **Set 0** — camera + lights UBOs. |
| `MaterialDescriptors` | `renderer/MaterialDescriptors` | **Set 1** — PBR material textures. |
| `ScenePipeline` | `renderer/ScenePipeline` | The deferred G-buffer pipeline (owned by `GeometryProducer`). |
| `DeferredLightingPipeline` | `renderer/DeferredLightingPipeline` | The lighting pipeline + its layout (owned by `LightingPass`). |
| `SceneGeometry` | `renderer/SceneGeometry` | Vertex/index buffers + draw-call recording. |
| `swish::resource` | `renderer/ResourceManager` | Free functions (was a static class): one-time-command layout transition and buffer-to-image copy, depth-format pick. Buffers/images come from VMA (`GpuBuffer` / `GpuImage`). |
| `swish::pipeline` | `renderer/Pipeline` | `PipelineConfig` struct + free functions `create_graphics` / `create_compute` / `create_layout` (was a static class); the cache comes from `RendererServices::pipelineCache`. |
| `RainSystem` | `renderer/RainSystem` | Falling-rain streaks + wetness accumulation. |
| `GlassPass` | `renderer/GlassPass` | Forward transparent pass for BLEND car glass. |
| `WindshieldRainPass` | `renderer/WindshieldRainPass` | Refractive windshield rain + wetness map + wiper. |
| `TextureManager` | `renderer/TextureManager` | Owns all GPU textures (`Texture` = one image + view + sampler). |
| `CameraController` | `core/CameraController` | Per-mode camera placement, cockpit FOV kick + shake, mouse-look offsets, free-fly and helicopter input. |
| `VelocityPass` | `renderer/VelocityPass` | Debug-gated per-object velocity target (car body + wheels) for TAA / motion blur. |
| `DofPass` | `renderer/DofPass` | Depth-of-field pass (`dof.frag`). |
| `Camera` | `scene/Camera` | POV camera: WASD free-fly + mouse look + car-attached presets (cockpit/chase/…). |
| `CameraMode` | `scene/CameraMode.h` | Enum of camera presets cycled with **C**. |
| `ModelManager` | `scene/ModelManager` | Loads GLB/glTF cars; mesh normalization + glass tagging. |
| `SceneManager` | `scene/SceneManager` | `Scene` = `std::function<void(Renderer&)>` that populates the world; switching. |
| `RoadScene` | `scene/RoadScene` | The road/world scene (`MeshBuilder` builds its geometry). |
| `Entity` / `CarEntity` | `scene/Entity` | Base scene object with a transform; `CarEntity` = drivable car physics. |

---

## Debug UI (live-tuning)

Everything here is gated behind `#ifdef SWISH_DEBUG_UI` (built by `make debug`) so the **release build output is byte-identical** — identity defaults (identity gizmo correction, no material overrides, fixed exposure) reproduce prior behaviour. Full write-up: [`docs/debug-ui.md`](debug-ui.md). Files live under [`src/debug/`](../src/debug/).

| Term | Meaning in Swish |
|------|------------------|
| **Dear ImGui** | The immediate-mode GUI library backing the live-tuning panel (`DebugUI`). |
| **Live-tunables / `DebugParams`** | The struct of slider-backed values (`src/debug/DebugParams.h`) edited in the panel and uploaded each frame. |
| **Scene-params UBO (set 4)** | `SceneParamsUniform` — `DebugParams` marshalled to a vec4-aligned std140 UBO (11 rows, 176 B) bound at **set 4** in `lighting.frag` (debug builds only). Release hardcodes the same values via `SP_*` macros (no set 4). It was set 3 until IBL took that slot. |
| **ImGuizmo** | The gizmo library used for the in-viewport transform handles. |
| **Sun-orientation gizmo** | A rotate gizmo (`sunGizmoRot`) that reorients the sun direction; a quaternion correction calibrates the visual axis. |
| **Steering-wheel gizmo** | A gizmo (`steerOverride`, pitch/roll/quat spin-axis) to pose/fix the wheel and override the steering angle. |
| **Per-material editor** | An override table (`matOverrides[MAT_COUNT]`) to live-edit each material's albedo/metallic/roughness. |
| **Presets (toml)** | Save/load the whole `DebugParams` set to `.toml` in `$SWISH_PRESETS_DIR` or `~/.config/swish/presets/` (`DebugParamsIO`, toml++); the old `build/config/presets/` is read as a fallback. |
| **Edit vs drive mode** | Backtick toggles between ImGui interaction (edit) and driving the car (drive). |

---

## Key globals & files

| Name | Where | Meaning |
|------|-------|---------|
| `Vec2/3/4`, `Mat4`, `u32`, … | [`src/utils/types/Types.h`](../src/utils/types/Types.h) | `glm` and fixed-width aliases (plain `swish::`) — use these, not raw `glm::`. |
| `MAX_FRAMES_IN_FLIGHT` | [`src/renderer/RendererConfig.h`](../src/renderer/RendererConfig.h) | `2` — frames recorded concurrently. |
| `kWuPerMetre`, `kWuPerFt`, `m()`, `ft()`, `kPi` | [`src/utils/units/Units.h`](../src/utils/units/Units.h) | `1000.0f` WU per metre, `304.8f` WU per foot, unit converters and pi. Never re-type `1000`, `0.3048` or pi. |
| `road::kLie`, `kService`, `kExit`, `kCrownSlope` | [`src/scene/RoadScene/RoadParams.h`](../src/scene/RoadScene/RoadParams.h) | Shared road cross-section constants read by both `RoadScene` and `RoadGeometry`. |
| `SET_*` / `BIND_*` | [`shaders/common/bindings.glsl`](../shaders/common/bindings.glsl) | Single source of descriptor set / binding numbers for the lighting-pass shaders; re-exported to C++ as `swish::bind::k*` in [`renderer/Bindings.h`](../src/renderer/Bindings.h). |
| `PassList.def` | [`src/renderer/Passes/`](../src/renderer/Passes/PassList.def) | The ordered frame: `PRODUCER` / `PASS` / `PASS_DEBUG` lines; line order = recording order (pinned by `test_renderer_PassRegistry.cpp`). |
| `swish::vkx` | [`src/utils/vulkan/`](../src/utils/vulkan/README.md) | Vulkan helpers: barriers, `Rendering.h`, `make_color_pass`, `Descriptors.h`, `Samplers.h`, `ImageView.h`, `RenderScope.h`, `push_range<T>`. |
| `swish::tag` | [`src/utils/meta/Tags.h`](../src/utils/meta/Tags.h) | Empty tag types (`flag_t`, `real_t`) for compile-time overload selection (tag dispatch); `tunables::visit` uses them. |
| `road::kPuddleCoverage` | [`RoadParams.h`](../src/scene/RoadScene/RoadParams.h) | The one C++ constant for puddle coverage (0.501); physics uses it, tests pin it against the `Tunables.def` default and the `lighting.frag` literal. |
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
| `G` | Cycle the weather HDRI preset (partly cloudy / cloudy / very cloudy / golden hour; procedural sky if an HDRI is missing) |
| `V` | Toggle the windshield wiper |
| `C` | Cycle camera: cockpit → chase → hood → bumper → roof → side → helicopter → free-fly |
| `W` `A` `S` `D` `Q` `E` + Shift | Free-fly move; in helicopter, **Q/E** raise/lower height |
| `Esc` | Toggle mouse-look capture |
| Backtick | *(debug build)* Toggle edit (ImGui) ↔ drive mode |

---

*Add a term here when you introduce a new abbreviation, pass, or convention — it's the first place a new reader (or future you) will look.*
