# Car System — Implementation Guide

## Coordinate Convention (read this first)

One definition of "forward" is shared by the mesh, the physics, and the
renderer, so the body always lines up with the direction of travel:

- The loader **normalizes the mesh** so the nose points **+X** at yaw 0.
- Heading: `forward = R_y(yaw)·(+X) = (cos(yaw), 0, −sin(yaw))`.
- Increasing yaw turns the body **left** (counterclockwise from above), so
  positive steering (= right) *decreases* yaw.
- **yaw = +90° faces −Z** — down the road, the same way the camera looks.

## Load-Time Mesh Normalization (node-walk loader)

`ModelManager::load_car()` walks the glTF node tree and bakes each mesh
node's transform **relative to the node named `RootNode`** into its
vertices. The chain *above* RootNode (Sketchfab axis conversion × FBX unit
scale, which nets ~10×) must not be baked; the chains *below* it place
pivoted parts — steering wheel, wipers, dials — correctly. 1,665 of the
1,667 mesh nodes carry a non-identity transform, which is why the interior
was missing before this existed. RootNode-relative space is the asset's raw
mesh space: meters, Y-up, nose +Z.

```mermaid
flowchart LR
    GLB["porsche.glb<br/>1667 mesh nodes, 20 materials"]
    Walk["node walk: bake<br/>inverse(W_RootNode) · W_node<br/>positions · normals (inv-transpose)<br/>· tangents"]
    Glass["skip alphaMode == BLEND<br/>(3 glass prims — opaque G-buffer<br/>would wall off the cockpit;<br/>forward pass = Phase 5)"]
    Rotate["rotate +90° about Y<br/>(x,y,z) → (z,y,−x)<br/>nose +Z → +X"]
    Ground["ground: shift Y so<br/>min.y = 0 (tire contact)"]
    Gate{{"log bbox gate:<br/>4.57 × 1.29 × 2.03 m ✓<br/>(real 911 height — the old 1.66<br/>was misplaced geometry)"}}

    GLB --> Walk --> Glass --> Rotate --> Ground --> Gate
```

After this, `set_position(y = 0)` means "tires resting on the road surface"
(the road plane is at world Y = 0) and `set_rotation(0, 90, 0)` points the
nose down −Z.

## Cockpit Camera

The app starts in **cockpit mode**; `C` toggles cockpit ↔ free-fly.
No scene graph — the eye is *derived* from the car transform each frame:

```mermaid
flowchart LR
    Seat["kSeatEye = (−0.32, 1.05, −0.34)<br/>meters, normalized mesh space<br/>(behind/above wheel center<br/>at (0.18, 0.76, −0.34))"]
    MCar["× M_car<br/>(entity scale 1000<br/>converts m → WU)"]
    Eye["camera position"]
    Heading["car heading θ"]
    Look["+ mouse look<br/>yaw ±150°, pitch ±60°"]
    Yaw["camera yaw = −θ + look<br/>(camera fwd = (cos, ·, sin);<br/>car fwd = (cos, 0, −sin))"]

    Seat --> MCar --> Eye
    Heading --> Yaw
    Look --> Yaw
```

In cockpit mode WASD camera flight is disabled (arrows drive the car);
in free-fly the camera detaches wherever it was and WASD/mouse work as
before. Mouse-look offsets reset on every toggle.

## Class Structure

```mermaid
classDiagram
    class Entity {
        #Vec3 m_position
        #Vec3 m_rotation
        #Vec3 m_scale
        +set_position(Vec3)
        +set_rotation(Vec3)
        +set_scale(Vec3)
        +get_model_matrix() Mat4
        +update(float dt)*
    }

    class ModelEntity {
        -MeshData m_mesh
        -vector~Submesh~ m_submeshes
        +set_mesh_data(MeshData)
        +add_submesh(Submesh)
        +get_draw_calls() vector~DrawCall~
    }

    class CarEntity {
        -float m_forward_speed
        -float m_steering_angle
        -float m_min_x
        -float m_max_x
        +handle_input(GLFWwindow*, float dt)
        +update(float dt)
        +set_road_bounds(float, float)
        +get_speed() float
    }

    class ModelManager {
        -Renderer* m_renderer
        +load_car(string path) unique_ptr~CarEntity~
    }

    class Renderer {
        -SceneGeometry m_sceneGeometry
        -SceneGeometry m_dynamicGeometry
        +upload_dynamic_geometry(MeshData, DrawCalls)
        +update_dynamic_draw_calls(DrawCalls)
        +destroy_dynamic_geometry()
    }

    class SceneGeometry {
        -VkBuffer m_vertexBuffer
        -VkBuffer m_indexBuffer
        -vector~DrawCall~ m_drawCalls
        +upload(services, MeshData, DrawCalls)
        +record_draws(cmd, pipeline, materials)
        +update_draw_calls(DrawCalls)
    }

    Entity <|-- ModelEntity
    ModelEntity <|-- CarEntity
    ModelManager --> CarEntity : creates
    Renderer --> SceneGeometry : m_dynamicGeometry
    App --> CarEntity : owns m_car
    App --> Renderer : calls update_dynamic_draw_calls
```

---

## Per-Frame Data Flow

