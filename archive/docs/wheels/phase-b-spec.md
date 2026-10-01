# Wheel Phase B — quarter-car suspension spec (992 GT3 Touring)

Status: spec, 2026-08-15. Supersedes the ride-rate numbers in [`README.md`](../../../docs/wheels/README.md)
(`fn ≈ 1.4 Hz`, `ζ ≈ 0.45`) and [`research/03-implementation-plan.md`](research/03-implementation-plan.md)
`:568-577`, which are passenger-comfort values from when the target was a 992 Turbo S.

> **Path note (2026-09-29):** `CarPhysics.h:NN` and `App.cpp:NNN` citations predate the deep-clean split. `CarPhysics.h`
> is now an umbrella over `CarParams.h`, `Powertrain.h`, `Tire.h`, `Drivetrain.h`, `Longitudinal.h`, `Bicycle.h`,
> `WetGrip.h`; `App.cpp` is split into `App.cpp` / `AppInit.cpp` / `AppInput.cpp`. Use symbols with `rg`.

Produced by two independent derivations (a `suspension-planner` agent and the main session) that were
cross-checked against each other. Where they disagreed, the resolution is recorded inline — the
disagreements were the most valuable output.

## 0. Target car — already documented in-repo

[`research/02-graphics-vulkan.md`](research/02-graphics-vulkan.md) `:100-101` records the asset as a
Sketchfab-exported **911 GT3 Touring 2022**, and `:162` notes tyre sizes 255/35 ZR20 front /
315/30 ZR21 rear — GT3 spec, not Turbo S. The repo already knew; `CarPhysics.h`'s `CarParams`
(1670 kg, 478 kW, AWD) was simply never updated. Correcting that is **Wave 2 step 3**, deliberately
out of Phase B scope because it ripples into the already-tested P0 longitudinal/lateral physics.

**This spec is written so it does not care.** Corner mass is derived from `CarParams::mass` at
runtime, and the design inputs are *ride frequency and damping ratio*, not raw `k`/`c`. When step 3
retargets 1670 → ~1418 kg, `k` and `c` re-derive automatically and the ride frequencies — the thing
actually being specified — stay correct.

## 1. Ride rates

| Corner | `fn` (Hz) | `ζ` |
|---|---|---|
| Front | **2.3** | **0.55** |
| Rear | **2.5** | **0.60** |

**These are engineering estimates, not a sourced OEM spring sheet.** Reasoning: since `fn ∝ √k` and
GT3s run roughly 2–3× a base Carrera's linear rate, the comfort/sports band of 1.3–1.6 Hz maps up by
√2–√3 ≈ 1.41–1.73× → ≈2.2–2.6 Hz. Damping is set at the firm end so the visible response is one
crisp overshoot rather than a wallow; road cars sit ~0.25–0.4, race cars 0.5–0.7, and a visual-only
suspension is better slightly over-damped than bouncy. Rear stiffer than front follows the flat-ride
convention and suits a rear-engined car.

At the old 1.4 Hz the front rate would be ~2.2× too soft for this car — hence superseded, not
inherited.

## 2. Per-corner state

```cpp
// WheelKinematics.h — pure SI, no GLM, matching the existing house style.
struct CornerSpring {
    float x_m   = 0.f;  // vertical offset from neutral (m); + = corner sits HIGHER
                        // than the already-Y-snapped car origin (§5 sign convention)
    float v_mps = 0.f;  // dx/dt
};

struct SuspensionParams {          // Phase-B scoped; does NOT touch CarParams/TireParams
    float fnFrontHz      = 2.3f;
    float zetaFront      = 0.55f;
    float fnRearHz       = 2.5f;
    float zetaRear       = 0.60f;
    float travelMaxM     = 0.035f; // ±35 mm bump/rebound — estimate, tunable (§9)
    float unsprungMassKg = 40.f;   // per corner
};
```

On `CarEntity`: `std::array<CornerSpring,4> m_corner_susp{}` — corner order 0 FL · 1 FR · 2 BL · 3 BR,
matching `WheelFrame` / `Submesh::wheel_corner`. All four start `{0,0}`.

