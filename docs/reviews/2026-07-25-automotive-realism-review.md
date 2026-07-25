# Automotive Realism Review

Date: 2026-07-25  
Scope: vehicle feel, car presentation, camera/input/audio, and supporting scene realism. This is a
read-only recommendation report; it does not authorize new deferred GPU work.

## Executive summary

Swish has a stronger wet-road visual presentation than vehicle simulation feel. The Porsche's
power-limited longitudinal model, dynamic bicycle model, and basic wheel spin/steer provide a good
base, but the car still lacks suspension response, wet grip behavior, analog control, speed-linked
camera cues, and audio. These omissions make highway speed feel less physical than the renderer
looks.

The most valuable near-term work is configuration-driven car tuning, speed-scaled steering, visual
suspension, and rain-coupled tire grip—not another large rendering feature.

## Current baseline

- [`src/scene/Entity/CarPhysics.h`](../../src/scene/Entity/CarPhysics.h) contains longitudinal power
  limitation, a dynamic bicycle derivative, tire saturation, and a friction-circle model.
- [`src/scene/Entity/WheelKinematics.h`](../../src/scene/Entity/WheelKinematics.h) supplies Phase A
  wheel spin and front steering.
- The primary road contact is a scalar surface-height snap through `RoadGeometry::road_surface_y`;
  interchange following adds grade but not a full four-wheel body response.
- Car parameters remain hard-coded instead of being supplied through a dedicated car configuration.
- The main play camera is cockpit/free-fly with a fixed FOV and digital keyboard inputs.
- No engine, tire, rain, wiper, or transmission audio system is present.

## Highest-priority recommendations

### P0 — Wheel Phase B: visual suspension and body attitude

The car reads as a body snapped to road height rather than a sprung mass. Follow the documented
Phase B direction in [`docs/wheels/README.md`](../wheels/README.md): sample road height at hub
locations and use the result only to modify visual wheel/body transforms. Keep the authoritative
physics position and treadmill/rebase behavior unchanged until a broader dynamics model is designed.

Acceptance criteria:

- The car shows restrained crown lean, pitch over grades, and smooth wheel travel.
- Mainline and interchange behavior remain stable.
- Existing car-physics tests remain green.

### P0 — Couple rain to tire grip and wheel visuals

Rain currently changes the environment but not tire friction. Add a controlled rain-to-`tireMu`
mapping and use slip state to drive wheel spin/lockup behavior. Validate dry and maximum-rain braking
and cornering explicitly; the goal is a measurable handling distinction without making the car
uncontrollable.

### P1 — Move tuning into `config/car.toml`

Hard-coded mass, power, drag, steering, seat position, and spawn values inhibit iteration. Mirror
the baked road-configuration pattern with a `CarConfig` that contains physics, steering, cockpit,
and spawn sections. This makes repeated handling and camera tuning reproducible without recompiling,
while retaining safe defaults for missing fields.

### P1 — Speed-sensitive steering

Constant steering rate and lock make high-speed highway inputs too twitchy. Scale effective lock and
input/return rates by forward speed, preserving low-speed maneuverability. Add focused tests for
zero, cruising, and top-speed steering behavior so this becomes an intentional handling curve rather
than a hidden magic constant.

### P1 — Improve speed cues in the cockpit

Use speed and acceleration/braking transients for a modest FOV kick and damped head motion in the
cockpit camera path in [`src/core/App/App.cpp`](../../src/core/App/App.cpp). Keep values subtle:
camera shake should augment physical feel rather than conceal unstable vehicle motion.

## Car appearance and presentation

### Curated material presets

The black Porsche needs a release-quality paint preset rather than relying on debug-only overrides.
Tune known material slots for a low-roughness clear-coated paint impression while keeping the cabin
appropriately dry in rain. Improve interior slot roughness/metalness for leather, carbon, and metal
details. Add emissive headlight, taillight, and gauge treatments when the material/G-buffer layout
permits it.

### Camera and HUD

Add chase, hood, and orbit modes after body/wheel motion is credible; they make suspension and
vehicle silhouette inspectable. A small release-safe speed/gear HUD would make acceleration and
handling changes legible. Preserve the debug UI separation.

### Audio

Audio is the largest missing feedback channel. Start with an engine layer driven by speed and
throttle, then add wet tire hiss, asphalt hum, wiper/rain sound, and brief brake/turbo effects.
Integrate this after defining a small driving-state model so thresholds do not become scattered
across rendering and input code.

## Longer-term realism

- Sample road height under all four wheels and derive body pitch/roll from axle and side deltas.
- Add weight-transfer terms atop the visual-suspension model before attempting a full rigid-body
  solver.
- Introduce a driving state machine for coast, accelerate, brake, reverse, and idle behavior.
- Support analog/gamepad input to enable partial throttle and trail braking.
- Add a curved centerline, roadside props, and ramp/signage polish to improve the feeling of driving
  through a place rather than along an infinite tile.

## Recommended order

1. Bake `config/car.toml` and add speed-scaled steering.
2. Implement and test visual suspension (Wheel Phase B).
3. Couple wetness to tire grip and slip visuals (Wheel Phase C).
4. Add cockpit FOV/head-motion cues and a chase camera.
5. Add a minimal HUD and basic engine audio.
6. Curate release vehicle/interior/emissive material presets.

## Validation checklist

- Run the existing test suite after physics/configuration changes.
- Drive and visually inspect both rain extremes, including braking and lane changes.
- Compare cockpit and chase views at low speed, cruising speed, and top speed.
- Check that camera and visual-suspension changes remain stable through treadmill rebasing and
  interchanges.
