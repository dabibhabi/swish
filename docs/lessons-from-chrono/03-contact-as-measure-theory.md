---
title: Lessons from Chrono 03 — contact as measure theory
tags:
  - chrono
  - math
  - measure-theory
  - convex-analysis
part: 3
status: complete
---

# 03 · Contact as measure theory

> [!abstract] Goal
> The deepest math in the Chrono tree, made concrete. When bodies collide, velocity *jumps* —
> and a jumping velocity has no classical acceleration. Chrono's contact formulation takes that
> seriously instead of smoothing it away, and the trail of consequences runs through bounded
> variation, differential measures, convex cones, and one gorgeous closed-form projection. Every
> abstraction below is pinned to the line of code that embodies it.

## 1. Why impacts break calculus

**Layman's terms.** Drop a rigid ball on a rigid floor. The instant it lands, its velocity flips
from $-v$ to $+ev$ — a *jump*. Acceleration is the derivative of velocity, and a function with a
jump has no derivative there; the "force" at impact would need to be infinite for zero time.
Newton's $F = ma$, read classically, is simply false at that instant.

Engineering codes usually dodge this by making the floor slightly squishy (penalty springs — then
velocity is steep but continuous, and stiffness explodes) — the same move as the tire
regularizations in [02 §7](02-tire-models.md). Chrono's NSC ("non-smooth contacts") formulation
refuses the dodge: it keeps the jump, and upgrades the mathematical objects until the equation
makes sense again.

**The math.** The right function space for velocity is **BV**, functions of *bounded variation*:

$$
v \in BV([0,T];\mathbb{R}^n) \iff \sup_{\text{partitions}} \sum_i \lVert v(t_{i+1}) - v(t_i)\rVert < \infty
$$

BV functions are exactly the ones that can jump but not oscillate infinitely; they have left and
right limits $v^-(t), v^+(t)$ everywhere, and at most countably many jumps. The classical
derivative may not exist — but a BV function possesses a **differential measure** $dv$: the
unique (signed, vector-valued) Borel measure with

$$
\int_{(s,t]} dv = v^+(t) - v^+(s).
$$

By Lebesgue decomposition, $dv = \textcolor{green}{\dot v\,dt} + \textcolor{red}{\sum_i
[v]_i\,\delta_{t_i}} + (\text{singular-continuous part, zero in mechanics})$: an absolutely
continuous part — ordinary acceleration on smooth stretches — plus **Dirac atoms at the
impacts**, each weighted by the jump $[v]_i = v^+ - v^-$. "Acceleration" survives only as this
measure. The dynamics becomes a **measure differential equation/inclusion (MDI)** — equality of
measures, not of functions:

$$
\boxed{\ M\,dv = F(q, v, t)\,dt + C_q^\top\, d\lambda\ }
$$

where $d\lambda$ is likewise a measure: absolutely continuous where contacts push steadily
(forces), atomic at impacts (**impulses**). Percussions and finite forces coexist in one
equation because both are just parts of a measure.

**The code.** Chrono says this out loud, twice. The stepper stores "acceleration" as a
difference quotient — the discrete shadow of a measure, never a pointwise derivative:

```cpp
// examples/chrono/src/chrono/timestepper/ChTimestepperImplicit.cpp:429-430
integrable->StateScatterAcceleration(
    (Vnew - V) * (1 / dt));  // -> system auxiliary data (i.e acceleration as measure, fits DVI/MDI)
```

And the integrable interface warns you not to ask for classical derivatives:

```
// examples/chrono/src/chrono/timestepper/ChIntegrable.h:111-115 (doc comment)
"some solvers (ex in DVI) cannot compute a classical derivative dy/dt when v is a
function of bounded variation, and f or R are distributions (e.g. impulsive
contacts). In a DVI setting, one computes Dy, and returns Dy*(1/dt) ...
a DVI would compute impulses I, and return L = I*(1/dt)."
```

## 2. The time-stepper: integrate the measure, never differentiate

**Layman's terms.** If acceleration only exists as a measure, don't build your integrator on
acceleration. Build it on *impulse* — the measure of a whole timestep, $\int_{(t,t+h]} d\lambda$
— which is always a plain finite number whether the step contained smooth rolling, an impact, or
both. This is the Anitescu–Stewart–Trinkle move, and it's why Chrono's default stepper works at
all.

