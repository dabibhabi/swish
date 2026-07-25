# Performance and Latency Audit — Moderator's Ruling

Date: 2026-07-25  
Scope: CPU submission, GPU frame time, synchronization, and scalability. This is a code audit, not
a profiler trace; all performance impact statements should be validated on target hardware.

## Moderator's ruling

The renderer appears more likely to be fill-rate and draw-submission bound than synchronization
bound. Release 2x SSAA multiplies internal pixel work by roughly four, while the scene repeats many
road draws in the G-buffer and each shadow cascade. Full-screen SSR, god-rays, and deferred lighting
add substantial work at the enlarged internal resolution. This is not an algorithmic mystery. It is
the GPU being asked to do expensive work repeatedly and then being blamed for having a frame time.

The present two-frame fence-reuse design is safe. Stop pointing at the fence wait before collecting
evidence: reducing expensive work is much more likely to improve both frame time and input latency
than “optimizing” correct synchronization into a race.

## The expensive nonsense to measure first

### P0 — 2x internal render scale

`PostProcessManager::kRenderScale` in
[`src/renderer/PostProcessManager/PostProcessManager.h`](../../src/renderer/PostProcessManager/PostProcessManager.h)
is 2.0. G-buffer attachments, depth, deferred lighting, SSR, and much of the forward work therefore
operate at four times the swapchain pixel count.

First measurement: compare otherwise identical release captures at 1.0 and 2.0 scale, recording
GPU frame time, VRAM use, and image-quality differences. Four times the pixels means four times the
opportunity to waste bandwidth; do not call 2x “free quality” without the numbers. TAA is already
present for debug evaluation, so a future temporal-AA decision can be made against data rather than
assuming SSAA is affordable.

### P0 — Road draw multiplication

Road geometry is emitted as thousands of small indexed draws. Treadmill chunk instances are replayed
for the G-buffer and across the three CSM shadow cascades. `SceneGeometry::record_draws` in
[`src/renderer/SceneGeometry/SceneGeometry.cpp`](../../src/renderer/SceneGeometry/SceneGeometry.cpp)
also binds descriptor state per draw, magnifying CPU/driver overhead.

Measure actual submitted and culled draws from the existing debug counters, then capture a driving
frame in RenderDoc. If this is dominant, batch by material and investigate merged meshes, indirect
draws, or GPU culling before optimizing a `vec3` multiplication and writing a victory post about it.

### P0 — Always-on full-screen cost

The following paths should be timed independently:

| Pass | Location | Cost concern |
|---|---|---|
| SSR | [`shaders/ssr.frag`](../../shaders/ssr.frag) | Full-resolution, multi-step ray march |
| God-rays | [`shaders/godrays.frag`](../../shaders/godrays.frag) | Dozens of samples per half-resolution pixel |
| Deferred lighting | [`shaders/lighting.frag`](../../shaders/lighting.frag) | Cook-Torrance lighting, point lights, and manual PCF |

Add timestamp queries around each `record*Pass` in `Renderer::recordCommandBuffer`; read results on a
later frame to avoid introducing a measurement stall. Use the debug toggles only as a first
isolation tool, then confirm with timestamps.

## The secondary problems you still should not ignore

### Per-frame allocations and CPU work

`CarEntity::get_draw_calls`, windshield/glass draw-call production, treadmill offset vectors, and
point-light ordering create or process container data every frame. Reuse member vectors with
appropriate capacity and profile first; this is a frame-pacing improvement candidate rather than a
guaranteed top-frame-time win.

`CameraUniforms::update` selects nearby lights with `partial_sort`. Its cost grows with lamp count,
so retain a count in profiling output as road-side density grows.

### HDR copies and layout transitions

The main command buffer repeatedly transitions HDR images between attachment and sampled layouts for
screen-space effects. The windshield path also performs a full image snapshot copy. Barriers are
necessary for correctness, but the sequence can create bandwidth pressure and reduce overlap.

Use a RenderDoc frame capture to count HDR transitions and inspect the windshield copy. Only merge
passes or change layouts after confirming an actual bubble or bandwidth cost.

### IBL rebake hitch

`IBLManager::bake` uses queue-idle waits before rewriting environment maps. This is reasonable at
load time, but live weather or sun changes can cause a visible debug hitch. Time the rebake directly;
if it matters in the workflow, amortize faces/mips across frames or defer the visual update.

## Synchronization assessment

`Renderer::drawFrame` waits on the current frame fence before reuse, then acquires the swapchain
image. This correctly prevents writes to resources that remain in flight. Increasing frames in flight
may raise throughput in a GPU-bound case but can increase presentation/input latency and memory use.
Do not treat the fence wait as a correctness bug or change it before gathering frame-time evidence.
Higher frames in flight can trade a benchmark number for worse responsiveness, which is not a win in
a driving simulator.

## Performance triage guide

1. Establish a repeatable 30-second driving route and resolution.
2. Capture average, p1, and worst frame times for release and debug configurations.
3. Measure 1x versus 2x internal scale.
4. Time each major render pass with delayed-readback Vulkan timestamp queries.
5. Record submitted/cull-rejected draw counts in G-buffer and each shadow cascade.
6. Capture two frames in RenderDoc: spawn and a distant, lamp-dense road section.
7. Profile the app thread with `perf record -g` or an equivalent Linux profiler.
8. Repeat with rain, SSR, and god-rays independently disabled to establish the floor cost.

Rules for the capture:

1. Use the same route, weather, camera, resolution, and build mode for each comparison.
2. Change one cost source per run; a pile of disabled effects tells you nothing useful.
3. Read timestamp queries a frame later. A profiler that stalls the renderer is not measuring the
   renderer you intend to ship.
4. Keep both GPU time and CPU frame pacing. Better average FPS with worse p1 time is not a real fix.
5. Make a pass cheaper only after it appears in the budget. Guessing is for comment threads, not
   optimization.

## Do this in order

1. Measure scale, draw count, and per-pass GPU time.
2. Select an image-quality/performance policy for render scale.
3. Batch or cull road submissions if capture confirms command-recording pressure.
4. Reduce SSR/god-ray resolution or quality only if they appear in the frame-time budget.
5. Reuse hot-path vectors and cache per-frame camera inverses where profiling identifies CPU jitter.
6. Address HDR copy/layout churn and live IBL hitches only after the higher-impact items.
