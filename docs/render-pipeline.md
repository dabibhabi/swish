# Render Pipeline

Each frame records 6 render passes into a single command buffer, submitted once to the graphics queue.

```
Scene geometry          Dynamic geometry (car)
      │                        │
      └──────────┬─────────────┘
                 ▼
        ┌─────────────────┐
        │   G-buffer Pass │  basic.vert + gbuffer.frag
        │   (full res)    │  Albedo · Normal · Material · Depth
        └────────┬────────┘
                 │  barrier → SHADER_READ_ONLY
                 ▼
        ┌─────────────────┐
        │  Lighting Pass  │  fullscreen.vert + lighting.frag
        │   (full res)    │  HDR RGBA16F
        └────────┬────────┘
                 │  barrier → SHADER_READ_ONLY
                 ▼
        ┌─────────────────┐
        │  Bloom Extract  │  fullscreen.vert + bloom_extract.frag
        │   (1/4 res)     │  Bright pixels
        └────────┬────────┘
                 │  barrier → SHADER_READ_ONLY
                 ▼
        ┌─────────────────┐
        │  Bloom Blur H   │  fullscreen.vert + bloom_blur.frag
        │   (1/4 res)     │  Horizontal Gaussian
        └────────┬────────┘
                 │  barrier → SHADER_READ_ONLY
                 ▼
        ┌─────────────────┐
        │  Bloom Blur V   │  fullscreen.vert + bloom_blur.frag
        │   (1/4 res)     │  Vertical Gaussian → blurred bloom
        └────────┬────────┘
                 │
        ┌────────┴────────┐
        │   Composite     │  fullscreen.vert + composite.frag
        │   (full res)    │  HDR + bloom → ACES → swapchain
        └─────────────────┘
```

See [`docs/diagrams/render-pipeline.excalidraw`](diagrams/render-pipeline.excalidraw) for the visual diagram.

## Pass Summary

| # | Pass | Shaders | Resolution | Output format |
|---|------|---------|-----------|--------------|
| 1 | G-buffer | `basic.vert` + `gbuffer.frag` | Full | Albedo `R8G8B8A8_SRGB`, Normal `R16G16B16A16_SFLOAT`, Material `R8G8B8A8_UNORM`, Depth `D32_SFLOAT` |
| 2 | Deferred lighting | `fullscreen.vert` + `lighting.frag` | Full | HDR `R16G16B16A16_SFLOAT` |
| 3 | Bloom extract | `fullscreen.vert` + `bloom_extract.frag` | 1/4 | Bright pixels `R16G16B16A16_SFLOAT` |
| 4a | Bloom blur H | `fullscreen.vert` + `bloom_blur.frag` | 1/4 | Horizontally blurred `R16G16B16A16_SFLOAT` |
| 4b | Bloom blur V | `fullscreen.vert` + `bloom_blur.frag` | 1/4 | Fully blurred bloom `R16G16B16A16_SFLOAT` |
| 5 | Composite | `fullscreen.vert` + `composite.frag` | Full | Swapchain format (e.g. `B8G8R8A8_SRGB`) |

---

<details>
<summary><strong>Pass 1 — G-buffer</strong></summary>

**Shaders:** `basic.vert` + `gbuffer.frag`  
**Render pass:** `PostProcessManager::get_gbuffer_render_pass()`  
**Framebuffer:** one per frame-in-flight (`get_gbuffer_framebuffer(frameIndex)`)  
**Draw call source:** `SceneGeometry::record_draws()` — one `vkCmdDrawIndexed` per `DrawCall`

### MRT Outputs

| Attachment | Format | Contents |
|-----------|--------|---------|
| Color 0 — Albedo | `VK_FORMAT_R8G8B8A8_SRGB` | Base color; fragments with α < 0.5 are discarded |
| Color 1 — Normal | `VK_FORMAT_R16G16B16A16_SFLOAT` | World-space normal remapped from $[-1,1]$ to $[0,1]$ |
| Color 2 — Material | `VK_FORMAT_R8G8B8A8_UNORM` | R = metallic (currently written as the constant `0.04` — every surface is treated as a dielectric), G = roughness, B = 0 |
| Depth | `VK_FORMAT_D32_SFLOAT` | Used in Pass 2 to reconstruct world position from UV + depth |

### Descriptor Sets

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` — view, proj, camera world position, sun direction + color |
| 0 | 1 | UBO | `LightsUBO` — array of up to `MAX_POINT_LIGHTS` (16) point lights |
| 1 | 0 | Combined sampler | Albedo texture |
| 1 | 1 | Combined sampler | Normal map |
| 1 | 2 | Combined sampler | Roughness/metallic map |

### Push Constants

```glsl
layout(push_constant) uniform Push {
    mat4 model;   // model-to-world transform
    vec4 color;   // tint multiplier
} push;
```

### Normal Encoding

World-space normals are stored remapped as:

$$n_\text{stored} = \frac{n_\text{world} + 1}{2}$$

The lighting pass reverses this: $n_\text{world} = n_\text{stored} \cdot 2 - 1$.

### After This Pass

Image barrier transitions:
- Albedo, normal, material → `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- Depth → `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`