**The code** — the impulse-level linear system, verbatim from the stepper that Chrono's own
comment calls "the typical Anitescu stabilized timestepper for DVIs"
(`ChTimestepperImplicit.cpp:465-467`):

```cpp
// examples/chrono/src/chrono/timestepper/ChTimestepperImplicit.cpp:490-493
// becomes the Anitescu/Trinkle timestepper:
//
// [ M - dt*dF/dv - dt^2*dF/dx    Cq' ] [ v_new  ] = [ M*(v_old) + dt*f]
// [ Cq                           0   ] [ -dt*l  ] = [ -C/dt - Ct ]
```

Note what's being solved for: $v_{\text{new}}$ directly (never $\ddot q$), and $dt\cdot\lambda$ —
an impulse. The bookkeeping is explicit: `L *= dt; // because reactions = forces, here L =
impulses` (`:481`) on the way in, `L *= (1.0/dt)` (`:513`) on the way out. The `-C/dt` term on
the right is *constraint stabilization*: any existing penetration $C < 0$ is scheduled for
removal within one step, folded into the same solve. The `ChSystemDescriptor.h:54-56` notes
confirm the convention system-wide: with the Anitescu stepper, `f` holds $F\,dt$, `q` holds
delta-speeds, `b` holds the $C/dt$ term.

There is a second variant — "the Tasora stabilized timestepper for DVIs" (`:551-552`) — which
separates the two jobs: one velocity-impulse solve, then a *position projection* solving
$[M\ C_q^\top;\ C_q\ 0]\,[\delta q;\, -\lambda] = [0;\, -C]$, described in-code as "Position
stabilization (single Newton step on constraints, **with mass matrix as metric**)" (`:595`).
That phrase is doing real geometry — hold it until [04 §6](04-solvers-and-metric-spaces.md).

## 3. Friction as an inclusion, not a function

**Layman's terms.** Static friction is not a function of velocity. At zero sliding velocity the
friction force can be *anything* in a whole disc of values — whatever holds the box still, up to
$\mu F_n$. One input, many admissible outputs: mathematically that's a **set-valued map**, and
the equation of motion becomes an *inclusion* ("the force lies in this set") rather than an
equation.

**The math — the three convex objects.** Everything is built from the **Coulomb friction cone**,
one per contact:

$$
K = \left\{ (f_n, f_u, f_v) \in \mathbb{R}^3 \;:\; \sqrt{f_u^2 + f_v^2} \le \mu f_n \right\}
$$

a closed convex cone (check: closed under addition and positive scaling since $\mu f_n$ is
linear in $f_n$). Its **polar cone** is

$$
K^\circ = \{\, y : \langle x, y\rangle \le 0 \ \ \forall x \in K \,\}
= \left\{ (g_n, g_u, g_v) : \sqrt{g_u^2 + g_v^2} \le -\tfrac{1}{\mu} g_n \right\}
$$

— the downward cone with *reciprocal* opening angle (the polar of a wide cone is a narrow one;
they are orthogonal along their boundary generators). And the **normal cone** to $K$ at a point
$x \in K$:

$$
N_K(x) = \{\, y : \langle y,\, z - x\rangle \le 0 \ \ \forall z \in K \,\}
$$

— all directions that "point out of" $K$ at $x$: $\{0\}$ in the interior, a ray on a smooth
boundary point, fatter at the apex. The whole contact law compresses into one line, the **cone
complementarity condition**:

$$
K \ni \gamma \ \perp\ c \in N_K(\gamma)
$$

read: the impulse $\gamma$ lies in the cone, the relative contact velocity $c$ lies in the
normal cone at $\gamma$, and they are orthogonal. Unpack it and every case of contact behavior
falls out — separation ($\gamma = 0$, anything in $N_K(0) = K^\circ$ allowed for $c$: the bodies
may fly apart), sticking ($\gamma$ interior, so $N_K = \{0\}$, forcing $c = 0$: no sliding), and
sliding ($\gamma$ on the cone boundary, $c$ along the outward normal there — which is precisely
the *maximum dissipation principle*, friction opposing slip as hard as the cone allows).

**The code.** Chrono states the whole hierarchy in the system descriptor's header — linear
problems, LCPs, and CCPs as one family parameterized by which convex set $Y$ each multiplier
lives in:

