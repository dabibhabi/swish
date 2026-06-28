# shaders

GLSL 4.50 source files compiled to SPIR-V by `glslc` during the CMake build. Output goes to `build/shaders/*.spv`.

## Compilation

CMake discovers all `*.vert` and `*.frag` files automatically via `SHADER_SOURCES` and compiles them with:

```cmake
glslc <shader> -o build/shaders/<shader>.spv
```

You do not need to rerun CMake when editing an existing shader; just rebuild:

```bash
cmake --build build --target shaders
```

## Descriptor Set Layout

All geometry shaders share the same layout:

| Set | Binding | Type | Contents |
|-----|---------|------|---------|
| 0 | 0 | UBO | `CameraUBO` (view, proj, camPos, sun direction) |
| 0 | 1 | UBO | `LightsUBO` (point lights array) |
| 1 | 0 | Combined image sampler | Albedo texture |
| 1 | 1 | Combined image sampler | Normal map |
| 1 | 2 | Combined image sampler | Roughness/metallic map |

Post-process passes (lighting, bloom, composite) bind their own sets — they do not use set 1 (material textures).

## Shared Vertex Shader

`fullscreen.vert` is used by all post-process passes. It emits a fullscreen triangle from constants — no vertex input binding required:

```glsl
vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
```

Invoke with `vkCmdDraw(cmd, 3, 1, 0, 0)`.

## File Reference

| File | Pass | Notes |
|------|------|-------|
| `basic.vert` | G-buffer | Per-vertex transform; outputs world pos + normal |
| `gbuffer.frag` | G-buffer | MRT: albedo, normal, material; alpha-tests (discard if α < 0.5) |
| `fullscreen.vert` | All post-process | Shared fullscreen triangle shader |
| `lighting.frag` | Deferred lighting | Reads G-buffer; GGX PBR + point lights + ambient |
| `bloom_extract.frag` | Bloom extract | Luma threshold bright-pass |
| `bloom_blur.frag` | Bloom blur H + V | 9-tap Gaussian; direction via push constant |
| `composite.frag` | Composite | HDR + bloom → ACES tonemapping → sRGB output |
| `basic.frag` | (unused / debug) | Forward shading fallback |

## Adding a New Pass

1. Add `<name>.vert` / `<name>.frag` to this directory — CMake picks it up on the next configure.
2. Load SPIR-V at runtime:
   ```cpp
   auto vert = FileIO::readBinaryFile(std::string(SHADER_DIR) + "name.vert.spv");
   ```
3. Wrap `VkShaderModule` creation in `ScopedShaderModule` (see `Pipeline.cpp`) to avoid leaks.
4. Update this file's reference table.

See [CONTRIBUTING.md § Adding a new shader pass](../CONTRIBUTING.md) for the full checklist.
