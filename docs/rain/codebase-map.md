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

## Renderer integration (`src/renderer/Renderer/Renderer.cpp`)
- Pass order (`recordCommandBuffer`): G-buffer → lighting → rain → glass → **[snapshot]** → windshield-rain
  → HDR→`SHADER_READ_ONLY` barrier → bloom → composite.
- `update()` block (~L313) computes screen-flow + speed factor and calls `WindshieldRainPass::update`.
- Helper `ResourceManager::insertImageBarrier(cmd, img, old, new[, aspect])` — **does not** cover
  `TRANSFER_SRC/DST` layouts, so the snapshot uses explicit `vkCmdPipelineBarrier`.

## Windshield tagging (`src/scene/ModelManager/ModelManager.cpp`)
- Loaded model: `assets/Porsche/porsche.glb`. Glass nodes:
  - `Kit1_Window_Geo_lodA_..._Window_Material_0` — outer glass (single combined mesh: windshield + side + rear)
  - `Kit1_WindowInside_Geo_lodA_...` — inner cabin-facing pane (was the "rain inside cabin" culprit)
  - `..._RED_GLASS_0` — taillight glass (excluded)
- `isWindshield` (~L244) flows into `Submesh::is_windshield`. Now excludes `WindowInside_Geo`.
- Mesh is normalized at load so **nose = +X** (`ModelManager.cpp:83, 330`). Windshield outward normal ≈ +X.

## Input (`src/core/App/App.cpp:234-260`)
- ESC = cursor capture; C = cockpit/free-fly; R = rain cycle (`set_rain_intensity`).
- Car = arrow keys (`CarEntity.cpp`); free-fly camera = WASD/Q/E/Shift (`Camera.cpp`).
- **`V` was free** → wiper toggle. (Note: an earlier exploration wrongly cited a `toggleWiper()` in
  `examples/DownPour/src/DownPour.cpp`; that's an unrelated example — no wiper existed.)

## Build glue
- `windshield_rain.{vert,frag}` registered in `CMakeLists.txt` (`shaders` target); no new shader files.
