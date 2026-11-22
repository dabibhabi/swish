# Rigged Porsche (993 RS): what to check in the app

Branch `feature/rigged-porsche`. The unit tests all pass (352 release, 377 debug), but **nothing here has been checked by eye
yet**. One debug session covers all four parts. Tick each box when you've seen it. Where something looks wrong, each part says where to fix it.

```sh
pkill -x swish; SWISH_CAR=/Users/adminh/Current_Projects/swish/assets/blend/porsche_rigged.glb make debug
```

Keys: **backtick** switches between edit and drive mode (ImGui), **C** cycles the camera, **R** cycles rain (off → light → heavy),
**V** cycles the wipers, and the **arrows** drive.

---

## 0. Load (look at the terminal)
- [ ] There's no `WHEEL RECOVERY FAILED`, and the load log shows `(windshield: 1)`.
- [ ] The debug Telemetry redline reads **6800**, which means the 993 profile is active. If it reads 7200, the named rig wasn't
      detected; check `car_rig_pivot_from_node` in [`CarRig.h`](../src/scene/Entity/CarRig.h).

## 1. Wheels and steering
- [ ] **Ackermann:** in edit mode, under Steering, tick Override and set Angle to **29.5**. Press C for the Side or Helicopter camera.
  - Correct: the **right** front turns more than the left (about 34.5° vs 25.7°).
  - Road Wheels → Ackermann at **0** makes the two fronts parallel.
  - *If the left front turns more:* flip `zw` in `CarEntity::get_wheel_steer_rad`.
- [ ] **Calipers:** at the same pose, each caliper stays with its rotor at both Ackermann values and never spins.
  - *If one spins or lags:* check the `s.wheel_spins` branch in `CarEntity::get_draw_calls`.
- [ ] **Steering wheel:** in the Cockpit camera at override +29.5, the rim turns about **487° clockwise** (2.7 turns lock to lock).
  - *If it turns the wrong way:* check `car_rig_steering_frame` in [`CarRig.h`](../src/scene/Entity/CarRig.h).
- [ ] **Spin:** drive in the Chase camera and take a tight turn. Set Spin multiplier to about 0.1 to see it.
  - The wheels roll forward, and the outer wheels spin a little faster.
  - On crown changes, the wheels and calipers bounce together.
  - *If the wheels roll backwards:* check `spin_sign`.

## 2. Wipers (test both rain extremes)
- [ ] **Modes:** in the cockpit, press **R** until the rain is heavy. Each press of **V** should print `[wiper] Intermittent`, then `Low`, `High`, and
      `Off`.
- [ ] **Motion:** the blades park flat along the bottom of the glass, toward the driver. They sweep up to about 105° (near the top, toward the passenger
      side), slow down at each reversal, and on **Off** they finish the current wipe and park; they never stop mid-glass.
- [ ] **Clearing:** the drop-free area sits exactly where the blades swept.
  - *If it lags or is offset:* check `kWiperBladeInnerFrac` and `project_to_screen` in `WindshieldRainPass.cpp`.
  - *If the cleared band is too thin or wide:* change `kRigBand`.
- [ ] **Rain 0:** no drops and no clearing artifacts.
- [ ] **Blades lift or clip the glass:** lower Wet / Rain → `Wiper sweep (deg)`. The Blender fix keeps them within −1.2 to +5.7 mm of the glass up to 120°.
- [ ] **Rain looks faint on this raked windshield:** the front gate `smoothstep(0.30, 0.60, forwardness)` in `windshield_rain.frag` lets through
      about 65%; lower it if needed.
- [ ] **Default car** (run again without `SWISH_CAR`): the screen-space blade now parks at the left and sweeps right. This is the intended release
      change.

## 3. Dashboard needles (cockpit)
- [ ] **Startup sweep:** at load, both needles sweep to full scale (tach about 7600, speedo 300) and back in about 1.2 s. The tach then settles at about 970 rpm.
- [ ] **Calibration:** under Gauge Needles, tick "Force test readings".
  - Set rpm to **1000 / 4000 / 7000**, then km/h to **60 / 140 / 260**.
  - Each needle should sit on its painted mark and glide there without overshoot.
- [ ] **If something's off:**
  - Both needles run backwards: flip `kNeedleClockwiseSign` in [`GaugeNeedles.h`](../src/scene/Entity/GaugeNeedles.h).
  - Constant offset on one needle: change "Tach / Speedo needle rest".
  - Tach error that grows with rpm: change the slope or offset. The fit is known to be 1.5° off at 7k; a least-squares fit of (−144.3, 35.75) brings it within 0.9°.
  - Speedo wrong between marks: edit `speedo_kmh` / `speedo_deg`.
  - Needles bounce or lag: change "Needle damping tau".
- **Saving a fix:** copy any corrected value into [`config/car.toml`](../config/car.toml) `[gauges]` **and** `kGaugeDefaults`, because a test pins the two
  equal. The same goes for `[wheels]` / `[wipers]` and their C++ defaults.

## 4. How it drives (993 RS profile)
- [ ] It feels like a lighter, rear-drive, 300 PS car: 6 gears, fuel cut at 6800, top speed about 277 km/h (about 172 mph).
  - The steering is slower than the 992's (16.5:1).
  - The gear ratios and steering spec are from memory, so they're marked as estimates in `CarProfile.h`.

---

When this all passes, tell Claude. The remaining Stage 2 steps are then: **make the rigged car the default**, and **delete the old heuristic
loader** (`Combined3DWheel*` / `Steering_Wheel`).
