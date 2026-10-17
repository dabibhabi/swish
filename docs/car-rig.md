---
verified: 2026-10-12
against: 57e772b   # git rev-parse --short HEAD
owner: docs-scribe
---

# Car rig: the named-node convention

How a car GLB tells Swish where its wheels, calipers, steering wheel, dial needles and wipers are, and how each
part moves. The reference car is the rigged 1995 911 Carrera RS (993),
[`assets/blend/porsche_rigged.glb`](../assets/blend/porsche_rigged.glb). It is the default car, so no
`SWISH_CAR` is needed. `SWISH_CAR=Porsche/porsche.glb` still loads the old shipped Porsche.
Known limits are listed [below](#known-limits).

| Code | Role |
|---|---|
| [CarRig.h](../src/scene/Entity/CarRig.h) | name → role, ancestry tagging, pivot normalization, wheel / steering frames, `rig_part_sweep` |
| [CarRigGeometry.h](../src/scene/Entity/CarRigGeometry.h) | measured wheelbase, tracks, tyre radii |
| [ModelManager.cpp](../src/scene/ModelManager/ModelManager.cpp) `load_car` | tries the named rig first; explicit-rig, Hellcat and heuristic paths are fallbacks |
| [WheelKinematics.h](../src/scene/Entity/WheelKinematics.h) | `wheel_rolling_speed`, `ackermann_wheel_angle`, `wheel_spin_advance` |
| [Wiper.h](../src/scene/Entity/Wiper.h), [WiperBlade.h](../src/scene/Entity/WiperBlade.h) | wiper modes and motion; arm tip and blade points |
| [GaugeNeedles.h](../src/scene/Entity/GaugeNeedles.h) | tach fit, speedo table, damped follower, startup sweep |
| [CarProfile.h](../src/scene/Entity/CarProfile.h) | per-car physics (`turbo_s_992()`, `rs_993()`) |
| [CarConfig](../src/scene/CarConfig/CarConfig.h) + [config/car.toml](../config/car.toml) | `[wheels]`, `[gauges]`, `[wipers]` values, read in both builds |
| Tests | [test_car_rig.cpp](../tests/test_car_rig.cpp) (parses the real GLB), [test_car_profiles.cpp](../tests/test_car_profiles.cpp), [test_wiper.cpp](../tests/test_wiper.cpp), [test_gauge_needles.cpp](../tests/test_gauge_needles.cpp), [test_wheel_articulation.cpp](../tests/test_wheel_articulation.cpp) |

## 1. Node convention

The convention applies when **all four** `Wheel_*` pivots exist (`car_rig_detect`). Names match exactly. A pivot is an
Empty; the meshes under it inherit its role from the nearest named ancestor (`car_rig_tag_nodes`), so child names such as
`Wheel_FL_Rim` are free. All pivots are siblings under the scene root.

| Node | Role | Local axis (glTF) | Behaviour |
|---|---|---|---|
| `Wheel_FL`, `Wheel_FR`, `Wheel_RL`, `Wheel_RR` | hub pivot; tyre, rim and rotor meshes as children | axle = local **X** | spin at $\omega_i$; fronts steer (Ackermann); all travel with the Phase B suspension |
| `Caliper_FL` … `Caliper_RR` | at the same hub point | (none) | steer and travel with the upright; **never spin** |
| `SteeringWheel` | column pivot | column = local **Z** | turns by $\delta\cdot\rho$ (steer angle × profile ratio); clockwise on a right steer |
| `Needle_RPM`, `Needle_Speed` | needle pivot at the dial centre | sweep = local **−Z** (the dial normal, toward the driver) | follow rpm / km/h through the dial calibration |
| `Wiper_Driver`, `Wiper_Passenger` | spindle pivot | spindle = local **+Y** | parked at 0; sweep by the wiper angle |
| any node whose name contains `GLASS_FRONT` | windshield pane | (none) | the only pane that gets windshield rain and the wiper clear |

Corners: rig `RL`/`RR` are Swish corners `BL`/`BR` (index 0 FL, 1 FR, 2 BL, 3 BR). Car lights (`Headlight_L`, `*_Lamp`, …)
are a separate exact-name convention in [CarLights.h](../src/scene/Entity/CarLights.h).

**Frames.** The asset is authored glTF Y-up, nose **+Z**, driver/left **+X**, with no `RootNode`. Each pivot's world
matrix goes through the same transform as the vertices:

$$
(x,y,z)\mapsto(z,\,y,\,-x)\quad\text{(nose }+X\text{, up }+Y\text{, right }+Z\text{)},\qquad y\mathrel{-}=y_{ground},\qquad \text{columns normalized}
$$

Column $k$ of the normalized frame is the image of the node's local axis $k$, so the axes in the table survive the swap.

**Wheel checks** (`car_rig_wheel_frame`; any failure → `WHEEL RECOVERY FAILED`, rigid wheels):

| Check | Limit |
|---|---|
| hub lies in its named corner's quadrant | sign of $x$ and $z$ |
| hub position | $0.4 \le |x| \le 1.2$ m, $0.8 \le |z| \le 1.8$ m, $0.2 \le y \le 0.6$ m (authored frame) |
| spinning pieces under `Wheel_XX` | ≥ 1 |
| axle direction | $|a_z| \ge 0.9$ in car space (within ~26° of lateral) |
| rolling radius = grounded hub height | 0.25–0.45 m |

The wheel frame is rebuilt orthonormal from the hub's local X (col0 = axle, col1 = up orthogonalized against it,
col2 = col0 × col1), so a mirrored or scaled pivot still gives a proper rotation. `spin_sign` is $-1$ when the axle
points to $+Z$.

