# Phase 2 — Hydroplane spin-out

> **Path note (2026-09-29):** the `CarPhysics.h:NNN` and `App.cpp:NNN` citations below predate the deep-clean split.
> `CarPhysics.h` is now an umbrella over `CarParams.h`, `Powertrain.h`, `Tire.h`, `Drivetrain.h`, `Longitudinal.h`,
> `Bicycle.h`, `WetGrip.h` (`src/scene/Entity/`); `App.cpp` is split into `App.cpp` / `AppInit.cpp` / `AppInput.cpp`.
> Line numbers are historical; use symbols with `rg`.

**Owning agent:** `vehicle-physics-engineer`.
**Status:** design settled via grilling (2026-08-23), depends on Phase 1 landing first (same
tire/load-transfer math, extended to negative $v_x$).
**Diagrams:** [`docs/diagrams/phase2-spin-trigger-small.excalidraw`](../../docs/diagrams/phase2-spin-trigger-small.excalidraw) ·
[`docs/diagrams/phase2-spinout-dynamics-large.excalidraw`](../../docs/diagrams/phase2-spinout-dynamics-large.excalidraw).

## 1. Why this phase exists

The user wants the car to "actually spin from the hydroplane and move realistically." Today it
can't — the model was deliberately built to *prevent* that.

## 2. What we found

`CarPhysics.h`'s wet-traction model already computes something close to a real hydroplane response
— it just clamps the output before it can become a spin:

```cpp
// CarPhysics.h:275-277 — the clamp that makes spin-out impossible today
inline float max_yaw_rate(const CarParams& p, float vx, float mu) {
    return mu * p.gravity / std::max(vx, 1.0f);
}
```

```cpp
// tests/test_car_physics.cpp:628-649 — the clamp already collapses hard under hydroplane mu
// rMaxDry ≈ 0.432 rad/s (~24.7°/s)  →  rMaxPlane ≈ 0.031 rad/s (~1.8°/s)
// asserted: rMaxPlane < rMaxDry * 0.1
```

Meanwhile `dynamic_bicycle_deriv` (`CarPhysics.h:279-308`) already saturates lateral tire force at
$\mu \cdot F_z$ — the *real* physics for a spin is already sitting there. Every integration path
just clamps the yaw rate right back down to `max_yaw_rate()` before it can diverge
(`CarEntity.cpp:150-151`, comment at `CarPhysics.h:271-273`: "understeer plow at the edge" — a
deliberate design choice, made before this phase existed).

## 3. Decisions from grilling (with reasoning)

1. **Emergent, not scripted.** Relax `max_yaw_rate()`'s clamp under low $\mu$ and let the existing
   tire-saturated bicycle model diverge on its own — no separate "spin state" enum, no canned
   rotation animation. This is the same philosophy Phase 1 used for the 0-60 target (emergent from
   physics, not a state machine), applied consistently.
2. **Hydroplane-only trigger for this phase.** General dry-road oversteer-into-spin is real and
   would use the identical mechanism, but is explicitly deferred — Phase 2's tuning/testing surface
   stays scoped to the wet case the user actually asked for.
3. **Skill-based recovery, not scripted.** Because the mechanism is emergent, recovery is too: if
   the player countersteers/lifts throttle while $\mu$ is recovering (car exits the puddle, speed
   drops below the hydroplane threshold), `max_yaw_rate()`'s clamp re-engages naturally and catches
   the car — no separate recovery-window logic to write.
4. **No post-spin auto-reorientation.** The car ends up facing wherever the physics leaves it —
   backward, sideways, mid-lane, doesn't matter. See §4 for why this is safe.
5. **No new collision system.** The car is laterally clamped to the road width today
   (`CarEntity.cpp:166-167`, `m_min_x`/`m_max_x`) and has **no** guardrail/off-road collision at all.
   A spin stays inside that existing clamp. Real crash consequences were explicitly ruled out of
   scope — a substantially bigger, separate feature.
