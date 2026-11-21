---
title: Renderer next steps (you write the code)
aliases: [renderer next steps, E1-E5 guide]
tags: [swish, renderer, todo, guide]
updated: 2026-11-01
branch: bug/fix-camera-ownership
ai-generated: true
---

# Renderer next steps

> [!warning] AI-generated
> Claude wrote this from reading the code on branch `bug/fix-camera-ownership` (HEAD `4f13766` plus the uncommitted R5/R6 edits). It contains **no applied code**. Every snippet is the shape of a change for you to type. Line numbers are from that tree; re-check them if you pull or switch branches.

> [!info] Rendering this note in Obsidian
> LaTeX and `mermaid` work out of the box. The `tikz` blocks need the community plugin **TikZJax**. Without it they show as plain code. The diagrams use neon outlines with translucent fills and no hard-coded text color, so the plugin's dark-mode text inversion keeps labels readable in both themes. I compile-checked them with `pdflatex` on black and white pages, but TikZJax supports fewer packages than a full TeX install, so a block can still fail to render. Code Styler hints (`title:`, `ln:`, `hl:`) need the **Code Styler** plugin. `renderer-terms-README.md` explains every term used below.

## Where you are

`codex/lookdev-workbench` was renamed to `bug/fix-camera-ownership`. They are one branch.

| ID | Item | State |
|----|------|-------|
| R1 | Quit hang while minimized | done and committed |
| R2 | `set_camera(unique_ptr)` | done and committed |
| R3 | Remove dead debug getters | done (only `request_capture` remains) |
| R4 | `<array>` / `<string>` includes | done |
| R5 | `cleanup()` null-device guard | done, uncommitted |
| R6 | clang-format on `Renderer.h/.cpp` | done, uncommitted, dry run reports 0 warnings |
| **E1-E5** | experience items | **open: this README** |

Build and `make test` (390/390) passed after R5/R6. I did not run the app.

## The frame loop and where each item sits

```tikz
\begin{document}
\definecolor{ncy}{HTML}{00E5FF}\definecolor{nmg}{HTML}{FF2BD6}\definecolor{ngr}{HTML}{39FF14}
\definecolor{nye}{HTML}{FFE600}\definecolor{nor}{HTML}{FF8A00}\definecolor{nvi}{HTML}{B388FF}
\tikzset{fcy/.style={draw=ncy,very thick,fill=ncy,fill opacity=0.22,text opacity=1},
 fmg/.style={draw=nmg,very thick,fill=nmg,fill opacity=0.22,text opacity=1},
 fgr/.style={draw=ngr,very thick,fill=ngr,fill opacity=0.22,text opacity=1},
 fye/.style={draw=nye,very thick,fill=nye,fill opacity=0.22,text opacity=1},
 fnor/.style={draw=nor,very thick,fill=nor,fill opacity=0.22,text opacity=1},
 arr/.style={draw=nvi,thick}}
\begin{tikzpicture}[font=\scriptsize,
  box/.style={rounded corners,minimum height=0.8cm,minimum width=1.5cm,align=center,text opacity=1},
  blk/.style={box,fmg}, ok/.style={box,fcy}]
  \node[ok]  (a) at (0,0)    {read input\\ \& update};
  \node[blk] (b) at (2.2,0)  {wait fence\\ (E2, E3)};
  \node[blk] (c) at (4.4,0)  {acquire\\ (E2, E3)};
  \node[ok]  (d) at (6.6,0)  {record};
  \node[ok]  (e) at (8.8,0)  {submit};
  \node[ok]  (f) at (11,0)   {present};
  \draw[arr,-latex] (a)--(b); \draw[arr,-latex] (b)--(c); \draw[arr,-latex] (c)--(d);
  \draw[arr,-latex] (d)--(e); \draw[arr,-latex] (e)--(f);
  \draw[arr,-latex] (f.south) -- ++(0,-0.7) -| (a.south);
  \node[fnor,dashed,rounded corners,align=center] (e1) at (6.6,1.8) {E1: weather key\\ stalls here (bake)};
  \draw[draw=nor,thick,-latex,dashed] (e1) -- (d);
\end{tikzpicture}
\end{document}
```

Red boxes are the two places the CPU can block waiting for the GPU or the display. E4 and E5 are outside the loop (a title update, and a save at shutdown).

## Suggested order

