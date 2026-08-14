---
title: Lessons from Chrono 05 — rotations and Lie groups
tags:
  - chrono
  - math
  - rotations
  - lie-groups
part: 5
status: complete
---

# 05 · Rotations & Lie groups: stepping on a curved state space

> [!abstract] Goal
> A rigid body's orientation doesn't live in a vector space — it lives on a curved manifold, and
> Chrono's integrator respects that with a genuinely elegant trick: 7 position coordinates, 6
> velocity coordinates, and an exp/log pair gluing them together. This is the shortest chapter
> and the purest math-per-line in the series.

## 1. The mismatch: 7 position coordinates, 6 velocities

**Layman's terms.** Chrono stores a body's orientation as a unit quaternion — four numbers with
one rule: their squared sum is exactly 1. Four numbers, one constraint → three actual degrees of
freedom. Angular velocity, though, is an honest 3-vector. So the state has *seven* position
numbers but only *six* velocity numbers per body, and you can't just do `x += v * dt` — adding a
3-vector to a quaternion isn't even type-correct, and adding a 4-vector to it slides you off the
unit sphere.

**The math.** Unit quaternions form the Lie group $SU(2)$ (the double cover of the rotation
group $SO(3)$); the tangent space at the identity — the Lie algebra $\mathfrak{so}(3)$ — is
$\mathbb{R}^3$, the space of angular-velocity/rotation vectors. The two are connected by the
exponential map and its inverse:

$$
\exp: \mathbb{R}^3 \to SU(2), \quad
\exp(\boldsymbol\phi) = \Big(\cos\tfrac{\theta}{2},\ \frac{\sin(\theta/2)}{\theta}\,\boldsymbol\phi\Big),
\qquad \theta = \lVert\boldsymbol\phi\rVert
$$

$$
\log: SU(2) \to \mathbb{R}^3, \quad
\log(q) = \frac{2\,\operatorname{atan2}(\lVert\vec q\rVert,\, q_0)}{\lVert\vec q\rVert}\ \vec q
$$

## 2. The code — exp and log with their singularities handled

```cpp
// examples/chrono/src/chrono/core/ChQuaternion.h:1009-1027
inline void ChQuaternion<Real>::SetFromRotVec(const ChVector3<Real>& angle_axis) {
    Real theta_squared = angle_axis.Length2();
    // For non-zero rotation:
    if (theta_squared > 1e-30) {
        Real theta = std::sqrt(theta_squared);
        Real half_theta = theta / 2;
        Real k = std::sin(half_theta) / theta;
        m_data[0] = std::cos(half_theta);
        m_data[1] = angle_axis.x() * k;
        ...
    } else {
        // For almost zero rotation:
        Real k(0.5);
        m_data[0] = Real(1.0);
        m_data[1] = angle_axis.x() * k;
```