```
// examples/chrono/src/chrono/solver/ChSystemDescriptor.h:32-49 (doxygen, trimmed)
The problem is described by a variational inequality VI(Z*x-d,K):
  | H -Cq'|*|q|- | f|= |0|
  | Cq -E | |l|  |-b|  |c|
with Y ∋ l ⊥ c ∈ N_y  (N_y = normal cone to Y)
 * Linear Problem:                          Y_i = R   (all bilateral)
 * Linear Complementarity Problem (LCP):    0 ≤ c ⊥ l ≥ 0   (Y_i = R+)
 * Cone Complementarity Problem (CCP):      Y ∋ l ⊥ c ∈ N_y (Y_i are friction cones)
```

A revolute joint, a one-way contact, and a friction cone are the *same object* to the solver —
only the set differs. That unification is the architectural payoff of doing the convex analysis
properly.

## 4. The projection: Moreau's decomposition in nine lines

**Layman's terms.** Every solver in [04](04-solvers-and-metric-spaces.md) needs one primitive:
take a trial impulse (possibly illegal — negative pressure, too much friction) and snap it to
the **nearest legal one**. "Nearest" makes this a metric projection onto the cone, and for the
friction cone the answer has a closed form with three cases: already legal → keep; so illegal
that pulling is the only interpretation → zero; otherwise → slide it orthogonally onto the cone's
surface.

<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 420" font-family="sans-serif">
  <defs>
    <marker id="pk" markerWidth="9" markerHeight="9" refX="7" refY="3" orient="auto">
      <path d="M0,0 L7,3 L0,6 Z" fill="#c0392b"/>
    </marker>
  </defs>
  <text x="320" y="26" text-anchor="middle" font-size="17" font-weight="bold" fill="#2e86c1">Projection onto the friction cone K — the three branches (cross-section)</text>
  <!-- axes -->
  <line x1="320" y1="380" x2="320" y2="60" stroke="#888" stroke-width="1.5"/>
  <line x1="60" y1="300" x2="580" y2="300" stroke="#888" stroke-width="1.5"/>
  <text x="330" y="72" font-size="13" fill="#888">f_n (normal impulse)</text>
  <text x="540" y="318" font-size="13" fill="#888">‖f_t‖</text>
  <!-- cone K: |ft| <= mu fn, mu ~ 0.8 => boundary lines from origin, slope fn = ft/mu -->
  <polygon points="320,300 470,80 170,80" fill="#1e8449" fill-opacity="0.18" stroke="#1e8449" stroke-width="2"/>
  <text x="320" y="140" text-anchor="middle" font-size="14" font-weight="bold" fill="#1e8449">inside K → untouched</text>
  <text x="320" y="158" text-anchor="middle" font-size="12" fill="#1e8449">(f_t² &lt; μ² f_n²)</text>
  <!-- polar cone K°: |gt| <= -gn/mu -> narrow downward -->
  <polygon points="320,300 380,376 260,376" fill="#b9770e" fill-opacity="0.22" stroke="#b9770e" stroke-width="2"/>
  <text x="320" y="400" text-anchor="middle" font-size="13" font-weight="bold" fill="#b9770e">inside K° → project to 0 (apex)</text>
  <!-- middle region label -->
  <text x="510" y="230" text-anchor="middle" font-size="14" font-weight="bold" fill="#c0392b">between → orthogonal</text>
  <text x="510" y="248" text-anchor="middle" font-size="14" font-weight="bold" fill="#c0392b">onto the generator</text>
  <!-- example projection arrow: point P=(490,250)-ish onto right generator (direction (150,-220)/norm) -->
  <circle cx="470" cy="255" r="4" fill="#c0392b"/>
  <text x="482" y="262" font-size="12" fill="#c0392b">γ</text>
  <line x1="470" y1="255" x2="416" y2="160" stroke="#c0392b" stroke-width="2" marker-end="url(#pk)"/>
  <circle cx="416" cy="160" r="4" fill="#1e8449"/>
  <text x="428" y="152" font-size="12" fill="#1e8449">P_K(γ)</text>
  <!-- polar-side example: point that maps to zero -->
  <circle cx="410" cy="355" r="4" fill="#b9770e"/>
  <line x1="410" y1="355" x2="326" y2="304" stroke="#b9770e" stroke-width="1.5" marker-end="url(#pk)"/>
  <!-- generators dashed continuation into lower half for polar-orthogonality intuition -->
  <line x1="320" y1="300" x2="200" y2="380" stroke="#1e8449" stroke-width="1" stroke-dasharray="4 4" opacity="0.5"/>
  <line x1="320" y1="300" x2="440" y2="380" stroke="#1e8449" stroke-width="1" stroke-dasharray="4 4" opacity="0.5"/>
