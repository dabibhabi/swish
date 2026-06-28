# shaders

GLSL 4.50 source files compiled to SPIR-V by `glslc` during the CMake build. Output goes to `build/shaders/*.spv`.

## Compilation

CMake discovers all `*.vert` and `*.frag` files via `SHADER_SOURCES` and compiles them automatically. Rebuild shaders without reconfiguring:

```bash
cmake --build build --target shaders
```

To iterate on a shader without a full rebuild:

```bash
glslc shaders/lighting.frag -o build/shaders/lighting.frag.spv
```

---

## Descriptor Set Layout

All geometry shaders share the same global layout:

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` — view, proj, camera world position, sun direction + color |
| 0 | 1 | UBO | `LightsUBO` — array of `PointLightData[MAX_POINT_LIGHTS]` |
| 1 | 0 | Combined sampler | Albedo texture |
| 1 | 1 | Combined sampler | Normal map |
| 1 | 2 | Combined sampler | Roughness/metallic map |

Post-process passes (lighting, bloom, composite) do not use set 1. They bind their own input images via dedicated descriptor sets managed by `PostProcessManager`.

---

## Shared Vertex Shader — `fullscreen.vert`

All post-process passes share one vertex shader that generates a fullscreen triangle entirely from `gl_VertexIndex` — no vertex buffer bound:

```glsl
const vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
const vec2 uv[3]  = vec2[](vec2( 0, 0), vec2(2, 0), vec2( 0, 2));
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
| `lighting.frag` | Deferred lighting | GGX Cook-Torrance PBR; sun + up to 16 point lights |
| `bloom_extract.frag` | Bloom extract | BT.709 luma threshold bright-pass |
| `bloom_blur.frag` | Bloom blur H + V | 9-tap separable Gaussian; direction via push constant |
| `composite.frag` | Composite | ACES Narkowicz tonemapping; HDR + bloom + AO |
| `basic.frag` | Debug / unused | Forward shading fallback |

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

$$G(\mathbf{l}, \mathbf{v}) = G_1(\mathbf{l})\, G_1(\mathbf{v}), \qquad G_1(\mathbf{x}) = \frac{\mathbf{n} \cdot \mathbf{x}}{(\mathbf{n} \cdot \mathbf{x})(1-k)+k}, \qquad k = \frac{(\alpha+1)^2}{8}$$

**Diffuse (Lambertian):**

$$f_\text{diffuse} = \frac{\text{albedo}}{\pi} \cdot (1 - F) \cdot (1 - \text{metallic})$$

Metals have no diffuse term (metallic = 1 → diffuse = 0).

</details>

<details>
<summary><strong>Bloom Extract — BT.709 Luma</strong></summary>

`bloom_extract.frag` computes perceptual luminance using ITU-R BT.709 coefficients:

$$L = 0.2126\,R + 0.7152\,G + 0.0722\,B$$

These weights reflect the human eye's relative sensitivity to each channel (most sensitive to green). Pixels with $L > \text{threshold}$ pass through; others output black. The 1/4-resolution target limits the blur kernel cost.

</details>

<details>
<summary><strong>Bloom Blur — Separable Gaussian</strong></summary>

`bloom_blur.frag` applies a 9-tap 1D Gaussian kernel. Weights are precomputed from $\sigma = 2$:

$$w_i = \frac{e^{-i^2/(2\sigma^2)}}{\displaystyle\sum_{j=-4}^{4} e^{-j^2/(2\sigma^2)}}, \qquad i \in \{-4, -3, \dots, 4\}$$

Normalization ensures energy conservation. The H and V passes share the same shader; direction is selected by push constants `(texel_x, texel_y)`. Two separable passes approximate a 2D Gaussian with $O(2n)$ samples instead of $O(n^2)$.

</details>

<details>
<summary><strong>Composite — ACES Tonemapping</strong></summary>

`composite.frag` applies the Narkowicz 2015 ACES filmic approximation after combining HDR + bloom + AO:

$$f(x) = \frac{x(ax + b)}{x(cx + d) + e}$$

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

1. Add `<name>.vert` / `<name>.frag` to this directory — CMake picks it up on the next configure.
2. Load SPIR-V in your subsystem's `init()`:
   ```cpp
   auto code = FileIO::readBinaryFile(std::string(SHADER_DIR) + "name.frag.spv");
   ```
3. Wrap `VkShaderModule` creation in `ScopedShaderModule` (see `Pipeline.cpp`) to prevent leaks on pipeline-creation failure.
4. Fullscreen passes: draw with `vkCmdDraw(cmd, 3, 1, 0, 0)` — no vertex input.
5. Update the file reference table above.

See [CONTRIBUTING.md § Adding a new shader pass](../CONTRIBUTING.md) for the full checklist.
