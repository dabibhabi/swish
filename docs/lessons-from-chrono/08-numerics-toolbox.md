---
title: Lessons from Chrono 08 — the numerics toolbox
tags:
  - chrono
  - numerics
  - numerical-analysis
part: 8
status: complete
---

# 08 · The numerics toolbox: the transferable machinery

> [!abstract] Goal
> The numerical analysis that shows up *everywhere* in Chrono regardless of subsystem —
> interpolation, inversion, quadrature, smoothing, stiff integration — collected as a toolbox,
> each tool with its theory, its Chrono embodiment, and its swish use.

## 1. Piecewise-linear tables (`ChFunctionInterp`)

The workhorse behind torque maps, converter curves, spring tables. Two design points worth
copying:

- **Cached-iterator locality.** Lookups keep the last bracketing position (`m_last_greater`);
  physics queries are nearly-sequential in $x$ (RPM this step ≈ RPM last step), so amortized
  lookup is $O(1)$ instead of $O(\log n)$ — the numerics equivalent of swish's treadmill
  chunk reuse.
- **Extrapolation is a *policy*, not an accident.** Outside the table, `GetVal` continues
  linearly with the end slope; passing zero derivative gives clamp-to-constant
  (`ChFunctionInterp.cpp:49-57`). Compare the three domain policies met in this series:
  continue-by-slope (here), clamp ($q$ via `pn_max`, [02 §3](02-tire-models.md)), reflect
  (torque converter $R > 1$, [06 §1](06-driveline-powertrain-drivers.md)) — and the fourth,
  refuse (`ChCubicSpline`'s asserts, §2). Right answer depends on what consumes the value;
  wrong answer is *not choosing*.

## 2. Cubic splines by the Thomas algorithm (`ChCubicSpline`)

For smooth curves through data (a real torque curve, a camera path), the classic: solve for
second derivatives $y''_i$ from continuity of $y'$ at the knots, giving a tridiagonal system —
interior rows

$$
\frac{\Delta t^-}{6} y''_{i-1} + \frac{\Delta t^- + \Delta t^+}{3} y''_i + \frac{\Delta t^+}{6} y''_{i+1}
= \frac{y_{i+1} - y_i}{\Delta t^+} - \frac{y_i - y_{i-1}}{\Delta t^-}
$$

solved by one forward-elimination/back-substitution sweep with **no pivoting**
(`ChCubicSpline.cpp:114-123`). Why is pivot-free safe here? **Strict diagonal dominance**: each
diagonal $(\Delta t^- + \Delta t^+)/3$ exceeds the off-diagonal sum $(\Delta t^- + \Delta
t^+)/6$ — by a factor of 2, uniformly. Dominance ⇒ the elimination multipliers stay < 1 ⇒ no
growth ⇒ stability without row swaps, *and* invertibility (dominance implies nonsingularity via
Gershgorin). A proof you can carry in your head, and the standard example of "structure makes
the naive algorithm correct." Boundary conditions (natural / clamped slope / prescribed
curvature, independently per end, `:69-112`) and a strict no-extrapolation contract
(`assert(t >= m_t[0])`) complete it. Evaluation returns $y, y', y''$ together from one local
cubic — cheap derivatives for anything that needs slopes (a spline-based camera wants exactly
this).

## 3. Inverting a monotone map by tabulation (the `area2depth` trick)

Problem from tire-terrain contact: the ENVELOPE method measures the *area* $A$ of the
tire-ground overlap, but the force law wants penetration *depth* $\delta$. The forward map is
the circular-segment formula

$$
A(\delta) = \tfrac{1}{2} r^2 \big(\alpha - \sin\alpha\big), \qquad \alpha = 2\arccos(1 - \delta/r)
$$

— closed-form forward, no closed-form inverse. Chrono's move: since $A(\delta)$ is strictly
increasing, **tabulate the forward map and read the table backwards**:

```cpp
// examples/chrono/src/chrono_vehicle/wheeled_vehicle/ChTire.cpp:368-378 (ConstructAreaDepthTable)
double alpha = 2 * std::acos(1 - depth / r);
double area  = 0.5 * r * r * (alpha - std::sin(alpha));
area2depth.AddPoint(area, depth);      // note the argument order: x = area, y = depth
```

