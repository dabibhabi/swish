# Claude Code Prompt — Realistic Windshield Rain + Wiper (multi-agent)


```text
You are the ORCHESTRATOR for a graphics task in the Swish repo (a C++17 / Vulkan deferred
renderer). Your job is to fix the windshield rain effect so it looks realistic AND can be
wiped off, by coordinating a small team of subagents. Do NOT write feature code yourself
until the plan is approved — your role is to dispatch agents, merge their findings, and
keep everyone grounded in real evidence.

================================================================================
GROUND RULES (read first — these exist to save tokens and prevent hallucination)
================================================================================
1. SINGLE SOURCE OF TRUTH: The problem is already diagnosed in `issue.md` at the repo
   root. Every agent MUST read `issue.md` first and treat it as the spec. Do not
   re-derive the root causes; build on them.
2. EVIDENCE OR SILENCE: No agent may propose a change to a file/function/line it has not
   actually opened and read. Every claim about the codebase must cite a real path and
   line range. If unsure, read the file — do not guess Vulkan API usage or struct layouts.
3. SHARED ARTIFACTS, NOT CHAT: Agents communicate by writing short Markdown files under
   `docs/rain/` (create the folder). Keep each file tight (bullet points, code refs, no
   prose padding). Later agents READ earlier files instead of re-discovering. This is how
   we save tokens — knowledge is written once and reused.
4. NO SCOPE CREEP: Touch only what the plan requires. Match the repo conventions in
   `.cursorrules` and `CONTRIBUTING.md` (namespace `swish`, two-phase init/cleanup,
   `VK_CHECK`, `Vec4`-only UBOs, one class per folder, register passes through Renderer).
5. STOP-AND-CONFIRM: After the planning phase, present the merged plan and WAIT for my
   approval before implementing.

================================================================================
REFERENCE MATERIAL (agents should consult, then summarize — do not paste raw)
================================================================================
The `issue.md` "Resources" section already lists these. Treat them as the technique
canon. The single most important idea across ALL of them: drops are rendered by
REFRACTING the already-rendered scene through a drop normal/height field — NOT by adding
glowing geometry. Adopt that.

- Tatarchuk, "Artist-Directable Real-Time Rain Rendering in City Environments" (SIGGRAPH
  2006). Canonical: per-cell water mass -> dynamic normal map -> scene refraction +
  Fresnel; droplet merging/wetting trails.
  https://advances.realtimerendering.com/s2006/Tatarchuk-Rain.pdf
- Rousseau et al., "Realistic Real-Time Rain Rendering" (GPU rain), refraction via a
  precomputed wide-angle mask.
  https://classes.cs.uchicago.edu/archive/2022/fall/23700-1/papers/gpu-rain.pdf
- "Heartfelt" rain-on-glass (Martijn Steinrucken / BigWings): stick-slip drop motion
  (sawtooth), layered drop fields, normal from finite differences, mipmap blur.
  https://github.com/jeantimex/raindrop
- Godot "Rain on Glass": compact SDF drop + SCREEN_TEXTURE refraction + per-pixel blur.
  https://godotshaders.com/shader/rain-on-glass/
- olivierprat/rain-on-windshield (Unity HDRP): height-field rain on a real windshield
  WITH A WIPER and flow maps — the key reference for the wipe feature.
  https://github.com/olivierprat/rain-on-windshield
- Codrops "Rain & Water Effect Experiments" + Radiant "How to render rain on glass":
  the water-map + background-refraction two-pipeline intuition.

================================================================================
PHASE 1 — RESEARCH (run these subagents IN PARALLEL)
================================================================================
Dispatch the following agents at the same time. Each produces exactly one artifact file
and nothing else. Tell each agent: "Read issue.md first. Cite real lines. Be terse."

AGENT A — "codebase-cartographer" (read-only):
  Map the current rain/glass rendering path end to end. Output `docs/rain/codebase-map.md`:
    * How `WindshieldRainPass` is constructed, updated, recorded, recreated
      (`src/renderer/WindshieldRainPass/WindshieldRainPass.{h,cpp}`).
    * Current shaders `shaders/windshield_rain.{vert,frag}` — what each input/uniform does.
    * How the HDR scene color image + views are created and exposed
      (`src/renderer/PostProcessManager/PostProcessManager.{h,cpp}` — find the getter that
      `RainSystem` uses for the HDR depth view, and whether an equivalent color-view getter
      exists or must be added).
    * How `RainSystem` reads/uses the HDR image + inserts barriers (it is the closest
      working analog — `src/renderer/RainSystem/RainSystem.cpp`).
    * The windshield/glass tagging in `src/scene/ModelManager/ModelManager.cpp` and the
      draw-call plumbing in `Renderer.cpp` (`recordWindshieldRainPass`, `update_*_draw_calls`).
    * Exact integration points the rewrite will touch, each with file:line.

AGENT B — "refraction-technique" (read-only + web):
  Study the refraction-based drop technique from the references above. Output
  `docs/rain/technique-refraction.md`:
    * The minimal algorithm to render water-on-glass via scene refraction in a fragment
      shader (drop height field -> normal via finite differences -> offset SCREEN/HDR UV ->
      textureLod with blur driven by drop density).
    * Layered drop model for size variety + realistic density (target ~60-200 drops across
      the glass, multiple scales), tight body + Fresnel rim + small specular dot.
    * Stick-slip / sawtooth drop motion and trail thinning (from Heartfelt).
    * Concrete GLSL snippets adapted to GLSL 4.50 / Vulkan (sampler2D HDR input, no
      Godot/WebGL-isms). Note what must change vs the current additive frag.

AGENT C — "wiper-mechanic" (read-only + web):
  Design the windshield-wiper / rain-removal feature. Output `docs/rain/technique-wiper.md`:
    * How rain-on-glass demos implement wiping (persistent "wetness/water map" texture that
      a wiper clears, then rain re-wets over time — see olivierprat & Heartfelt wiper).
    * Recommend the Swish-appropriate approach: a ping-pong R8/R16F "wetness map" texture
      updated each frame (rain adds wetness; wiper sweep subtracts along an animated arc),
      sampled by the rain frag to mask drops. Specify texture format, where it lives
      (likely owned by `WindshieldRainPass`), and the per-frame update (a tiny compute or
      fullscreen pass, or CPU-driven uniform for a simple arc).
    * The control surface: a key to toggle/trigger the wiper (match the existing input
      pattern — `R` cycles rain today; pick an unused key and cite where input is handled).
    * Keep it as simple as possible while still reading as a real wiper sweep.

================================================================================
PHASE 2 — SYNTHESIS + PLAN (single agent, after A/B/C finish)
================================================================================
AGENT D — "architect":
  Read `issue.md` + all three `docs/rain/*.md` artifacts. Do NOT re-research. Produce ONE
  file `docs/rain/implementation-plan.md` containing:
    * Final design: data flow for (1) refraction-based drops and (2) the wiper wetness map,
      drawn as a short mermaid diagram and tied to the existing pass ordering
      (G-buffer -> lighting -> rain -> glass -> windshield rain -> bloom -> composite).
    * Exact file-by-file change list (create/modify), each with the specific functions and
      why. Must include:
        - New HDR-color sampler input to `WindshieldRainPass` (+ a `PostProcessManager`
          color-view getter if missing) and the required image barrier so the HDR scene is
          readable before the windshield rain pass.
        - Rewrite of `shaders/windshield_rain.frag` to refractive + layered drops; fix the
          vertex shader to use surface/glass-space UV (mesh `inUV`) instead of raw screen UV.
        - Pipeline change: additive -> alpha/refraction blend; enable BACK-face culling so
          the cabin-facing surface is not shaded.
        - `ModelManager.cpp`: restrict `isWindshield` to the OUTER FRONT pane only (exclude
          `WindowInside_Geo` and side windows).
        - Wetness-map texture + wiper update + input wiring.
        - `CMakeLists.txt` (new shaders/sources), and doc updates (`CHANGELOG.md`,
          `shaders/README.md`, `src/renderer/README.md`).
    * A correctness checklist mapped 1:1 to the two issues in `issue.md` (blobs fixed;
      no rain inside cabin) plus the wiper acceptance criteria.
    * A build/verify step (the repo builds with `cmake -B build && cmake --build build`;
      shaders compile via the `shaders` target).
  Then STOP and present the plan to me for approval.

================================================================================
PHASE 3 — IMPLEMENTATION (only after I approve the plan)
================================================================================
Execute `docs/rain/implementation-plan.md`. Prefer ONE focused implementation agent that
owns the cross-cutting Vulkan changes (pass + C++ + CMake) to avoid merge conflicts, and
optionally a second agent for the pure-GLSL shader work that the first integrates. Rules:
  * Follow the plan; if reality contradicts it, update the plan file first, then code.
  * After edits, build and compile shaders; fix any lint/build errors you introduce.
  * Keep diffs minimal and conventional. Update the docs listed in the plan.
  * Report back with: what changed (file list), how each `issue.md` defect is resolved,
    how to trigger the wiper, and any follow-ups.

================================================================================
DELIVERABLES RECAP
================================================================================
- docs/rain/codebase-map.md            (Agent A)
- docs/rain/technique-refraction.md    (Agent B)
- docs/rain/technique-wiper.md         (Agent C)
- docs/rain/implementation-plan.md     (Agent D) <- approval gate
- Implemented code + updated CHANGELOG/READMEs (Phase 3)

Begin with Phase 1: confirm you have read `issue.md`, then dispatch Agents A, B, and C in
parallel.
```
