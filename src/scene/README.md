# src/scene

Scene management, procedural geometry, entity physics, and asset loading.

## Scene Switching

`Scene` is an alias for a callable (`using Scene = std::function<void(Renderer&)>`, no wrapper class) that sets up all geometry and entities for a scene; `SceneManager` owns a vector of scenes and `set_active_scene(int index)` runs one. The callable is re-run after every swapchain recreation so GPU resources are never stale. `App::init_scenes` (`core/App/AppInit.cpp`) builds the single road scene like this:

```cpp
std::vector<Scene> scenes;
scenes.push_back([this](Renderer& renderer) { build_road_scene(renderer); });
m_sceneManager = std::make_unique<SceneManager>(*m_renderer, std::move(scenes));
m_sceneManager->set_active_scene(0);
```

Switching scenes waits for GPU idle, clears the old geometry, runs the new callable, then rebuilds material descriptors. (The `register_scene(name, lambda)` API in older docs never matched this code; `Renderer::register_scene_manager` was deleted 2026-09-29.)

---

## Entity Hierarchy

```
Entity
  position · rotation (Euler) · scale
  get_model_matrix() → chained glm::translate · glm::rotate · glm::scale
  │
  └── ModelEntity
        GLB mesh reference, material ID
        │
        └── CarEntity
              bicycle-model physics
              steering wheel submesh animation
```

<details>
<summary><strong>CarEntity — Bicycle Model Physics</strong></summary>

The car uses a kinematic bicycle model for steering/position: a 2-wheel simplification treating the front axle as one steerable wheel and the rear axle as fixed. Heading $\psi$ and steering angle $\delta$ are stored in **degrees** (`m_rotation.y`, `m_steering_angle`) and converted to radians for trig. The mesh is normalized so the nose points $+X$ at $\psi=0$. Longitudinal speed and lateral/yaw dynamics, however, are **not** kinematic — see the two subsections below; `CarEntity.h` only owns steering/speed-cap knobs (`kMaxSteer`, `kSteerRate`, ...), not the force model.

### Throttle / Brake — force balance, not exponential drag (`Longitudinal.h::longitudinal_accel`)

`CarPhysics.h` is now a 25-line umbrella over topic headers in `src/scene/Entity/`: `CarParams.h` (`CarParams`, `TireParams`), `Powertrain.h`, `Tire.h`, `Drivetrain.h`, `Longitudinal.h`, `Bicycle.h`, `WetGrip.h`. Below, "`CarPhysics.h`" means whichever of these holds the symbol.

`handle_input` only sets intent (`m_throttle`, `m_brake`, `m_reverse` ∈ `{0,1}`/bool); `update()` integrates a real longitudinal force balance in SI units (`Longitudinal.h`), converting to/from world units at the `CarEntity` boundary:

$$m\dot{v} = F_\text{drive} - F_\text{brake} - \tfrac{1}{2}\rho C_d A v^2 - C_\text{rr} m g$$

$F_\text{drive}$ is power-limited ($P_\text{max}\eta_\text{drivetrain}/v$, floored at $v=3\,\text{m/s}$ to avoid the $1/v$ singularity) and capped by **rear-axle-only traction** (the 992 GT3 is RWD), including longitudinal load transfer under acceleration — see `rear_traction_cap_force`:

$$a_x = \frac{\mu g\,(a/L)}{1 - \mu\,h_\text{cg}/L} \approx 8.22\ \text{m/s}^2 \quad\text{(traction-limited launch)}$$

