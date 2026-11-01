---
status: active
updated: 2026-09-29
title: Modular agent platform + next wow toward UE-reference driving look
---

# Current user goal

## One-liner
Build Swish so Claude Code can ship features through **modular specialists**, while the product
keeps moving toward the **UE cinematic reference look** (glossy Porsche on a wet/dense road) without
breaking MoltenVK or release identity.

## Success looks like
1. Agents/skills/hooks in `.claude/` are the default way work gets done (right specialist, right
   docs, CHANGELOG + visual verify).
2. Next product wins (live list: `tasks/todo.md`; status verified 2026-09-29 against `tasks/todo.md`
   and `docs/adr/0001-*`):
   - ~~Clearcoat + flake hero-car paint~~ **shipped 2026-08-23**
   - ~~HDRI vs procedural IBL~~ **shipped 2026-08-21** (HDRI, procedural is the fallback)
   - ~~Per-object velocity buffer~~ **shipped 2026-08-23** (debug-gated; TAA is still debug-only, SSAA is the release default)
   - ~~Wheel Phase B (ride height / quarter-car)~~ **shipped 2026-08-15**; Phase C (slip/lockup visuals + rain-μ) still open
   - Endless-road Layer 5 polish (signage, lights, junction UX) still open
   - Decision pending: promote TAA to the release default over SSAA
3. Every non-trivial change is documented (CHANGELOG + docs when contracts change) and explained
   at **layman / technical / rigor** levels with reading pointers when I ask to understand.

## Reported issues to fix (2026-08-15 — from live screenshots)
> **Status 2026-09-29:** issues 1, 3, 5, 6 are recorded as fixed/shipped in `tasks/todo.md` (2026-08-15
> worklist); issue 2 (spray) has its shader half done with per-wheel emitters still open; issue 4 is
> mostly landed (squat/dive/roll deferred). The original report text is kept below for context;
> `tasks/todo.md` (and `tasks/archive/`) hold the outcome.

Work these with agents (right specialist per item). Each is debug-UI-tunable, release-safe, and
must be **visually verified** (screenshot at extremes) before it's called done.

1. **Exterior over-exposed — road & sky are blown out (washed-out pink/white).**
   The chase-cam shot shows the sky and asphalt near-white with crushed highlights and a strong
   pink cast. Auto-exposure / tonemap is biasing far too bright. Suspect auto-exposure target EV,
   metering weights, or AgX white point; check sky-luminance feeding the exposure loop.
   → `post-cinema-engineer` (auto-exposure + AgX) with `realism-features-engineer`; verify at
   day extremes.

2. **Tire spray reads as fog, not water.**
   Behind the car the road spray is a soft white cloud (looks like fog) instead of discrete droplets
   /mist kicked up from the tires. Wants particle-like water: tighter, directional, shorter-lived,
   grounded at the contact patch. → `rain-weather-engineer` (`SpraySystem` / `spray_sim.comp`,
   `spray.vert/frag`); verify with rain at 0 and 1.0.

3. **Cockpit sky looks different from exterior + no sun.**
   From inside the cabin the sky tone/exposure doesn't match the exterior view and the sun disc is
   missing. Reconcile the sky/IBL sampling and sun rendering so interior and exterior agree, and the
   sun is visible through the windshield. → `lighting-ibl-engineer` (sky/IBL) + check glass/windshield
   pass; coordinate exposure with issue 1.

4. **Porsche 911 Turbo dynamics not implemented (suspension, engine, drivetrain specs).**
   Add real 911 Turbo realism: quarter-car suspension (ride height / squat / dive / roll), an engine
   + drivetrain model (torque curve, gearing, redline/shift), and matching spec constants. Principled
   models over magic numbers (Chrono lessons). → `suspension-planner` → `suspension-implementer`, then
   `vehicle-physics-engineer` for engine/drivetrain; `visual-verifier` at the end. Sequence after or
   alongside Wheel Phase B in `tasks/todo.md`.

5. **Vehicle motion should *feel* realistic + a visual speed readout.**
   The way speed builds and carries should feel physical (acceleration/momentum/weight transfer, not a
   floaty ramp), tied to the engine/drivetrain model in issue 4. Add a **visual speed map/gauge** — an
   on-screen HUD (digital MPH/KMH + tach/needle, or reuse the cockpit gauge cluster) showing how fast
   the car is driving. → `vehicle-physics-engineer` for the motion feel (pair with F1 FOV-kick/shake in
   `tasks/todo.md`); HUD via a debug-UI/overlay pass with `debug-ui-engineer` + `renderer-engineer`.

6. **Realistic hydroplaning.**
   Above a speed/water-depth threshold the tires should lose grip on standing water — reduced steering
   authority + traction, a brief float/skate, recovery as speed drops or water clears. Couple to the
   wetness map / puddle mask and the tire-μ model (Phase C rain-μ tie-in). Principled model, tunable
   thresholds, verify at rain 0 and 1.0. → `vehicle-physics-engineer` + `rain-weather-engineer`;
   grounds in the Chrono wet-road lessons (`docs/lessons-from-chrono/`, file 07 `effective_mu()`).

## Constraints (non-negotiable)
- macOS + MoltenVK — **no ray tracing**; reflections = IBL + SSR only.
- Release build (`SWISH_DEBUG_UI=OFF`) stays byte-identical unless change is intentional + verified.
- Commit/push **only when I ask**.
- Units: `WORLD_SCALE = 1000`.
- Prefer principled models (Chrono lessons, research docs) over magic constants.

## Non-goals (for this CURRENT)
- Full city GI / Unreal parity in one jump
- Multiplayer / networking
- Shipping a second distinct vehicle model before traffic Path A (reuse Porsche GLB)

## Preferred specialists
- Product framing: `product-manager`
- Orchestration: `feature-orchestrator`
- Paint: `materials-brdf-engineer` + `shader-author`
- IBL: `lighting-ibl-engineer` (ask user before HDRI asset)
- Wheels (Phase C): `vehicle-physics-engineer` → `visual-verifier` (Phase B shipped; `suspension-*` agents are for it)
- Docs / teaching: `docs-scribe` + skill `explain-three-levels`

## Reading map (start here)
- [`CLAUDE.md`](../../CLAUDE.md) — project rules and build commands (no status claims)
- [`docs/REPO_MAP.md`](../REPO_MAP.md) · [`docs/RECIPES.md`](../RECIPES.md) — where things live, how to add things
- [`tasks/todo.md`](../../tasks/todo.md) — weighted wow table
- [`docs/learning-path.md`](../learning-path.md) — study questions + resources
- [`.claude/README.md`](../../.claude/README.md) — agent/skill roster