One line of insight — `AddPoint(f(x), x)` inverts $f$ — plus piecewise-linear interpolation
between samples. Error is $O(h^2)$ in the sample spacing, no Newton iteration in the hot loop,
works for *any* strictly monotone map. Swish's `steady_state_top_speed` bisection
(`CarPhysics.h:112-122`) solves the same shaped problem (invert a monotone scalar map) the
other standard way; know both — table when called per-frame, bisection when called once.

## 4. Quadrature on a singular integrand (and its honest error)

The same ENVELOPE method integrates penetration over the tire chord with a 100-point rectangle
rule (`ChTire.cpp:423-437`), integrand $\sqrt{r^2 - x^2}$-shaped — whose derivative blows up at
the chord ends $x = \pm r$. Rectangle-rule error analysis assumes a bounded derivative; with the
square-root singularity the global rate degrades from $O(h)$ to $O(h^{3/2})$-ish, concentrated
in the two end cells. Chrono's answer is engineering-honest: $n = 100$ makes the absolute error
irrelevant next to terrain-model error, so nobody reaches for the fancy fix (Gauss–Jacobi
weights or the substitution $x = r\sin u$ that removes the singularity exactly — the in-tree
`ChQuadrature` Gauss tables would serve). The lesson is the *reasoning order*: identify the
singularity, bound its damage, compare to model error, then decide whether the elegant fix earns
its complexity. Usually it doesn't.

## 5. The regularization bestiary (collected)

Every entry replaces something non-smooth with something computable — the recurring theme of
the series. Collected in one table, with where each earns its keep:

| Technique | Chrono site | Shape | Use when |
| :-------- | :---------- | :---- | :------- |
| Additive floor in denominator | tire `vta = R‖ω‖ + v_num` ([02 §7](02-tire-models.md)); Stanley `max(u, u_min)` ([06 §4](06-driveline-powertrain-drivers.md)) | exact at speed, biased near zero | ratios that blow up; consumer tolerates small-signal bias |
| `tanh` / sine-step in place of `sgn`/step | rolling resistance `tanh(ω)`; `ChFunctionSineStep` blends | $C^1$, bounded | the value feeds an integrator or the eye — chatter is the enemy |
| Model handoff (blend two *models*) | Dahl↔TMeasy over 1–3 m/s ([02 §4](02-tire-models.md)) | $C^1$ in the blend | the physics genuinely differs across the threshold |
| State-based (bristle ODE) | `CombinedCoulombForces` | smooth dynamics, hysteresis for free | stiction/memory effects matter |
| Tikhonov / compliance (cfm) | $-E$ block ([04 §7](04-solvers-and-metric-spaces.md)) | strictly convexifies | degenerate/redundant systems; the parameter has physical meaning |
| Under-relaxed projection | PSOR sharpness ([04 §4](04-solvers-and-metric-spaces.md)) | averaged operator | fixed-point iteration on a merely-nonexpansive map |
| **No regularization — solve the inclusion** | the NSC solver itself ([03](03-contact-as-measure-theory.md)) | exact non-smoothness | the discontinuity IS the physics and stiffness would explode |

The sine-step deserves its formula, since swish will want it (the hydroplane gate in
[07 §4](07-wet-roads-and-rain.md) uses its cubic cousin):

$$
y = y_1 + (y_2 - y_1)\left(\xi - \frac{\sin 2\pi\xi}{2\pi}\right),\qquad \xi = \frac{x - x_1}{x_2 - x_1}
$$

$y' \propto 1 - \cos 2\pi\xi$ vanishes at both ends → $C^1$ but **not** $C^2$ ($y''$ jumps at
the seams). The cubic smoothstep $3\xi^2 - 2\xi^3$ is the same class; the quintic
$6\xi^5 - 15\xi^4 + 10\xi^3$ buys $C^2$ when the output feeds something differentiating twice
(a camera position does; a friction coefficient doesn't).

## 6. Stiff ODEs: A-stability, and the two integrator tricks worth stealing

**A-stability in one paragraph.** Apply an integrator to the test equation $\dot y = \lambda y$,
$\mathrm{Re}\,\lambda < 0$. Explicit Euler gives $y_{n+1} = (1 + h\lambda) y_n$ — stable only
for $|1 + h\lambda| < 1$, i.e. $h < 2/|\lambda|$: the *fastest* mode dictates the step even when
you don't care about resolving it. Implicit (backward) Euler gives $y_{n+1} = y_n/(1 -
h\lambda)$ — stable for **every** $h$ (A-stable): stiff modes are *damped*, not resolved, and
the step is chosen by accuracy on the modes you care about. The trapezoidal rule is also
A-stable, second-order, but damps stiff modes only marginally ($|R(\infty)| = 1$) — which is
exactly why vehicle work prefers HHT (tunable damping $\alpha$) over trapezoidal, per the
[existing guide](../../examples/chrono/VEHICLE_DYNAMICS_GUIDE.md#core-dynamics-solver).

**Trick 1 — implicit made explicit-cost.** When the stiff ODE is *linear in the new state*,
backward Euler's implicit equation solves in closed form — the Dahl bristle
([02 §4](02-tire-models.md)) does this and gets unconditional stability for one division. Check
for this structure before reaching for a nonlinear solver; it's common (any
$\dot z = a(t) - b(t)\,z$ relaxation).

