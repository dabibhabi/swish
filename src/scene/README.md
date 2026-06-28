# src/scene

Scene management, procedural geometry, entity physics, and asset loading.

## Scene Switching

`SceneManager::set_active_scene(lambda)` accepts a callable that receives a `SceneContext&` and is responsible for:

1. Uploading geometry via `SceneGeometry::upload()`
2. Loading textures via `TextureManager::load()`
3. Returning a list of draw calls (or setting up entities)

The lambda is stored and re-invoked after swapchain recreation so scene data is never stale.

## Components

### RoadScene (`RoadScene/`)

Procedurally generates all road geometry from a `RoadConfig` loaded from a baked binary (see `tools/toml_baker`).

Key `RoadConfig` fields:

| Field | Effect |
|-------|--------|
| `lane_width` | Width of each lane in feet |
| `lane_count` | Number of lanes per direction |
| `segment_length` | Length of one road tile |
| `kCrownSlope` | Road surface crown angle (drainage) |
| `billboard_spacing` | Distance between roadside billboard trees |

Generation is single-pass: road surface, lane markings, gore strips, guardrails, and billboards are all emitted in one traversal of the segment list. The `RoadLayout` struct is computed once and threaded through all generator functions to avoid redundant recomputation.

### Entity Hierarchy

```
Entity (base — position, rotation, scale; chained glm transforms)
  └── ModelEntity (adds GLB mesh reference)
        └── CarEntity (bicycle-model physics)
```

`CarEntity` physics summary:
- Speed: exponential drag (`exp(-kDragCoeff * dt)`)
- Steering: clamped to `[-kWheelLockToDeg, +kWheelLockToDeg]` before `tan()` to avoid blow-up
- Heading: wrapped via `fmod` to prevent floating-point drift over long runs

### Camera (`Camera/`)

Two modes:
- **Free-fly**: WASD movement, mouse look, sprint multiplier
- **Cockpit**: locked to car entity with `m_bounds` clamping (prevents camera clipping through car interior)

`Bounds2D { min_x, max_x, min_y, max_y }` defines the cockpit cage.

### ModelManager (`ModelManager/`)

Loads GLB files via `tinygltf`. Node-walk bake:
1. Traverses all mesh nodes
2. Normalizes vertex positions into a unit AABB
3. Merges rotation and bounding-box passes into one traversal
4. Produces `MeshData` (interleaved vertex buffer) and `DrawCall[]` ready for `SceneGeometry::upload()`

For more detail on car mesh normalization, see [docs/car_system.md](../../docs/car_system.md).

## Diagrams

- [docs/diagrams/scene-data-flow.excalidraw](../../docs/diagrams/scene-data-flow.excalidraw) — asset → GPU → draw calls