**Corner sprung mass**, reusing the same axle split already used for tyre loads (`CarPhysics.h:97-98`):

```
m_sprung_total = CarParams::mass - 4 * unsprungMassKg
m_corner_front = m_sprung_total * (b / (a+b)) / 2     // b/L = 39.2%
m_corner_rear  = m_sprung_total * (a / (a+b)) / 2     // a/L = 60.8%
```

At today's 1670 kg: **296 kg front / 459 kg rear**. After the step-3 retarget to ~1418 kg:
**245 kg / 384 kg**. No code change needed between those — that is the point.

## 3. `suspension_step`

$$m\,\ddot x = -k\,(x - x_{\text{target}}) - c\,\dot x, \qquad k = m(2\pi f_n)^2, \qquad c = 2\zeta\sqrt{km}$$

Semi-implicit (symplectic) Euler — **velocity first, then position using the new velocity**. That
ordering is what makes this energy-stable; forward Euler on the same system gains amplitude every
cycle.

```cpp
inline void spring_from_ride_freq(float m_kg, float fn_hz, float zeta,
                                  float& k_n_per_m, float& c_ns_per_m) {
    const float omega_n = kTwoPi * fn_hz;
    k_n_per_m  = m_kg * omega_n * omega_n;
    c_ns_per_m = 2.f * zeta * std::sqrt(k_n_per_m * m_kg);
}

// One semi-implicit Euler step of a quarter-car corner. Contract: stable iff
// h < (2/omega_n)*(sqrt(1+zeta^2) - zeta). The caller (suspension_advance)
// guarantees this; this function does not check it.
inline CornerSpring suspension_step(CornerSpring s, float x_target_m,
                                    float k_n_per_m, float c_ns_per_m,
                                    float m_kg, float h_s, float travel_max_m) {
    const float a = -(k_n_per_m/m_kg)*(s.x_m - x_target_m) - (c_ns_per_m/m_kg)*s.v_mps;
    s.v_mps += a * h_s;                 // velocity FIRST
    s.x_m   += s.v_mps * h_s;           // position with the NEW velocity
    if (s.x_m >  travel_max_m) { s.x_m =  travel_max_m; s.v_mps = std::min(s.v_mps, 0.f); }
    if (s.x_m < -travel_max_m) { s.x_m = -travel_max_m; s.v_mps = std::max(s.v_mps, 0.f); }
    return s;
}
```

**Travel limits without ringing.** Clamp, then zero *only* the velocity component still driving into
the wall. This is deliberately **not** a progressive bump-stop spring: that would raise local `ωₙ` and
could itself violate the §4 bound at the slider ceiling. A projection onto the convex interval
`[-travel_max, travel_max]` is **nonexpansive** (1-Lipschitz), so composing it after an already-stable
linear recurrence can only remove energy, never add it.

## 4. Stability bound

The semi-implicit one-step map has `det A = 1 − 2ζωₙh`, `tr A = 2 − 2ζωₙh − ωₙ²h²`; the Jury
conditions collapse to

$$\boxed{\;h < \frac{2}{\omega_n}\left(\sqrt{1+\zeta^2}-\zeta\right)\;}$$

Note this **reduces to `hω < 2` at ζ = 0**, and is *stricter* for ζ > 0 — damping tightens the bound
rather than loosening it, which is the easy thing to get wrong. The bound depends only on `ωₙ` and
`ζ`, **not on mass**, provided `k` and `c` are derived from `(m, fn, ζ)` — so §0's live-mass
derivation cannot destabilise it.

| Case | `fn`, `ζ` | `h_max` | Margin over 1/240 |
|---|---|---|---|
| Old doc | 1.4, 0.45 | 147.0 ms | 35.3× |
| GT3 front | 2.3, 0.55 | 81.8 ms | **19.6×** |
| GT3 rear | 2.5, 0.60 | 72.1 ms | **17.3×** |
| Slider ceiling | 6.0, 1.5 | 16.1 ms | 3.85× |

