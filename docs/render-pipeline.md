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

---

## Pass 1 — G-buffer

**Shaders:** `basic.vert` + `gbuffer.frag`  
**Resolution:** full swapchain extent  
**Render pass:** `PostProcessManager::get_gbuffer_render_pass()`  
**Framebuffer:** one per frame-in-flight (`get_gbuffer_framebuffer(frameIndex)`)

### Outputs (MRT)

| Attachment | Format | Contents |
|-----------|--------|---------|
| Color 0 — Albedo | `VK_FORMAT_R8G8B8A8_SRGB` | Base color (alpha-tested; `discard` if α < 0.5) |
| Color 1 — Normal | `VK_FORMAT_R16G16B16A16_SFLOAT` | World-space normal, encoded [−1,1] → [0,1] |
| Color 2 — Material | `VK_FORMAT_R8G8B8A8_UNORM` | R=metallic, G=roughness |
| Depth | `VK_FORMAT_D32_SFLOAT` | Used in lighting pass for position reconstruction |

### Descriptor sets

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` (view, proj, camPos, sun) |
| 0 | 1 | UBO | `LightsUBO` (point lights) |
| 1 | 0–2 | Combined image sampler | Albedo, normal map, roughness map |

### Push constants

```glsl
layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} push;
```

### After this pass

Image barrier: albedo, normal, material → `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`  
Depth barrier: depth → `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`

---

## Pass 2 — Deferred Lighting

**Shaders:** `fullscreen.vert` + `lighting.frag`  
**Resolution:** full swapchain extent  
**Draw:** `vkCmdDraw(cmd, 3, 1, 0, 0)` — one fullscreen triangle, no vertex input  
**Render pass:** `PostProcessManager::get_lighting_render_pass()`  
**Framebuffer:** one per frame-in-flight

### Output

| Attachment | Format | Contents |
|-----------|--------|---------|
| Color 0 — HDR | `VK_FORMAT_R16G16B16A16_SFLOAT` | Linear HDR color (values > 1.0 for bright areas) |

### Descriptor sets

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` |
| 0 | 1 | UBO | `LightsUBO` |
| 1 | 0–3 | Combined image sampler | G-buffer albedo, normal, material, depth |

### Push constants

```glsl
layout(push_constant) uniform Matrices {
    mat4 invView;
    mat4 invProj;
} push;
```

`invProj` is used to reconstruct world position from depth + screen UV. This matrix is pushed per frame; it is not inverted inside the shader.

### Lighting model

- **Sun:** directional light, GGX specular, Lambertian diffuse
- **Point lights:** distance attenuation, up to `MAX_POINT_LIGHTS` (16) entries
- **Ambient:** sky-gradient approximation

### After this pass

Image barrier: HDR → `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`

---

## Pass 3 — Bloom Extract

**Shaders:** `fullscreen.vert` + `bloom_extract.frag`  
**Resolution:** 1/4 of full extent  
**Render pass:** `PostProcessManager::get_bloom_render_pass()`

### Input

| Binding | Contents |
|---------|---------|
| 0 | HDR image (from Pass 2) |

### Output

Pixels where `dot(color, vec3(0.2126, 0.7152, 0.0722)) > threshold` are passed through; others output black. This bright-pass mask drives the blur.

### Push constants

```glsl
layout(push_constant) uniform PostProcessParams {
    float threshold;
    float bloom_intensity;
    float exposure;
    // ...
} pp;
```

---

## Pass 4a — Bloom Blur (Horizontal)

**Shaders:** `fullscreen.vert` + `bloom_blur.frag`  
**Resolution:** 1/4 res  
**Input:** bloom extract image  
**Output:** horizontally blurred image

9-tap Gaussian along U. Push constant `texel_x = 1.0 / width`, `texel_y = 0.0`.

---

## Pass 4b — Bloom Blur (Vertical)

**Shaders:** `fullscreen.vert` + `bloom_blur.frag`  
**Resolution:** 1/4 res  
**Input:** horizontal blur image  
**Output:** fully blurred bloom image

9-tap Gaussian along V. Push constant `texel_x = 0.0`, `texel_y = 1.0 / height`.

The same shader handles both directions — only the push constant changes.

---

## SSAO — Disabled

SSAO images (`ao_full`, `ao_blur`) are allocated by PostProcessManager but the SSAO and AO-blur passes are not recorded. The composite pass samples the AO image; a one-time upload of white pixels (`primeAOTexture`) prevents undefined-layout reads.

Tracking: VUID-09600 (sampler reads from UNDEFINED layout image) is pending the SSAO re-enable.

---

## Pass 5 — Composite

**Shaders:** `fullscreen.vert` + `composite.frag`  
**Resolution:** full swapchain extent  
**Render pass:** `PostProcessManager::get_composite_render_pass()`  
**Framebuffer:** one per **swapchain image** (`get_composite_framebuffer(imageIndex)`)

### Inputs

| Binding | Contents |
|---------|---------|
| 0 | HDR color (Pass 2) |
| 1 | Bloom (Pass 4b) |
| 2 | AO (white fallback until SSAO enabled) |

### Processing

```
color = hdr + bloom * bloom_intensity
color = color * ao
color = ACES_tonemap(color * exposure)
```

ACES Narkowicz 2015 formula with named constants `kAcesA` through `kAcesE`.

### Output

| Attachment | Format | Contents |
|-----------|--------|---------|
| Color 0 | Swapchain format (e.g. `VK_FORMAT_B8G8R8A8_SRGB`) | Final LDR sRGB frame |

### Push constants

```glsl
layout(push_constant) uniform PostProcessParams {
    float threshold;
    float bloom_intensity;
    float exposure;
    // ...
} pp;
```

---

## Frame Synchronization

Before recording:
1. `waitForFence(frame)` — wait for the GPU to finish this frame slot's previous work
2. `vkAcquireNextImageKHR(..., imageAvailable[frame], ...)` — get the next swapchain image
3. `resetFence(frame)` — reset the fence for this frame slot

Submit:
- Wait semaphore: `imageAvailable[frame]`
- Signal semaphore: `renderFinished[imageIndex]`  ← indexed by swapchain image, not frame
- Signal fence: `inFlightFence[frame]`

Present:
- Wait semaphore: `renderFinished[imageIndex]`

See [`docs/diagrams/vulkan-sync.excalidraw`](diagrams/vulkan-sync.excalidraw) for the timing diagram.