| Order | Item | Effort | Risk | Needs you in the app |
|-------|------|--------|------|----------------------|
| 1 | E1 step 1: measure the weather-key stall | 10 min | none | yes |
| 2 | E5 atomic cache write | 15 min | low | yes (restart twice) |
| 3 | E3 timeouts | 20 min | low-medium (false alarm, see E3) | yes |
| 4 | E4 fps readout | 15 min | low | yes |
| 5 | E1 fix (pick an option from the measurement) | 30 min to a few hours | depends on option | yes |
| 6 | E2 input latency (measure first; the fix may be bigger than it looks) | 15 min to 2 h | medium | yes (feel test) |
| last | gates, changelog | 20 min | none | no |

---

## E1. The weather key stalls the picture

### What happens

Press `G` and the picture freezes. On this branch the path is:

1. `App::on_cycle_weather` (`AppInput.cpp:84`)
2. `WeatherController::set_preset` (`WeatherController.cpp:44`)
3. `IBLManager::set_hdri` (`IBLManager.cpp:368`), which does two things back to back on the main thread:
   - `loadEquirect` (about L326-358): `stbi_loadf` decodes a roughly 5 MB `.hdr` file from `assets/Sky/` into floats, copies it to a staging buffer, uploads it, and does blocking layout transitions.
   - `bakeFromHdri` (`IBLManager.cpp:426`): `vkQueueWaitIdle`, then 6 faces of equirect-to-cube, then `bakeConvolutions` (L388), then another wait for the GPU in `endOneTime`.

> [!warning] Your 113.6 ms was measured on `master`
> `master` bakes a procedural sky with `IBLManager::bake`, which has no file load. This branch loads an HDRI first. The number here is probably different. Re-measure before choosing anything.

### Step 1: measure three numbers

Wrap the three pieces in `IBLManager::set_hdri` and `bakeFromHdri`, press `G` once, read the log, then delete the timing.

```cpp title:"IBLManager.cpp, set_hdri() and bakeFromHdri()" ln:true
using clk = std::chrono::steady_clock;
auto ms = [](clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

// in set_hdri:
const auto t0 = clk::now();
if (!loadEquirect(path)) return false;
const auto t1 = clk::now();
bakeFromHdri();
const auto t2 = clk::now();
std::fprintf(stderr, "[IBL] load %.1f ms, bake (incl. idle-wait) %.1f ms\n", ms(t0, t1), ms(t1, t2));

// in bakeFromHdri, around the first vkQueueWaitIdle:
const auto w0 = clk::now();
vkQueueWaitIdle(m_svc.graphicsQueue);
std::fprintf(stderr, "[IBL] idle-wait %.1f ms\n", ms(w0, clk::now()));
```

Needs `<chrono>` and `<cstdio>`. Write the results here:

| Piece | ms |
|-------|----|
| `loadEquirect` (disk + decode + upload) | _____ |
| idle-wait (frames already in flight) | _____ |
| bake GPU work | _____ |

### Step 2: pick by what the numbers say

| Biggest piece | Pick | Why |
|---------------|------|-----|
| bake GPU work | **Option A** (shader early-out) | removes about 75% of the prefilter work |
| `loadEquirect` | **Option B or C** | the cost is CPU decode and upload, a shader change does not touch it |
| idle-wait | none of these | the GPU was busy with frames, not with the bake. Tell me the numbers |
| everything under about 20 ms | do nothing | one dropped frame is not visible |

At 60 Hz, a stall of $s$ ms drops about $s / 16.7$ frames:

$$
\text{dropped frames} \approx \frac{s}{1000 / 60} = \frac{s}{16.7}
$$

### Option A. Skip the 256 identical reads at roughness 0 (GPU part)

`ibl_prefilter.frag` takes 256 GGX samples per pixel. Mip 0 has roughness 0, where the GGX sampler returns $H = N$ every time, so all 256 reads hit the same texel. Mip 0 also holds most of the pixels:

