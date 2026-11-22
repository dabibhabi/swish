# Wheels — tuning notes

Knobs, ranges, the maths behind them and sources, appended after every phase.

## Phase C — visual wheel slip (2026-11-20)

Slip ratio (SAE convention, with a floor so a launch from rest still spins visibly):

$$\kappa = \frac{\omega r - v}{\max(|v|, v_{min})}$$

Target from the axle's friction use $u = F_x/(\mu F_z) \in [-1, 1]$:

$$\kappa^\ast = \operatorname{sign}(u)\, \operatorname{smoothstep}(u_0, 1, |u|)\, \kappa_{cap},\qquad \kappa_{cap} = \begin{cases}\kappa_{spin} & \text{drive}\\ \kappa_{abs}\ \text{or}\ 1 & \text{brake (ABS on / off)}\end{cases}$$

The chase $\kappa_{n+1} = \kappa_n + (\kappa^\ast - \kappa_n)(1 - e^{-\Delta t/\tau})$ is exact for any $\Delta t$.

| Knob (`WheelSlipParams`, `WheelKinematics.h`) | Default | Range to try | Effect |
|---|---|---|---|
| `utilStart` | 0.92 | 0.85–0.98 | How close to the cap slip starts. Lower = slip shows earlier. |
| `spinSlip` | 0.15 | 0.08–0.4 | Wheelspin at the drive cap. 0.1–0.15 is a road tyre's peak-μ slip; above 0.3 reads as a burnout. |
| `absSlip` | 0.15 | 0.08–0.2 | Braking slip with ABS (ABS holds the tyre near peak μ). |
| `lockSlip` | 1.0 | 1.0 | ABS off: −1 = a locked wheel. |
| `tauSpin` / `tauLock` / `tauRecover` | 0.12 / 0.10 / 0.08 s | 0.05–0.3 | Ramp times. The spec asks for lockup as a ~0.3 s ramp, never a snap (3τ ≈ 95 %). |
| `vMin` | 3 m/s | 1–5 | Slip reference floor at low speed. |

Known limits: slip angle is still per axle (no per-wheel load or Ackermann slip), and spin is only visible on the axle at its cap (the AWD split puts the rear there first). There is no skid-mark renderer yet.

## Phase B2 — weight transfer: squat, dive, roll (2026-11-20)

Moments about the contact plane, per corner:

$$\Delta F_{z,\text{long}} = \mp\frac{m\,a_x\,h}{2L}\ (\text{front / rear}),\qquad \Delta F_{z,\text{lat}} = -s\,\frac{m_{axle}\,a_y\,(h - h_{rc})}{t}$$

where $s = +1$ for a right-hand corner, $a_y = v_x r$ is right-positive, and $m_{axle}$ is the axle's static mass share. Compression target $x = \Delta F_z/k$ plus the existing ARB shift. Steady-state roll per $g$ (symmetric axle, ARB ratio $\rho$):

$$\phi \approx \frac{2\,x}{t} = \frac{2\,m_{axle} g (h - h_{rc})}{k\,t^2 (1 + 2\rho)}$$

Expected pitch at 1 g braking with $m = 1640$ kg, $h = 0.46$ m, $L = 2.45$ m and $k_F \approx 22$ kN/m: front $x \approx 0.07$ m, giving about 2.5–3°. The test pins 0.5–3.5° after 0.5 s of full braking.

| Knob | Where | Default | Effect |
|---|---|---|---|
| `kRollCentreM` | `CarEntity::update_wheel_visuals` | 0.12 m [est] | Higher = less body roll (more load goes through the links). |
| `hCg` | `CarProfile` | 0.46 m [est] | Scales both pitch and roll. |
| Ride frequency / damping | `SuspensionParams` | 1.4 Hz, ζ 0.45 | Stiffer = less attitude; lower ζ = more overshoot after a stop. |
| ARB ratios | `AntiRollParams` | 0.35 / 0.22 | Higher = less roll. |

Not modelled: anti-dive and anti-squat geometry (they would cut pitch by their percentage), and tyre load sensitivity. Lateral transfer does not change axle grip yet; that would be a real handling change.

**Sources**
- T. D. Gillespie, *Fundamentals of Vehicle Dynamics*, SAE, 1992: ch. 2 (longitudinal load transfer), ch. 7 (roll centre, roll stiffness).
- W. F. Milliken and D. L. Milliken, *Race Car Vehicle Dynamics*, SAE, 1995: ch. 16 and 18 (lateral load transfer, elastic vs geometric split through the roll centre).
- H. B. Pacejka, *Tire and Vehicle Dynamics*, 3rd ed., Butterworth-Heinemann, 2012: ch. 1 and 4 (slip-ratio definition; peak μ near κ ≈ 0.1–0.15).
- R. Rajamani, *Vehicle Dynamics and Control*, 2nd ed., Springer, 2012 (Univ. of Minnesota course text): ch. 4 (slip ratio), ch. 13 (tyre models).


## B1: physics substep (2026-11-20, AI-drafted)

| Knob | Where | Now | Range | Effect |
|------|-------|-----|-------|--------|
| Physics substep (Hz) | Physics panel, `carSubstepHz` | 240 | 0-1000 | 0 = one step per frame (release behaviour). Higher = smaller steps, steadier at low fps. |

