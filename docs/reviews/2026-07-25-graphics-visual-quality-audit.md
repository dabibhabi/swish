# Graphics and Visual-Quality Audit — Moderator's Ruling

Date: 2026-07-25  
Scope: Vulkan renderer, shaders, post-processing, and the visual-tuning workflow. This is a
read-only review; findings are based on the current `debug-ui` branch and are not measurements.

## Moderator's ruling

Swish has a mature deferred foundation: cascaded sun shadows, sky-derived IBL, wet-road SSR,
screen-space rain, bloom, and a strong debug-tuning system. So no, the answer is not to slap
another random BRDF tweak onto a bare highway and declare it “AAA.” The largest gap to the
cinematic reference is lighting-energy balance, cinematic post-processing, atmospheric depth, and
environmental content working together.

The immediate work is boring in the correct way: rebalance sky/specular energy, make shipped sun
shadows actually visible, and improve the post stack already in the tree. Do not reopen the large
deferred GPU features just because a new idea sounds cool; the project has explicit decisions for
those in `CLAUDE.md`.

## Current pipeline and workflow

`Renderer::recordCommandBuffer` in
[`src/renderer/Renderer/Renderer.cpp`](../../src/renderer/Renderer/Renderer.cpp) coordinates a
shadow atlas, G-buffer, deferred lighting, SSR, god-rays, forward rain/glass/windshield rendering,
bloom, and final AgX composite. The debug build additionally exposes SSAO, DOF, TAA/motion blur,
and auto exposure.

The iteration workflow is a real strength:

- [`src/debug/DebugParams.h`](../../src/debug/DebugParams.h) centralizes live parameters.
- TOML presets and `DebugParamsIO` make experiments reproducible.
- `SWISH_DEBUG_UI` and shader `SP_*` defaults protect release behavior.
- [`tasks/lessons.md`](../../tasks/lessons.md) correctly requires visual verification rather than
  relying only on successful builds.

## The parts that need fixing

### P0 — Rebalance lighting energy

`shaders/lighting.frag` receives sky energy as diffuse irradiance, prefiltered specular IBL, and a
wet-sheen contribution. Those paths are not mutually budgeted, so glossy and wet surfaces can look
milky or over-bright.

Recommended direction: attenuate diffuse sky energy as specular Fresnel rises and bound wet sheen
inside the same energy budget. Do not “fix” a blown-out road by randomly lowering exposure until
the rest of the scene dies. Validate with dry and rain-at-1.0 captures before tuning constants. This
aligns with the existing reflection-versus-ambient item in
[`tasks/todo.md`](../../tasks/todo.md).

### P0 — Make the release shadow setting intentional

The tuned `shadowFloor = 1.0f` default in `DebugParams` drives the shader toward fully visible sun
lighting, which can visually nullify otherwise functional CSM shadows. Building CSM and then dialing
it out of the image is peak “why is this GPU hot?” behavior. Decide whether the intended release look
is truly shadow-free; if not, establish a calibrated floor and check it under both clear and wet
conditions. Do not retain expensive shadow work when its visual result is deliberately disabled.

### P1 — Promote and improve existing cinematic post systems

The project already has substantial implementations behind debug gating:

- `DofPass` uses a gather-based DOF path but is disabled by default.
- `TaaPass` supplies reprojection and a YCoCg clamp, while release still relies on 2x SSAA.
- SSAO contact darkening is a debug-only path; release primes a neutral AO texture.

DOF is the highest-impact cinematic cue in the project's reference material. Improve its aperture
shape before promoting it, then validate focus transitions in motion. Treat a release transition
for TAA or SSAO as a separate, benchmarked decision because release output and performance will
change.

### P1 — Improve atmosphere and bloom

Current haze and wet fog in `lighting.frag` establish distance, but they do not provide height-based
air density or sun-tinted airlight. A height-fog extension is a high-return addition to existing
shader infrastructure. Likewise, the current quarter-resolution, separable bloom is functional but
not lens-like; a Karis-style bloom pyramid plus restrained vignette/grain would improve highlight
roll-off and presentation.

### P2 — Increase the sense of place

`IBLManager` bakes the procedural sky into environment maps, which is coherent but cannot reflect
the buildings, trees, and visual clutter that make a black car feel placed in an environment. The
highest ceiling improvement is richer road-side content; a real HDR environment should only proceed
after the asset-versus-procedural-sky decision recorded in `CLAUDE.md`.

## Workflow and documentation gaps

The renderer has grown beyond the simplified pass descriptions in
[`docs/render-pipeline.md`](../render-pipeline.md). Some status claims across the pipeline,
realism, and backlog documents also differ from the active release/debug gates. Reconcile the
feature matrix before using documentation to prioritize work.

The visual-QA instructions are macOS-oriented. Add an equivalent Linux window-capture procedure so
the requirement to inspect actual output remains practical in the current development environment.

## Do this in order

1. Rebalance diffuse, specular IBL, and wet-sheen energy in `lighting.frag`.
2. Set and visually validate an intentional shipped CSM shadow floor.
3. Upgrade and validate the existing DOF path.
4. Add height fog/sun airlight, then improve bloom and final lens polish.
5. Evaluate TAA/SSAO release promotion against the measured SSAA cost.
6. Reconcile renderer documentation and the feature-status backlog.

## How not to cargo-cult the fix

1. Change one visual variable group at a time; do not mix BRDF, fog, exposure, and bloom changes.
2. Temporarily force an obvious extreme, capture the actual game window, and confirm the code path is
   visible before “tuning” it.
3. Return the temporary force to a deliberate value, then compare clear, rain-at-0, and rain-at-1.0.
4. Record the final parameter values in a preset or configuration—not only in an ImGui session.
5. Only promote a debug-only feature after it passes the release-output and performance checks.

## Validation checklist

- Capture clear and rain-at-0/rain-at-1.0 output at normal and deliberately extreme settings.
- Inspect road, paint, cabin, and shadow contact—not only the sky or a single frame.
- Record whether a recommendation changes debug-only behavior or release output.
- Use GPU pass timings before promoting debug post-processing to release.