**`h = 1/240 s` does not need to change for the stiffer GT3 springs** — this was the main open
question. Even the deliberately-extreme slider ceiling keeps 3.85×. Reaching the bound at 1/240 would
need `fn ≈ 23 Hz` (`k ≈ 6.25 MN/m`), ~100× the GT3 estimate; the fix there would be more substeps,
not an architecture change.

## 5. Substepping

Adaptive count, **no persistent accumulator** — `h_used ≤ h_target` always satisfies §4, and there is
no replay/determinism requirement that would justify the extra hidden state:

```cpp
inline CornerSpring suspension_advance(CornerSpring s, float x_target_m,
                                       float k_n_per_m, float c_ns_per_m, float m_kg,
                                       float dt_s, float h_target_s, float travel_max_m,
                                       int max_substeps = 32) {
    dt_s = std::min(dt_s, max_substeps * h_target_s);
    const int   N = std::max(1, static_cast<int>(std::ceil(dt_s / h_target_s)));
    const float h = dt_s / static_cast<float>(N);
    for (int i = 0; i < N; ++i)
        s = suspension_step(s, x_target_m, k_n_per_m, c_ns_per_m, m_kg, h, travel_max_m);
    return s;
}
```

At `h_target = 1/240` and the app's existing global dt cap (`App.cpp:292`, 1/15 s), N = 16.

The local `max_substeps = 32` is a deliberate second guard: the App-level cap already bounds N, but
it is shared and mutable elsewhere. This makes an extreme hitch cost at most 32 substeps regardless —
the suspension visually lags one stalled frame rather than doing unbounded work that lengthens the
*next* frame and cascades.

## 6. Road input

`App.cpp:359-361` already sets `pos.y = road_surface_y(pos.x)` from the car **origin** X each frame.
Phase B must not re-decide the car's bulk height — it supplies only the per-corner **residual**:

```
h_road_i  = road_surface_y(world X of corner i's hub)   // from car_model * wf.frame
x_target_i = (h_road_i - road_surface_y(pos.x)) / kWorldUnitsPerMeter
```

On flat road all four targets are 0 and the suspension sits at neutral, so the resting pose is
identical to today's. It responds to crown, grade change and cross-slope — differences the body snap
cannot express — rather than fighting the snap.

## 7. Draw-matrix composition

Today (`CarEntity.cpp:170-231`):

```
wheelM[c] = car_model · steer · spin
  spin  = F · R_x(sign·θ) · F⁻¹
  steer = T(P) · R_y(−δ) · T(−P)
```

Insert the offset **left of steer**, in car-local space:

```
wheelM[c] = car_model · T(0, x_c, 0) · steer · spin
steerM[c] = car_model · T(0, x_c, 0) · steer
```

Left of `steer` means the steer pivot `P` is unaffected by travel — correct, since the upright
carries the pivot. At `x_c = 0` the translation is identity and the product is bit-identical to
today's.

> **UNITS — this bit the first implementation.** `x_c` goes in **as metres, unconverted**. The
> translation is applied *after* `car_model`, which already carries the entity scale of 1000, so it
> lives in the same mesh space `kSeatEye` is authored in (`App.cpp:411-415`). An earlier draft of
> this spec wrote `T(0, x_c · WU_PER_M, 0)`; that scales by 1000 **twice** and throws each wheel
> about **14 m** into the air on ordinary road crown (0.0145 m × 1000 × 1000 = 14,500 WU). The
> symptom is empty wheel arches and wheels visibly detached from the car while driving. Test 8 below
> exists to catch exactly this.

## 8. The invariant

**Never write `m_position` or `m_rotation` from the suspension path.** Structure so violating it is
hard: `update_wheel_visuals(dt)` touches only `m_corner_susp`; the offset is applied *only* inside
`get_draw_calls` as a local matrix; the call site sits in `App.cpp` **after** `m_treadmill.update(...)`
(~`:367`) so bounds and rebase have settled.

Verification grep (`tasks/bench-run-checklist.md` §4) — must return nothing:

```bash
git diff -U0 -- src/scene/Entity/CarEntity.cpp src/scene/Entity/CarEntity.h src/core/App/App.cpp \
  | grep -E '^\+' | grep -E 'm_position|m_rotation'
```