</svg>

**The math.** The organizing theorem is **Moreau's decomposition**: for a closed convex cone $K$
in a Hilbert space, every $x$ splits *uniquely and orthogonally* into its projections onto the
cone and its polar,

$$
x = P_K(x) + P_{K^\circ}(x), \qquad \langle P_K(x),\, P_{K^\circ}(x)\rangle = 0
$$

— the conic generalization of "decompose into a subspace and its orthogonal complement" (for a
subspace $V$, $K^\circ = V^\perp$ and this *is* that theorem). The three regions in the picture
are exactly: $x \in K$ (polar part zero), $x \in K^\circ$ (cone part zero — project to the
apex), and the generic middle (both parts nonzero — project onto the boundary generator). For
the middle case, minimize $\lVert (f_n, f_t) - (\hat f_n, \hat f_t)\rVert^2$ subject to $\hat f_t
= \mu \hat f_n$ (2-D, by rotational symmetry around the cone axis): a one-variable least squares
whose solution is

$$
\hat f_n = \frac{\mu\, f_t + f_n}{\mu^2 + 1}, \qquad \hat f_t = \mu \hat f_n,
$$

with the tangential *direction* preserved.

**The code** — all of it, and it is exactly the theorem:

```cpp
// examples/chrono/src/chrono/solver/ChConstraintContactNormal.cpp:51-95 (trimmed)
// Anitescu-Tasora projection on cone generator and polar cone
// (contractive, but performs correction on three components: normal,u,v)

double f_n = l_i + cohesion;
double mu2 = friction * friction;
double f_n2 = f_n * f_n;
double f_t2 = (f_v * f_v + f_u * f_u);

// inside lower cone or close to origin? reset normal, u, v to zero!
if ((f_n <= 0 && f_t2 < f_n2 / mu2) || (f_n < 1e-14 && f_n > -1e-14)) {
    this->SetLagrangeMultiplier(0);
    constraint_U->SetLagrangeMultiplier(0);
    constraint_V->SetLagrangeMultiplier(0);
    return;
}
// inside upper cone? keep untouched!
if (f_t2 < f_n2 * mu2)
    return;

// project orthogonally to generator segment of upper cone
double f_t = std::sqrt(f_t2);
double f_n_proj = (f_t * friction + f_n) / (mu2 + 1);
double f_t_proj = f_n_proj * friction;
double tproj_div_t = f_t_proj / f_t;
```

Three details reward a close read:

- **`f_t2 < f_n2 / mu2` is the polar-cone test** — reciprocal opening angle, exactly $K^\circ$
  from §3. The code and the convex analysis are the same text in two languages.
- **`cohesion` translates the cone apex** along the normal axis ($f_n = l_i + c$, subtracted
  back at `:92`) — sticky contacts are the *same* cone, shifted. A one-line generalization that
  costs nothing because the theory is affine-equivariant.
- **The `1e-14` apex deadband** is a confession: $P_K$ is well-defined *everywhere* (projections
  onto closed convex sets are unique), but at the apex it is not differentiable and the
  *direction* of $P_K(x)$ is exquisitely sensitive to $x$ — nearby floats on either side of the
  polar boundary map to wildly different rays. The deadband trades a measure-zero sliver of
  exactness for numerical calm. Real analysis you can see: continuity everywhere,
  $C^1$-smoothness *except* on the boundary rays and the apex — and the code special-cases
  precisely the non-smooth set.

## 5. What the projection is *not*: two honest limitations

**No stick/slip μ distinction.** (Verified 2026-08-05 — grep + call sites.) The NSC contact path
consumes exactly one friction coefficient:

```cpp
// examples/chrono/src/chrono/physics/ChContactNSC.cpp:53
Nx.SetFrictionCoefficient(mat.static_friction);
```

