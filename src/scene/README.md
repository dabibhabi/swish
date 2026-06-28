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

The car uses a kinematic bicycle model: a 2-wheel simplification that treats the front axle as a single steerable wheel and the rear axle as fixed.

### Velocity & Drag

Speed decays with exponential drag (exact solution to $\dot{v} = -k_\text{drag} \cdot v$):

$$v(t + dt) = v(t) \cdot e^{-k_\text{drag} \cdot dt}$$

This is **time-step independent** — unlike Euler integration ($v \mathrel{-}= k \cdot v \cdot dt$), which accumulates error at large $dt$. The value $k_\text{drag}$ is `kDragCoeff` in `CarEntity.h`.

Throttle and brake add/subtract a fixed impulse per frame before the drag step:

$$v \mathrel{+}= \text{throttle} \cdot k_\text{accel} \cdot dt - \text{brake} \cdot k_\text{brake} \cdot dt$$

Speeds below `kSpeedDeadZone` (0.5 m/s) are snapped to zero to prevent infinite creep.

### Steering

Raw steering input is clamped to the physical wheel-lock limit before computing the turn radius. This prevents $\tan(\delta)$ from blowing up near $\pm 90°$:

$$\delta = \text{clamp}\!\left(\delta_\text{input},\; -\delta_\text{max},\; +\delta_\text{max}\right)$$

$$\delta_\text{max} = \arctan\!\left(\frac{k_\text{WheelLockToDeg} \cdot \pi}{180}\right)$$

The steering wheel visual rotation uses `kSteerRatio` to convert road wheel angle to column angle (typically $\approx 16:1$ for a rack-and-pinion).

### Turn Rate (Yaw Rate)

From the bicycle model geometry:

$$\dot{\psi} = \frac{v \cdot \tan(\delta)}{L}$$

where:
- $\psi$ = yaw (heading)
- $v$ = current speed
- $\delta$ = (clamped) front wheel steering angle
- $L$ = wheelbase (distance between axles)

Integrated with Euler for the heading:

$$\psi_{t+dt} = \psi_t + \dot{\psi} \cdot dt$$

### Position Update

$$x_{t+dt} = x_t + v_t \cos(\psi_t) \cdot dt$$

$$z_{t+dt} = z_t + v_t \sin(\psi_t) \cdot dt$$

### Heading Wrap

Heading is kept in $(-\pi, \pi]$ via `fmod` to prevent floating-point drift on long drives:

$$\psi = \text{fmod}(\psi + \pi,\; 2\pi) - \pi$$

</details>

---

## RoadScene (`RoadScene/`)

Procedurally generates I-495 highway geometry from a `RoadConfig` loaded from a baked binary (`road.bin`).

<details>
<summary><strong>RoadConfig Fields</strong></summary>

| Field | Effect |
|-------|--------|
| `lane_width` | Width of each lane in feet (converted to metres via `kFt`) |
| `lane_count` | Number of lanes per direction |
| `segment_length` | Length of one road tile in metres |
| `kCrownSlope` | Road surface cross-slope for drainage (radians) |
| `billboard_spacing` | Distance between tree billboards along the shoulder |
| `barrier_height` | Jersey barrier height in metres |
| `lamp_spacing` | Street lamp interval in metres |

</details>

<details>
<summary><strong>Generation Algorithm</strong></summary>

`RoadScene::generate()` is single-pass. A `RoadLayout` struct is computed once at the top:

```
RoadLayout {
    total_width   = lane_width * lane_count * 2 + shoulder_width * 2
    barrier_left  = -(total_width / 2)
    barrier_right =  (total_width / 2)
    crown_y(x)    = -|x| * tan(kCrownSlope)   // road surface elevation at lateral offset x
}
```

This struct is threaded to all sub-generators (road surface, markings, barriers, shoulders, billboards, lamps, overpass) so no sub-generator recomputes widths independently. Billboards use a Wang integer hash seeded by segment index to get deterministic pseudo-random rotations without `rand()`.

</details>

---

## Camera (`Camera/`)

FPS-style perspective camera. Stores angles as $(yaw, pitch)$ in degrees; computes view matrix via `glm::lookAt`.

<details>
<summary><strong>Coordinate System & View Matrix</strong></summary>

```
+X = right   +Y = up   −Z = forward (into screen)
Yaw   0° = looking along −Z
Yaw −90° = looking along +X  (default spawn)
Pitch +90° = looking straight up  (clamped to ±89° to avoid gimbal lock)
```

Forward vector from angles:

$$\hat{f} = \begin{pmatrix} \cos(\text{yaw}) \cos(\text{pitch}) \\ \sin(\text{pitch}) \\ \sin(\text{yaw}) \cos(\text{pitch}) \end{pmatrix}$$

View matrix: $V = \text{lookAt}(\mathbf{p},\ \mathbf{p} + \hat{f},\ \hat{Y})$

</details>

<details>
<summary><strong>Movement & Cockpit Bounds</strong></summary>

**Free-fly:** WASD moves along the camera's forward/right/up axes. Sprint multiplier $k_\text{sprint} = 3\times$ when Shift is held:

$$\Delta\mathbf{p} = s_\text{base} \cdot k_\text{sprint} \cdot \hat{d} \cdot dt$$

**Cockpit mode:** `App` overrides `m_position` and `m_yaw` each frame from `CarEntity::get_model_matrix()`. The camera is additionally clamped to a `Bounds2D` cage:

```cpp
struct Bounds2D { float min_x, max_x, min_y, max_y; };
```

This prevents the camera from clipping through the dashboard or B-pillar.

`asin` used for pitch reconstruction is clamped to $[-1, 1]$ before calling to prevent `NaN` from floating-point rounding past the domain boundary.

</details>

---

## ModelManager (`ModelManager/`)

Loads GLB files via `tinygltf`. Produces normalized `MeshData` + `DrawCall[]` ready for `SceneGeometry::upload()`.

<details>
<summary><strong>Node-Walk Normalization</strong></summary>

The raw GLB node tree encodes transforms as $W_\text{node} = W_\text{root} \cdot T_\text{local}$. `ModelManager` bakes all node transforms relative to the root node in a single traversal:

$$M_\text{bake} = W_\text{root}^{-1} \cdot W_\text{node}$$

Applied to every vertex position, normal, and tangent. After baking, a second pass computes the axis-aligned bounding box and normalizes positions into a unit AABB centered at the origin:

$$\mathbf{p}_\text{norm} = \frac{\mathbf{p}_\text{baked} - \mathbf{c}}{\max(\text{extents})}$$

where $\mathbf{c}$ is the AABB center and $\text{extents} = (\text{max} - \text{min}) / 2$.

Rotation and bounding-box passes are merged into one node traversal. `pixel_count` for each primitive is hoisted out of the inner loop. `alphaMode == BLEND` primitives (glass) are skipped.

See [`docs/car_system.md`](../../docs/car_system.md) for the full algorithm with diagrams.

</details>

---

## Diagrams

- [docs/diagrams/scene-data-flow.excalidraw](../../docs/diagrams/scene-data-flow.excalidraw) — asset → GPU → draw calls
