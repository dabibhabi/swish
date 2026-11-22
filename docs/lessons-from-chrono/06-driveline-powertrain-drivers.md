---
title: Lessons from Chrono 06 — driveline, powertrain, drivers
tags:
  - chrono
  - powertrain
  - control
part: 6
status: complete
---

# 06 · Driveline, powertrain & drivers: torque paths and the controllers that steer them

> [!abstract] Goal
> The 1-D mechanical world (shafts, converters, gearboxes) and the control layer above it. Less
> deep math than [03](03-contact-as-measure-theory.md)/[04](04-solvers-and-metric-spaces.md),
> but dense in *engineering judgment* — including the best cautionary tale about units in the
> whole tree. The Willis differential and shaft-network basics are in the
> [existing guide](../../archive/local/examples/chrono/VEHICLE_DYNAMICS_GUIDE.md#driveline--powertrain);
> this file goes into what it skipped.

## 1. The torque converter: a fluid coupling in two lookup tables

**Layman's terms.** An automatic transmission has no clutch pedal — engine and gearbox are
coupled by a doughnut of spinning oil. The impeller (engine side) flings oil at the turbine
(gearbox side); at a standstill launch the fluid *multiplies* torque (~2×, like a fluid lever),
and near lockup it transmits ~1:1 with a little slip. The whole device is characterized
empirically by two curves of one variable — the speed ratio $R = \omega_{\text{out}}
/\omega_{\text{in}}$: a **capacity factor** $K(R)$ ("how much torque does the impeller absorb at
a given speed") and a **torque ratio** $T(R)$ ("how much is multiplied").

**The math.** The pump law is quadratic in impeller speed:

$$
T_{\text{in}} = -\left(\frac{\omega_{\text{in}}}{K(R)}\right)^{2}, \qquad
T_{\text{out}} = T(R)\cdot\lvert T_{\text{in}}\rvert
$$

with the stator (the reaction member bolted to the case) absorbing the difference — torque
multiplication without violating Newton's third law, because there are *three* torque paths, not
two. Dimensional sanity check: for $T \sim \omega^2$ to hold, $K$ carries units of
$\text{rad/s}/\sqrt{\text{N·m}}$ — remember this for §2.

**The code:**

```cpp
// archive/local/examples/chrono/src/chrono/physics/ChShaftsTorqueConverter.cpp:98-116 (trimmed)
double mR = GetSpeedRatio();
if (mR > 1) { mR = 1 - (mR - 1); state_warning_reverseflow = true; }
if (mR < 0) mR = 0;
...
double mK = K->GetVal(mR);
double mT = T->GetVal(mR);
torque_in = -std::pow((shaft1->GetPosDt() / mK), 2);
if (state_warning_reverseflow) torque_in = -torque_in;
if (state_warning_reverseflow) torque_out = -torque_in;      // clutch mode, TR = 1
else                           torque_out = -mT * torque_in;
```

Note what the first two lines are: the curves were *identified* on $R \in [0,1]$ (driving), but
the sim will visit $R > 1$ (engine braking — turbine overruns impeller) and $R < 0$ (rollback
with drive engaged). Chrono's answers — reflect $R$ past 1, clamp below 0 — are heuristic
*continuations outside the identification domain*, each only $C^0$ at the seam. There's a
general lesson here that applies to every fitted map in a simulator (tire tables included, see
[02 §3](02-tire-models.md)): **a fitted model has a domain, the simulation does not, and the
gap must be closed by an explicit policy** — reflect, clamp, extrapolate, or refuse. Chrono
picks per-case and flags the weird regimes with warning states rather than silently proceeding.

## 2. The 5.4× unit trap — the best cautionary tale in the tree

In the HMMWV's converter data file, the *active* capacity-factor curve and a *commented-out
alternative* coexist:

```cpp
// archive/local/examples/chrono/src/chrono_models/vehicle/hmmwv/powertrain/HMMWV_AutomaticTransmissionShafts.cpp:46-52
map->AddPoint(0.00, 15);
map->AddPoint(0.25, 15);
map->AddPoint(0.50, 15);
map->AddPoint(0.75, 16);
map->AddPoint(0.90, 18);
map->AddPoint(1.00, 35);
```

with the ghost dataset a few lines below running from **81 to 235** — a factor of ~5.4 in scale.
Same physical device, two unit conventions for $K$ (the classic culprits: rad/s vs RPM inside
the ratio, or $\sqrt{\text{N·m}}$ vs $\sqrt{\text{lbf·ft}}$). And because the pump law squares
$K$, mixing them wouldn't change torque by 5.4× — it would change it by
$5.4^2 \approx \textcolor{red}{29\times}$, silently, with no NaN, no assertion, just a car that
launches like a rocket or a slug.

> [!warning] The transferable rule
> A unit error inside a *squared* (or worse) term amplifies quadratically and produces plausible-
> looking wrong numbers. Swish's protections against exactly this: SI-only pure headers with the
> conversion confined to one boundary (`CarPhysics.h:3-5`), the `kWorldUnitsPerMeter` /
> `WORLD_SCALE` duplication being a known wart worth unifying (done 2026-09-29: `util::units::kWuPerMetre`), and behavior-pinning tests
> (`test_car_physics.cpp`'s launch-accel ≈ μg and top-speed 88–96 m/s brackets) that would catch
> a 29× energy error instantly. The HMMWV file would have been saved by one such test.

## 3. Gear shifting: hysteresis + latency, and nothing else

```cpp
// archive/local/examples/chrono/src/chrono_vehicle/powertrain/ChAutomaticTransmissionShafts.cpp:127-146 (trimmed)
if (time - m_last_time_gearshift < m_gear_shift_latency)  // avoid bursts of gear shifts
    return;
if (m_shift_mode == ShiftMode::AUTOMATIC && m_drive_mode == DriveMode::FORWARD) {
    double gearshaft_speed = m_shaft_ingear->GetPosDt();
    if (gearshaft_speed > m_upshift_speed) { ... SetGear(m_current_gear + 1); }
    else if (gearshaft_speed < m_downshift_speed) { ... SetGear(m_current_gear - 1); }
}
```

Two mechanisms, both anti-chatter: a **speed hysteresis band** (upshift at 2500 RPM, downshift
at 1200 — the wide gap means the post-shift RPM lands *inside* the band, so no immediate
reversal) and a **latency lockout** (1 s minimum between shifts — a refractory period covering
transients the hysteresis alone can't, e.g. RPM flare during the shift itself). This is the
standard two-layer defense for *any* discrete mode switch driven by a continuous signal, and
swish already deploys the pattern in miniature: the 5→8 m/s kinematic↔dynamic yaw *blend*
(`CarEntity.cpp:112-113`) is hysteresis's smooth cousin, and rain's 3-step key cycle is a
latency-free discrete switch that stays clean only because a human is in the loop. If swish
ever auto-switches modes off a noisy signal (hydroplaning on/off per
[07 §5](07-wet-roads-and-rain.md) is the live candidate) — build both layers.

## 4. Three steering controllers, three answers to low speed

Chrono ships five path-following steering controllers (`ChSteeringController.h`); three teach
something.

**PID with geometric error** (`ChSteeringController.cpp:138-178`). The error isn't "distance to
path" — it's the *signed projected distance* from a **sentinel point** cast ahead of the vehicle
to its closest point on the path, with sign from an orientation test:

```cpp
// archive/local/examples/chrono/src/chrono_vehicle/utils/ChSteeringController.cpp:154-171 (trimmed)
m_sentinel = ref_frame.TransformPointLocalToParent(m_dist * ChWorldFrame::Forward());
CalcTargetLocation();
ChVector3d err_vec = m_target - m_sentinel;
double temp = Vdot(Vcross(sentinel_vec, target_vec), ChWorldFrame::Vertical());
double err = ChSignum(temp) * err_vec.Length();
m_errd = (err - m_err) / step;                 // backward difference
m_erri += (err + m_err) * step / 2;            // trapezoidal rule
double steering = m_Kp * m_err + m_Ki * m_erri + m_Kd * m_errd;
```

The lookahead distance *is* the damping: error measured ahead of the car anticipates, so a pure
P controller behaves like PD on the actual lateral offset. Honest flaws, visible in nine lines:
no integral anti-windup, no output saturation, and `sgn × ‖·‖` has a kink at zero error (the
derivative term chatters when tracking well). Which is why —

**Stanley** (`ChSteeringController.cpp:648+`, the DARPA Grand Challenge law) patches each flaw
by construction:

$$
\delta = \psi_{\text{err}} + \arctan\!\left(\frac{K\,e}{\max(u, u_{\min})}\right)
$$

- The $\arctan$ **saturates the cross-track response** — far from the path you steer *at* it at
  a bounded angle rather than winding up.
- $u$ in the denominator makes the gain speed-scheduled, and $u_{\min}$ floors it — the same
  low-speed regularization as the tire models' `m_vnum` ([02 §7](02-tire-models.md)), solving
  the same $1/v$ singularity in a third context.
- A **sine-step dead zone** (`w = SineStep(|err|, deadZone, 0, 2·deadZone, 1)`) fixes the kink
  the PID left: error is smoothly gated to zero near zero, killing chatter — plus a PT1
  (first-order) filter modeling actuation delay.

The progression PID → Stanley is a compact case study in *designing the nonlinearity in* rather
than clamping after the fact.

**ACC / adaptive cruise** (`ChAdaptiveSpeedController.cpp:75+`) — a gap policy worth copying
verbatim someday: remap measured following distance affinely so that "at minimum distance" reads
as speed 0 and "at desired gap" reads as the cruise speed, then feed the result to the ordinary
speed PID. Two set-points, one linear map, degenerates to plain cruise at large gaps — and one
latent division-by-near-zero when the two set-points nearly coincide, guarded only by a strict
`>`. (Spiritual kin to swish's `steady_state_top_speed` bisection: reduce a control question to
a scalar root/interpolation problem and solve *that*.)

## 5. → swish

- **The engine model swish has is Chrono's "SimpleMap" tier** — `longitudinal_accel`'s
  power-capped drive force is equivalent to `ChEngineSimpleMap` with a flat power curve and no
  shaft dynamics. The upgrade that would actually be *felt* isn't shafts — it's a **torque
  curve + gear ratios** feeding the same traction cap, which changes launch character (torque
  hole between shifts, the RPM-dependent push). If that's ever wanted: two `ChFunctionInterp`-
  style tables ([08 §2](08-numerics-toolbox.md)) and the §3 shift machine, ~60 lines, all pure
  header. It is *not* on the current roadmap — wet grip ([07](07-wet-roads-and-rain.md))
  outranks it decisively for the rain-realism goal.
- **Steering feel.** Swish's steering already has rate + return-rate shaping
  (`kSteerRate`/`kSteerReturn`, `CarEntity.h:126-127`). The Stanley lesson applies the day any
  *assist* or auto-centering-under-load is added: put the saturation inside an $\arctan$-shaped
  law, floor the speed in any $1/v$, and gate small errors with a smooth dead zone rather than
  an `if`.
- **The latency+hysteresis pattern** (§3) goes straight into the hydroplaning switch in
  [07 §5](07-wet-roads-and-rain.md).

---

**Next:** [07 — wet roads and rain](07-wet-roads-and-rain.md): the chapter this series exists
for.