`sliding_friction` exists on the material struct, is combined, serialized, written to CSV — and
never reaches a force computation. Chrono's NSC friction is a single-μ, rate-independent
Coulomb cone; any static-vs-kinetic or Stribeck behavior must come from a *tire model* on top
([02 §4](02-tire-models.md)'s bristles do exactly this). Lesson: a materials API can promise
more than the solver consumes — trust the call sites, not the struct.

**Rolling resistance breaks separability.** The rolling/spinning constraint's projection
(`ChConstraintRollingNormal.cpp:46-129`) runs *two more* cone projections against the **same**
normal multiplier the sliding projection just set — and writes it back. So the full
per-contact projection is not a clean product of independent projections; two blocks fight over
$f_n$. The in-code comment even flags part of it as "approximate." This quietly weakens the
convergence assumptions of everything in [04](04-solvers-and-metric-spaces.md) — and is why the
docs warn that nonzero rolling friction makes simulations "2x slower" (`ChContactMaterialNSC.h:
24-31`): the iteration has to reconcile the tug-of-war.

## 6. Restitution: a velocity-bias hack, labeled honestly

A proper Moreau impact law would treat bounce inside the measure framework (an atomic impulse
satisfying a restitution inclusion). Chrono instead injects Newton restitution as a bias on the
CCP right-hand side: if the pre-impact approach speed is large enough and the contact will
still be closed next step, add $e\cdot v_{\text{approach}}$ to the target normal velocity
(`ChContactNSC.cpp:111-146`, gated by `min_rebounce_speed`). It works, it's cheap, and it's
*not* the measure-theoretic impact law — the target velocity is set per-contact, ignoring
simultaneous-impact coupling (the Newton's-cradle failure case). A good example of a code
knowing exactly which corner it cut.

## 7. Nonexpansive, not contractive — a metric-space postscript

The code comment at `ChConstraintContactNormal.cpp:51` calls the projection "contractive." In
metric-space language it is actually **firmly nonexpansive**:

$$
\lVert P_K(x) - P_K(y)\rVert^2 \;\le\; \langle P_K(x) - P_K(y),\, x - y\rangle
\;\Rightarrow\; \lVert P_K(x) - P_K(y)\rVert \le \lVert x - y\rVert
$$

(1-Lipschitz — never expands distances — but with Lipschitz constant exactly 1, not $< 1$, so
Banach's fixed-point theorem does **not** apply and no geometric convergence rate follows). This
is not pedantry; it is the precise reason the solvers in [04](04-solvers-and-metric-spaces.md)
need their extra machinery — Nesterov momentum, spectral steps, under-relaxation — rather than
naive fixed-point iteration: with merely nonexpansive maps you get convergence only via averaged
iterations (Krasnosel'skiĭ–Mann) and only at sublinear rates. The distinction between
"contractive" and "nonexpansive" is exactly where the engineering pain lives.

## 8. → swish

Nothing in this chapter should be *built* in swish — that's a conclusion, not a gap
([09 §4](09-swish-map.md) makes the case). What transfers is smaller and sharper:

- **The friction circle is already yours.** `max_yaw_rate`'s clamp (`CarPhysics.h:75-77`) and
  the tire saturation at `:101-102` are 1-D shadows of $P_K$ — projections onto an interval.
  When Phase C couples longitudinal and lateral grip, the right primitive is the 2-ball
  projection from the Dahl combiner ([02 §4](02-tire-models.md)), which is $P_K$ for the
  fixed-$F_z$ slice of the cone. You already know its closed form.
- **Keep velocity-level thinking.** Swish's brake logic already has the sign-flip guard
  (`CarEntity.cpp:61-62`) — a tiny Moreau move: reasoning about the post-step velocity rather
  than integrating a force that would overshoot zero. That instinct scales.
- **When a quantity jumps, budget the jump, don't differentiate it.** The treadmill rebase and
  ribbon-follower pose overwrites are swish's "impacts" — state jumps outside the ODE. The MDI
  lesson is to make every downstream consumer (camera, spray velocity, future suspension) read
  *differences of state*, never finite-difference "accelerations" across such a frame.

---

**Next:** [04 — solvers and metric spaces](04-solvers-and-metric-spaces.md): now that every
iterate can be projected, how do you actually *solve* the CCP?