6. **No new VFX.** Reuse the existing GPU road-spray system (already reacts to wheel speed/wetness)
   rather than building dedicated skid-mark decals or smoke particles.
7. **Must validate the lateral model at negative $v_x$.** This is the one piece of real,
   non-optional engineering work: a spin will very plausibly pass $v_x$ through zero and negative as
   the car rotates, and nothing in `CarPhysics.h` has ever been checked there.

## 4. Why "just let it spin" doesn't break the rest of the engine

We fact-checked every system downstream of car heading/position before committing to "no reset,"
because an emergent spin could plausibly violate assumptions elsewhere:

| System | Assumption about car heading? | Verdict |
|---|---|---|
| `Treadmill.cpp` chunk streaming | None — `kCar`/`kStart`/`kEnd` are pure functions of `pos.z` (`Treadmill.cpp:61-63`), never car rotation | Tolerant, as long as the car doesn't drift backward past `kChunksBehind` (2 chunks, ~600 m) before recovering |
| Chase/cockpit camera | `fwd = car.get_forward()` drives the eye/target math directly (`App.cpp:601,608-613`) — arbitrary heading is numerically safe, just shows "the road behind" if spun 180° | Tolerant, no crash, no special-casing needed |
| Lateral position | Hard X-axis clamp only, no Z/heading check (`CarEntity.cpp:166-167`) | Already bounds the spin to the paved surface |
| Reverse-gear model | A **separate**, simplistic low-speed creep (`kMaxReverseSpeed` ≈ 27 mph, `CarEntity.h:217-218`) — unrelated to the lateral dynamics model | **Not** reused for spin-out; the dynamic bicycle model must handle negative $v_x$ on its own terms (§5) |

See `phase2-spinout-dynamics-large.excalidraw` for this as a diagram: relaxed clamp → bicycle model
→ (treadmill / camera / X-clamp, all tolerant) → recovery loop feeding back into the clamp.

## 5. The math: negative-$v_x$ validation

The friction-circle constraint itself is sign-agnostic:

$$
F_x^2 + F_y^2 \le (\mu F_z)^2
$$

but the *derived* quantities built on top of it in `CarPhysics.h` were written and tested only for
$v_x > 0$. Two concrete things to check/fix before relying on this during a spin:

1. **`max_yaw_rate(p, vx, mu) = mu * g / max(vx, 1.0)`** — the `max(vx, 1.0)` floor was written
   assuming $v_x \ge 0$; at negative $v_x$ this silently produces the *forward-speed-1* clamp instead
   of a sign-correct one. Needs to become `max(std::abs(vx), 1.0f)` at minimum, and the derivation
   re-checked for whether yaw-rate sign convention still matches steering input sign when $v_x < 0$.
2. **Slip-angle formulas in `dynamic_bicycle_deriv`** — slip angle is typically
   $\alpha = \arctan\!\big(\frac{v_y + \ell \cdot r}{v_x}\big)$ for some corner offset $\ell$; this
   blows up or flips sign incorrectly as $v_x \to 0$ and changes sign, which is exactly the regime a
   spin passes through. Needs either a small-$v_x$ regularization (blend toward a kinematic model
   below some speed floor, mirroring the existing kinematic/dynamic blend the codebase already uses
   at `CarEntity.cpp:135-152`) or an explicit sign-symmetric rewrite.

This is real, non-trivial verification work — not a formality — and should be done with dedicated
unit tests (extending `tests/test_car_physics.cpp`) before any visual spin-out testing, so a garbage
NaN/runaway-yaw result doesn't get mistaken for "the physics is just violent."

## 6. Tools & resources

- **`tests/test_car_physics.cpp`** — extend with negative-$v_x$ cases for `max_yaw_rate` and the
  slip-angle formulas before touching gameplay-visible behavior.