</details>

---

<details>
<summary><strong>Pass 2 — Deferred Lighting</strong></summary>

**Shaders:** `fullscreen.vert` + `lighting.frag`  
**Draw:** `vkCmdDraw(cmd, 3, 1, 0, 0)` — fullscreen triangle, no vertex input  
**Output:** HDR color image `VK_FORMAT_R16G16B16A16_SFLOAT`

### World Position Reconstruction

Instead of storing world position in the G-buffer, Pass 2 reconstructs it from the depth buffer and the inverse projection + view matrices (pushed as push constants):

$$\mathbf{p}_\text{ndc} = \bigl(uv \cdot 2 - 1,\ d \cdot 2 - 1,\ 1\bigr)$$

$$\mathbf{p}_\text{view} = P^{-1} \cdot \mathbf{p}_\text{ndc}$$

$$\mathbf{p}_\text{world} = V^{-1} \cdot \mathbf{p}_\text{view}$$

The inverse matrices are computed on the CPU each frame and pushed via:

```glsl
layout(push_constant) uniform Matrices {
    mat4 invView;
    mat4 invProj;
} push;
```

### Lighting Model — Cook-Torrance GGX

The BRDF for each light is:

$$f_r(\mathbf{l}, \mathbf{v}) = \frac{D(\mathbf{h})\, F(\mathbf{v},\mathbf{h})\, G(\mathbf{l},\mathbf{v},\mathbf{h})}{4\,(\mathbf{n} \cdot \mathbf{l})\,(\mathbf{n} \cdot \mathbf{v})}$$

where $\mathbf{h} = \text{normalize}(\mathbf{l} + \mathbf{v})$ is the half-vector.

<details>
<summary>GGX Normal Distribution Function (NDF)</summary>

$$D(\mathbf{h}) = \frac{\alpha^2}{\pi \left[(\mathbf{n} \cdot \mathbf{h})^2 (\alpha^2 - 1) + 1\right]^2}$$

where $\alpha = \text{roughness}^2$ (Disney remapping — perceptually linear roughness).

</details>

<details>
<summary>Fresnel — Schlick Approximation</summary>

$$F(\mathbf{v}, \mathbf{h}) = F_0 + (1 - F_0)(1 - \mathbf{v} \cdot \mathbf{h})^5$$

$F_0 = \text{lerp}(0.04,\ \text{albedo},\ \text{metallic})$ — dielectrics default to 4% reflectance.

</details>

<details>
<summary>Smith Geometry Masking-Shadowing</summary>

$$G(\mathbf{l}, \mathbf{v}, \mathbf{h}) = G_1(\mathbf{l}) \cdot G_1(\mathbf{v}), \qquad G_1(\mathbf{x}) = \frac{\mathbf{n} \cdot \mathbf{x}}{(\mathbf{n} \cdot \mathbf{x})(1 - k) + k}$$

$$k = \frac{(\text{roughness} + 1)^2}{8}$$

Note $k$ uses **roughness directly**, not $\alpha = \text{roughness}^2$ — the conventional remap for analytic (non-IBL) lights.

</details>

### Point Light Attenuation

Attenuation is a smooth radius-based falloff, **not** physical inverse-square:

$$\text{att} = \operatorname{clamp}\!\left(1 - \frac{d}{r},\ 0,\ 1\right)^2$$

where $d$ is the fragment-to-light distance and $r = \text{positionRadius.w}$ is the light's influence radius. Beyond $r$ the contribution is exactly zero (lights with `att < 0.001` are `continue`d), which bounds the per-fragment light loop cost.

### Sky

Fragments with depth ≥ 0.9999 (no geometry) bypass the BRDF and return a procedural sky: a horizon→zenith gradient plus a sun disc from `pow(max(dot(viewDir, sunDir), 0), 32)`. No skybox texture is sampled.

### After This Pass

HDR image barrier → `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`

</details>

---

<details>
<summary><strong>Pass 3 — Bloom Extract</strong></summary>

**Shaders:** `fullscreen.vert` + `bloom_extract.frag`  
**Resolution:** 1/4 of swapchain extent  
**Input:** HDR image from Pass 2

### Luma Soft-Knee

Brightness is the ITU-R BT.709 perceptual luminance:

$$L = 0.2126\,R + 0.7152\,G + 0.0722\,B$$

Rather than a hard cutoff, a **soft knee** ramps the contribution in smoothly so bloom doesn't pop on at the threshold:

$$c = \max(L - \text{threshold},\ 0), \qquad \text{out} = \text{color}\cdot\frac{c}{c + 1}$$

The coefficients weight green heavily because the human eye is most sensitive to green wavelengths.

### Push Constants

```glsl
layout(push_constant) uniform Params {
    float threshold;   // luma cutoff
    float intensity;   // used by composite, not extract
    float exposure;    // used by composite, not extract
} params;
```

</details>

---

<details>
<summary><strong>Passes 4a / 4b — Bloom Blur (Horizontal + Vertical)</strong></summary>

**Shaders:** `fullscreen.vert` + `bloom_blur.frag` (same shader, different push constant)  
**Resolution:** 1/4 res  

### Separable Gaussian Blur

A 2D Gaussian is separable into two 1D passes (horizontal then vertical). Each pass is a **13-tap Gaussian optimized to 7 bilinear taps** at even offsets, with fixed normalized weights:

$$w = [\,0.0044,\ 0.0540,\ 0.2420,\ 0.3991,\ 0.2420,\ 0.0540,\ 0.0044\,]$$

$$\text{out}(uv) = \sum_{k} w_k \cdot \text{color}\bigl(uv + o_k\cdot\mathbf{d}\bigr), \qquad o_k \in \{-6,-4,-2,0,2,4,6\}$$

The weights sum to 1 (energy preserved). The bilinear taps fetch two texels per sample, so 13 effective taps cost only 7 fetches.

### Direction Push Constant (`texelSize`)

| Pass | `texelSize` |
|------|-------------|
| Blur H | `(1.0 / width, 0.0)` |
| Blur V | `(0.0, 1.0 / height)` |

The per-tap offset is $\mathbf{d} = \text{texelSize}$, so $o_k\cdot\mathbf{d}$ walks along one axis only.

</details>

---

<details>
<summary><strong>Pass 5 — Composite (ACES Tonemapping)</strong></summary>

**Shaders:** `fullscreen.vert` + `composite.frag`  
**Framebuffer:** one per **swapchain image** (not per frame-in-flight)  
**Output:** final sRGB frame written directly to the swapchain image

### Inputs

| Binding | Source |
|---------|--------|
| 0 | HDR color (Pass 2) |
| 1 | Blurred bloom (Pass 4b) |
| 2 | AO (white fallback — SSAO disabled) |

### Composite Formula

The buffers combine **in this order** — AO multiplies the scene first, *then* bloom is added (so glow is not darkened by occlusion), then exposure:

$$\text{color} = \bigl(\text{hdr}\cdot\text{ao} + \text{bloom}\cdot k_\text{intensity}\bigr)\cdot\text{exposure}$$

$$\text{color} = \text{ACES}(\text{color})$$

### ACES Tonemapping — Narkowicz 2015

$$f(x) = \frac{x(ax + b)}{x(cx + d) + e}$$

| Constant | Value |
|----------|-------|
| $a$ (`kAcesA`) | 2.51 |
| $b$ (`kAcesB`) | 0.03 |
| $c$ (`kAcesC`) | 2.43 |
| $d$ (`kAcesD`) | 0.59 |
| $e$ (`kAcesE`) | 0.14 |

This is a rational approximation of the ACES RRT+ODT curve. It maps HDR linear values $[0, \infty)$ to display-referred $[0, 1]$, with a characteristic S-curve that lifts shadows and rolls off highlights. Applied per-channel before sRGB gamma.

</details>

---

<details>
<summary><strong>SSAO — Disabled</strong></summary>

The SSAO shaders (`ssao.frag`, `ao_blur.frag`) exist and the AO images are allocated by `PostProcessManager`, but the SSAO and AO-blur passes are **not recorded**. The composite pass still samples the AO binding, so the AO target is primed with white pixels once at init — AO then multiplies as `1.0` (a no-op) until SSAO is re-enabled.

**Status:** pending VUID-09600 fix (sampler reads from an image in undefined layout).

</details>

---

## Frame Synchronization

Before recording each frame:

1. `waitForFence(frame)` — stall CPU until the GPU finishes this frame slot's previous work
2. `vkAcquireNextImageKHR(..., imageAvailable[frame], ...)` — get next swapchain image index
3. `resetFence(frame)` — reset fence so it can be signaled again

Submit:
- **Wait semaphore:** `imageAvailable[frame]`
- **Signal semaphore:** `renderFinished[imageIndex]` — indexed by swapchain image, not frame slot (VUID-00067)
- **Signal fence:** `inFlightFence[frame]`

Present:
- **Wait semaphore:** `renderFinished[imageIndex]`

See [`docs/diagrams/vulkan-sync.excalidraw`](diagrams/vulkan-sync.excalidraw) for the timing diagram.
