# shaders

GLSL 4.50 source files compiled to SPIR-V by `glslc` during the CMake build. Output goes to `build/shaders/*.spv`.

## Compilation

`CMakeLists.txt` builds the `SHADER_SOURCES` list (verified 2026-09-29: a `file(GLOB ... CONFIGURE_DEPENDS)` over `*.vert`, `*.frag` and `*.comp`; if that ever changes back to a manual list, `CMakeLists.txt` is the source of truth). Debug builds compile with `-DSWISH_DEBUG_UI` (see `SHADER_DEFINES`). Repo-wide map: [docs/REPO_MAP.md](../docs/REPO_MAP.md) (pass to shader to file table). Rebuild shaders without reconfiguring:

```bash
cmake --build build --target shaders
```

To iterate on a shader without a full rebuild:

```bash
glslc -I shaders/common -I shaders shaders/lighting.frag -o build/shaders/lighting.frag.spv
```

### Shared includes (`shaders/common/*.glsl`)

Shared GLSL lives in `shaders/common/` and is pulled in with `#include "name.glsl"` (glslc
resolves it via `-I shaders/common -I shaders`; no `GL_GOOGLE_include_directive` needed).

| File | Provides |
|------|----------|
| `bindings.glsl` | `SET_*` / `BIND_*` descriptor numbers (camera/lights, G-buffer, shadow, IBL, scene-params). Plain `#define NAME <int>` only, so C++ can `#include` it later. Used in `layout(set = SET_X, binding = BIND_Y)` (macros in `layout()` compile fine). |
| `camera.glsl` | `CameraUBO`. Pick the host-struct prefix with a define BEFORE the include: none = 5 members, `CAMERA_UBO_WEATHER` = + `weather`, `CAMERA_UBO_CSM` = + CSM tail (needs `NUM_CASCADES`). |
| `lights.glsl` | `PointLight` + `LightsUBO` |
| `math.glsl` | `PI` |
| `hash.glsl` / `hash11.glsl` | `hash(vec2)` / `hash11(uint)` |
| `oct.glsl` | `octEncode` (define `SWISH_OCT_ENCODE`) / `octDecode` (`SWISH_OCT_DECODE`) |
| `color.glsl` | `LUMA_709` (bloom/composite/TAA) and `LUMA_601` (lighting wet-albedo desaturation only). Deliberately separate, never unify. |
| `puddle.glsl` | world-space puddle mask shared by `lighting.frag` and `ssr.frag` (C++ wet-grip mirror must match) |

**Adding a shared file:** create `shaders/common/<name>.glsl` with an include guard, `#include` it
from the shader(s), rebuild. Rules that keep the release `.spv` byte-identical
(`tests/golden/release_spv.sha256`, glslc 1.4.321.0 from the Vulkan SDK):

- Move only text-identical code. A CameraUBO with a different member prefix is a different
  block, so it stays behind an opt-in define rather than being merged.
- glslc emits UNUSED functions, so functions not used by every includer go behind an opt-in
  define or their own file (`hash.glsl` vs `hash11.glsl`, `oct.glsl`).
- Use `#define` for constants like `LUMA_*`; a global `const vec3` changes the emitted SPIR-V.

**Dependency tracking.** CMake passes `-I shaders/common -I shaders` and, for Ninja / Makefile
generators, `-MD -MF <spv>.d` + `DEPFILE`, so touching an include rebuilds exactly the shaders
that include it. The **Xcode generator ignores `DEPFILE`**; there every shader instead `DEPENDS`
on all `shaders/common/*.glsl` (editing any include rebuilds all shaders).
Check `which -a glslc`: the golden hashes assume the SDK glslc; a newer Homebrew glslc will
differ (configure with `-DGLSLC=$HOME/VulkanSDK/1.4.321.0/macOS/bin/glslc`).

---

## Descriptor Set Layout