One explicit Euler step has local error $O(\Delta t^2)$ and is only stable while $\Delta t < 2/\lambda$ for the fastest
rate $\lambda$ in the model. A tyre with relaxation, or a wheel on a stiff spring, has $\lambda$ in the hundreds per
second, so the step must be a few milliseconds. The frame is cut into $n = \lceil \Delta t \cdot f \rceil$ equal slices.

Try it: set 0, drive a slalom at a low frame rate, then set 240. Sources: Glenn Fiedler, "Fix Your Timestep!"
(gafferongames.com); Witkin and Baraff, *Physically Based Modeling* course notes (CMU / SIGGRAPH), "Differential
Equation Basics". Neither link was opened in this session.


## B2-B5: 6-DOF car with spinning wheels (2026-11-20, AI-drafted)

Turn it on: `make debug`, Physics panel, tick "Physics model: rigid body". All knobs below are in that panel.

| Knob | Default | Range | What you feel |
|------|---------|-------|---------------|
| Tyre curve shape C | 1.45 | 1.1-1.9 | Grip left in a full slide: 1.1 keeps 99 %, 1.45 keeps 76 %, 1.9 keeps 16 %. Higher = snappier, harder to catch. |
| Slip ratio at peak | 0.10 | 0.05-0.20 | How much wheelspin or lock gives best traction. Lower = sharper. |
| Front / rear slip angle at peak | 7 / 6 deg | 3-12 | Lower = stiffer, more direct. Rear below front = stable; rear above front = tail-happy. |
| Tyre load sensitivity | 0.12 | 0-0.3 | Grip lost as a tyre is loaded. Higher = weight transfer matters more (more lift-off oversteer). |
| Front / rear anti-roll bar | 30000 / 18000 N/m | 0-80000 | More front = understeer; more rear = oversteer. |
| Brake bias to the front | 0.62 | 0.4-0.8 | Lower = rear locks first under braking. |
| Downforce multiplier | 1.0 | 0-3 | High-speed stability. |
| Traction control, ABS | on | | Off = wheelspin and lock-ups. |

First things to try for a 911 feel: lift off mid-corner at 60 mph (the tail should step out and be catchable); if it
is too tame raise load sensitivity to 0.2 or the rear slip angle to 7; if it spins, raise the front bar.

Formulas: see the changelog entry "Gave the 6-DOF chassis spinning wheels". Reading: Pacejka, *Tire and Vehicle
Dynamics* (magic formula, similarity method); Milliken and Milliken, *Race Car Vehicle Dynamics* (load transfer,
anti-roll bars); Brian Beckman, "The Physics of Racing" (free series, friction circle and slip); Guiggiani, *The
Science of Vehicle Dynamics*. Lecture slides to look up: TU Delft "Vehicle Dynamics" and MIT OCW 2.003 for the
rigid-body part. None of these were opened in this session.

## B6 / realism A0-A2: keyboard feel + 6-DOF ungated (2026-11-21, AI-drafted)

Debug-only defaults (release keeps assist 0 and instant pedals until the owner signs off). Panel: "Physics" (group `car`).

| Knob (`Tunables.def`) | Default | Range | What it does |
|---|---|---|---|
| `car.key_counter_steer_assist` | 0.7 | 0-1 | Blend of the arrow keys toward steering about the front axle's travel direction. 0 = the old grip-limited stick. |
| `car.key_assist_slip_deg` | 7 | 2-12 | Front slip angle full key commands while assisted. Keep near `tire_alpha_peak_front_deg`. |
| `car.key_throttle_rise` / `_fall` | 4 / 8 /s | 0-20 / 0-30 | Throttle press and lift rate. 0 = instant. |
| `car.key_brake_rise` / `_fall` | 5 / 10 /s | 0-20 / 0-30 | Brake press and release rate. DOWN drops the throttle at once. |

Assist (front slip in the right-positive convention of `Bicycle.h`):

$$\delta_c = \arctan\frac{v_l + a r}{|v_x|},\qquad w = k\,\mathrm{clamp}\!\left(\frac{v_x - 3}{8 - 3}, 0, 1\right),\qquad \delta = (1-w)\,s\,\delta_{lim} + w\,(\delta_c + s\,\alpha_{max})$$

With the stick at 0 the front slip is 0, so the only yaw moment left is the rear's, which turns the car back
toward its velocity (caster-like). The 6-DOF model now always substeps at $\ge 240$ Hz, so engine torque, the rev
limit and TC are re-evaluated every 4.2 ms.

Pedal ramp: $\dot u = \pm R$. A lift at 0.3 g moves $\Delta F_z = m a_x h / L \approx 760$ N; at 8 /s that takes
0.125 s instead of one frame.

**What to try in the app (`make debug`):** Physics model = rigid body, 30 m/s on a curve, lift off and let go of
the keys; the tail should step out, then the car straightens itself. Hold opposite arrow: it should catch. Set
assist 0 to compare.

Reading: Pacejka, *Tire and Vehicle Dynamics* (slip angles); Milliken and Milliken, *Race Car Vehicle Dynamics*
(caster, hands-off stability); Rajamani, *Vehicle Dynamics and Control* (bicycle model, driver models). Not opened
in this session.
