---
title: Lessons from Chrono 04 — solvers and metric spaces
tags:
  - chrono
  - math
  - optimization
  - metric-spaces
part: 4
status: complete
---

# 04 · Solvers & metric spaces: making the cone problem converge

> [!abstract] Goal
> [03](03-contact-as-measure-theory.md) ended with a problem — find $\gamma \in K$ with
> $N\gamma - r \in -N_K(\gamma)$ — and a primitive, the nonexpansive projection $P_K$. This file
> is how Chrono actually solves it: four iterative solvers, each a different answer to "what
> metric should the iteration move in?" It's the most transferable *optimization* content in the
> tree, even though swish will never run any of it.

## 1. The problem, restated three equivalent ways

After Schur-complementing the velocities out of the Anitescu system ([03 §2](03-contact-as-measure-theory.md)),
what remains is a problem in the impulses $\gamma$ alone, with the **Delassus operator**
$N = C_q M^{-1} C_q^\top - E$ (assembled matrix-free in `ChSystemDescriptor.cpp:519-590`;
$-E$ is compliance, see §7):

1. **CCP:** $K \ni \gamma \perp (N\gamma - r) \in N_K(\gamma)$
2. **Convex QP over a cone:** $\min_{\gamma \in K}\ f(\gamma) = \tfrac12 \gamma^\top N \gamma - \gamma^\top r$
3. **Fixed point:** $\gamma = P_K\big(\gamma - t\,(N\gamma - r)\big)$ for any $t > 0$

The equivalences are the whole game. (1)→(2): the CCP is the first-order optimality condition of
the QP (the normal-cone condition *is* "no feasible descent direction"). (2)→(3): a constrained
minimizer is exactly a fixed point of the projected-gradient map — this is standard, but worth
proving once by unwinding definitions: $\gamma^\star$ minimizes iff $-\nabla f(\gamma^\star) \in
N_K(\gamma^\star)$ iff $\gamma^\star = P_K(\gamma^\star - t\nabla f(\gamma^\star))$, using the
projection's variational characterization $\langle x - P_K x,\, z - P_K x\rangle \le 0\ \forall
z \in K$.

Form (3) gives every solver its **convergence certificate**. Define the *natural residual*

$$
\rho_t(\gamma) = \frac{1}{t}\,\big\lVert \gamma - P_K\big(\gamma - t(N\gamma - r)\big)\big\rVert
$$

— the distance you move under one projected-gradient step, rescaled. $\rho_t(\gamma) = 0$ iff
$\gamma$ solves the problem, so $\rho$ is a computable "how unsolved is it" metric. Chrono uses
*literally this* as its stopping test:

```cpp
// archive/local/examples/chrono/src/chrono/solver/ChSolverAPGD.cpp:155-160
double gdiff = 1.0 / (nc * nc);
sysd.SchurComplementProduct(tmp, gammaNew);  // tmp = N * gammaNew
tmp = gammaNew - gdiff * (tmp - r);
sysd.ConstraintsProject(tmp);                // tmp = ProjectionOperator(gammaNew - gdiff * g)
tmp = (gammaNew - tmp) / gdiff;
double res = tmp.norm();
```

> [!note] Why the solvers exist at all
> $N$ is only positive **semi**definite (redundant contacts create a nullspace), $P_K$ is
> nonexpansive but not contractive ([03 §7](03-contact-as-measure-theory.md)), and $n_c$ is
> thousands. So: no direct solve, no Banach geometric rate, and every iteration must be
> matrix-free ($N v$ via two sparse products). Everything below is engineering *around* those
> three facts. A hard guard enforces the setting — APGD/BB/PSOR all throw unless the problem
> supports the Schur form (`ChSolverAPGD.cpp:31-36`), i.e. no FEA stiffness blocks; only ADMM
> (§5) tolerates those.

## 2. APGD — Nesterov acceleration under projection

**Layman's terms.** Plain projected gradient descent inches downhill, projecting each step back
into the cone; it converges at rate $O(1/k)$. Nesterov's trick — take the step from an
*extrapolated* point that overshoots along your recent direction of travel, like a ball with
momentum — improves that to $O(1/k^2)$ *without* any extra matrix products. APGD is that trick,
plus three safety systems: a backtracking line search (because nobody knows the Lipschitz
constant), a monotone-best memory (because accelerated methods oscillate), and a restart rule
(because momentum can point uphill).