```tikz
\begin{document}
\definecolor{ncy}{HTML}{00E5FF}\definecolor{nmg}{HTML}{FF2BD6}\definecolor{ngr}{HTML}{39FF14}
\definecolor{nye}{HTML}{FFE600}\definecolor{nor}{HTML}{FF8A00}\definecolor{nvi}{HTML}{B388FF}
\tikzset{fcy/.style={draw=ncy,very thick,fill=ncy,fill opacity=0.22,text opacity=1},
 fmg/.style={draw=nmg,very thick,fill=nmg,fill opacity=0.22,text opacity=1},
 fgr/.style={draw=ngr,very thick,fill=ngr,fill opacity=0.22,text opacity=1},
 fye/.style={draw=nye,very thick,fill=nye,fill opacity=0.22,text opacity=1},
 fnor/.style={draw=nor,very thick,fill=nor,fill opacity=0.22,text opacity=1},
 arr/.style={draw=nvi,thick}}
\begin{tikzpicture}[font=\small]
  \draw[fmg]  (0,0)      rectangle (7.5,0.8)   node[midway]{mip 0 (256$^2$): 75\%};
  \draw[fcy]  (7.5,0)    rectangle (9.375,0.8);
  \draw[fye]  (9.375,0)  rectangle (9.844,0.8);
  \draw[fgr]  (9.844,0)  rectangle (9.961,0.8);
  \draw[fnor] (9.961,0)  rectangle (10,0.8);
  \node (m1) at (7.2,1.7) {mip 1 (128$^2$): 18.8\%};
  \draw[ncy,thick,->] (m1.south) ++(0.9,0) -- (8.4,0.85);
  \node (m2) at (10.4,2.5) {mips 2--4: 6.2\%};
  \draw[nye,thick,->] (9.9,2.2) -- (9.9,0.85);
  \node[below,align=center] at (5,-0.1) {each mip costs pixels $\times$ 256 samples $\times$ 6 faces};
\end{tikzpicture}
\end{document}
```

$$
\frac{6 \cdot 256^2}{6\,(256^2 + 128^2 + 64^2 + 32^2 + 16^2)} = \frac{65536}{87296} \approx 0.75
$$

Why the shortcut is exact. With $a = \text{rough}^2 = 0$:

$$
\cos\theta = \sqrt{\frac{1 - \xi_y}{1 + (a^2 - 1)\,\xi_y}} = \sqrt{\frac{1 - \xi_y}{1 - \xi_y}} = 1
\;\Rightarrow\; H = N,\quad L = 2(V \cdot H)H - V = N,\quad N \cdot L = 1
$$

So the loop averages 256 copies of `texture(envMap, N)`. Replace it with one read.

```cpp 
title:"shaders/ibl_prefilter.frag, in main(), after float rough = pc.params.x;" ln:true
    if (rough < 0.001) {
        outColor = vec4(texture(envMap, N).rgb, 1.0);  // mirror: all samples hit the same texel
        return;
    }
```

> [!warning] This changes the `.spv`, which the golden test pins
> `tests/golden/release_spv.sha256` holds the hash of every compiled shader. After this edit the `.spv` hash test **will fail**. That failure is correct. Per `CLAUDE.md`: regenerate with `SWISH_UPDATE_GOLDENS=1` **only** for an intended behavior change, and only with a changelog entry. Also: compile with the SDK `glslc` (set `$VULKAN_SDK`), or the hash will not match for an unrelated reason.

- Expected (an estimate): the prefilter work falls by about 75%.
- How to check: re-run the timing. Then look at a glossy panel on the car, clear weather and overcast, before and after. It must look the same.

### Option B. Preload every HDRI at startup (CPU part)

Decode all preset HDRIs once, keep the float data (or the GPU image), so a key press only re-bakes.

Memory for 2k equirects in `RGBA32F` (`kEquirectFormat`, `IBLManager.h:57`):

$$
2048 \times 1024 \times 4\ \text{channels} \times 4\ \text{B} = 33{,}554{,}432\ \text{B} = 32\ \text{MiB per HDRI}
$$

$$
7\ \text{files} \times 32\ \text{MiB} = 224\ \text{MiB}
$$

On Apple silicon the GPU shares system memory, so that is real RAM. Also a longer startup (7 decodes). The trade is simple: **no load stall, but about 224 MiB and a slower launch**.

### Option C. Prefetch only the next preset on a worker thread

While driving, decode the file for `next_weather_preset(...)` on a second thread. On `G`, upload and bake only.

- Memory: one extra HDRI, 32 MiB.
- Threading rules you must follow: decode on the worker, but **all Vulkan calls stay on the main thread**. `stb_image` keeps a global failure string (`stbi_failure_reason`), so do not read it from two threads, or define `STBI_THREAD_LOCAL`.
- Handle "pressed `G` before the decode finished": either wait (a shorter stall than today), or ignore the press.
- Cost: this is the most code and the most ways to go wrong. Pick it only if Option B's memory or startup cost is unacceptable.