$F_\text{brake} = \mu m g$ acts on **all four wheels** regardless of drivetrain layout (brakes aren't drive-limited). Top speed and 0–60 time therefore **emerge** from this balance rather than being hardcoded — see `steady_state_top_speed()` and `tests/test_car_physics.cpp`. Reverse is a separate bounded low-speed creep (`kReverseAccel`), not run through this model. Below `kSpeedDeadZone` the speed snaps to 0; if speed is exactly 0, `update()` early-returns (no turning in place).

### Steering

Input integrates the angle and auto-returns to center, then clamps to the road-wheel limit (it is the **angle**, not a radius, that is clamped — there is no $\arctan$ here):

$$\delta \mathrel{+}= \pm k_\text{steerRate}\,dt \quad\text{(LEFT/RIGHT)}; \qquad \delta \to 0 \text{ at } k_\text{steerReturn}\,dt \text{ (no input)}$$

$$\delta = \operatorname{clamp}(\delta,\ -k_\text{maxSteer},\ +k_\text{maxSteer})$$

### Yaw Rate & Position — dynamic bicycle model above ~5 m/s (`Bicycle.h::dynamic_bicycle_deriv`)

Below `kBlendSpeed` (5 m/s) yaw rate is the pure kinematic relation:

$$\dot{\psi}_\text{kin} = \frac{v}{L}\tan(\delta) \quad[\text{rad/s}]$$

Above it, lateral velocity and yaw rate are integrated from a dynamic bicycle model with saturating (grip-limited) tire forces, then blended with $\dot\psi_\text{kin}$ over 5→8 m/s and clamped to the friction circle ($|\dot\psi| \le \mu g / v_x$, `max_yaw_rate`) so the car can understeer/slide instead of following a rigid geometric arc — see `TireParams` (axle geometry $a,b$, cornering stiffnesses, CG height) in `CarParams.h`. In both regimes:

$$\psi \mathrel{-}= \deg(\dot{\psi})\, dt$$

Yaw *decreases* for positive (right) steering/yaw-rate because $R_y$ is counter-clockwise from above. Heading is wrapped to $(-180°, 180°]$:

$$\psi = \operatorname{fmod}(\psi + 180,\ 360) - 180$$

Position advances along the heading plus lateral drift, with forward defined to match the mesh nose:

$$\hat{f} = (\cos\psi,\ 0,\ -\sin\psi), \qquad \mathbf{p} \mathrel{+}= (\hat{f}\, v + \hat{r}\, v_\text{lateral})\, dt$$

then $x$ is clamped to the road bounds (`set_road_bounds`).

### Steering-Wheel Animation

The visual wheel column angle scales the road-wheel angle by the steering ratio, applied as a rotation about the local $Z$ axis of the steering-wheel submesh:

$$\theta_\text{column} = \delta \cdot k_\text{steerRatio}, \qquad k_\text{steerRatio} = \frac{k_\text{WheelLockToDeg}}{k_\text{maxSteer}} = \frac{450}{35} \approx 12.86$$

### Constants — steering/speed-cap knobs (`CarEntity.h`, tuned for the 1000 WU/m scale)

The **longitudinal + tire dynamics** constants (mass, power, drag, tire μ, axle geometry, cornering stiffness) live in `CarParams`/`TireParams` in `CarParams.h`, in **SI units** (kg, W, m, N/rad, ...) — not here, and not in world units. `CarEntity.h` keeps only the kinematic/control knobs below:

| Constant | Value | Meaning |
|----------|-------|---------|
| `kMaxForwardSpeed` | 92 000 WU/s | normalization only — real top speed is **emergent**, see `Bicycle.h::steady_state_top_speed` |
| `kMaxReverseSpeed` | 12 000 WU/s | ≈ 27 mph, hard cap on reverse |
| `kReverseAccel` | 6 000 WU/s² | gentle low-speed reverse creep (not run through the force model) |
| `kSpeedDeadZone` | 0.5 WU/s | dead-stop threshold |
| `kMaxSteer` | 35° | road-wheel clamp |
| `kWheelLockToDeg` | 450° | steering-wheel lock-to-lock |
| `kSteerRate` | 90°/s | steer-in rate |
| `kSteerReturn` | 120°/s | return-to-center rate |
| `kWheelbase` | 2 450 WU | ≈ 2.45 m axle spacing ($L$); matches `TireParams::a + b` in `CarParams.h` |

`CarParams` (992 GT3 Touring, RWD, `CarParams.h`): `mass` 1418 kg · `powerMax` 375 000 W (503 hp) · `dragCoeff` 0.34 · `frontalArea` 2.05 m² · `tireMu` 1.10. `TireParams`: `a`/`b` 1.49/0.96 m (CG→front/rear axle) · `hCg` 0.45 m (CG height, drives longitudinal load transfer) · `cAlphaFront`/`cAlphaRear` 80 000/150 000 N/rad.

</details>

---

## Corridor (`Corridor/`) — the real Long Island Expressway

**This is the road the game actually drives** (Phase 3a, 2026-08-23). A finite **17.994 mi**
corridor derived from real OpenStreetMap data — the actual I-495 centreline from the Queens-Midtown
Tunnel to exit 49 — compacted from 31.20 real miles. It **replaced** the `RoadScene` +
`Treadmill` endless-treadmill road below, which is now unwired.
Design + as-built numbers: [`docs/phase-3-osm-lie-corridor.md`](../../docs/phase-3-osm-lie-corridor.md).

| File | Role |
|---|---|
| `CorridorAsset.h` | Packed binary alignment format + loader. Baked offline by [`tools/lie_corridor.py`](../../tools/lie_corridor.py); a missing/stale asset **throws** rather than falling back to the fictional road. |
| `CorridorAlignment.{h,cpp}` | The curve, in double precision. Converts `(station, lateral) ↔ (x, z)`, samples position/tangent/**fractional** lane count, and answers cross-section offsets. |
| `CorridorScene.{h,cpp}` | Mesh generation as the free function `build_corridor_segments` (was a one-method `CorridorScene` class): segment (1.2 km) → tile (300 m) → cross-section band. |
| `CorridorDriver.{h,cpp}` | Origin rebase, visible-segment window, corridor-space lateral clamp, terminus stops. |
| `CorridorBounds.cpp` | The engine-free half of `CorridorDriver` (alignment attach + drivable bounds), split out so the unit tests link it without Vulkan/GLFW. |

**The one idea to understand.** The retired road could treat "along the road" as world Z and
"across the road" as world X, because it was axis-aligned. A real alignment curves, so every query
the game needs — surface height, drivable bounds, how far along you are — is phrased in the
corridor's own frame, `(station s, lateral t)`, with `t` positive to the right of travel
(eastbound). `CorridorAlignment` is the only thing that converts between frames, and **both the mesh
generator and the driving code use it**, so the surface you see and the surface you drive on cannot
drift apart.

Consequences worth knowing before touching this code:

- `CarEntity`'s own world-X clamp is **disabled** (bounds set wide open); the lateral clamp happens
  in `CorridorDriver` in corridor space.
- The committed carriageway (`m_side`) is **state**, not the sign of the current lateral offset. That
  is what makes the median barrier impassable — see the comment in `CorridorBounds.cpp`.
- `RoadGeometry::road_surface_y(x)` is redirected to a **flat** height via `set_flat_surface`,
  because the crown formula derives height from world X. This stretch of the LIE is flat anyway.
- Two subdivision levels exist for two different reasons: **segments** carry one vertex buffer and
  one double-precision render offset; **tiles** carry one draw call per band and one arc-length UV
  restart (300 m, a multiple of every surface texture tile size — same reason the retired canonical
  chunk was 300 m).

---

## RoadScene (`RoadScene/`)

> **Retired as the active road (2026-08-23)** — replaced by `Corridor/` above. `generate()`,
> `generate_chunk`, `generate_service_roads` and `generate_interchange` are no longer called by
> `App`, so they upload nothing and drive zero draws. They are kept (and still covered by
> `tests/test_road_scene.cpp`) because stage 3b replaces the interchange geometry and should delete
> them then. `MeshBuilder` is very much still live: the corridor uses it, including the new
> `emit_sweep_band` / `emit_sweep_wall` / `emit_sweep_dashes` primitives alongside the original
> `emit_ribbon`.

Procedurally generates highway geometry from a `RoadConfig`. Defaults model the I-495 Long Island Expressway near Jericho, Nassau County. Configure via setters (`set_lane_width`, etc.) or construct from a baked `RoadConfig`, then call `generate()` which returns a `SceneData { meshData, drawCalls, lights }`.

All dimensions are in **world units** (`util::units::kWuPerMetre` = 1000 WU per metre, [`utils/units/Units.h`](../utils/units/Units.h)). `RoadScene::kFt` is an alias of `util::units::kWuPerFt` (304.8 WU per foot). Shared cross-section constants (`road::kLie`, `kService`, `kExit`, `kCrownSlope`, `snap_dash_to_chunk`) live in [`RoadScene/RoadParams.h`](RoadScene/RoadParams.h); `RoadGeometry` reads the same values.

| File | Contents |
|------|----------|
| `RoadScene.h` / `RoadScene.cpp` | class, config ctor/getters/setters, the two ordered element tables (`intro_elements()`, `chunk_elements()`), `generate()`, `generate_chunk()` |
| `MeshBuilder.{h,cpp}` | quad / ribbon / sweep primitives shared with the corridor |
| `RoadScene_Surfaces.cpp` | grass, road surfaces, shoulders, markings, curbs, rumble strips, AO, dirt strips |
| `RoadScene_Furniture.cpp` | jersey barrier, guardrail, HOV diamonds, sign posts, overpass, marginal trees, sound barriers, street lamps |
| `RoadScene_Ramps.cpp` | service roads, crossover, interchange, exit ramp |
| `RoadConfig.h`, `RoadParams.h` | baked-config struct; shared constexpr road constants |

<details>
<summary><strong>RoadConfig Fields</strong> (real layout — see RoadConfig.h)</summary>

| Group | Fields |
|-------|--------|
| Header | `magic` (0x53575243 "SWRC"), `version` (3) |
| Road geometry | `road_length`, `lane_width`, `lane_count`, `shoulder_width_wb`, `shoulder_width_eb`, `grass_extent` |
| Barrier | `barrier_width`, `barrier_height` |
| Guardrail | `rail_width`, `rail_height` |
| Curb | `curb_height`, `curb_width` |
| Texture tiling | `asphalt_tile`, `grass_tile`, `concrete_tile`, `metal_tile` |
| Markings | `marking_y_offset`, `dash_length`, `dash_gap` |
| Colors / tints | `shoulder_tint`, `barrier_tint`, `rail_tint`, `white_marking`, `yellow_marking`, plus per-material tints |

`RoadConfig::kCrownSlope` (= `road::kCrownSlope` = 0.02f, a 2% cross-slope for drainage) is a `static constexpr`, **not** a serialized field. The struct is `#pragma pack(push, 1)` and `static_assert`-ed trivially copyable for safe binary I/O. See [tools/README.md](../../tools/README.md) for the baker workflow and [docs/data-types.md](../../docs/data-types.md) for the full struct.

</details>

<details>
<summary><strong>Generation Algorithm</strong></summary>

`generate()` computes a small `RoadLayout` once and threads it (by `const&`) to every section generator, so none of them recompute road widths independently. The generators run in the order of the `intro_elements()` table (the chunk path walks `chunk_elements()`); `tests/test_road_scene.cpp` pins that order:

```cpp
struct RoadLayout {
    float eb_start;    // eastbound start X (barrier + clearance)
    float road_width;  // total road width (lane_count * lane_width)
    float wb_inner;    // westbound inner edge (-road_width)
};
```

Each generator builds its geometry over a `[z_near, z_far]` span via a shared `MeshBuilder` (which exposes `addHorizontalQuad`, `addVerticalFace`, `addSlopedQuad`, `addDashedLine`). The section generators are:

```
grass · road_surfaces · shoulders · jersey_barrier · guardrail
solid_markings · dashed_markings · curbs · rumble_strips · dirt_strips
ambient_occlusion · hov_diamonds · sign_posts · overpass
sound_barriers · exit_ramp · marginal_trees · street_lamps
```

(The old full-corridor `generate_trees` was dead code and was deleted in the 2026-09-29 clean-up; only `generate_marginal_trees` remains.)

`street_lamps` is the only generator that also appends to the `lights` vector (one `LightDesc` per lamp).

</details>

---

## Camera (`Camera/`)

FPS-style perspective camera. Stores angles as $(yaw, pitch)$ in degrees; computes the view matrix via `glm::lookAt`.

<details>
<summary><strong>Coordinate System & View Matrix</strong></summary>

```
+X = right   +Y = up
Pitch clamped to ±89° (avoids gimbal lock / lookAt degeneracy)
```

Forward vector from angles:

$$\hat{f} = \begin{pmatrix} \cos(\text{yaw})\cos(\text{pitch}) \\ \sin(\text{pitch}) \\ \sin(\text{yaw})\cos(\text{pitch}) \end{pmatrix}$$

So at pitch 0: yaw $0° \to +X$, yaw $90° \to +Z$, yaw $180° \to -X$, yaw $-90° \to -Z$. (Note the car uses a *different* forward convention, $(\cos\psi, 0, -\sin\psi)$ — the two are independent.)

View: $V = \operatorname{lookAt}(\mathbf{p},\ \mathbf{p} + \hat{f},\ \hat{Y})$

Projection uses `glm::perspective` with the Vulkan Y-flip — a classic gotcha versus OpenGL:

```cpp
proj[1][1] *= -1.0f;   // Vulkan clip-space Y points down
```

The legacy `set_target()` API back-computes yaw/pitch from a direction; its `asin` argument is `clamp`ed to $[-1, 1]$ so floating-point overshoot can't produce `NaN`.

</details>

<details>
<summary><strong>Movement & Collision Bounds</strong></summary>

**Free-fly (spectator):**
- **W / S** move along forward *flattened to the XZ plane* (`normalize(forward.x, 0, forward.z)`) — you don't fly up/down by looking up
- **A / D** strafe along the right vector
- **Q / E** move straight up / down
- **Left Shift** doubles speed:

$$\Delta\mathbf{p} = s_\text{base}\cdot k_\text{sprint}\cdot \hat{d}\cdot dt, \qquad k_\text{sprint} = 2$$

**Collision bounds:** when enabled (`set_collision_enabled(true)`), the free-fly position is clamped to a `Bounds2D` box so the spectator can't pass through the barriers/guardrails:

```cpp
struct Bounds2D { float min_x, max_x, min_y, max_y; };
```

**Cockpit mode** is separate: `App` overrides the camera's position and yaw each frame from `CarEntity::get_model_matrix()`, so the camera rides with the car. This is driven by `App`, not by the `Camera` class itself.

</details>

---

## ModelManager (`ModelManager/`)

Loads GLB files via `tinygltf`. Produces normalized `MeshData` + `DrawCall[]` ready for `SceneGeometry::upload()`.

<details>
<summary><strong>Node-Walk Normalization</strong></summary>

The raw GLB node tree encodes each mesh's world transform as $W_\text{node}$. `ModelManager` bakes every node transform **relative to the root node** so the result is independent of how the artist parented things:

$$M_\text{bake} = W_\text{root}^{-1} \cdot W_\text{node}$$

applied to position, normal, and tangent of every vertex. The mesh is then oriented and grounded into the engine's convention: a $+90°$ rotation about $Y$ and a vertical offset so the wheels sit at $Y = 0$, leaving the **nose pointing $+X$ at yaw 0** (matching `CarEntity`'s forward vector). The same normalized frame is recorded per steering-wheel submesh as `Submesh::sw_pivot_frame` so the wheel can be rotated about its true pivot.

`alphaMode == BLEND` primitives (glass) are skipped. See [`docs/car_system.md`](../../docs/car_system.md) for the full algorithm with diagrams.

</details>

---

## See Also

- [docs/data-types.md](../../docs/data-types.md) — exact layouts for `MeshData`, `DrawCall`, `Submesh`, `LightDesc`, `RoadConfig`, `MaterialId`
- [docs/diagrams/scene-data-flow.excalidraw](../../docs/diagrams/scene-data-flow.excalidraw) — asset → GPU → draw calls
