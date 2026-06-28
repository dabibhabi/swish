# Technique — windshield wiper

## How demos do it
Production rain-on-glass (olivierprat/rain-on-windshield; Heartfelt) keep a persistent **wetness map**
texture: rain adds wetness; the wiper clears a swept band; wetness re-grows over time. The drop
normal/height drives refraction; the wiper just modulates the wetness mask.

## Chosen approach for Swish — analytic (no extra texture)
Two options were considered:
- **(a) ping-pong wetness-map texture** updated by a compute/fullscreen pass — most realistic
  (true re-wetting trails) but adds a texture, a pass, and ping-pong plumbing.
- **(b) analytic blade in the UBO** — the fragment shader masks drops behind a rotating blade SDF.

Chose **(b)**: it matches Swish conventions (Vec4-only UBOs, one class per folder, no new passes) and
adds the least plumbing. Re-wetting reads naturally because the drops are procedural — once the blade
sweeps past, the field is simply visible again.

## Motion + mask (glass space)
- UBO `Vec4 wiperState = (bladeAngle, phase, enabled, speed)`. CPU advances `phase` while enabled;
  `bladeAngle = sin(phase) * 1.15` (~±66° sweep across and back).
- Blade = distance to a rotating line segment from a pivot at the windshield UV bottom-centre:
  ```glsl
  vec2 dir = vec2(sin(angle), -cos(angle));
  float t  = clamp(dot(uv - pivot, dir), 0.0, len);
  float d  = length((uv - pivot) - dir * t);
  float clear = smoothstep(0.05, 0.0, d);   // thin swept band
  coverage *= (1.0 - clear);
  ```
- Pivot/length/half-sweep are tunable to the windshield's UV layout (verify visually).

## Control
- **`V`** toggles the wiper (edge-detected, mirrors the `R` rain key in `src/core/App/App.cpp`).
  Continuous on/off (not single-sweep) — simplest state, reads as "wipers on".
- `App` → `Renderer::set_wiper_enabled(bool)` → threaded into `WindshieldRainPass::update`.

Implemented in [`WindshieldRainPass`](../../src/renderer/WindshieldRainPass/) +
[`windshield_rain.frag`](../../shaders/windshield_rain.frag).

### References
- olivierprat/rain-on-windshield (Unity HDRP, wiper + flow maps) — https://github.com/olivierprat/rain-on-windshield
- Heartfelt rain-on-glass — https://github.com/jeantimex/raindrop