### Option D. Smaller upload format

Convert to `RGBA16F` after decode. Halves memory (16 MiB each) and upload bandwidth, but you change `kEquirectFormat` and the staging copy. The sky loses precision only above half-float range, which the exposure scale already compresses.

### Not recommended

| Idea | Why not |
|------|---------|
| Debounce slider drags | `WeatherController::maybe_rebake_ibl` already returns when an HDRI is in use (`uses_hdri()`), so sliders do not rebake HDRI skies. A debounce only matters on the procedural fallback, which only runs if an HDRI file is missing. |
| Bake one face per frame | The cubes are re-rendered while frames still sample them. It needs double-buffered cubes (twice the memory) plus a layout/barrier plan. |
| Lower `kPrefilterDim` | Blurs the mirror mip, which is what makes the clearcoat look glossy. |

### E1 checklist

- [ ] Measure: load ___ ms, idle-wait ___ ms, bake ___ ms
- [ ] Pick: A / B / C / D / none
- [ ] If A: edit the shader, run `make build`, expect the `.spv` golden to fail, regenerate it on purpose, add the changelog entry
- [ ] Re-measure after: ___ ms
- [ ] Look at reflections before and after (clear + overcast)
- [ ] Delete the timing code

---

## E2. Input is sampled before the vsync block

### What I got wrong the first time

The earlier note said the block is the fence wait at the top of `drawFrame`. On this branch the present mode is **FIFO only** (`Swapchain.cpp:127`). With FIFO the CPU usually blocks inside `vkAcquireNextImageKHR` (`Renderer_Frame.cpp:58`), waiting for a free image, not in the fence wait. So moving only the fence wait earlier may do almost nothing. Measure before you edit.

### The idea

```tikz
\begin{document}
\definecolor{ncy}{HTML}{00E5FF}\definecolor{nmg}{HTML}{FF2BD6}\definecolor{ngr}{HTML}{39FF14}
\definecolor{nye}{HTML}{FFE600}\definecolor{nor}{HTML}{FF8A00}\definecolor{nvi}{HTML}{B388FF}
\tikzset{fcy/.style={draw=ncy,very thick,fill=ncy,fill opacity=0.22,text opacity=1},
 fmg/.style={draw=nmg,very thick,fill=nmg,fill opacity=0.22,text opacity=1},
 fgr/.style={draw=ngr,very thick,fill=ngr,fill opacity=0.22,text opacity=1},
 fye/.style={draw=nye,very thick,fill=nye,fill opacity=0.22,text opacity=1},
 fnor/.style={draw=nor,very thick,fill=nor,fill opacity=0.22,text opacity=1},
 arr/.style={draw=nvi,thick}}
\begin{tikzpicture}[x=0.7cm,y=1cm,font=\footnotesize]
  \node[anchor=east] at (-0.3,1.6) {Before};
  \draw[fcy]  (0,1.3) rectangle (1,1.9)  node[midway]{in};
  \draw[fcy]  (1,1.3) rectangle (4,1.9)  node[midway]{update};
  \draw[fmg]  (4,1.3) rectangle (10,1.9) node[midway]{blocked};
  \draw[fgr]  (10,1.3) rectangle (14,1.9) node[midway]{GPU draw};
  \draw[arr,<->] (0.5,1.1) -- (14,1.1) node[midway,below]{input age at display = 14 ms};
  \node[anchor=east] at (-0.3,0) {After};
  \draw[fmg]  (0,-0.3) rectangle (6,0.3)  node[midway]{blocked};
  \draw[fcy]  (6,-0.3) rectangle (7,0.3)  node[midway]{in};
  \draw[fcy]  (7,-0.3) rectangle (10,0.3) node[midway]{update};
  \draw[fgr]  (10,-0.3) rectangle (14,0.3) node[midway]{GPU draw};
  \draw[arr,<->] (6.5,-0.5) -- (14,-0.5) node[midway,below]{input age at display = 8 ms};
\end{tikzpicture}
\end{document}
```

The numbers in the picture are an example, not measurements. The point: if you sleep in the block **before** reading input, the input is fresher by the length of the block.

$$
\text{gain} = L_{\text{before}} - L_{\text{after}} = t_{\text{blocked}} \le T
$$