- **Visual verification:** per this project's convention, drive through a puddle at speed in
  `make debug`, screenshot at both wetness extremes (0 and 1.0), confirm the spin looks controllable
  at low wetness and genuinely loses grip at high wetness — "physically accurate" was the ask, so
  both ends of the range need checking, not just the dramatic one.
- **`docs/GLOSSARY.md:103`** — flagged during the earlier clearcoat/velocity-buffer grilling
  session as needing an update once related motion-vector work lands; unrelated to this phase's own
  changes but worth a glance if `vehicle-physics-engineer`'s changes touch the same area.

## 7. Open risks / follow-ups

- If the negative-$v_x$ fix reveals the dynamic bicycle model needs a more invasive rewrite than
  expected, that's a signal to pause and re-grill before proceeding — don't silently expand scope.
- General dry-road spin-out (not hydroplane-triggered) is a natural, cheap follow-on once this
  mechanism exists, but is out of scope here by design.

---

# AS-BUILT (2026-08-23)

**Shipped.** Tests 117 → **137** (20 new), both configs (`SWISH_DEBUG_UI` ON and OFF) clean,
visually verified at wetness 0 and 1.0. Zero debug scaffolding in release.

This section records what was actually built and **corrects the sections above where they turned
out to be wrong**. Read it in preference to §2-§5 where they disagree.

## A1. The spec's central premise is false: relaxing the clamp does nothing

§2/§3.1 assert that "the *real* physics for a spin is already sitting there" and that every
integration path "just clamps the yaw rate right back down". That was measured and it is **not**
the case. With **static** axle loads (which is what `axle_lateral_forces` used), when both axles
saturate at the same sign the yaw moment is

$$
a\,\mu F_{zf} - b\,\mu F_{zr}
= \mu\!\left(a\,\frac{mgb}{L} - b\,\frac{mga}{L}\right) = 0
$$

**identically**, because each axle's static load is proportional to the *opposite* lever. The
saturated bicycle model is therefore yaw-**neutral**, not unstable: it holds whatever yaw rate it
had and plows. Measured, hydroplane μ, 34 m/s, 10° of steer held for 6 s:

| configuration | peak yaw rate |
|---|---|
| clamp enforced (shipped, pre-Phase-2) | 4.0 °/s |
| clamp fully released, nothing else changed | **2.4 °/s** |

Releasing the clamp made it *lower*. No amount of relaxation can make a yaw-neutral model diverge.
`tests/test_car_physics.cpp` pins both the algebra and the observable consequence
("Static-load saturation gives IDENTICALLY zero yaw moment").

## A2. What was actually needed (four changes, all in `CarPhysics.h` + `CarEntity.cpp`)

1. **Close the friction circle in the longitudinal→lateral direction.** Each axle's lateral
   capacity becomes $\sqrt{(\mu F_z)^2 - F_x^2}$ with $F_z$ carrying longitudinal load transfer.
   That turns the degenerate moment above into

   $$
   a\,\mu F_{zf}(a_x) - b\,\mu F_{zr}(a_x) = -\mu\,m\,a_x\,h_{cg}
   $$

   which is non-zero whenever the car is accelerating or braking, and under braking it is an
   *oversteer* moment. This is exactly the extension Phase 1's own comment in
   `axle_lateral_forces` said Phase 2 would take on — so it was anticipated by the code even
   though the spec's narrative missed it. `longitudinal_accel` now optionally reports the signed
   **per-axle** longitudinal tire force (`AxleLongitudinal`), fed back one step later.
2. **The body-frame transport term** $\dot v_x = F_x/m + v_l r$, which was missing outright.
   Without it, rotation spuriously creates or destroys kinetic energy:
   $\frac{d}{dt}\frac{|V|^2}{2} = \frac{v_x F_x + v_l F_y}{m} - v_x v_l r$. With it the $r$ terms
   cancel, rotation alone does no work, and a spinning car keeps its momentum — which is what makes
   $v_x$ genuinely pass through zero and go negative as the car comes round.
