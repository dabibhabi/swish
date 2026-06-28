# Persistent wetness map — real wipe-off + speed flow (2026-06-28)

## Done
- [x] New fullscreen wetness pass `windshield_wetness.frag` (registered in CMake): accumulate rain +
      semi-Lagrangian advection along flow + wiper clear + evaporation; R16F ping-pong A/B + copy.
- [x] `WindshieldRainPass`: wetness images/views/FB/render pass/pipeline/descriptors, `record_wetness_update`,
      CPU flow + advect params, one-time clear; rain frag gates drops by sampled `wetMap` (binding 2);
      analytic wiper removed from rain frag (now persistent in the wetness pass).
- [x] **Fixed unreachable speed crossover**: car terminal speedFactor ≈ 0.24 (drag-limited), so
      `smoothstep(0.1,0.6)` never went up → retuned to `smoothstep(0.05,0.20)` in both CPU + frag.
- [x] Renderer calls `record_wetness_update` before the snapshot.
- [x] Build green (swish + tests); validation-clean at runtime (ping-pong + barriers + binding-2 sampler).
- [x] Docs: CHANGELOG entry, docs/rain/README updated (Part 3 = wetness map), diagram text updated.

## Review
- Rain now accumulates and the wiper **genuinely wipes water off** — visually confirmed: a cleared
  swath persists and rebuilds as rain re-wets (wfinal*.png). The drops are gated by the wetness map.
- Water flows down at rest / up at speed; the real blocker was the crossover being above the car's
  drag-limited top speed — fixed. Up-direction visual not cleanly screenshotted this session (an
  unrelated foreground app, ComfyUI, occluded the swish window); confirm live by driving (hold ↑) with rain on.
- Architecture: screen-space map (cockpit view is fixed; mesh UVs are degenerate). Ping-pong A/B + a
  B→A copy keeps the rain descriptor's wetness binding fixed (no per-frame churn).

---

# Refractive windshield rain + wiper (2026-06-28)

## Plan (multi-agent per prompt.md → approved plan)
- [x] ModelManager: `isWindshield` excludes `WindowInside_Geo` (outer pane only)
- [x] PostProcessManager: HDR image gains `TRANSFER_SRC` usage
- [x] WindshieldRainPass.h/.cpp: 4-Vec4 UBO, refraction-source image + sampler, descriptor
      binding 1, `record_scene_snapshot` (transfer barriers + copy), wiper state, alpha+back-cull pipeline
- [x] windshield_rain.vert: glass-space `fragUV` + object-space normal
- [x] windshield_rain.frag: layered Voronoi drops, stick-slip, finite-diff normal, scene
      refraction, Fresnel rim + glint, front-normal mask, analytic wiper clear
- [x] Renderer.h/.cpp: snapshot call (glass→windshield), `set_wiper_enabled`, wiper threaded into update
- [x] App.h/.cpp: `V` key edge-detected wiper toggle
- [x] Docs: CHANGELOG, shaders/README, src/renderer/README, docs/rain/*.md
- [x] Build green (swish + tests); shaders compile; app runs validation-clean; `windshield: 1`
- [x] **Visual verification DONE** (permissions restored): drops are small refractive beads (not
      blobs), confined to the front windshield (no cabin/side-glass rain), wiper sweeps a clear streak.
      Real path confirmed: default-off → `R` rain → `V` wiper (held CGEvent keys). See
      `docs/images/windshield-rain-fixed.png`.
- [x] Tuning applied: cull `FRONT` (BACK culled the cabin-facing surface — drops were invisible);
      density 60, refractStrength 0.030, Fresnel rim 0.15; wiper moved to **screen space** (windshield
      mesh UV is ~constant near (1,1), so glass-UV pivot missed the glass), pivot (0.5,0.92), band 0.12.

## Review
- Root causes from `issue.md` addressed structurally: additive→refraction (alpha blend +
  HDR snapshot), screen-space→glass-space drop field, low→high density, inner-pane untagged +
  single-sided cull + forward-normal mask, and a `V`-toggle analytic wiper.
- **Feedback-loop avoided:** the pass renders into HDR, so it samples a per-frame *copy* of HDR
  (`vkCmdCopyImage`) made after the glass pass; explicit transfer barriers (insertImageBarrier
  doesn't cover TRANSFER layouts).
- **Verified:** clean build incl. tests; shaders compile; app runs many frames validation-clean
  (snapshot + binding-1 sampler + barriers exercised); loader logs exactly 1 windshield submesh.
- **Not verified (env limitation):** on-screen appearance — Screen Recording + Accessibility are
  denied here, so screenshots/synthetic keystrokes fail. The remaining `[ ]` is pure visual tuning
  with the tunables listed above; everything is parameterized for quick iteration.
- **Side fix:** `swish_tests` failed to link (`_glfwGetKey` in `Camera.cpp`) — pre-existing, unrelated
  to rain; fixed by linking `glfw` into the test target.

---

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