## 2. Rigging a new car in Blender

1. **Scale and axes.** Model at real size, metres. Keep the car's nose on Blender **−Y** and its left on **+X**: the
   exporter's Y-up swap, glTF $(x,y,z)$ = Blender $(x, z, -y)$, then gives glTF nose +Z and left +X, the frame above.
   Check the glTF axes after export rather than trusting the Blender view.
2. **Pivots are Empties, not bones.** One Empty per pivot, parented as an ordinary object. Never apply transforms to the
   Empties (that bakes the pivot into the child mesh). Parent the meshes with **Keep Transform**. Pivot scale stays
   `(1,1,1)` on both sides; mirror left/right by rotation, not negative scale. The full rule list is in
   [car-asset-pipeline.md](car-asset-pipeline.md) §2.
3. **Pivot placement.** The Y-up exporter maps an Empty's local axes as glTF $(X, Y, Z)$ = Blender $(X, Z, -Y)$:

   | Pivot | Place at | glTF axis (§1) | Blender local axis |
   |---|---|---|---|
   | `Wheel_*`, `Caliper_*` | hub centre | axle +X | +X |
   | `SteeringWheel` | rim centre, on the column | column +Z (the loader flips it to face the driver if needed) | −Y |
   | `Needle_*` | the dial's centre pin | sweep −Z (toward the driver) | +Y |
   | `Wiper_*` | the spindle, **normal to the glass**; parked pose = angle 0 | spindle +Y | +Z |
4. **Wiper spindle check.** A spindle that is off the glass normal drives the blade into the glass mid-sweep. On the
   993, S0a fixed this by re-solving each spindle's Euler angles to a fitted glass normal, then giving the child mesh the
   inverse correction so the parked pose did not move:

   | 993 blade gap over 0–120° (+ = above the glass) | |
   |---|---|
   | before S0a | −74 mm (inside the glass) |
   | after S0a | −1.2 mm to +5.7 mm |

5. **Names.** Exact names from §1. The front glass node name must contain `GLASS_FRONT`.
6. **Export.** Export only the `Collection` collection (it leaves out the `RIG` collection with the `RIG_Controls`
   driver Empty). Use Y-up, no animations (the pose is driven at runtime), lights **on** (the `*_Lamp` nodes are
   `KHR_lights_punctual`), and no cameras. Headless, as S0a did it:

   ```python
   # blender -b porsche_rigged.blend --python export.py -- out.glb
   import bpy, sys
   out = sys.argv[sys.argv.index('--') + 1]
   vl = bpy.context.view_layer
   vl.active_layer_collection = vl.layer_collection.children['Collection']
   bpy.ops.export_scene.gltf(
       filepath=out, export_format='GLB', export_yup=True,
       use_active_collection=True, use_active_collection_with_nested=True,
       export_animations=False, export_lights=True, export_cameras=False)
   ```

7. **Check the load log.** `make debug` with `SWISH_CAR=<abs path>` should print `named rig, wheelbase … track …`,
   one `wheel FL/FR/BL/BR … radius … spin sign …` line per corner, `(windshield: 1)` and `wiper arm reach … m` per arm.
   It must not print `WHEEL RECOVERY FAILED`. Then add `tests/test_car_rig.cpp`-style checks for the new GLB.
8. **Credit and licence.** Add a row to [assets/CREDITS.md](../assets/CREDITS.md).

## 3. The math

