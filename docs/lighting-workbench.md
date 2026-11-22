# Lighting workbench

The workbench separates the live Swish render from its ImGui controls. It keeps the actual Vulkan renderer, car, materials, and lighting path, so lighting changes are visible in context without loading the long driving corridor.

## Launch

```sh
make lookdev
```

This builds/runs the debug configuration with `SWISH_LOOKDEV=1`. The main window shows the car on a flat studio pad; **Swish Lighting Controls** is a separate, resizable GLFW/Vulkan window. `make debug` also opens the separate controls window, with the normal driving scene.

The workbench pad keeps its first-launch width and asphalt look, but is now **2.5 km long** along the car's forward axis (5× the original 500 m). The car is bounded to ±600 m from the center. Each road edge stays 650 m from that bound, beyond the camera's 600 m far plane, so the car cannot reach the road end or drive through the visible horizon. The camera therefore continues to show the skybox instead of exposing a close road edge.

Use a gamepad or focus the controls window and press **I/K** for throttle/brake and **J/L** to steer while tuning. Arrow keys in the game window continue to drive. Simulation runs while controls are adjusted. Enable **Pause simulation** in the controls window only when a still frame is useful. Press **F2** while the game window is focused to hide/show the controls window.

The existing debug controls edit scene/render parameters live. Shader source hot reload remains available through the existing Render Tools panel for the pipelines that support it; changing shader source does not promise a preview independent of the engine. The workbench is not a generic GLSL playground: it uses Swish's Vulkan shaders, descriptor layouts, render passes, assets, and scene inputs.

## What is and is not isolated

- The studio run skips corridor generation/update and keeps the same car and renderer, so it avoids the full road scene setup while preserving relevant lighting/material context.
- This does **not** bypass all engine startup: Vulkan, assets, IBL, and render passes still initialize.
- macOS runs this Vulkan renderer through the project's MoltenVK path. This is separate from OpenGL shader preview extensions and from command-line SPIR-V validation.
- Shader compilation/runtime errors still come from the Swish shader hot-reload/build path. The ImGui controls are now in another window; no separate preview process or generic shader uniform editor was added.

## Build and validation

The debug executable is `build-debug/swish`. Shader compilation is part of the CMake build using the project's configured `glslc`.

```sh
make debug-build
cmake --build build-debug --target shaders
ctest --test-dir build-debug --output-on-failure
```

Use `make debug` for the regular driving scene with live controls, or `make lookdev` for the focused pad. Existing shader hot reload is documented in the Render Tools panel. To resume work after shader errors, use the terminal/compiler diagnostics and the panel's shader reload controls.

## Known limitation

The windows and swapchains were verified at startup on the Apple M3 Max. The road length/bound/far-plane relationship is enforced by compile-time assertions. A screenshot-based check of the final road horizon was not possible in this desktop session.