3. **Hydroplane-gated release of the quasi-steady clamps.** `max_yaw_rate` is a *quasi-steady*
   bound: it assumes $\dot v_l = 0$ so that $a_y = v_x r$. The general statement
   $a_y = \dot v_l + v_x r \le \mu g$ is already enforced structurally (both axle forces are
   clamped at $\mu F_z$), so under a diverging sideslip the clamp is not physics — it is a
   numerical rail that happens to suppress the divergence. It is now blended toward a **kinematic
   pivot rail** $r \le V/\ell$ ($\ell = L/2$; ~28 rad/s at highway speed, i.e. a pure NaN guard),
   and the 45° sideslip cap $|v_l| \le |v_x|$ toward $|v_l| \le |V|$ (inert at full release).
4. **A rate limit on how fast those bounds may tighten** (`rate_limited_bound`). Found in a live
   run, not in theory: when the hydroplane ends, `planeFrac` falls to 0 within a frame or two and
   the bounds snap back, **deleting real momentum** — the body speed dropped 31.8 → 12.5 m/s in a
   single 0.5 s sample, ~4 g that no tire produced. A clamp may stop a state growing; it must not
   brake harder than friction can. The bound may now tighten at most at $\mu g$ (sideslip) or
   $2\mu m g a b/(L I_z)$ (yaw) per step. After the fix the same recovery decays at exactly
   $\mu g \approx 0.59\,g$, measured.

## A3. The gate: how decision 2 ("hydroplane-only") became structural rather than tuned

`effective_mu`'s own hydroplane smoothstep is hoisted into `hydroplane_fraction(speed, film, g)` —
**one** definition of "is the car planing". It is zero unless there is standing water deeper than
the tread can clear *and* the speed is past the NASA/Horne plane speed. **Every** Phase-2 term is
weighted by it, so dry roads, merely-wet roads and standing water taken slowly are provably
untouched, with no threshold invented for the purpose. Confirmed in-game: with the flood forced on
(`planeFrac = 1.00`, every Phase-2 term at full strength) but wetness 0, the car peaked at 5.6° of
sideslip during a full-lock input, returned to 0.0°, and accelerated cleanly to 163 mph.

Two related speed-argument fixes fell out of the same reasoning — both use the **body-speed
magnitude** $\sqrt{v_x^2+v_l^2}$ where the forward component was used before:

* `effective_mu`/`hydroplane_fraction`: hydroplaning depends on how fast the *tire* moves over the
  water. Keying off $v_x$ made a spinning car "stop hydroplaning" because its nose had rotated.
* `kDynamicBlendSpeed`: the threshold asks whether the dynamic tire model is well-conditioned. A car
  sliding at 39 m/s with only 4.7 m/s of that pointing forward was being handed to the *kinematic*
  model, which zeroes `m_lateral_velocity` — teleporting 37 m/s of lateral momentum out of existence
  mid-slide.

## A4. §5 negative-$v_x$ findings (the spec was partly out of date, and there was a live bug)

* **`max_yaw_rate` — §5.1 is stale.** The signature is `max_yaw_rate(vx, p)` (μ arrives via
  `p.tireMu`, not a third argument), and the floor was **already** `std::max(std::fabs(vx), 1.0f)`
  before Phase 2 — the `std::max(vx, 1.0f)` the spec quotes does not exist in the tree. Re-derived:
  the bound comes from $|a_y| = |v_x r| \le \mu g$, is symmetric in $r$, and therefore carries no
  sign convention to reconcile with steering input. No fix needed; a test now pins the symmetry and
  the value so it cannot regress.
