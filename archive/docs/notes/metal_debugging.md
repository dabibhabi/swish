# Tracking Vulkan → Metal calls in Xcode (MoltenVK on macOS)

Two paths: a fast CLI capture (no Xcode project needed), and a full Xcode integration. Do Path A first to confirm everything works, then graduate to B.

---

## Path A — Capture from terminal (5 minutes)

Skips Xcode-as-IDE entirely. Run `swish` from the shell with magic env vars, MoltenVK auto-captures a `.gputrace` file, and you double-click it to open in Xcode's Metal debugger.

```bash
cd /Users/adminh/swish

# Required: turns on Metal's capture infrastructure
export METAL_CAPTURE_ENABLED=1

# MoltenVK: capture the first frame only (option 2)
export MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=2

# Where to dump the capture (file MUST NOT exist beforehand)
export MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE=/tmp/swish_frame.gputrace

# Bonus: log every Vulkan call MoltenVK receives, with timing
export MVK_CONFIG_TRACE_VULKAN_CALLS=5

# Bonus: see SPIR-V → MSL shader conversions
export MVK_CONFIG_DEBUG=1
export MVK_CONFIG_LOG_LEVEL=4

# Make sure no stale capture file exists
rm -f /tmp/swish_frame.gputrace

./build/swish
```

Then open the capture:

```bash
open /tmp/swish_frame.gputrace
```

Xcode launches into the Metal debugger with that frame loaded. Stdout will be flooded with every Vulkan call MoltenVK saw — pipe to a file if you want to grep it later:

```bash
./build/swish 2> /tmp/swish_vk.log
```

---

## Path B — Full Xcode integration

Your project is CMake/Make with no `.xcodeproj`. Generate one in a sibling build dir so it does not clobber the current build:

```bash
cd /Users/adminh/swish
cmake -G Xcode -B build-xcode -S .
open build-xcode/swish.xcodeproj
```

Once Xcode opens:

### 1. Edit the scheme

`Product → Scheme → Edit Scheme…` (or `⌘<`)

### 2. Run tab → Diagnostics — turn ON

- Metal API Validation
- Metal Shader Validation
- GPU Frame Capture → set to **Metal** (not Automatic)

### 3. Run tab → Arguments → Environment Variables

Add these (no `export`, just key=value):

| Variable | Value |
|---|---|
| `METAL_CAPTURE_ENABLED` | `1` |
| `MVK_CONFIG_LOG_LEVEL` | `4` |
| `MVK_CONFIG_DEBUG` | `1` |
| `MVK_CONFIG_TRACE_VULKAN_CALLS` | `5` |
| `MVK_CONFIG_PERFORMANCE_TRACKING` | `1` |
| `MVK_CONFIG_SHADER_LOG_ESTIMATED_GLSL` | `1` |
| `MVK_CONFIG_SHADER_DUMP_DIR` | `/tmp/swish_shaders` |

### 4. Build & run

`⌘R`. Once the window appears with rendering, look for the **camera icon** in Xcode's debug bar (bottom strip with FPS / Memory / GPU). Click it → **Capture GPU workload**.

---

## What to inspect once captured

The Metal debugger opens four useful panels:

- **Debug Navigator (left)** — tree of `MTLCommandBuffer` → `MTLRenderCommandEncoder` → individual draw calls. This is the Metal-side view of what your Vulkan calls became.
- **Bound Resources** — for each draw, the textures, buffers, and pipeline state at that moment. Verify descriptor sets translated correctly.
- **Geometry Viewer** — see vertex inputs/outputs at each pipeline stage.
- **Shaders** — Metal Shading Language source MoltenVK generated from your SPIR-V. Compare against `/tmp/swish_shaders/`.
- **Performance** (top tab) — GPU counters per encoder: bandwidth, ALU utilization, fragment/vertex cycles.

---

## Cheat sheet — MoltenVK env vars

| Var | What it does |
|---|---|
| `METAL_CAPTURE_ENABLED=1` | **Required** for any GPU capture. Apple's gate. |
| `MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE` | `0` off, `1` whole device lifetime, `2` first frame only |
| `MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE` | Path for `.gputrace`. **File must not pre-exist.** |
| `MVK_CONFIG_LOG_LEVEL` | `0`–`4` (4 = debug spam) |
| `MVK_CONFIG_DEBUG` | `1` enables shader + extra diagnostics |
| `MVK_CONFIG_TRACE_VULKAN_CALLS` | `0` off, `3` enter/exit, `5` with timing |
| `MVK_CONFIG_PERFORMANCE_TRACKING` | `1` collects stats; query via `vkGetPerformanceStatisticsMVK()` |
| `MVK_CONFIG_SHADER_LOG_ESTIMATED_GLSL` | `1` prints reverse-translated GLSL during MSL compile |
| `MVK_CONFIG_SHADER_DUMP_DIR` | Directory; auto-created. Dumps SPIR-V + generated MSL per pipeline |

---

## Recommended first experiment

1. Run **Path A** with `MVK_CONFIG_TRACE_VULKAN_CALLS=5` and `MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=2`.
2. While `swish` is running, in another terminal: `ls /tmp/swish_shaders` and watch shaders get translated.
3. Quit, then `open /tmp/swish_frame.gputrace`.
4. In Xcode: pick a `vkCmdDraw*` (it shows as a Metal draw inside a render encoder), open the **Shaders** pane, and read the MSL MoltenVK generated from your SPIR-V. Compare to the original GLSL in `shaders/`.

That is the magic moment — you literally see the Vulkan → Metal translation on screen.

---

## Common footguns

1. **Stale `.gputrace` file** — MoltenVK refuses to overwrite. Always `rm -f` first.
2. **Forgetting `METAL_CAPTURE_ENABLED=1`** — without it Metal silently disables capture.
3. **GPU Frame Capture set to "Automatic"** in scheme — sometimes picks the wrong API. Force **Metal**.
4. **Running a Release build** — Metal API Validation only fires in Debug.

---

## Sources

- [MoltenVK Configuration Parameters](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)
- [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- [Apple — Capturing a Metal workload in Xcode](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode)
- [Apple — Metal debugger](https://developer.apple.com/documentation/xcode/metal-debugger)
