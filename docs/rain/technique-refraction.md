# Technique — rain on glass via scene refraction (GLSL 4.50 / Vulkan)

The canonical idea across all references (Tatarchuk SIGGRAPH 2006; Heartfelt/BigWings; Godot
"Rain on Glass"; Codrops; Radiant): **drops refract the already-rendered scene through a
normal/height field — they do not add light.**

## 1. Minimal algorithm
1. Build a drop **height field** `h(uv)` in glass-space UV (layered Voronoi/SDF).
2. **Normal via finite differences**: `n = normalize(vec2(h(uv+ex)-h, h(uv+ey)-h))`.
3. **Refract**: `uv' = gl_FragCoord.xy/screenSize ± n * refractStrength`; sample the HDR scene
   `textureLod(sceneRefr, uv', lod)`. In Vulkan the "scene" is a sampler2D bound from a *snapshot*
   of the HDR color target (you cannot sample the render target you are writing).
4. Optional `lod`/blur scaled by wetness for a defocused wet look (skipped in v1 — single-level snapshot).

## 2. Layered drop model
- 2–3 Voronoi layers at high frequency (~60–200 cells **across** the glass), different seeds/offsets.
- Per-drop hash → size + animation phase. **Tight body** (`smoothstep(rim,0,sd)`) + sharp Fresnel rim
  + small specular dot. Replaces the single global `beadScale=12` (which made screen-sized blobs).

## 3. Stick-slip motion
- Drops cling, then suddenly slide (sawtooth): `phase=fract(seed+t*spd); slip=smoothstep(0.55,1,phase)`;
  move the cell point along the flow by `slip`. Flow = `mix(gravityDown, aeroUp, smoothstep(0.1,0.6,speed))`
  in **glass space** (keeps the existing speed→flow idea).

## 4. Two coordinate spaces (critical)
| Space | Used for | Why |
|-------|----------|-----|
| **Glass UV** (`inUV`) | drop field + wiper | drops stick to the pane, constant physical size |
| **Screen UV** (`gl_FragCoord/size`) | refraction lookup | distorts the rendered scene correctly |

## 5. Vulkan adaptation vs the old additive frag
- `layout(set=1,binding=1) uniform sampler2D sceneRefr;` (HDR snapshot).
- Output `vec4(refractedColor + rim/glint, coverage)` with **alpha blend**, not `vec4(color*a, a)` additive.
- Vertex emits glass-space `inUV` (+ object normal for the front mask), not screen-space NDC UV.
- Drop density driven by a UBO field, not a hardcoded `beadScale`.

Implemented in [`shaders/windshield_rain.frag`](../../shaders/windshield_rain.frag).

### References
- Tatarchuk, "Artist-Directable Real-Time Rain Rendering," SIGGRAPH 2006 — https://advances.realtimerendering.com/s2006/Tatarchuk-Rain.pdf
- Heartfelt rain-on-glass (BigWings), WebGPU port — https://github.com/jeantimex/raindrop
- Godot "Rain on Glass" — https://godotshaders.com/shader/rain-on-glass/
- Codrops rain & water — https://tympanus.net/codrops/2015/11/04/rain-water-effect-experiments/
- Radiant "How to render rain on glass" — https://radiant-shaders.com/learn/rain-on-glass