**The real analysis, spelled out.** $k(\theta) = \sin(\theta/2)/\theta$ has a *removable
singularity* at $\theta = 0$: the function is undefined there, but
$\lim_{\theta\to 0} k = \tfrac12$ exists, and the extension is analytic
($k = \tfrac12 - \tfrac{\theta^2}{48} + O(\theta^4)$). Mathematically removable — numerically
still dangerous, because computing $\sin(\theta/2)/\theta$ at $\theta = 10^{-20}$ divides one
underflow-threatened tiny number by another. The branch at $\theta^2 \le 10^{-30}$ (i.e.
$\theta \lesssim 10^{-15}$, right at double's precision floor) replaces the ratio with the
zeroth Taylor term, whose error $O(\theta^2) \approx 10^{-30}$ is far below machine epsilon.
The branch point is chosen so *neither side ever loses a digit* — the textbook pattern for every
$\sin x / x$-shaped kernel.

The log side has a second, subtler numerics decision:

```cpp
// examples/chrono/src/chrono/core/ChQuaternion.h:1035-1037
if (sin_squared > 1e-30) {
    Real sin_theta = std::sqrt(sin_squared);
    Real k = 2 * std::atan2(sin_theta, m_data[0]) / sin_theta;
```

The obvious formula is $\theta = 2\arccos(q_0)$ — and it's numerically *bad*: near $\theta = 0$,
$\arccos$ has derivative $-1/\sqrt{1 - q_0^2} \to \infty$, so an ULP of error in $q_0$ becomes
$O(\sqrt{\varepsilon})$ of error in $\theta$ (catastrophic conditioning exactly where you
operate most — small incremental rotations). $\operatorname{atan2}(\lVert\vec q\rVert, q_0)$
computes the *same angle* with $O(\varepsilon)$ conditioning everywhere, because it takes both
sine and cosine information instead of inverting the flat top of the cosine. Same function, same
domain, different formula — one of them loses half your digits.

## 3. The retraction step — why ‖q‖ = 1 survives forever

**Layman's terms.** Here's the trick that makes the 7/6 mismatch workable: the *integrator*
never touches quaternions. It works entirely in 3-vector land — "rotate 0.003 rad about
roughly-this-axis" — and hands that increment to the *body*, which converts it into a small
quaternion via exp and *multiplies* it on. Multiplying two unit quaternions gives a unit
quaternion, always. Nothing ever drifts off the sphere, so nothing ever needs renormalizing.

**The code** — increment and its exact inverse:

```cpp
// examples/chrono/src/chrono/physics/ChBody.cpp:152-158
// ADVANCE ROTATION: R_new = DR_a * R_old
// (using quaternions, local or abs:  q_new = Dq_a * q_old =  q_old * Dq_l  )
ChQuaternion<> q_old(x.segment(off_x + 3, 4));
ChQuaternion<> rel_q;
rel_q.SetFromRotVec(Dv.segment(off_v + 3, 3));
ChQuaternion<> q_new = q_old * rel_q;
```

```cpp
// examples/chrono/src/chrono/physics/ChBody.cpp:172-177
// ROTATION (quaternions): Dq_loc = q_old^-1 * q_new,
//  because   q_new = Dq_abs * q_old   = q_old * Dq_loc
ChQuaternion<> rel_q = q_old.GetConjugate() * q_new;
Dv.segment(off_v + 3, 3) = rel_q.GetRotVec().eigen();
```

**The math.** In geometric-integration language this is a **retraction**: the update
$q_{n+1} = q_n \cdot \exp(\Delta\boldsymbol\phi)$ maps a tangent-space increment onto the
manifold, and `IntStateGetIncrement` is its exact inverse
$\Delta\boldsymbol\phi = \log(q_n^{-1} q_{n+1})$ — a local chart centered at the current state.
Three things fall out for free:

- **Constraint preservation by algebra, not by projection.** $\lVert q_1 q_2\rVert =
  \lVert q_1\rVert\,\lVert q_2\rVert$, so unit norm is preserved to the roundoff of a single
  product per step, with *zero* accumulated drift — compare the naive
  $q \mathrel{+}= \tfrac12 \omega q\,dt$-then-renormalize, which is a projection method
  fighting drift it created.
- **No coordinate singularities.** Charts are local (each step re-centers at $q_n$), so the
  $\theta = \pi$ trouble of a *global* rotation-vector parameterization is never reached —
  per-step increments are tiny.
- **The right invariance.** Note `q_old * rel_q` (increment on the *right* = body-frame
  increment) — matching angular velocity being expressed in body coordinates. The comment spells
  out both conventions; picking one and writing it down is half the battle in any rotation code.

One more connection: the quaternion kinematics the integrator's velocities live in is
$\dot q = \tfrac12\,\{0, \boldsymbol\omega\}\, q$ (`ChQuaternion.h:1156-1199`, both frame
conventions plus the inverse $\boldsymbol\omega = 2\,\mathrm{vec}(\dot q\, q^*)$) — the
differential of the group multiplication, i.e. the same Lie structure seen at velocity level.

## 4. A metric on SO(3), briefly

The geodesic distance between orientations is the plain rotation angle between them:
$d(q_1, q_2) = \lVert \log(q_1^{-1} q_2)\rVert$ — a bi-invariant Riemannian metric (invariant
under rotating both arguments, left or right). That's the principled answer to "how different
are two orientations?", and `IntStateGetIncrement` computes exactly this quantity's argument.
If you ever score orientation error (camera smoothing, replay diffing, a test asserting "the
body ended up within 0.5° of upright"), this — not quaternion-component differences, not Euler
angle deltas — is the number to threshold. Cheap corollary: $d = 2\operatorname{atan2}(\lVert
\vec{(q_1^{-1}q_2)}\rVert, |(q_1^{-1}q_2)_0|)$ (the $|\cdot|$ handles the $\pm q$ double cover).

## 5. → swish

Swish's rotation state is deliberately humbler — a Y-heading in degrees plus loader-provided
per-corner frames — and that's correct for a car on a road. The transferable pieces:

- **The wrap is a 1-D retraction.** `wrap_angle_2pi` in
  [`WheelKinematics.h:18-21`](../../src/scene/Entity/WheelKinematics.h) keeps wheel spin on the
  circle $S^1$ for exactly the reason Chrono keeps $q$ on $S^3$ — and its comment even does the
  ULP analysis (θ unwrapped to $10^4$ rad costs ~$10^{-3}$ rad of float resolution ≈ 0.06° of
  visible rim quantization; wrapped, ULP stays ~$4\times10^{-7}$). Same theorem, one dimension
  down: **keep state on its manifold and precision is free**; let it wander and you pay in
  drift or in renormalization. The heading wrap to ±180° (`CarEntity.cpp:122-123`) is the same
  discipline.
- **If body attitude ever becomes dynamic** (Phase B stores visual heave/pitch/roll — fine as
  three small angles; a future free-body camera or rollover state would not be), the upgrade
  path is exactly Chrono's: keep increments as 3-vectors, compose via quaternion multiply, and
  steal `SetFromRotVec`/`GetRotVec` including both numerics branches. GLM has
  `glm::exp`/`glm::log` for quats, but *check their small-angle behavior before trusting them*
  — the two branches above are the part naive implementations omit.
- **The conjugation pattern is already in the tree.** The wheel draw-matrix composition
  $F\, R_x(s\theta)\, F^{-1}$ (`CarEntity.cpp:178-196` — spin conjugated through each corner's
  measured baked frame) is change-of-chart reasoning: *express the simple motion in the frame
  where it is simple, then conjugate into the world*. That's the same maneuver as Chrono's
  body-frame increment, and it's why the cambered wheels don't wobble.

---

**Next:** [06 — driveline, powertrain, drivers](06-driveline-powertrain-drivers.md) — back to
earth: gears, torque converters, and controllers.