**Trick 2 — the stepper landscape as a decision, not a default.** Chrono ships
Euler-implicit-linearized (fast, 1st-order, one linear solve — the DVI default), HHT (2nd-order,
tunable damping — vehicle demos), and the Anitescu/Tasora DVI pair
([03 §2](03-contact-as-measure-theory.md)); the demos *choose per problem*. Swish-scale
translation of the same decision:

| swish subsystem | Stiff? | Right choice (and current state) |
| :-------------- | :----- | :------------------------------- |
| longitudinal + bicycle | no (smooth forces, clamps) | semi-implicit Euler at render dt — current, correct |
| wheel spin | no (kinematic) | exact update — current |
| Phase B suspension ($f_n$ up to slider max) | **yes, by slider** | semi-implicit Euler at **fixed** $h = 1/240$ s: stable iff $h < \tfrac{2}{\omega_n}\big(\sqrt{1+\zeta^2} - \zeta\big)$ — the planned design, bound already derived in [wheels 03 §2.3](../wheels/research/03-implementation-plan.md) |
| Dahl-style bristle (if Phase C ever wants stiction) | yes | closed-form BDF1 per Trick 1 — free stability |

The suspension row is the two-clock pattern from [01 §4](01-architecture.md): fixed substep
under variable render dt, sized by the *stiffest slider position*, not the default.

## 7. Bekker–Wong SCM, for the shelf

Not road-relevant, but the most self-contained "worked constitutive model" in the tree and the
natural reference if swish ever grows soft shoulders. Seventy lines
(`SCMTerrain.cpp:1364-1465`) implement: Bekker pressure–sinkage $\sigma = (K_c/b +
K_\phi)\,z^n$; Mohr–Coulomb shear cap $\tau_{\max} = c + \sigma\tan\phi$; Janosi–Hanamoto shear
mobilization $\tau = \tau_{\max}(1 - e^{-j/K_j})$ with the shear displacement $j$ *accumulated
over the contact's history* — and an elastic/plastic split where `sigma_yield` ratchets upward,
never down. That ratchet is a **one-sided hysteresis operator**: the terrain remembers every
rut (state that is a *functional of the path*, not the current position — the same BV-flavored
territory as [03 §1](03-contact-as-measure-theory.md), which is why plasticity and impact
theory share math). Also note the enforced well-posedness guard `elastic_K = max(elastic_K,
Bekker_Kphi)` (`SCMTerrain.cpp:163`) — a parameter *validated against another parameter* at set
time, a cheap pattern swish's debug-UI setters could copy wherever sliders can contradict each
other.

## 8. → swish, condensed

1. **Adopt the table type.** One `PiecewiseLinear` utility (sorted pairs, cached index,
   explicit extrapolation policy enum) unlocks: engine torque curves
   ([06 §5](06-driveline-powertrain-drivers.md)), TMeasy-lite landmark scheduling
   ([02 §8](02-tire-models.md)), wet-μ vs film curves ([07](07-wet-roads-and-rain.md)), and
   debug-UI-editable response curves — all currently blocked on "we only have constants."
   Pure header, trivially Catch2-testable, ~40 lines.
2. **The Phase B substep** is already correctly designed; §6 is its theory. The one addition
   worth making when it lands: a unit test that sets the sliders to max stiffness and asserts
   boundedness over 10 simulated seconds — testing the *stability bound*, not just the
   behavior (the wheels plan's risk 6 asks for exactly this).
3. **Choose smoothstep class by consumer** (§5): sine-step/cubic for forces and gates, quintic
   only where something differentiates twice. And keep the bestiary table handy — "which row
   am I in?" is the fastest code review question for any new `max()`, `tanh()`, or blend.

---

**Next:** [09 — the swish map](09-swish-map.md): every lesson, one table, priorities settled.
