# src/scene

Scene management, procedural geometry, entity physics, and asset loading.

## Scene Switching

`SceneManager::set_active_scene(lambda)` stores a callable that sets up all geometry and entities for a scene. The lambda is re-invoked after every swapchain recreation so GPU resources are never stale.

```cpp
sceneManager.register_scene("highway", [&]() {
    RoadScene road;
    road.generate(services);
    renderer.upload_scene_geometry(road.getMeshData(), road.getDrawCalls());
    renderer.set_scene_lights(road.getLights());
    renderer.set_camera(new Camera(...));
});
sceneManager.set_active_scene("highway");
```

Switching scenes calls `vkDeviceWaitIdle`, tears down current scene geometry, then runs the new lambda.

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

The car uses a kinematic bicycle model: a 2-wheel simplification treating the front axle as one steerable wheel and the rear axle as fixed. Heading $\psi$ and steering angle $\delta$ are stored in **degrees** (`m_rotation.y`, `m_steering_angle`) and converted to radians for trig. The mesh is normalized so the nose points $+X$ at $\psi=0$.

### Throttle / Brake (`handle_input`)

Arrow UP adds throttle; DOWN brakes if moving forward, else reverses:

$$v \mathrel{+}= k_\text{accel}\,dt \quad\text{(UP)}; \qquad v \mathrel{-}= \begin{cases} k_\text{brake}\,dt & v > 0 \\ k_\text{accel}\,dt & v \le 0 \end{cases}\ \text{(DOWN)}$$

then clamped to $v \in [-k_\text{maxReverse},\ k_\text{maxForward}]$.

### Drag (`update`)

Speed decays with **true exponential drag** (exact solution to $\dot{v} = -k_\text{drag}\,v$):

$$v(t + dt) = v(t)\, e^{-k_\text{drag}\, dt}$$

This is time-step independent, unlike Euler decay ($v \mathrel{-}= k v\,dt$). Below $k_\text{deadZone}$ the speed snaps to 0; if speed is exactly 0, the update early-returns (no turning in place).

### Steering

Input integrates the angle and auto-returns to center, then clamps to the road-wheel limit (it is the **angle**, not a radius, that is clamped — there is no $\arctan$ here):

$$\delta \mathrel{+}= \pm k_\text{steerRate}\,dt \quad\text{(LEFT/RIGHT)}; \qquad \delta \to 0 \text{ at } k_\text{steerReturn}\,dt \text{ (no input)}$$

$$\delta = \operatorname{clamp}(\delta,\ -k_\text{maxSteer},\ +k_\text{maxSteer})$$

### Yaw Rate & Position

$$\dot{\psi} = \frac{v}{L}\tan(\delta) \quad[\text{rad/s}], \qquad \psi \mathrel{-}= \deg(\dot{\psi})\, dt$$

Yaw *decreases* for positive (right) steering because $R_y$ is counter-clockwise from above. Heading is wrapped to $(-180°, 180°]$:

$$\psi = \operatorname{fmod}(\psi + 180,\ 360) - 180$$

Position advances along the heading, with forward defined to match the mesh nose:

$$\hat{f} = (\cos\psi,\ 0,\ -\sin\psi), \qquad \mathbf{p} \mathrel{+}= \hat{f}\, v\, dt$$

then $x$ is clamped to the road bounds (`set_road_bounds`).

### Steering-Wheel Animation

The visual wheel column angle scales the road-wheel angle by the steering ratio, applied as a rotation about the local $Z$ axis of the steering-wheel submesh:

$$\theta_\text{column} = \delta \cdot k_\text{steerRatio}, \qquad k_\text{steerRatio} = \frac{k_\text{WheelLockToDeg}}{k_\text{maxSteer}} = \frac{450}{35} \approx 12.86$$

### Constants (`CarEntity.h`, tuned for the 1000 WU/m scale)

| Constant | Value | Meaning |
|----------|-------|---------|
| `kMaxForwardSpeed` | 30 000 WU/s | ≈ 108 km/h |
| `kMaxReverseSpeed` | 8 000 WU/s | |
| `kAccel` | 18 000 WU/s² | throttle / reverse |
| `kBrakeAccel` | 24 000 WU/s² | braking |
| `kDragCoeff` | 2.5 /s | exponential drag rate |
| `kSpeedDeadZone` | 0.5 WU/s | dead-stop threshold |
| `kMaxSteer` | 35° | road-wheel clamp |
| `kWheelLockToDeg` | 450° | steering-wheel lock-to-lock |
| `kSteerRate` | 90°/s | steer-in rate |
| `kSteerReturn` | 120°/s | return-to-center rate |
| `kWheelbase` | 2 800 WU | ≈ 2.8 m axle spacing ($L$) |

</details>

---

## RoadScene (`RoadScene/`)

Procedurally generates highway geometry from a `RoadConfig`. Defaults model the I-495 Long Island Expressway near Jericho, Nassau County. Configure via setters (`set_lane_width`, etc.) or construct from a baked `RoadConfig`, then call `generate()` which returns a `SceneData { meshData, drawCalls, lights }`.

All dimensions are in **world units** (`WORLD_SCALE = 1000` WU per metre). `kFt = 0.3048f * WORLD_SCALE` converts feet → WU.

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

`kCrownSlope = 0.02f` (a 2% cross-slope for drainage) is a `static constexpr`, **not** a serialized field. The struct is `#pragma pack(push, 1)` and `static_assert`-ed trivially copyable for safe binary I/O. See [tools/README.md](../../tools/README.md) for the baker workflow and [docs/data-types.md](../../docs/data-types.md) for the full struct.

</details>

<details>
<summary><strong>Generation Algorithm</strong></summary>

`generate()` computes a small `RoadLayout` once and threads it (by `const&`) to every section generator, so none of them recompute road widths independently:

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
trees · ambient_occlusion · hov_diamonds · sign_posts · overpass
sound_barriers · exit_ramp · street_lamps
```

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
