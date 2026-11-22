# GLSL Shader Playground

A small OpenGL GLSL example with external compiler validation and an optional live editor preview. It does not require a C++ renderer.

## Requirements and shader inputs

- macOS with Homebrew and `glslangValidator`, `glslc`, and `spirv-val` on `PATH`.
- `basic.vert` and `basic.frag` use **GLSL 3.30 core**. They demonstrate a vertex/fragment pair intended for an OpenGL host. `basic.vert` expects position and normal attributes at locations 0 and 1, `modelViewProjection`; `basic.frag` expects interpolated normals and `baseColor` and `lightDirection` uniforms.
- `effects/animated.glsl` uses Shader Studio's Shadertoy-compatible GLSL interface (`mainImage`, `iTime`, `iResolution`). It is a fragment-only preview shader, not the `basic.vert`/`basic.frag` program. `iTime` is elapsed seconds and `iResolution` is preview size in pixels.
- Files with stage suffixes (`.vert`, `.frag`, `.geom`, `.comp`) identify their stage from the suffix. A generic `.glsl` file needs `#pragma shader_stage(vertex|fragment|geometry|compute|tesscontrol|tesseval)` to identify its stage; otherwise it is stage-ambiguous and skipped. The animated preview has a `mainImage` entry point, so it is intentionally preview-compiled by Shader Studio rather than treated as a standalone OpenGL program. Include snippets without `main()` are skipped in project scans.

## Validate and compile

From the repository root, use **Terminal → Run Task**:

- `Shader: Validate current OpenGL GLSL` — validates the active file using its stage.
- `Shader: Validate OpenGL example project` — validates the OpenGL sample shaders.
- `Shader: Compile current Vulkan GLSL to SPIR-V` / `Shader: Compile all Vulkan GLSL to SPIR-V` — compiles Vulkan GLSL into `build/spirv/`, preserving source-relative paths.
- `Shader: Validate current SPIR-V` / `Shader: Validate all generated SPIR-V` — validates binaries with `spirv-val --target-env vulkan1.2`.

Equivalent commands from the repository root:

```sh
./tools/shader-tools.sh validate-gl shader-playground/shaders/basic.vert
./tools/shader-tools.sh validate-gl-project
./tools/shader-tools.sh compile-vulkan shader-playground/shaders/basic.vert
./tools/shader-tools.sh validate-spirv shader-playground/shaders/basic.vert
```

Open this folder as a workspace to get its local task names and recommended extensions. The task script lives at `../tools/shader-tools.sh`.

## Live visual preview

Install/open **Shader Studio** (`teaqu.shader-studio`) in VS Code, open `shaders/effects/animated.glsl`, then use the status bar Shader Studio control and choose **Open Panel** (or **Open in Browser**). It recompiles edits live and supplies Shadertoy-style `iTime` and `iResolution`. Its preview is a WebGL fragment-shader environment; it is not a native OpenGL application and does not execute the sample's ordinary `main()` vertex/fragment pair as a linked OpenGL program. The full extension also documents meshes (plane/cube/sphere), vertex stages, and geometry, but availability depends on the selected preview pipeline and shader interface. For the reliable minimal preview, use `animated.glsl` on the fullscreen quad.

Shader Studio is already installed in VS Code on this Mac. Install it manually in Cursor if desired. **Nifty Shaders** (`nifty.nifty-shaders`) is a separate diagnostics/highlighting extension and does not provide a visual renderer; it is available for VS Code and Cursor/Open VSX. Both extensions are recommended in `.vscode/extensions.json`.

## macOS notes

The preview workflow uses a WebGL-capable editor webview/browser. OpenGL GLSL checking uses `glslangValidator`. Vulkan GLSL compilation and SPIR-V validation are separate offline tooling steps; they do not execute Vulkan on macOS. No MoltenVK, CUDA, or Linux graphics stack is installed or required here.