### Step 1: measure how long the block really is

```cpp title:"Renderer_Frame.cpp, around the fence wait (L27) and acquire (L58)" ln:true
const auto a0 = clk::now();
m_syncObjects->waitForFence(m_device->getDevice(), m_currentFrame);
const auto a1 = clk::now();
// ... acquireImage runs ...
const auto a2 = clk::now();
// once per second, print the averages of (a1 - a0) and (a2 - a1)
```

Print the running average of "fence wait" and "acquire" separately.

| Measured | Meaning | Choose |
|----------|---------|--------|
| both under about 2 ms | no vsync block to hide | **Option D** (do nothing) |
| fence wait is the big one | the GPU is behind | **Option A** |
| acquire is the big one (expected with FIFO) | the vsync wait | **Option B** or **C** |

### Option A. `wait_frame_ready()` (fence only)

Smallest change. A new public method on `Renderer` that does the fence wait, called at the top of the `App` loop before `frame_delta` (`App.cpp:88`).

```cpp title:"shape of the change" ln:true
// Renderer.h (public):   void wait_frame_ready();
// Renderer.cpp:          void Renderer::wait_frame_ready() { m_syncObjects->waitForFence(m_device->getDevice(), m_currentFrame); }
// App.cpp, in the loop:  m_renderer->wait_frame_ready();   // before frame_delta(...)
```

Keep the wait inside `drawFrame`: it becomes a no-op. Safe, but it only helps when the fence wait is the long one.

### Option B. `begin_frame()` = fence wait **and** acquire, before input

Move `acquireImage` out of `drawFrame` into a `begin_frame()` that stores the image index. `drawFrame` then uses the stored index. This hides the whole vsync block.

Things you must handle:
- `acquireImage` can call `recreateSwapchain` and return "no image". `begin_frame` must report that so `App` skips drawing this frame.
- The debug SSAA path returns early after the fence wait (`Renderer_Frame.cpp:35-41`). It must also leave `begin_frame` in a consistent state.
- You are changing a function that every frame goes through. Test minimize, resize and the SSAA button after.

### Option C. Present mode: prefer MAILBOX

`master` already did this: pick `VK_PRESENT_MODE_MAILBOX_KHR` if the surface offers it, else FIFO. MAILBOX does not wait for vblank, so the block mostly disappears.

| | FIFO (now) | MAILBOX |
|---|---|---|
| Waits for vblank | yes | no |
| Frame rate | locked to the display | uncapped |
| Latency | higher | lower |
| Side effects | none | high GPU use and heat unless you add a frame limiter |

You would have to add your own limiter, or accept hundreds of fps. The comment in `Swapchain.cpp:125` says the project chose FIFO on purpose to cap fps.

### Option D. Leave it

If Step 1 shows both blocks are small, or you cannot feel the difference, stop. Do not add code for a gain you cannot perceive.

### E2 checklist

- [ ] Measure fence wait: ___ ms, acquire: ___ ms
- [ ] Pick: A / B / C / D
- [ ] Apply, then **feel test** (steer in `make run`, before and after). Only you can judge this.
- [ ] If it feels the same, revert it

---

## E3. A GPU hang looks like a frozen window

### What happens

Two waits have no limit:

| Where | Code |
|-------|------|
| `SyncObjects.cpp:53-54` | `kNoFenceTimeout = UINT64_MAX` |
| `Renderer_Frame.cpp:58` | `vkAcquireNextImageKHR(..., UINT64_MAX, ...)` |

If the GPU hangs, the app freezes with no message and no way to see what stalled.

### What to do

Replace `UINT64_MAX` with a timeout, and say which wait timed out.

```cpp title:"SyncObjects.cpp (waitForFence): shape of the change" ln:true
constexpr uint64_t kFenceTimeoutNs = 5'000'000'000ull;  // 5 s
const VkResult     r = vkWaitForFences(device, 1, &m_inFlightFences[frameIndex], VK_TRUE, kFenceTimeoutNs);
if (r == VK_TIMEOUT) throw std::runtime_error("GPU fence wait timed out after 5 s: possible GPU hang");
VK_CHECK(r);
```

`SyncObjects.cpp` already gets `<stdexcept>` through `utils/vulkan/Check.h`, so no new include.

