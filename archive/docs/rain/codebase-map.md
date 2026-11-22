# Codebase map — windshield rain / glass path

Ground-truth map of the rain/glass rendering path (verified by direct read).

## WindshieldRainPass lifecycle
- `init / update / record_draws / recreate / cleanup` — `src/renderer/WindshieldRainPass/WindshieldRainPass.cpp`.
- Renders **into** the HDR image as a color attachment (`createRenderPass`, `LOAD_OP_LOAD`,
  `COLOR_ATTACHMENT_OPTIMAL`) + read-only depth. → cannot sample the live HDR (feedback loop);
  a **snapshot** is required for refraction.
- Descriptor set 1: binding 0 = rain UBO; (now) binding 1 = refraction-source sampler. Set 0 = camera (bound by Renderer).
- Pipeline config via `PipelineConfig` (`src/renderer/Pipeline/Pipeline.{h,cpp}`): `additiveBlending`/`enableBlending`,
  `cullMode`, depth test/write. `frontFace = COUNTER_CLOCKWISE`.

## HDR plumbing (`src/renderer/PostProcessManager`)
- HDR = `R16G16B16A16_SFLOAT`, per-frame (`PP_MAX_FRAMES = 2`). Getters `get_hdr_image/get_hdr_view`.
- Usage was `COLOR_ATTACHMENT | SAMPLED`; **added `TRANSFER_SRC`** so it can be a copy source.
- Made via the shared `makeColorImage` lambda in `createImages()`.

## Renderer integration (`src/renderer/Passes/PassList.def`, `Renderer_Frame.cpp`)
*(Since 2026-09-29 the frame is the pass registry; `recordCommandBuffer` in `Renderer_Record.cpp` only calls `PassRegistry::record_all`.)*
- Pass order (`PassList.def`): Geometry (G-buffer) → Lighting → … → Rain → SprayDraw → Glass → WindshieldRain (**[snapshot]** then
  drops) → [Taa] → HdrFinalize (HDR→`SHADER_READ_ONLY` barrier) → bloom → composite (`Passes/BloomPasses.cpp`, `CompositePass.cpp`).
- The per-frame CPU work (screen-flow + speed factor) runs in `WindshieldRainPass::update`, called by
  `PassRegistry::update` from `drawFrame` (`Renderer_Frame.cpp`); `RainSystem::beginFrame` steps the rain and publishes wetness.
- Barriers: `swish::vkx::image_barrier` (`utils/vulkan/Barrier.h`) has a checked row per supported layout pair; the legacy
  `ResourceManager::insertImageBarrier` was deleted (2026-09-29); the snapshot's `TRANSFER_SRC/DST` transitions and the
  luminance-blit transitions are table rows in `Barrier.h` too.

## Windshield tagging (`src/scene/ModelManager/ModelManager.cpp`)
- Loaded model: `assets/Porsche/porsche.glb`. Glass nodes:
  - `Kit1_Window_Geo_lodA_..._Window_Material_0` — outer glass (single combined mesh: windshield + side + rear)
  - `Kit1_WindowInside_Geo_lodA_...` — inner cabin-facing pane (was the "rain inside cabin" culprit)
  - `..._RED_GLASS_0` — taillight glass (excluded)
- `isWindshield` (~L244) flows into `Submesh::is_windshield`. Now excludes `WindowInside_Geo`.
- Mesh is normalized at load so **nose = +X** (`ModelManager.cpp:83, 330`). Windshield outward normal ≈ +X.

## Input (`src/core/App/AppInput.cpp`, key table `m_key_bindings`)
- ESC = cursor capture; C = camera preset cycle; R = rain cycle (`set_rain_intensity`); V = wiper; G = weather preset.
- Car = arrow keys (`CarEntity.cpp`); free-fly camera = WASD/Q/E/Shift (`Camera.cpp`).
- **`V` was free** → wiper toggle. (Note: an earlier exploration wrongly cited a `toggleWiper()` in
  `examples/DownPour/src/DownPour.cpp`; that's an unrelated example — no wiper existed.)

## Build glue
- `windshield_rain.{vert,frag}` are picked up by the `shaders/*.{vert,frag,comp}` glob in `CMakeLists.txt` (`shaders` target); no list to edit.
