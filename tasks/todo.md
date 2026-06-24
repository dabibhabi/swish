# Car-on-road correctness + documentation

## Context
Car system (Entity/CarEntity, ModelManager::load_car, dynamic geometry path,
App wiring) already existed and built clean. GLB inspection + live screenshots
revealed two placement bugs: the mesh origin is at axle height (car sank 36cm
into the asphalt) and the mesh nose points mesh +Z while the old yaw convention
put it at world −X (car drove sideways, and visual yaw mirrored the physics
heading in Z).

## Plan
- [x] Add mutable `getVertices()` to MeshData (SceneTypes.h)
- [x] Normalize mesh in ModelManager::load_car: rotate +90° about Y so nose
      +Z → +X (positions, normals, tangents), then ground min.y → 0; log bbox
      (gate: 4.57 × 1.66 × 2.03 m ✓, grounded by 0.362 m)
- [x] Make physics match the visual convention: forward = R_y(yaw)·(+X)
      = (cos, 0, −sin); yaw −= ω·dt (positive steer = right) — CarEntity.cpp
- [x] Spawn yaw −90° → +90° (nose down −Z) — App.cpp
- [x] Rebuild + tests: 32/32 pass
- [x] Visual verification via screenshots + synthetic CGEvent key input:
      rear view from camera ✓, tires on asphalt ✓, throttle drives down the
      lane ✓, left steer curves left with matching body yaw ✓, X clamp holds
      car on roadway ✓
- [x] docs/car_system.md: new convention section + load-normalization mermaid
      flowchart; fixed spawn snippet and yaw table row
- [x] architecture.excalidraw: updated loader box, driving-model box, and
      asset-facts box with implemented normalization + convention

## Round 2: cockpit camera + full interior rendering (2026-06-10)

- [x] Node-walk loader: bake inverse(W_RootNode)·W_node into vertices
      (normals inv-transpose, tangents mat3) — 1665/1667 mesh nodes had
      non-identity transforms; steering wheel + interior now render
- [x] Skip alphaMode==BLEND prims (3 glass) so cockpit can see out
      (interim until Phase 5 forward pass)
- [x] Cockpit camera in App: default mode, C toggles free-fly; eye =
      M_car · kSeatEye(−0.32, 1.05, −0.34 m); yaw = −heading + look;
      mouse look clamped ±150°/±60°
- [x] Verified via screenshots: full dash/wheel/gauges render, road
      visible through windshield, camera rides car while driving,
      free-fly detaches, exterior intact, no stray geometry
      (white road marks = pre-existing lane markings)
- [x] Bbox gate after baking: 4.57 × 1.29 × 2.03 m (1.29 = real 911
      height; old 1.66 was misplaced geometry), grounded by ~0.001
- [x] Docs: car_system.md node-walk + cockpit sections; excalidraw
      loader/cockpit/glass boxes updated

## Review
- The "drive on the road correctly" bug was two independent convention
  mismatches (vertical origin, heading axis), both fixed at the loader level
  so entity transforms keep clean semantics: position.y = 0 means tires on
  road; R_y(yaw)·(+X) is both the nose direction and the velocity direction.
- Verified live: app runs with validation layers clean; bbox gate logged at
  load. Screenshots: /tmp/swish_fix2_zoom.png (grounded, rear view),
  /tmp/swish_drive1.png (drove down lane), /tmp/swish_steer2_zoom.png
  (left turn, body yawed with the curve).
- Deferred (per plan/car_system_port.md): config-driven tunables, cockpit
  camera, FSM physics upgrade, articulated steering wheel, glass pass.