**The math and the code, interleaved.** The objective's gradient $\nabla f = N\gamma - r$ is
$L$-Lipschitz with $L = \lambda_{\max}(N)$ — unknown. Chrono seeds it with one Rayleigh-quotient
sample, $L_0 = \lVert N(\gamma_0 - \hat\gamma_0)\rVert / \lVert\gamma_0 - \hat\gamma_0\rVert$
(`ChSolverAPGD.cpp:92-100`), then trusts backtracking. The main step is textbook:

```cpp
// archive/local/examples/chrono/src/chrono/solver/ChSolverAPGD.cpp:112-114
sysd.SchurComplementProduct(g, y);  // g = N * y
gammaNew = y - t * (g - r);
sysd.ConstraintsProject(gammaNew);
```

Backtracking tests the **descent lemma** (the quadratic upper bound
$f(\gamma^+) \le f(y) + \langle\nabla f(y), \gamma^+ - y\rangle + \tfrac{L}{2}\lVert\gamma^+ -
y\rVert^2$ that holds whenever the trial $L$ majorizes the true one) and doubles $L$ until it
passes (`:124-141`). Momentum follows the classic $\theta$-recursion:

```cpp
// archive/local/examples/chrono/src/chrono/solver/ChSolverAPGD.cpp:143-150
// (15) theta_(k+1) = (-theta_k^2 + theta_k * sqrt(theta_k^2 + 4)) / 2
thetaNew = (-theta * theta + theta * std::sqrt(theta * theta + 4.0)) / 2.0;
// (16) Beta_(k+1) = theta_k * (1 - theta_k) / (theta_k^2 + theta_(k+1))
Beta = theta * (1.0 - theta) / (theta * theta + thetaNew);
// (17) y_(k+1) = gamma_(k+1) + Beta_(k+1) * (gamma_(k+1) - gamma_k)
yNew = gammaNew + Beta * (gammaNew - gamma);
```

($\theta_{k+1}$ solves $\theta_{k+1}^2 = \theta_k^2(1 - \theta_{k+1})$ — the algebraic condition
that makes the $O(1/k^2)$ telescoping estimate go through.) Then the two safety systems:

- **Monotone-best memory:** accelerated methods are *not* descent methods — $f$ can rise for
  several iterations. Chrono tracks the best-residual iterate $\hat\gamma$ (`:162-165`) and, at
  the end, **returns $\hat\gamma$, not the final iterate** (`:196-197`). Easy to miss, and
  exactly right.
- **Adaptive restart:** when the momentum direction opposes the gradient
  ($\langle\nabla f,\ \gamma_{k+1}-\gamma_k\rangle > 0$), kill it — reset $y = \gamma$,
  $\theta = 1$ (`:171-174`). This is O'Donoghue–Candès gradient restarting, and it's what makes
  Nesterov robust on problems (like this one) that violate the clean convexity assumptions.
  Plus an optimistic $L \leftarrow 0.9L$ each outer iteration (`:176-180`) so the step size
  re-grows after conservative phases.

