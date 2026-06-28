# src/core

Platform layer: window creation, the game loop, and input handling. No Vulkan knowledge lives here.

## Components

### App (`App/App.h`, `App/App.cpp`)

The top-level orchestrator. `App::run()` drives everything:

1. Creates `Window` and constructs `Renderer`
2. Registers `TextureManager`, `SceneManager`, `ModelManager` with Renderer via `register_*()` calls
3. Calls `Renderer::init()` and sets up the active scene
4. Enters the game loop:
   - Poll events
   - Compute `delta_time` (clamped to 1/15 s to avoid physics explosions after focus loss)
   - Update car physics and camera
   - Call `Renderer::drawFrame()`
5. On exit: `Renderer::cleanup()` then destructs

**Input handling notes:**
- Escape key uses edge detection (`m_esc_key_prev`) — only fires once per press, not every frame
- Resize flag (`wasResized()`) is consumed after `pollEvents()` to avoid double-recreation

### Window (`Window/Window.h`, `Window/Window.cpp`)

Thin GLFW wrapper:

| Method | Purpose |
|--------|---------|
| `init()` | `glfwCreateWindow`, set resize callback |
| `pollEvents()` | `glfwPollEvents()` — non-blocking, used each frame |
| `waitEvents()` | `glfwWaitEvents()` — blocks CPU until an event; used in minimize spin to avoid spinning |
| `mark_resized()` | Called by GLFW resize callback; sets internal flag |
| `wasResized()` | Returns and clears the resize flag |

## Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Free-fly camera |
| Arrow Up / Down | Accelerate / brake car |
| Arrow Left / Right | Steer car |
| C | Toggle cockpit / free camera |
| Escape | Release cursor |