* **Slip angles — §5.2 was right, and the bug was worse than "a regularization is missing".**
  Dividing by $\max(|v_x|, 1)$ was already correct (friction opposes the slip velocity whichever way
  the tire rolls), but the steer term was subtracted **unconditionally**. The wheel-frame expansion
  gives

  $$
  \alpha_f \approx \frac{v_l + a r}{|v_x|} - \operatorname{sgn}(v_x)\,\delta
  $$

  so at $v_x < 0$ steering produced a front lateral force in the **wrong direction** and a yaw
  moment opposite the kinematic reverse model. Measured at $v_x = -10$ m/s, $\delta = 0.2$ rad:
  −42.8° of heading in one second, i.e. exactly backwards.
  **This was a live, shipped bug, not a Phase-2-only concern:** reverse reaches
  `kMaxReverseSpeed` = 12 m/s, well above `kDynamicBlendSpeed` = 5 m/s, so the dynamic model has
  always been the one running in reverse. The `sgn(vx)` factor is applied **unconditionally**
  (not gated on `planeFrac`) because it is a bug fix, not a feature.
* **Three more places assumed $v_x \ge 0$** and would have silently eaten a spin:
  `kMaxReverseSpeed` was clamped unconditionally (it now lives on the driver-reverse branch, since a
  spun car's negative forward speed is real motion, not a reverse-gear command); the dead-stop snap
  and the `m_forward_speed == 0` early return both fired while the car was still sliding sideways at
  speed and would have **frozen the spin mid-rotation** (both now also require the lateral state to
  be quiet); and the brake's "don't drag the car backwards" sign-flip pin is suppressed while planing.

## A5. §4 tolerance table — RE-VERIFIED against the Phase-3a finite corridor

§4 was written against the retired endless treadmill. Corrected:

| System | §4 said | Actually, now |
|---|---|---|
| `Treadmill.cpp` chunk streaming | tolerant "as long as the car doesn't drift backward past `kChunksBehind` (~600 m)" | **Retired.** `CorridorDriver::update` picks its visible-segment window by camera-position bounding-sphere reject — heading- and direction-agnostic, and there is **no `kChunksBehind` limit at all**. Measured during a live spin: station ran *backwards* 0.366 → 0.356 mi (~16 m) with `segs=3` throughout and nothing amiss. **More** tolerant than before. |
| Chase/cockpit camera | tolerant, `get_forward()` is numerically safe | Unchanged and confirmed (`App.cpp:615-616`). Helicopter/Chase follow heading, so a slide reads as the road rotating under a car that stays axis-aligned in frame. |
| Lateral position | "hard X-axis clamp only (`CarEntity.cpp`, `m_min_x`/`m_max_x`)" | **Wrong now.** `CorridorDriver::place_at_start` sets `car.set_road_bounds(-1e9f, 1e9f)` — the world-X clamp is **disabled**. The bound is a corridor-space `(station, lateral)` clamp in `CorridorDriver::update` §3. Still bounds the spin, still touches only position (never heading / `m_yaw_rate` / `m_lateral_velocity`), so it is equally tolerant. Measured: `lat` pinned at exactly **+1900 WU** (`paint_inner + kLaneInset`) for a whole slide. |
| …and it is now **position-dependent** | not considered | Confirmed: `carriageway_bounds` reads `m_al->sample(s).lanes`, so the **outer** bound moves with the lane taper (the corridor runs 2→4 lanes). The **inner** (median) bound is lane-count independent. So a spin's available width genuinely varies along the route. |
| End-of-corridor turn-around | not considered | `carriageway_bounds` opens the **whole** width inside `in_turnaround(s)` (the two U-turn pads). A spin there can legally cross the centreline, after which the bound flips to the westbound window. Not a crash; just the widest place on the route to spin. |
| Reverse-gear model | "not reused for spin-out" | Correct, and now enforced — see the `kMaxReverseSpeed` fix in A4. |
| Wetness / puddles on the new corridor | not considered (blocker check requested) | **Working.** `App.cpp:442` still pushes `Renderer::get_wetness()` into the car; corridor road bands are emitted as `MAT_ASPHALT` (`CorridorScene.cpp:148-165`) which is what `SceneGeometry.cpp:162` turns into the `material.w` road tag that gates puddles in `lighting.frag`. Physics and shading also still agree about *where* the water is: both read render-frame XZ, and the corridor's origin rebase moves them together. Verified live: `film` reached 1.40, μ collapsed to 0.080. |

## A6. What it feels like, and what it does NOT do

Measured in the shipped configuration (heavy rain soaked to wetness ≈ 0.95, helicopter camera):

* **Sustained flooded stretch, cornering entry** — the definitive case. Enter at 37 m/s with
  ~6-8 °/s of yaw established on grip; μ collapses to 0.080; the yaw rate simply *persists* (there
  is no grip left to damp it) while momentum carries the car straight on. Sideslip runs
  2° → 54° → 88° → past 90°, $v_x$ 37 → 0 → **−34 m/s** (sliding backwards at 76 mph), body speed
  nearly conserved (39.7 → 31.6 m/s). Then grip returns and the slide scrubs off at $\mu g$.
* **The shipped patchy puddle field** gives a milder, ratcheting version: each ~0.2 s crossing adds
  sideslip, the dry gaps hold it, and β accumulates to 20-25° over several seconds. Very visible
  (the car crabs a quarter-turn out of line and throws spray from both rear wheels), but not a
  pirouette every time.
* **A straight-line hydroplane correctly does nothing.** With no yaw rate and no sideslip there is
  nothing for the lost grip to stop doing. There is a test for this, because the obvious way to get
  this feature wrong is to make low μ mean "random rotation".
* **No new VFX** (decision 6): the existing GPU road-spray reacts on its own — clearly visible as
  twin plumes off the sliding rear wheels.
* **No post-spin reorientation** (decision 4): runs ended with the car coasting *backwards* down the
  corridor at 11-14 m/s, or stopped against the median clamp facing wherever the physics left it.

## A7. Known limits / follow-ups (not done, deliberately)

1. **A full 360° spin is not reliably reachable in the shipped puddle field.** Every tire force
   scales with μ, so at μ = 0.08 the *largest possible* yaw acceleration is
   $2\mu m g a b/(L I_z) \approx 0.5$ rad/s² and the realizable destabilizing part is well under
   that. A spin needs 1-3 rad/s of yaw, i.e. seconds of sustained standing water. The visible spins
   above come from **yaw built on grip, then undamped by the water** — not from the water driving
   rotation. That is the correct phenomenology (hydroplaning makes you a passenger; the violent snap
   comes when grip returns *asymmetrically*), but the asymmetric part is unrepresentable here: the
   bicycle model has no left/right and `film_over_tread` samples one point.
2. **Candidate follow-ups, in order:** (a) a first-order lag on `filmOverTread` — the point sample
   under-represents a puddle relative to a 4.5 m car and a tread that needs several revolutions to
   evacuate water, and a lag would make the patchy field behave like the sustained one; (b) per-side
   water sampling, which is what a genuinely violent spin needs; (c) a combined-slip tire whose
   saturated force follows the slip-velocity *direction* — that would also fix A1's yaw-neutral
   degeneracy properly, instead of via load transfer.
3. **Pre-existing wart, out of scope, now documented:** because of A1's degeneracy the shipped model
   already lets a *merely wet* road at speed hold a steady ~8 °/s rotation with the wheel centred
   (both axles saturated ⇒ zero restoring moment). Phase 2 does not change this (`planeFrac` = 0
   there); the deferred dry-road spin phase should fix it, and the cheapest route is to un-gate the
   circle closure so load transfer breaks the degeneracy everywhere.
4. **Dry-road spin remains deferred** (decision 2), and the mechanism is now one line: drive
   `planeFrac` from tire saturation instead of the hydroplane gate. It was measured to work — and to
   make hard braking-while-steering on a dry road spin the car, which is why it stayed gated.