> [!warning] A bug-shaped detail, preserved for the record
> Inside the backtracking loop the re-projected trial is `gammaNew = y - t * g;`
> (`ChSolverAPGD.cpp:132`) — missing the `- r` that the main step at `:113` has (`g` holds
> $Ny$, not $Ny - r$). There's even a maintainer breadcrumb a few lines up: `//// RADU Check
> consistency (e.g. sign of 'r' in comments vs. code)` (`:102-103`). Consequences are bounded —
> the loop only sizes $L$, the accepted iterate comes from the correct outer step, monotone-best
> memory limits fallout, and backtracking still terminates ($L$-doubling forces the test true
> eventually) — but line-search steps computed during backtracking optimize a subtly different
> model than claimed. Lesson: when an algorithm has "the same formula" in two places, it
> doesn't; it has two formulas.

## 3. Barzilai–Borwein: the step length *is* a metric choice

**Layman's terms.** Gradient descent with a fixed step treats every direction of the problem as
equally curved — it moves in the *Euclidean* metric. The BB method instead asks, each iteration:
"judging by how the gradient just changed, what step would a perfectly-scaled problem have
taken?" and uses that. It's a poor man's Newton: one scalar of second-order information,
extracted from the last step, at zero extra cost.

**The math.** With $s = \gamma_{k} - \gamma_{k-1}$ and $y = \nabla f_k - \nabla f_{k-1}$
(so $y = Ns$ exactly, for our quadratic), the two BB steps are the least-squares solutions of
$\alpha^{-1} s \approx y$ and $s \approx \alpha y$:

$$
\alpha_{BB1} = \frac{\langle s, s\rangle_D}{\langle s, y\rangle}, \qquad
\alpha_{BB2} = \frac{\langle s, y\rangle}{\langle y, y\rangle_{D^{-1}}}
$$

(Chrono's variant weights by the block-diagonal preconditioner $D$ — the `mb_tmp` products at
`ChSolverBB.cpp:189-238`.) Both are Rayleigh-quotient estimates of curvature along $s$:
$\langle s, Ns\rangle / \langle s,s\rangle$ lies in $[\lambda_{\min}, \lambda_{\max}]$, so
$\alpha$ adapts to the *local* spectrum. The metric-space reading: BB is projected gradient in
an adaptively rescaled inner product — cheap online preconditioning.

Because $N$ is only PSD, $\langle s, y\rangle = \langle s, Ns\rangle$ can hit zero along
nullspace directions — and the code guards exactly that, falling back to fixed small steps:

```cpp
// archive/local/examples/chrono/src/chrono/solver/ChSolverBB.cpp (spectral step, even iterations)
double sDs = ms.dot(mb_tmp);  double sy = ms.dot(my);
if (sy <= 0) { alpha = neg_BB1_fallback; }               // = 0.11
else         { alpha = ChMin(a_max, ChMax(a_min, sDs / sy)); }
```

Since BB steps are wildly nonmonotone by design, they're paired with the
**Grippo–Lampariello–Lucidi nonmonotone Armijo** test — accept unless worse than the *max of the
last 10* objective values (`:145-182`, memory `n_armijo = 10`), with quadratic-interpolation
backtracking when it fails, plus an explicit rollback ("the method is not monotone and it is
quite 'noisy'" — their comment, `:247-253`). One more contrast worth having in your head: BB's
tolerance is **relative** (`maxd = lastgoodres / mg_p_init_norm`, `:258`), APGD's is
**absolute** — the same "converged" flag from two solvers doesn't mean the same thing.

## 4. PSOR — the default, and Mangasarian's relaxed projection

Chrono's default NSC solver (`ChSystemNSC.cpp:34`) is the humblest: projected Gauss–Seidel,
sweeping constraints one at a time,

$$
\Delta\lambda_i = -\frac{\omega}{g_i}\big( [C_q]_i\, q + b_i + \text{cfm}_i \lambda_i \big),
$$

immediately scattering each $\Delta\lambda_i$ into the velocities so later constraints in the
same sweep see it (`ChSolverPSOR.cpp:84-107` — friction triples update as a group, then project
once through the normal constraint). Two classical relaxations are exposed:

- **Over-relaxation** $\omega$: the SOR knob, default 1.0 ("a good value for Jacobi is 0.2" —
  `ChIterativeSolverVI.h:61-64`; Jacobi needs heavy damping because simultaneous updates
  double-count coupling).
- **Sharpness** $\theta$: an *under-relaxed projection*, $\lambda \leftarrow \theta
  P_K(\lambda) + (1-\theta)\lambda_{\text{old}}$ (`ChSolverPSOR.cpp:111-119`), citing
  Mangasarian's LCP iteration ("0.8…1.0; lower values improve accuracy but at the cost of
  slower convergence," `ChIterativeSolverVI.h:66-68`). Metric-space reading: replacing a
  nonexpansive map $T$ by its average $\theta T + (1-\theta)I$ makes it **averaged**, and
  Krasnosel'skiĭ–Mann says averaged maps converge where plain nonexpansive iteration may orbit
  — this is precisely the fix for the not-actually-contractive projection of
  [03 §7](03-contact-as-measure-theory.md).

Why is a $O(1/k)$-at-best sweep the *default*? Because per-iteration it touches each constraint
once (perfect for warm-started, mildly-violated frames), needs no $L$, no line search, no global
reductions — and games/HIL users value 10 cheap iterations over 3 expensive guaranteed ones.
The lesson generalizes: **solver rankings depend on the iteration budget**.

## 5. ADMM — the one that survives stiffness

`ChSolverADMM` (`ChSolverADMM.cpp`) splits the QP into "solve the linear physics" and "obey the
cone," coupling them by a scaled dual variable — the three classic steps visible verbatim at
`:286-311` (KKT solve with $\rho$-regularized diagonal, `z = P_K(\lambda + y/\rho)`, dual update
$y \mathrel{+}= \rho(\lambda - z)$). Two things make it earn its place:

- It's the only VI solver here that tolerates `ChKRMBlock` stiffness matrices — i.e. **FEA
  (ANCF/Reissner deformable tires) on NSC contact** — because it factors the full
  $[H\ C_q^\top; C_q\ \cdot]$ KKT matrix instead of requiring $H^{-1}$ applications.
- Its penalty parameter is auto-tuned by **residual balancing** — $\rho \leftarrow \rho\cdot
  \sqrt{r_{\text{prim}}/r_{\text{dual}}}$, clamped (`:357-424`, citing Wohlberg in-code) —
  because ADMM's convergence *rate* (not correctness) hinges on $\rho$, and the right $\rho$ is
  problem- and even phase-dependent.

## 6. The mass matrix as a metric — the geometry hiding in a "stabilization"

The Tasora stepper's position fix ([03 §2](03-contact-as-measure-theory.md)) solves

$$
\begin{bmatrix} M & C_q^\top \\ C_q & 0\end{bmatrix}
\begin{bmatrix}\delta q \\ -\lambda\end{bmatrix} =
\begin{bmatrix}0 \\ -C\end{bmatrix}
$$

— in-code: "Position stabilization (single Newton step on constraints, with mass matrix as
metric)" (`ChTimestepperImplicit.cpp:595`). Eliminate $\lambda$ and you get $\delta q =
-M^{-1}C_q^\top (C_q M^{-1} C_q^\top)^{-1} C$: the **minimum-$\lVert\cdot\rVert_M$ correction**
achieving $\Phi = 0$ to first order, i.e. the projection onto the constraint manifold in the
Riemannian metric $\langle u, v\rangle_M = u^\top M v$ — kinetic energy as distance. Why this
metric and not Euclidean? Because Euclidean projection would move heavy and light bodies equally
(in meters), injecting energy into heavy ones; the $M$-projection moves each body inversely to
its inertia, which is what a real constraint force would do. Also the friction-block averaging
subtlety: BB/PSOR require each contact's 3×3 diagonal preconditioner block averaged to a
*scalar* (`ChSystemDescriptor.cpp:438-472`, APGD passes `average=false`, BB/PSOR `true`) —
because a per-component diagonal metric that differs across $(n,u,v)$ would make the *Euclidean*
cone projection no longer the projection *in that metric*. You may only mix a diagonal metric
with the cone projection if the metric is **isotropic on each cone block**. Metric-consistency
bugs are silent; Chrono dodges this one by construction.

## 7. Compliance = Tikhonov regularization

The $-E$ block ("cfm," constraint force mixing — `ChSystemDescriptor.cpp:573`,
`ChContactMaterialNSC.h:43-71` with real units: m/N) adds $\tfrac{1}{2}\gamma^\top E \gamma$ to
the objective: physically *material compliance*, numerically **Tikhonov regularization** that
makes $N$ positive definite, killing the nullspace of redundant contacts. One parameter, two
honest readings — the best kind of hack. (Watch the stepper-dependence noted in
`ChContactNSC.cpp`'s SETTLE branch: compliance terms assume $h$-scaling, "not all steppers have
c = 1/h.")

## 8. → swish

Swish should build **none of these solvers** — [09 §4](09-swish-map.md) argues that formally.
What crosses over is the thinking:

- **Convergence tests you can compute.** The natural-residual idea — "apply one step of your own
  update; the movement *is* the error" — is how to test any future iterative bit in swish (the
  Phase B spring settling test in [docs/wheels/README.md](../wheels/README.md) is already
  shaped like this).
- **Return the best iterate, not the last.** APGD's $\hat\gamma$ discipline applies to any
  nonmonotone tuning loop — including "run the sim, score a screenshot" loops.
- **Name your metric.** The $M$-metric projection is the deep version of a rule swish already
  half-follows: when you clamp/snap a state, decide *in which coordinates* the snap is smallest
  (the yaw-rate clamp is a snap in $r$; the sideslip clamp in $v_l$ — both chosen because those
  are the perceptually-linear coordinates).
- **Budget-relative solver choice.** PSOR-as-default is the license for swish's closed-form
  models: at 60 fps with one vehicle, the "worse" method that costs nothing *is* the right one.

---

**Next:** [05 — rotations and Lie groups](05-rotations-and-lie-groups.md), the other place
Chrono quietly does differential geometry.
