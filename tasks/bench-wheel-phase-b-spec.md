# Benchmark task: Wheel Phase B (ride height + quarter-car suspension)

This is a **frozen task spec** — paste it verbatim as the opening prompt in both benchmark
sessions (`bench/dynamic-workflow` and `bench/manual-agents`) so the two orchestration
approaches attempt identical work. Do not paraphrase or add extra instructions when using it.

---

## Task prompt (paste everything below the line, verbatim)

---

Implement **Wheel Phase B** as specified in [docs/wheels/README.md](../docs/wheels/README.md)
(section "Phase B — ride height + suspension"). Summary of the spec:

**Scope:**
- Sample `road_surface_y` under the four hubs (Phase B's "raycast" is four O(1) evaluations of
  the existing crown formula in `RoadGeometry.cpp`).
- Per-corner spring targets → semi-implicit Euler `suspension_step` at a **fixed internal
  substep** h = 1/240 s. Stability bound: h < (2/ω_n)(sqrt(1+ζ²) − ζ), derived in
  `archive/docs/wheels/research/03-implementation-plan.md` §2.3.
- Defaults near f_n ≈ 1.4 Hz, ζ ≈ 0.45.
- Visual body heave/pitch/roll + per-wheel vertical travel composed **only into draw
  matrices** — never write `m_position` or `m_rotation` (must not fight the Y snap or ribbon
  follower).
- Springs relax to neutral while the ribbon follower owns the pose. Weight-transfer
  dive/squat/lean is explicitly **out of scope** for this phase (decided: ride-height first).

**Files likely touched:** `src/scene/Entity/WheelKinematics.h`, `src/scene/Entity/CarEntity.h/.cpp`
(a late `update_wheel_visuals(dt)` called from `src/core/App/App.cpp` after the treadmill),
`src/debug/DebugParams.h`, `src/debug/DebugUI.cpp` (k, ζ, travel sliders, gated
`#ifdef SWISH_DEBUG_UI`), `tests/test_car_physics.cpp`, `CHANGELOG.md`.

**Tests to add:**
- 29 mm crown-step response — overshoot consistent with ζ, settle within ≈ 4/(ζ·ω_n).
- Stability at h = 1/240 with max-slider stiffness.

**Acceptance criteria:**
1. Visible ≈1.1° crown lean while lane-keeping off the median; a lane change across the crown
   settles with one damped overshoot — no hard vertical snap, no ringing (verify via screenshot
   sequence).
2. Suspension never writes `m_position`/`m_rotation`. Existing physics tests stay untouched;
   behavior on ramps stays calm.
3. Slider extremes (max k, min ζ) stay bounded over a 10 s drive — no divergence.
4. `make test` stays green (currently 56/56, expect it to grow).
5. Release build (`SWISH_DEBUG_UI=OFF`) is byte-identical in behavior until this feature is
   intentionally shipped — i.e. gate any live-tunable UI, but the physical suspension effect
   itself is the one intentional visual change this phase ships (per
   `docs/wheels/README.md`'s staged-rollout convention).
6. Verify visually per `tasks/lessons.md`: window-only screenshots
   (`screencapture -R <window bounds>`, never full display), test both slider extremes, revert
   any temporary forced defaults before finishing.
7. Add a CHANGELOG.md entry per `CLAUDE.md`'s changelog rules (root cause/motivation, fix with
   file links, file-change table, LaTeX for the math, mermaid if it clarifies data flow).

Work on this task to completion: build, test, and visually verify before declaring it done.

---

## Benchmark bookkeeping (fill in per run, not part of the pasted prompt)

| | `bench/dynamic-workflow` | `bench/manual-agents` |
|---|---|---|
| Start timestamp | | |
| End timestamp | | |
| Wall-clock time | | |
| Token usage (in/out, cached) — via `/cost` | | |
| Tool-call / subagent-spawn count | | |
| `make test` result | | |
| Visual check result | | |
| `git diff --stat` vs `debug-ui` base | | |
| CHANGELOG entry present? | | |
| Unrequested commit/push? | | |

Record token usage with `/cost` at the end of each session (or your `ccusage`-equivalent) before
closing it — that number isn't recoverable after the session ends.