```cpp title:"Renderer_Frame.cpp (acquireImage)" ln:true
constexpr uint64_t kAcquireTimeoutNs = 5'000'000'000ull;
VkResult r = vkAcquireNextImageKHR(..., kAcquireTimeoutNs, ...);
// keep the out-of-date branch first, then:
if (r == VK_TIMEOUT) /* see the options below */;
```

### Option: what to do on `VK_TIMEOUT` in acquire

| Response | Good | Bad |
|----------|------|-----|
| `throw` with a clear message | shows the hang immediately | a covered window that holds the drawable for over 5 s would crash a healthy app |
| `return std::nullopt` (skip the frame) | harmless if it was a false alarm | a real hang just loops silently, so you lose the diagnostic |
| log once, then skip | both, but more code | needs a "logged already" flag |

My pick for a first version: **throw for the fence wait** (a fence that late is a real hang), **skip and log for acquire** (a covered window is a plausible false alarm).

> [!question] One thing I could not confirm
> I do not know whether MoltenVK blocks acquire for more than 5 s when the window is fully covered. Test it: cover the window with another app for 10 s, then uncover it. If it throws, switch acquire to the skip-and-log option.

### E3 checklist

- [ ] Edit the two waits
- [ ] Choose the acquire response: throw / skip / log-then-skip
- [ ] Test: minimize and restore (must not throw)
- [ ] Test: cover the window for 10 s, then uncover
- [ ] You cannot test a real GPU hang; that path is only reviewed by reading

---

## E4. No frame-rate readout in release

### Why `frame_delta` is the wrong clock

`App::frame_delta` (`App.cpp:108`) returns `min(delta, 1/15)` so a long hitch does not break physics. That cap hides stalls: a 113 ms freeze counts as 67 ms. For an fps number, use real elapsed time, not that value.

$$
\text{fps} = \frac{N_{\text{frames}}}{t_{\text{now}} - t_{\text{start}}}, \qquad \text{frame time (ms)} = \frac{1000\,(t_{\text{now}} - t_{\text{start}})}{N_{\text{frames}}}
$$

### Options

| Option | Where | Shows | Cost |
|--------|-------|-------|------|
| A. Window title | `App::run`, once per second | fps and ms in the title bar | about 10 lines, no UI |
| B. ImGui overlay (debug only) | `src/debug/DebugUI.cpp` | fps in the panel | none for release, only debug builds |
| C. Both | | | more code |

### Option A: shape of the change

Use `glfwGetTime()` directly in the loop. Do not touch `frame_delta`.

```cpp title:"App.cpp, in the run loop: shape of the change" ln:true
// before the loop:
double fps_t0 = glfwGetTime(); int fps_n = 0;

// inside the loop, once per iteration:
++fps_n;
const double now = glfwGetTime();
if (now - fps_t0 >= 1.0) {
    char title[96];
    std::snprintf(title, sizeof title, "Swish  %.0f fps  %.1f ms", fps_n / (now - fps_t0), 1000.0 * (now - fps_t0) / fps_n);
    glfwSetWindowTitle(m_window->getHandle(), title);
    fps_t0 = now; fps_n = 0;
}
```

Needs `<cstdio>`. Decision to make: wrap it in `#ifdef SWISH_DEBUG_UI` so release output stays byte-identical (the project rule), or accept a deliberate release change and log it in the changelog.

### E4 checklist

- [ ] Pick A / B / C
- [ ] Decide: debug-only or both builds
- [ ] Run it; the title updates once per second
- [ ] If it appears in release, add that to the changelog

---

## E5. Pipeline cache write is not atomic

### What happens

`Device::savePipelineCache` (`Device.cpp:196-203`) opens the file with `trunc`, which empties it at once, then writes. A crash in between leaves a short file. The path comes from `pipeline_cache_path()` (`Device.cpp:19`), which returns a `std::string` built from `util::paths::user_cache_dir()`.