All in car space (nose $+X$, up $+Y$, right $+Z$, metres). $F$ is a normalized pivot frame and $M_{car}$ the entity model
matrix (which carries the 1000 WU/m scale).

**Pivot conjugation.** A cabin part turned by angle $\alpha$ about its local axis $\hat a$:

$$
M=F\,R_{\hat a}(\alpha)\,F^{-1},\qquad \text{draw model}=M_{car}\,M
$$

| Part | $\hat a$ | $\alpha$ |
|---|---|---|
| wiper | $+Y$ | wiper angle $\theta(u)$ |
| needle | $-Z$ | $-\,\mathrm{rad}(\theta_{dial}-\theta_{rest})$ (clockwise for the driver) |
| steering wheel | $+Z$ | $-\delta\rho$ (`R_z(-sw_angle)`, with the debug axis correction $C$ between $F$ and $R$) |

**Wheels.** Corner $c$ with frame $F_c$, hub $P_c$, drawn steer $\delta_c$, spin angle $\theta_c$:

$$
M_c=M_{car}\,T_{travel}\,\underbrace{T(P_c)\,R_y(-\delta_c)\,T(-P_c)}_{\text{steer}}\,A_c\,\underbrace{F_c\,R_x(s_c\,\theta_c)\,F_c^{-1}}_{\text{spin}}
$$

Calipers use the same matrix without the spin factor. $A_c$ is the Phase 1 camber/toe delta (identity at rest).

**Per-wheel rolling speed.** $v_x$ is forward speed, $v_l$ lateral (right-positive), $\dot\psi$ yaw rate (right-positive),
$(x_i,z_i)$ the hub relative to the entity origin:

$$
v_i=(v_x-\dot\psi\,z_i)\cos\delta_i+(v_l+\dot\psi\,x_i)\sin\delta_i,\qquad
\omega_i=\frac{v_i}{R_i},\qquad
\theta_i\leftarrow\operatorname{wrap}_{2\pi}\!\big(\theta_i+k_{spin}\,\omega_i\,\Delta t\big)
$$

In a right turn the left (outer) wheels roll faster. `CarEntity::m_wheel_omega` stores $\omega_i$, and the visuals
integrate only from it, so the Phase C slip model only has to write $\omega$.

**Ackermann (draw-only).** The bicycle angle $\delta$ is a virtual centre wheel; pure Ackermann puts the turn centre on
the rear-axle line. A front hub at $z_w=\pm t/2$ (measured front track $t$, wheelbase $L$):

$$
\cot\delta_w=\cot\delta-\frac{z_w}{L}
\iff
\delta_w=\arctan\frac{L\tan\delta}{L-z_w\tan\delta}
\quad\Longrightarrow\quad
\cot\delta_o-\cot\delta_i=\frac{t}{L}
$$

$$
\delta_{c}=\delta+k\,(\delta_w-\delta),\qquad k=\texttt{ackermann}\in[0,1]
$$

The rears are always 0. The physics keeps the single $\delta$.

**Wiper motion.** One wipe is phase $u\in[0,1)$, going park → top → park:

$$
\theta(u)=\Theta\;\mathrm{mix}\!\Big(1-|2u-1|,\ \tfrac{1-\cos 2\pi u}{2},\ e\Big),\qquad \dot u=\frac{\text{wpm}}{60}
$$

| Mode | Rate | At the end of a wipe |
|---|---|---|
| Off | Low rate, only while finishing the current wipe | parks; never stops mid-glass |
| Intermittent | Low | parks, dwells `intermittent_dwell_s`, wipes again |
| Low / High | 45 / 65 wipes/min | straight into the next wipe |

A mode switch keeps the blade where it is and clears any pending dwell.

**Wiper clear.** One angle drives the arms and the rain. For a rigged arm with measured tip $\mathbf{tip}$ (pivot-local),
the wiped segment is $P(\alpha,f)=F\,R_y(\alpha)\,(f\,\mathbf{tip})$ for $f\in[0.3,1]$ (`kWiperBladeInnerFrac`).
`WindshieldRainPass::update_wiper` projects it to screen UV at last frame's angle $(a_0,b_0)$ and this frame's $(a_1,b_1)$;
[windshield_wetness.frag](../shaders/windshield_wetness.frag) clears

$$
c(\mathbf q)=\max\!\Big(\mathbb 1[\mathbf q\in\square(a_0,b_0,b_1,a_1)],\ 1-\mathrm{smoothstep}\big(b_x,b_y,\ d(\mathbf q,\overline{a_1b_1})\big)\Big),\qquad W\leftarrow W\,(1-c)
$$