```mermaid
stateDiagram-v2
    [*] --> PollInput : glfwPollEvents

    PollInput --> ModeToggle : C key (edge-detected)
    PollInput --> CameraInput : WASD + mouse (free-fly only)
    PollInput --> CarInput : Arrow keys

    CameraInput --> CameraUpdate : camera.process_keyboard()
    CarInput --> CarPhysics : car.handle_input()

    CarPhysics --> CarPhysics : clamp speed + steering
    CarPhysics --> PositionUpdate : car.update()

    PositionUpdate --> DrawCallRegen : car.get_draw_calls()
    note right of DrawCallRegen
        Stamps current
        model matrix into
        each DrawCall
    end note

    DrawCallRegen --> DynamicGeometry : renderer.update_dynamic_draw_calls()

    PositionUpdate --> CockpitCam : cockpit mode
    note right of CockpitCam
        eye = M_car · kSeatEye
        yaw = −heading + look_yaw
        pitch = look_pitch
    end note

    CameraUpdate --> RenderFrame : renderer.drawFrame()
    CockpitCam --> RenderFrame
    DynamicGeometry --> RenderFrame

    RenderFrame --> GBufferPass : recordGBufferPass()
    GBufferPass --> StaticGeometry : m_sceneGeometry.record_draws()
    GBufferPass --> DynGeometry2 : m_dynamicGeometry.record_draws()

    StaticGeometry --> LightingPass
    DynGeometry2 --> LightingPass
    LightingPass --> Composite
    Composite --> [*] : vkQueuePresentKHR
```

---

## Car Physics — State Diagram

```mermaid
stateDiagram-v2
    [*] --> Idle

    Idle --> Accelerating : UP arrow pressed
    Idle --> Reversing : DOWN arrow (from stop)

    Accelerating --> Coasting : UP released
    Accelerating --> Braking : DOWN pressed (speed > 0)
    Accelerating --> Accelerating : UP held\nspeed += kAccel × dt

    Coasting --> Idle : speed < 0.5
    Coasting --> Coasting : speed *= (1 - kDragCoeff × dt)

    Braking --> Coasting : DOWN released
    Braking --> Reversing : speed reaches 0, DOWN still held
    Braking --> Braking : speed -= kBrakeAccel × dt

    Reversing --> Coasting : DOWN released
    Reversing --> Reversing : speed -= kAccel × dt\n(negative direction)

    Accelerating --> Steering : LEFT or RIGHT held
    Reversing --> Steering : LEFT or RIGHT held
    Steering --> Accelerating : key released, steering → 0
    note right of Steering
        yaw_rate = (speed / wheelbase)
                 × tan(steering_angle)
        Bicycle model — radius
        shrinks with more steer
    end note
```

---

## Changing Car Position

The car is spawned in `App.cpp` around line 182:

```cpp
car_entity->set_position(Vec3(6501.0f, 0.0f, -20000.0f));
car_entity->set_rotation(Vec3(0.f, 90.f, 0.f));  // +90° = nose down -Z
car_entity->set_scale(Vec3(1000.f, 1000.f, 1000.f));
```

| Parameter | Axis | Effect |
|-----------|------|--------|
| `X` | Left/Right | Move car across lanes (EB road ~1728–19610 WU) |
| `Y` | Up/Down | 0 = tires on the road (mesh is grounded at load); camera eye = ~1448 WU |
| `Z` | Forward/Back | Camera starts at −5000; more negative = further ahead |
| `rotation.y` | Yaw | +90° faces −Z (down the road); −90° faces +Z (wrong way) |
| `scale` | All | 1000 = 1 meter in world units; raise if car appears too small |

**Quick question to think about:** If you want the car to start at the camera's exact position so you're looking at it from behind, what Z offset would you add to put it 30m ahead?

---

## Improving the Drive Feel — What to Think About

The current physics is a **simplified bicycle model**. Here is what it lacks and what you'd need to add for each improvement:

### 1. Ground Contact (most impactful)
Right now `Y` is fixed. A real vehicle should sit on the road surface. What data would you need to query at the car's four wheel positions to determine the Y the car should be at?

### 2. Wheel Rotation Animation
The GLB model is static — the wheels don't spin. The car has separate wheel meshes. What property of the car's physics state would you use as the input to drive a wheel rotation angle? Think about: distance traveled per frame.

### 3. Steering Return Feel
The steering currently springs back at a fixed rate (`kSteerReturn`). Real cars return faster at high speed (self-aligning torque). Look at `CarEntity::handle_input()` — can you make the return rate a function of `m_forward_speed`?

### 4. Speed-Dependent Steering Sensitivity
At highway speed, full lock steering is dangerous. Look at `kSteerRate` in `CarEntity.h`. How would you scale it down as speed increases? What curve would feel good — linear? exponential?

### 5. Camera Follow Mode
Right now WASD controls a free camera. A follow camera would sit behind the car at a fixed offset in the car's local space. Given the car's `get_position()` and `get_rotation().y`, how would you compute the camera's world position?

**Hint:** The offset vector `(0, 1500, 3000)` in local space (up and behind) needs to be rotated by the car's yaw before being added to the car's position. Which GLM function rotates a vector by an angle around the Y axis?