```tikz
\begin{document}
\definecolor{ncy}{HTML}{00E5FF}\definecolor{nmg}{HTML}{FF2BD6}\definecolor{ngr}{HTML}{39FF14}
\definecolor{nye}{HTML}{FFE600}\definecolor{nor}{HTML}{FF8A00}\definecolor{nvi}{HTML}{B388FF}
\tikzset{fcy/.style={draw=ncy,very thick,fill=ncy,fill opacity=0.22,text opacity=1},
 fmg/.style={draw=nmg,very thick,fill=nmg,fill opacity=0.22,text opacity=1},
 fgr/.style={draw=ngr,very thick,fill=ngr,fill opacity=0.22,text opacity=1},
 fye/.style={draw=nye,very thick,fill=nye,fill opacity=0.22,text opacity=1},
 fnor/.style={draw=nor,very thick,fill=nor,fill opacity=0.22,text opacity=1},
 arr/.style={draw=nvi,thick}}
\begin{tikzpicture}[font=\scriptsize,
  box/.style={rounded corners,minimum height=0.7cm,align=center,text opacity=1}]
  \node[box,fmg]  (a) at (0,1)   {open real file\\ \texttt{trunc}: now EMPTY};
  \node[box,fmg]  (b) at (4.6,1) {write data\\ crash here = short file};
  \draw[arr,-latex] (a)--(b);
  \node[box,fgr] (c) at (0,-0.2)  {write \texttt{.tmp}};
  \node[box,fgr] (d) at (3.2,-0.2) {check write ok};
  \node[box,fgr] (e) at (6.4,-0.2) {\texttt{rename} over real file\\ (one step)};
  \draw[arr,-latex] (c)--(d); \draw[arr,-latex] (d)--(e);
  \node[anchor=east] at (-1.8,1) {now};
  \node[anchor=east] at (-1.8,-0.2) {fix};
\end{tikzpicture}
\end{document}
```

### Options

| Option | Behavior | Note |
|--------|----------|------|
| A. temp file + rename | the real file is always the whole old or whole new version | recommended |
| B. A plus also save after the first full frame | a later crash still keeps the cache | one more call site in the frame loop |
| C. do nothing | worst case: one slow first start after a crash | the driver rejects a bad blob, so this is low risk |

### Option A: shape of the change

```cpp title:"Device.cpp, savePipelineCache(): shape of the change" ln:true
const std::string path = pipeline_cache_path();
const std::string tmp  = path + ".tmp";
{
    std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
    if (!out) return;
    out.write(data.data(), static_cast<std::streamsize>(size));
    if (!out) return;  // do not publish a short file
}
std::error_code ec;
std::filesystem::rename(tmp, path, ec);  // atomic on the same filesystem
```

Add `<filesystem>` and `<system_error>`. `<fstream>` is already included.

### E5 checklist

- [ ] Pick A / B / C
- [ ] Find the cache file (print `pipeline_cache_path()` once, or look in `user_cache_dir()`)
- [ ] Delete it, launch, quit cleanly: the file exists and no `.tmp` is left
- [ ] Launch again: startup is faster than the first
- [ ] Add `*.tmp` to `.gitignore` only if one ever shows up in `git status`

---

## Gates (after every item you apply)

Kill the running app before rebuilding.

```bash
make build
make debug-build      # builds the debug-UI config without launching
make test
make test-debug
make format-check
make check
```

- [ ] `make format-check` clean
- [ ] `make test` and `make test-debug` green (390/390 earlier)
- [ ] `make check` clean
- [ ] If you changed a shader: the `.spv` golden fails first, then is regenerated on purpose, with a changelog entry
- [ ] Release output unchanged where you did not intend a change (debug-only code stays under `#ifdef SWISH_DEBUG_UI`)

## Changelog

File: `docs/changelog/2026-11.md` (create it if absent), then `python3 scripts/changelog_index.py`. Format from `CLAUDE.md`: dated heading, a one-line past-tense summary, a collapsible technical summary with root cause, the fix with `[path](path)` links, and a file-change table.


### 2026-11-01 — Renderer audit fixes

Fixed a hang on quit while minimized, made camera ownership explicit and guarded renderer teardown.

<details><summary>Technical summary</summary>

**Root cause.** ... **Fix.** ... (links to the files)

| File | Change |
|------|--------|
| `src/renderer/Renderer/Renderer.cpp` | ... |

</details>


- [ ] One entry covering R1-R6 now (they are already done)
- [ ] A separate entry for each E item you ship, so a shader change is easy to find
- [ ] Add to `tasks/lessons.md`: a getter must not transfer ownership, and do not assume the branch: check `git branch --show-current` first
- [ ] Commit only when you ask. Never push unprompted.

## Open questions for you

1. After the three timings, which E1 option?
2. E2: are you willing to restructure `drawFrame` (Option B), or is a measurement plus Option A/D enough?
3. E3: throw, skip, or log-then-skip on an acquire timeout?
4. E4: debug-only or release too?