with band $(0.008, 0.02)$ (aspect-corrected UV). A car with no `Wiper_*` pivots uses one analytic screen-space blade:
pivot $(0.5,0.92)$, reach 0.95, band $(0.04,0.13)$, screen angle $-1.15+2.30\,\mathrm{clamp}(\alpha/\Theta,0,1)$ rad.

**Dials.** Angles are degrees clockwise from 12 o'clock, as the driver sees them.

$$
\theta_{tach}=-145°+36°\cdot\frac{\mathrm{clamp}(\text{rpm},\,0,\,7600)}{1000}
$$

$$
\theta_{spd}(v)=\theta_{j-1}+\frac{v-v_{j-1}}{v_j-v_{j-1}}\,(\theta_j-\theta_{j-1}),\qquad v_{j-1}<v\le v_j\ \ (\text{clamped to the table ends})
$$

**Needle follower.** A critically damped spring with $\omega_n=1/\tau$,
$\ddot x=-2\omega_n\dot x-\omega_n^2(x-x^*)$, stepped with the exact solution for a target held over $\Delta t$
(`needle_follow`). The step does not depend on frame rate, is stable at any $\Delta t$, and does not overshoot from rest:

$$
e=x-x^*,\quad k=\dot x+\omega_n e,\qquad
x'=x^*+(e+k\,\Delta t)\,e^{-\omega_n\Delta t},\qquad
\dot x'=(\dot x-\omega_n k\,\Delta t)\,e^{-\omega_n\Delta t}
$$

**Startup sweep.** For the first `sweep_duration_s` after load, $\theta=\theta_{min}+f\,(\theta_{max}-\theta_{min})$, where
$f$ smoothsteps from 0 to 1 over the first half and back over the second.

## 4. Dial calibration (993 RS)

Measured off the painted faces in the GLB atlas.

**Tach** ("1/min ×1000", numerals 1–7, red zone 6800–7600):

| rpm | Painted numeral | Fit $-145+36\,\text{rpm}/1000$ | Error |
|---|---|---|---|
| 1000 | −109.0° | −109° | 0.0° |
| 4000 | −0.4° | −1° | −0.6° |
| 7000 | +105.5° | +107° | +1.5° |

A least-squares fit $(-144.3°, 35.75°/1000)$ would bring the worst error to 0.9°; the rounded fit ships.

**Speedo** (km/h 0–300, non-linear; 0 is extrapolated, since the face has no 0):

| km/h | 0 | 10 | 20 | 60 | 100 | 140 | 180 | 220 | 260 | 300 |
|---|---|---|---|---|---|---|---|---|---|---|
| θ (deg) | −141 | −133 | −125.2 | −90.9 | −55.9 | −18.9 | 19.1 | 58.5 | 98.5 | 138.1 |

**Needle rest poses** (where each mesh points with no rotation): tach −144°, speedo −151°.

## 5. Config keys ([config/car.toml](../config/car.toml))

`CarConfig::load` reads the file at startup in **both** builds (toml++). A missing file or key keeps the C++ default, and
a test checks that the file equals those defaults. When you change a value, change the TOML and the matching C++
default together. Debug builds seed the Tunables sliders from the file.

| Table | Key | Default | Range / rule | C++ default | Debug slider |
|---|---|---|---|---|---|
| `[wheels]` | `ackermann` | 1.0 | clamp 0–1 | `CarWheelConfig` | `wheels` group |
| `[gauges]` | `tach_offset_deg` | −145.0 | | `kGaugeDefaults` | `gauges` group |
| | `tach_deg_per_krpm` | 36.0 | > 0 | | |
| | `tach_min_rpm`, `tach_max_rpm` | 0, 7600 | max > min | | (TOML only) |
| | `speedo_kmh`, `speedo_deg` | table above | 2–16 equal-length rows, km/h strictly increasing, deg non-decreasing; else ignored | `kSpeedoLut993` | (TOML only) |
| | `rpm_rest_deg`, `speed_rest_deg` | −144, −151 | | | |
| | `needle_tau_ms` | 80 | > 0 | | |
| | `sweep_duration_s` | 1.2 | ≥ 0 | | |
| `[wipers]` | `low_wipes_per_min` | 45 | clamp 1–120 | `WiperParams` | `wiper` group |
| | `high_wipes_per_min` | 65 | clamp 1–120 | | |
| | `intermittent_dwell_s` | 4.0 | clamp 0–30 | | |
| | `sweep_deg` | 105 | clamp 10–120 (blades leave the 993 glass past ~120°) | | |
| | `ease` | 1.0 | clamp 0–1 (0 linear, 1 cosine) | | |