The geometry shaders share this global layout:

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` — view, proj, camera world position, sun direction + color |
| 0 | 1 | UBO | `LightsUBO` — array of `PointLightData[MAX_POINT_LIGHTS]` |
| 1 | 0 | Combined sampler | Albedo texture |
| 1 | 1 | Combined sampler | Normal map |
| 1 | 2 | Combined sampler | Roughness/metallic map |

This is the layout for the **G-buffer (geometry) pipeline**. The deferred-lighting pass (`lighting.frag`) reuses **set 0** (camera + lights) but binds the **G-buffer images as its set 1** — binding 0 albedo, 1 normal, 2 material, 3 depth, **4 `gbClearcoatFlake`** (the always-on clearcoat/flake MRT written to `outClearcoatFlake`, location 3, by `gbuffer.frag`). The rest of `lighting.frag`'s sets: **2** = CSM shadow atlas, **3** = IBL (irradiance cube, prefiltered cube, BRDF LUT), **4** = `SceneParamsUBO` (debug live tunables, 11 vec4 rows). Bloom, SSAO, SSR, god-rays and composite bind their inputs via dedicated descriptor sets managed by `PostProcessManager` (composite: set 0 bindings 0-4 = hdrScene, bloomTex, aoTex, ssrTex, godraysTex). Push-constant blocks must be 16-byte multiples (MoltenVK).

The **rain forward pass** (`rain.vert` / `rain.frag`) uses set 0 = `CameraUBO` (shared with geometry), set 1 = `RainUBO` (wind direction, time, intensity, volume size — owned by `RainSystem`). No set-2 or push constants.

The **glass forward pass** (`glass.vert` / `glass.frag`) uses set 0 = `CameraUBO`. No set-1 binding. Push constants carry `{ Mat4 model; Vec4 color; }` per draw call.

The **windshield rain pass** (`windshield_rain.vert` / `windshield_rain.frag`) uses set 0 = `CameraUBO`, set 1 binding 0 = `WindshieldRainUBO` (flow dir, speed, time, wetness, intensity, drop density, refraction strength, screen size, wiper state — owned by `WindshieldRainPass`), set 1 binding 1 = `sampler2D` of the HDR scene **snapshot** that the drops refract. Push constants carry `{ Mat4 model; Vec4 color; }`.

---

## Shared Vertex Shader — `fullscreen.vert`

All post-process passes share one vertex shader that generates a fullscreen triangle entirely from `gl_VertexIndex` — no vertex buffer bound:

```glsl
fragUV      = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);  // (0,0) (2,0) (0,2)
gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);                   // (-1,-1) (3,-1) (-1,3)
```

The triangle is oversized (extends beyond NDC bounds) so the rasterizer clips it to exactly the viewport. This avoids a vertex buffer upload and the two-triangle quad split.

Invoke with: `vkCmdDraw(cmd, 3, 1, 0, 0)`

---

## File Reference

| File | Pass | Notes |
|------|------|-------|
| `basic.vert` | G-buffer | Per-vertex TBN matrix; outputs world pos, normal, tangent |
| `gbuffer.frag` | G-buffer | MRT write; single albedo sample; discard if α < 0.5 |
| `fullscreen.vert` | All post-process | Shared oversized-triangle shader |
| `lighting.frag` | Deferred lighting | GGX Cook-Torrance PBR; sun + up to 16 point lights; sky gradient for depth = 1; reads `camPos.w` as wetness to darken albedo + tint sky overcast |
| `rain.vert` | Rain forward pass | GPU-animated billboard quads; 8 192 instances; seed × volume wrapping in vertex shader; view-space streak orientation |
| `rain.frag` | Rain forward pass | Additive streak texture (src=ONE, dst=ONE); smoothstep edge + fade; outputs premultiplied color |
| `bloom_extract.frag` | Bloom extract | BT.709 luma soft-knee bright-pass |
| `bloom_blur.frag` | Bloom blur H + V | 13-tap (7 bilinear) separable Gaussian; direction via push constant |
| `composite.frag` | Composite | AO × HDR, + SSR + god-rays + bloom, exposure, white balance, **AgX** tonemap, post-tonemap grade (saturation / contrast / brightness) |
| `glass.vert` | Glass forward pass | Per-vertex TBN + world position; passes push-constant color to fragment shader |
| `glass.frag` | Glass forward pass | Fresnel rim opacity; sun Blinn-Phong specular; `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` blending |
| `windshield_rain.vert` | Windshield rain pass | Outputs glass-space mesh UV (`fragUV`) for the drop field + object-space normal for the front-pane mask |
| `windshield_rain.frag` | Windshield rain pass | Layered Voronoi drop **height field** → normal (finite differences) → **refracts** an HDR scene snapshot (lens); stick-slip motion, Fresnel rim + sun glint, forward-normal confinement, analytic wiper clear; **alpha** output |
| `basic.frag` | Unused | Forward shading fallback; no `.cpp` loads it (`ScenePipeline` uses `basic.vert` + `gbuffer.frag`) |
| `ssao.frag` | SSAO | Screen-space AO; loaded by `PostProcessManager` |
| `ao_blur.frag` | AO blur | Blurs the SSAO buffer; loaded by `PostProcessManager` |
| `depth_only.vert` / `.frag` | CSM shadow cascades | Depth-only pass; `DepthOnlyPipeline` |
| `velocity.vert` / `.frag` | Velocity (debug-gated) | Per-object motion vectors for car body + wheels; `VelocityPass` |
| `taa.frag` | TAA + motion blur (debug-gated) | Reprojection TAA with YCoCg neighbourhood clamp; `TaaPass` |
| `ssr.frag` | SSR | View-space depth ray-march; added at composite; `PostProcessManager` |
| `godrays.frag` | God-rays | Half-res radial blur toward the sun, sky-gated; added at composite |
| `dof.frag` | Depth of field | `DofPass` |
| `ibl_sky.frag` / `ibl_equirect.frag` | IBL bake stage 1 | Procedural sky or HDRI equirect to a cube face; `IBLManager` |
| `ibl_irradiance.frag` / `ibl_prefilter.frag` / `ibl_brdf.frag` | IBL bake stages 2-4 | Diffuse irradiance, GGX specular prefilter, split-sum BRDF LUT; `IBLManager` |
| `spray_sim.comp` | Road spray (compute) | Particle simulation into an SSBO; `SpraySystem` |
| `spray.vert` / `spray.frag` | Road spray (forward) | Additive camera-facing billboards from the SSBO; `SpraySystem` |
| `windshield_wetness.frag` | Windshield wetness map | Ping-pong wetness accumulation + wiper clear; `WindshieldRainPass` |

---

<details>
<summary><strong>Lighting Shader Math — GGX Cook-Torrance BRDF</strong></summary>

`lighting.frag` implements the Cook-Torrance microfacet BRDF. For each light, the reflected radiance is:

$$L_o(\mathbf{v}) = \sum_i \left(f_\text{diffuse} + f_r(\mathbf{l}_i, \mathbf{v})\right)\, (\mathbf{n} \cdot \mathbf{l}_i)\, E_i$$

where $E_i$ is the irradiance from light $i$ and $f_r$ is the specular term:

$$f_r(\mathbf{l}, \mathbf{v}) = \frac{D(\mathbf{h})\, F(\mathbf{v}, \mathbf{h})\, G(\mathbf{l}, \mathbf{v}, \mathbf{h})}{4\,(\mathbf{n} \cdot \mathbf{l})\,(\mathbf{n} \cdot \mathbf{v})}$$

**Normal Distribution (GGX/TR):**

$$D(\mathbf{h}) = \frac{\alpha^2}{\pi\left[(\mathbf{n} \cdot \mathbf{h})^2(\alpha^2 - 1) + 1\right]^2}, \qquad \alpha = \text{roughness}^2$$

**Fresnel (Schlick):**

$$F(\mathbf{v}, \mathbf{h}) = F_0 + (1 - F_0)(1 - \mathbf{v} \cdot \mathbf{h})^5, \qquad F_0 = \text{lerp}(0.04,\; \text{albedo},\; \text{metallic})$$

**Smith Geometry (Schlick-GGX):**

$$G(\mathbf{l}, \mathbf{v}) = G_1(\mathbf{l})\, G_1(\mathbf{v}), \qquad G_1(\mathbf{x}) = \frac{\mathbf{n} \cdot \mathbf{x}}{(\mathbf{n} \cdot \mathbf{x})(1-k)+k}, \qquad k = \frac{(\text{roughness}+1)^2}{8}$$

Note `k` is computed from **roughness directly**, not from $\alpha = \text{roughness}^2$ (a common convention for the analytic-light geometry term).

**Diffuse (Lambertian):**

$$f_\text{diffuse} = \frac{\text{albedo}}{\pi} \cdot (1 - F) \cdot (1 - \text{metallic})$$

Metals have no diffuse term (metallic = 1 → diffuse = 0).

**Point-light attenuation** is a smooth radius-based falloff (not physical inverse-square):

$$\text{att} = \operatorname{clamp}\!\left(1 - \frac{d}{r},\ 0,\ 1\right)^2$$

where $d$ is the fragment-to-light distance and $r$ is the light's influence radius (`positionRadius.w`).

**Sky pixels** (depth ≥ 0.9999, i.e. no geometry) skip lighting entirely and return a procedural sky gradient with a sun disc, so the background is shaded without a skybox texture.

</details>

<details>
<summary><strong>Bloom Extract — BT.709 Luma</strong></summary>

`bloom_extract.frag` computes perceptual luminance using ITU-R BT.709 coefficients:

$$L = 0.2126\,R + 0.7152\,G + 0.0722\,B$$

These weights reflect the human eye's relative sensitivity to each channel (most sensitive to green). Rather than a hard cutoff, it uses a **soft knee** so bloom fades in smoothly instead of popping at the threshold:

$$c = \max(L - \text{threshold},\ 0), \qquad \text{out} = \text{color}\cdot\frac{c}{c + 1}$$

The 1/4-resolution target limits the blur kernel cost.

</details>

<details>
<summary><strong>Bloom Blur — Separable Gaussian</strong></summary>

`bloom_blur.frag` applies a **13-tap 1D Gaussian, optimized to 7 bilinear taps** at even offsets. The fixed normalized weights are:

$$w = [\,0.0044,\ 0.0540,\ 0.2420,\ 0.3991,\ 0.2420,\ 0.0540,\ 0.0044\,]$$

sampled at offsets $\{-6, -4, -2, 0, +2, +4, +6\}\cdot\text{texelSize}$:

$$\text{out} = \sum_{k} w_k \cdot \text{tex}\bigl(uv + o_k\cdot \mathbf{d}\bigr)$$

The H and V passes share this shader; direction $\mathbf{d}$ is the push constant `texelSize` = `(1/w, 0)` for horizontal, `(0, 1/h)` for vertical. Two separable passes approximate a 2D Gaussian with $O(2n)$ samples instead of $O(n^2)$, and the bilinear taps halve the texture fetches.

</details>

<details>
<summary><strong>Composite — historical ACES notes (the shader now uses AgX)</strong></summary>

> **Stale-by-design section.** `composite.frag` now tone-maps with **AgX** (`AgX()` in the shader; see [docs/realism-features.md](../docs/realism-features.md)) and also adds SSR and god-rays before bloom, then applies exposure, white balance and a post-tonemap grade. The ACES Narkowicz math and constants below describe the *earlier* composite and are kept only as a reference for that curve; they are not what runs today.

The earlier composite combined the buffers **in this order** — AO multiplies the HDR scene, then bloom is added, then exposure — before tonemapping:

$$\text{hdr}' = \bigl(\text{hdr}\cdot\text{ao} + \text{bloom}\cdot k_\text{intensity}\bigr)\cdot\text{exposure}$$

then applies the Narkowicz 2015 ACES filmic approximation (clamped to $[0,1]$):

$$f(x) = \frac{x(ax + b)}{x(cx + d) + e}$$

Note bloom is added **after** AO, so glow is not darkened by occlusion.

| Named constant | Value |
|----------------|-------|
| `kAcesA` | 2.51 |
| `kAcesB` | 0.03 |
| `kAcesC` | 2.43 |
| `kAcesD` | 0.59 |
| `kAcesE` | 0.14 |

Applied per-channel (R, G, B independently). Output is display-referred $[0, 1]$ in linear space; the swapchain format (`_SRGB`) handles the final gamma transfer.

The ACES curve has a characteristic S-shape: it lifts dark values (shadow detail) and smoothly clips bright values (highlight roll-off) without hard clipping.

</details>

---

## Adding a New Pass

1. Add `<name>.vert` / `<name>.frag` (or `.comp`) to this directory — see `SHADER_SOURCES` in `CMakeLists.txt` (currently a glob with `CONFIGURE_DEPENDS`, so the next build picks it up). Full checklist: [docs/RECIPES.md § add-a-shader](../docs/RECIPES.md#add-a-shader).
2. Load SPIR-V in your subsystem's `init()`: most passes just set the paths on their `PipelineConfig` and let `Pipeline` read them (as in `DofPass.cpp`):
   ```cpp
   cfg.vertShaderPath = std::string(SHADER_DIR) + "fullscreen.vert.spv";
   cfg.fragShaderPath = std::string(SHADER_DIR) + "name.frag.spv";
   // or by hand: auto code = util::io::read_binary(std::string(SHADER_DIR) + "name.frag.spv");  // utils/io/File.h
   ```
3. Wrap `VkShaderModule` creation in `swish::vkx::make_shader_module` (`utils/vulkan/ShaderModule.h`, used by `Pipeline.cpp`): it is RAII, so the module is released on pipeline-creation failure, and it throws on empty or non-4-byte-multiple SPIR-V. Shared GLSL goes in `common/`.
4. Fullscreen passes: draw with `vkCmdDraw(cmd, 3, 1, 0, 0)` — no vertex input.
5. Update the file reference table above.

See [CONTRIBUTING.md § Adding a new shader pass](../CONTRIBUTING.md) for the full checklist.