## 9. Squat / dive / roll — deferred to B2

Load transfer needs `ax`/`ay` fed into corner forces, which couples suspension to the longitudinal and
lateral models that **Wave 2 step 3 is about to rewrite** (GT3 retarget, AWD→RWD, TMeasy-lite).
Landing kinematic travel first keeps the change isolated and attributable; layering load transfer onto
a model that is itself moving would make any regression impossible to localise.

When it lands:

$$\Delta F_{z,\text{long}} = \frac{m\,a_x\,h_{cg}}{L}, \qquad \Delta F_{z,\text{lat}} = \frac{m\,a_y\,h_{cg}}{t}$$

with `h_cg ≈ 0.45 m`, `L = 2.45 m`, track `t ≈ 1.55 m`; per-corner offset `Δx = ΔF_z / k`.

## 10. Tests (`tests/test_car_physics.cpp`)

```cpp
// 1. Settles to equilibrium from a displaced start.
CornerSpring s{0.03f, 0.f};
for (int i = 0; i < 2400; ++i) s = suspension_step(s, 0.f, kF, cF, mF, 1.f/240.f, 0.035f);
REQUIRE(std::abs(s.x_m)   < 1e-3f);
REQUIRE(std::abs(s.v_mps) < 1e-3f);

// 2. No amplitude growth — the property forward Euler fails.
CornerSpring s{0.03f, 0.f}; float late = 0.f;
for (int i = 0; i < 240; ++i) s = suspension_step(s, 0.f, kR, cR, mR, 1.f/240.f, 0.035f);
for (int i = 0; i < 240; ++i) { s = suspension_step(s, 0.f, kR, cR, mR, 1.f/240.f, 0.035f);
                                late = std::max(late, std::abs(s.x_m)); }
REQUIRE(late < 0.03f);

// 3. Travel clamped under extreme input.
CornerSpring s{};
for (int i = 0; i < 600; ++i) s = suspension_step(s, 10.f, kF, cF, mF, 1.f/240.f, 0.035f);
REQUIRE(s.x_m <=  0.035f + 1e-6f);
REQUIRE(s.x_m >= -0.035f - 1e-6f);

// 4. Symmetry — equal input gives equal corners.
REQUIRE(left.x_m == Approx(right.x_m));

// 5. Stability at the slider ceiling (fn=6, zeta=1.5) stays finite and bounded.
REQUIRE(std::isfinite(s.x_m));
REQUIRE(std::abs(s.x_m) <= 0.035f + 1e-6f);

// 6. Rest is exactly rest — guards the "unchanged at rest" property of §6/§7.
CornerSpring s{};
s = suspension_step(s, 0.f, kF, cF, mF, 1.f/240.f, 0.035f);
REQUIRE(s.x_m   == 0.f);
REQUIRE(s.v_mps == 0.f);

// 7. Adaptive substepping never exceeds the stability bound.
REQUIRE(dt_used / N <= 1.f/240.f + 1e-9f);
```

## 11. Debug-UI tunables

Into the existing "Road Wheels" section (`DebugUI.cpp:428`), following the established `DebugParams`
field → `#ifdef SWISH_DEBUG_UI` consumption pattern:

| Slider | Range | Default |
|---|---|---|
| Suspension enabled | bool | true |
| Front ride freq (Hz) | 1.0 – 6.0 | 2.3 |
| Rear ride freq (Hz) | 1.0 – 6.0 | 2.5 |
| Front ζ | 0.1 – 1.5 | 0.55 |
| Rear ζ | 0.1 – 1.5 | 0.60 |
| Travel limit (m) | 0.01 – 0.10 | 0.035 |
| Visual gain | 0.0 – 3.0 | 1.0 |

`k` and `c` are derived from `(fn, ζ, m)` rather than exposed directly — tuning in frequency space is
what keeps the numbers interpretable and the §4 bound checkable. The slider ceilings (6 Hz, ζ 1.5) are
the stress case test 5 must survive. **Visual gain** exaggerates travel for the temp-force step of the
visual-verification loop without changing the physics.