If any `[gauges]` value is invalid (`gauge_params_valid`), the whole table resets to the defaults. Debug-only test
controls (`test_on`, `test_rpm`, `test_kmh`) live in `Tunables.def`, not in the TOML.

## 6. Car profiles

`CarProfile` ([CarProfile.h](../src/scene/Entity/CarProfile.h)) is one car's full physics:

| Field | Type | Holds |
|---|---|---|
| `name` | `string_view` | label |
| `car` | `CarParams` | mass, power (reference), drivetrain efficiency, $C_d$, frontal area, $\mu$, wheelbase |
| `tire` | `TireParams` | $a$, $b$, $I_z$, $C_{\alpha f}$, $C_{\alpha r}$, $h_{cg}$ |
| `gearbox` | `GearboxParams` | 7 ratio slots, `gearCount`, final drive, shift rpm, dwell |
| `torque` | `TorqueCurve` | up to 12 (rpm, N·m) points + fuel-cut `redline` |
| `drivetrain` | `DrivetrainParams` | front torque share (0/0 = RWD) |
| `wheelRadiusM` | float | driven radius until the loader's wheel frames arrive |
| `steerLockDeg` | float | road-wheel lock, each side |
| `steerRatio` | float | steering-wheel deg per road-wheel deg |

`car_profile_valid` (a `static_assert` on both shipped profiles) checks the Ch01 gearbox validators, the torque curve,
upshift < redline, $a+b=L$, understeer ($C_{\alpha r}b > C_{\alpha f}a$) and positive sizes.

| | `turbo_s_992()` (default) | `rs_993(measured)` |
|---|---|---|
| Drive / box | AWD, 7-speed PDK | RWD, G50/31 6-speed: 3.154 / 2.000 / 1.522 / 1.242 / 1.024 / 0.820, FD 3.444 |
| Mass | 1670 kg | 1279 kg |
| Engine | 992 Turbo S table, cut 7200 | 300 PS at 6500, 355 N·m at 5400, cut 6800 |
| Steering | 35° lock, 450/35 ratio | 29.5° lock, 16.5:1 (2.70 turns lock to lock) |
| Wheelbase / radius | 992 constants | measured from the GLB when 1.8–3.2 m / 0.25–0.45 m, else 2.272 / 0.320 m |

**Selection.** `App::load_car` ([AppInit.cpp](../src/core/App/AppInit.cpp)) applies `rs_993(&geometry)` when the loader
reports a named rig **and** recovered all four wheels (`get_rig_geometry()->named_rig`). Every other car keeps the
default, which is exactly the pre-profile behaviour; the goldens pin it. `apply_profile` also resets wet-grip
`muDry`, re-clamps the steer angle and the gear, and moves the debug Telemetry redline (`get_redline_rpm`).

## Known limits

- **Not yet checked by eye.** The unit tests pass; the in-app checklist is
  [tasks/rigged-porsche-app-checks.md](../tasks/rigged-porsche-app-checks.md).
- **Wiper arm mid-span gap.** After S0a the blade sits within −1.2 to +5.7 mm of the glass, but the arm's mid-span stands
  16–29 mm off it. That comes from the mesh, not the spindle.
- **993 numbers from memory.** The gear ratios, final drive and steering ratio/turns were written without a source to
  check against (flagged `[spec?]` / "from memory" in `CarProfile.h`). Weight split, inertia, cornering stiffness and
  $h_{cg}$ are engineering estimates. Drag is calibrated to the 277 km/h top speed.
- **Mesh vs physics size.** The 993 mesh measures a 2.259 m wheelbase against a real 2.272 m; the profile uses the
  measured value so the wheels and physics agree.
- **Wheel spin is kinematic.** No slip yet. Phase C (wheel slip and lockup) will write `wheel_omega` and leave the draw
  path unchanged.
- **Ackermann is draw-only.** It also applies to the default car's fronts when its wheel frames are recovered.
- **The old heuristic loader** (`Combined3DWheel*`, `Steering_Wheel`, the translation-clustering path in `load_car`) is
  due for deletion once the rigged car becomes the default.
- **Tach fit** is 1.5° off at the 7k numeral (see §4).

## Credit

1995 Porsche 911 Carrera RS (993) by **ddiaz-design** (Sketchfab), licensed CC BY-NC-SA 4.0: attribution required,
non-commercial use only, and derivatives must use the same licence. Full row and terms:
[assets/CREDITS.md](../assets/CREDITS.md).
