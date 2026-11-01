# Phase 1 — Physics & speed accuracy (911 Turbo S retarget)

> **Path note (2026-09-29):** the `CarPhysics.h:NNN`, `App.cpp` and `src/utils/Smoothing.h` citations below predate the
> deep-clean. `CarPhysics.h` is now a 25-line umbrella over `CarParams.h`, `Powertrain.h`, `Tire.h`, `Drivetrain.h`,
> `Longitudinal.h`, `Bicycle.h`, `WetGrip.h` (`src/scene/Entity/`); `Smoothing.h` is `src/utils/math/Smoothing.h`;
> `App.cpp` is split into `App.cpp` / `AppInit.cpp` / `AppInput.cpp`. Line numbers are historical; use symbols with `rg`.

**Owning agent:** `vehicle-physics-engineer` (suspension curve derivation may be handed to
`suspension-planner` as a sub-step, per the project's existing Wheel Phase B split).
**Status:** **SHIPPED 2026-08-23.** Design settled via grilling the same day; implemented,
measured and verified — see [§8 As built](#8-as-built--measured-2026-08-23) for the numbers that
came out, and the two places the implementation deliberately departs from the text below.
**Diagrams:** [`docs/diagrams/phase1-awd-torque-small.excalidraw`](../../docs/diagrams/phase1-awd-torque-small.excalidraw) ·
[`docs/diagrams/phase1-physics-pipeline-large.excalidraw`](../../docs/diagrams/phase1-physics-pipeline-large.excalidraw)
(dark theme — open in [excalidraw.com](https://excalidraw.com) or the Excalidraw VS Code extension).

## 1. Why this phase exists

The user's complaint was "the car doesn't feel fast and doesn't hit its real 0-60 time." Before
touching any code we grilled the actual target: it turned out the car's *identity* itself was in
question.

## 2. What we found (facts, not opinions)

| Question | Finding | Source |
|---|---|---|
| What car is `CarPhysics.h` tuned for today? | 992 **GT3 Touring**, RWD, 6-speed manual, 1418 kg, 375 kW (503 hp) | `src/scene/Entity/CarPhysics.h:16-37` |
| What does `CLAUDE.md` call the car? | "911 Turbo" | `CLAUDE.md:6` |
| What does the actual GLB asset say? | User confirmed: it **is** a Turbo model — `CLAUDE.md`'s wording was simply wrong, not the asset itself | user, 2026-08-23 |
| What 0-60 does the current tuning hit? | 3.0-3.4 s asserted, 3.34 s measured | `tests/test_car_physics.cpp:146-165`, `tasks/todo.md:147-148` |
| Is the current transmission model manual or automatic? | Auto-shift with hysteresis + dwell (`tests/test_car_physics.cpp:46` "Auto-shift does not hunt") | `CarEntity.cpp` gearbox step |
| Does traction modeling support AWD today? | No — `rear_traction_cap_force()` caps the rear axle only | `CarPhysics.h:206-230` |

The auto-shift transmission model is a lucky break: it already matches a PDK-style dual-clutch
gearbox far better than the 6-speed-manual GT3 target it was originally written for. Retargeting to
the Turbo S doesn't fight the existing code — it aligns with it.

## 3. Decisions from grilling (with reasoning)

1. **Target car: 911 Turbo S, AWD.** Real reference spec locked in:
   - 0-60: **2.8 s** (independent-test consensus; Porsche's own factory claim is 2.6 s under ideal
     launch-control conditions — we're targeting the more commonly reproduced number)
   - Top speed: **~205 mph**
   - Power/torque: **641 hp / 590 lb-ft**
   - Curb weight: **~1670 kg** (DIN) — notably, these are the *exact* values `docs/wheels/phase-b-spec.md`
     says were in `CarParams` before the 2026-08-15 GT3 retarget, so this is largely a revert plus
     re-verification, not a from-scratch derivation.
   - **No visual asset changes.** The existing model is accepted as-is; asset generalization is a
     separate, future track.

2. **AWD via a slip-responsive PTU model, not a fixed split.** The real Turbo S's front axle
   engages reactively through a clutch-pack power-transfer unit, not a constant ratio. See
   [§4.1](#41-awd-torque-split) for the model.

3. **No electronic driver-aid state machine.** No launch-control RPM-hold, no PSM/PTM
   brake-intervention layer. The 2.8 s figure must **emerge** from the tire friction-circle +
   AWD-PTU model, the same way the current RWD traction-cap model is emergent today (not a scripted
   number). This keeps the physics surface area consistent with the existing design philosophy.

4. **Suspension: parametric camber/toe curves, not a runtime multibody solver.** The project has
   prior research into Project Chrono (a full constraint-based multibody engine) in
   `docs/lessons-from-chrono/`, but we deliberately did **not** reach for it here. Camber-gain and
   toe-gain as a function of suspension travel are derived *once, offline* from the real
   double-wishbone-front / multi-link-rear hardpoint geometry, then evaluated as a cheap per-frame
   curve lookup at runtime. Full constraint solving was considered and rejected as disproportionate
   cost for a single hero car.

5. **Bundled in:** FOV kick + camera shake (the already-tracked **F1** item in `tasks/todo.md`) —
   "feels fast" is as much a camera-presentation problem as a physics one.

6. **Explicitly split out:** engine audio. There is **zero** existing audio subsystem in this
   codebase (confirmed — no `OpenAL`/`miniaudio`/`SDL_mixer` reference anywhere in `src/` or
   `CMakeLists.txt`). RPM-synced engine sound is a genuinely separate body of work (library choice,
   mixing, asset sourcing) and is tracked as its own future item, not part of Phase 1.

## 4. The physics model

### 4.1 AWD torque split

The real Turbo S sends most torque to the rear axle at steady state, with the front axle engaging
through a clutch pack as rear-wheel slip or throttle demand rises. Modeled as a slip-responsive
blend rather than a fixed ratio:

$$
\tau_{\text{front}} = \tau_{\text{engine}} \cdot \Big( f_{\text{base}} + (1 - f_{\text{base}}) \cdot \text{clamp}\Big(\frac{s_{\text{rear}}}{s_{\text{max}}}, 0, 1\Big) \Big), \qquad \tau_{\text{rear}} = \tau_{\text{engine}} - \tau_{\text{front}}
$$

where $s_{\text{rear}}$ is rear-wheel slip ratio (or a throttle/launch proxy at low speed where slip
isn't yet measurable), $f_{\text{base}} \approx 0.05\text{–}0.1$ is the small always-present front
share, and $s_{\text{max}}$ is the slip level at which the PTU is considered fully engaged. Each
axle's delivered torque is still capped by that axle's own friction-circle limit (§4.2) — the split
determines *demand*, the tire model determines what's actually deliverable.

See the small diagram (`phase1-awd-torque-small.excalidraw`) for the flow: engine → PTU → rear
(primary) / front (reactive).

### 4.2 Tire friction circle (unchanged in kind, now applied per-axle for AWD)

$$
F_x^2 + F_y^2 \le (\mu \cdot F_z)^2
$$

This constraint already exists in `CarPhysics.h`'s dynamic bicycle model
(`dynamic_bicycle_deriv`, lines 279-308) for lateral force; Phase 1's work is applying the
longitudinal side of it per-axle instead of rear-only, with load transfer:

```cpp
// Illustrative — current rear-only shape (CarPhysics.h:206-230), to be generalized per-axle:
inline float rear_traction_cap_force(const CarParams& p, float ax_request) {
    const float Fz_rear = p.mass * p.gravity * (p.a / p.wheelbase)   // static + dynamic load transfer
                         + p.mass * ax_request * (p.hCg / p.wheelbase);
    const float axTraction = p.tireMu * p.gravity * (p.a / p.wheelbase)
                            / (1.0f - p.tireMu * p.hCg / p.wheelbase);
    return p.mass * axTraction;
}
```

Phase 1 extends this to compute `Fz_front`/`Fz_rear` under the same load-transfer physics, then
applies the AWD split from §4.1 against each axle's own $\mu \cdot F_z$ ceiling.

### 4.3 Suspension: parametric camber/toe curves

Rather than solving the double-wishbone/multi-link linkage as constrained rigid bodies every frame,
we derive gain curves once from the real hardpoint geometry (offline, e.g. in a small Python/Blender
script, similar in spirit to `tools/car_analyzer.py`), then fit and ship them as lookup curves:

$$
\gamma(\delta) = \gamma_0 + k_\gamma \cdot \delta + O(\delta^2), \qquad \tau(\delta) = \tau_0 + k_\tau \cdot \delta
$$

where $\delta$ is suspension travel from ride height (already tracked per-corner in
`WheelKinematics.h`'s `CornerSpring`), $\gamma$ is camber angle, $\tau$ is toe angle, and
$k_\gamma$/$k_\tau$ are the camber-gain/toe-gain constants read off the real geometry. This slots
directly into the existing per-corner suspension state:

```cpp
// WheelKinematics.h:40-43 today — CornerSpring has no orientation output yet:
struct CornerSpring {
    float x_m;    // travel from ride height, meters
    float v_mps;  // travel velocity
};
// Phase 1 adds a pure function of x_m — no new integration state:
// Vec2 camber_toe_from_travel(float x_m, const SuspensionGeometry& g);
```

Anti-roll bar coupling is added as a cross-corner correction term (left/right travel difference
feeding a small opposing force on each side), not a separate rigid body.

## 5. Full pipeline

See `phase1-physics-pipeline-large.excalidraw` for the complete per-frame flow: engine torque →
gearbox → AWD PTU split → tire friction circle → dynamic bicycle model → suspension (camber/toe +
ARB) → draw matrices → camera (FOV/shake).

## 6. Tools & resources

- **Existing test harness:** `tests/test_car_physics.cpp` already has the right *shape* of tests
  (launch acceleration, top speed, gear logic) — Phase 1 updates their asserted numbers to the Turbo
  S spec rather than writing a new harness.
- **`docs/lessons-from-chrono/`** — read for suspension theory background even though we're not
  using Chrono itself; the hardpoint/camber-gain derivation approach is informed by it.
- **`tools/car_analyzer.py`** pattern — the offline camber/toe curve-fitting script should follow
  this project's existing convention for small Python authoring tools (Blender-driven analysis of
  the GLB's actual suspension hardpoints, if present in the mesh; otherwise reasonable real-Turbo-S
  hardpoint coordinates from public suspension geometry).
- **Verification:** `make test` (ctest) with updated assertions, then window-only screenshots per
  this project's visual-verification convention — FOV kick/camera shake is something a human has to
  *see*, not just assert numerically.

## 7. Open risks / follow-ups

- The AWD friction-circle-per-axle change touches the same load-transfer math Phase 2 needs at
  negative $v_x$ — sequence Phase 1's axle-split work so Phase 2 doesn't have to re-derive it.
- Real Turbo S spring rates aren't sourced yet (only the GT3's `fnFrontHz=2.3`/`fnRearHz=2.5` exist
  today) — `vehicle-physics-engineer` should source or reasonably estimate Turbo S values rather than
  leaving the GT3's numbers in place under a Turbo S badge. **Closed** — see §8.

## 8. As built / measured (2026-08-23)

### Measured, not asserted

| Quantity | Reference | Measured | Where |
|---|---|---|---|
| 0-60 mph | 2.8 s | **2.758 s** (dt = 1/240, real stateful gearbox) | `tests/test_car_physics.cpp` |
| 0-100 mph | ~5.9 s (independent tests) | **5.904 s** | same harness |
| Top speed | ~205 mph | **205.1 mph** analytic; **203 mph and still climbing** in engine | `steady_state_top_speed()` / cockpit telemetry |
| rpm at Vmax | below 7200 redline | **6803** (power peak is 6750) | top-speed test |
| Launch accel | — | **10.301 m/s² = 1.050 g** | launch test |
| Peak braking | — | **1.064 g** | braking test |

The 0-60 is **1.5% quicker than the reference**, and that is reported rather than tuned away: the
model has no torque interruption across shifts, and there are two upshifts inside the sprint (a real
PDK loses ~50 ms each). Nothing in the code targets 2.8 s.

The sprint decomposes as: traction-limited (`mu*m*g` = 17.2 kN) from a standstill to 23.4 m/s, where
2nd gear's torque falls under the ceiling; then torque-limited through the 2nd→3rd shift; 60 mph
arrives in 3rd. 1st gear at the launch rpm makes 18.7 kN against the 17.2 kN ceiling — the margin is
thin *because of* the turbo hole, which is what stops the launch being a flat `mu*g` from t=0.

### Deviations from §4, with reasons

1. **§4.1's front-share formula saturates at 1.0**; the implementation caps it at
   `frontShareMax = 0.50`. Taken literally, `f_base + (1-f_base)·1` sends the *entire* engine
   torque to the front axle at full rear slip, which is both mechanically impossible for a clutch
   pack and worse than RWD once the front's own friction circle is applied (the rear would be given
   zero demand). A ceiling of 0.50 is the even-split figure quoted for the 911's PTU.
2. **`s_rear/s_max` is evaluated as rear-axle over-demand**, `(u-1)/lockSpan` with
   `u = F_demand,rear / (mu·F_z,rear)`. §4.1 explicitly permits a proxy where slip isn't measurable;
   this model has no wheel-inertia state anywhere, so slip ratio is never measurable, not just at low
   speed. Slip grows monotonically with excess drive torque past the friction peak, so the proxy is
   the same quantity up to a scale — and that scale is the one constant (`lockSpan = 0.30`).
3. **The PTU's lock fraction also gates cross-axle spill**, not just the demand split. This is what
   makes a closed clutch behave like a locked centre coupling (torque follows grip, ceiling = `mu·m·g`)
   and an open one behave like the old rear-only cap. Without it the split alone cannot produce an
   AWD launch. `rear_traction_cap_force()` is kept as the closed form this reduces to, with a test
   pinning the equivalence to 0.7%.
4. **Camber/toe gains are estimated from linkage-family behaviour, not digitised hardpoints.** §6
   suggested a Blender script over the GLB's suspension geometry — the GLB has none (wheels are rigid
   submeshes on loader-recovered pivot frames), and no public hardpoint table exists for the 992. The
   curve *shape* (§4.3's `γ₀ + k_γδ + O(δ²)`, linear toe) is implemented as written.
5. **FOV kick + camera shake (F1) was already shipped** on 2026-08-15 (`src/utils/math/Smoothing.h` +
   `App.cpp`'s cockpit block), so Phase 1 verified it rather than building it. `kMaxForwardSpeed`
   (the normaliser the speed term divides by) was *already* 92 000 WU ≈ 205 mph, i.e. already the
   Turbo S figure — so the retarget needed no camera retune.

### Turbo S ride rates (closes the §7 risk)

`fnFront = 1.8 Hz`, `ζ_front = 0.45`, `fnRear = 2.0 Hz`, `ζ_rear = 0.50`, travel ±45 mm, unsprung
42 kg/corner. Derived the same way `phase-b-spec.md` §1 derived the GT3's, but for a 1670 kg PASM
road car rather than a 1418 kg track car: ~1.35× softer springs, and `fn ∝ √k`. Yields 38.4 / 71.2
N/mm wheel rates. The §4 stability bound gives 114 ms / 98 ms against the 1/240 s substep — a
*larger* margin than the GT3 numbers had, so no substepping change. An anti-roll-bar test also
checks the bound in the stiffened roll mode.

### Sequenced for Phase 2

Load transfer, the friction circle and the per-axle decomposition are separate, individually-tested
functions — `axle_loads_long()`, `friction_circle_fx_max()`, `axle_lateral_forces()` — with the
lateral component already a first-class input. Phase 2 extends `axle_loads_long()` with the lateral
`ΔF_z` term (`phase-b-spec.md` §9) and validates the sign symmetry at negative `v_x`; it does not
re-derive any of this.
