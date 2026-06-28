# src/core

Platform layer: window creation, the main loop, and input handling. No Vulkan knowledge lives here — all GPU work is delegated to `Renderer`.

---

## App (`App/App.h`, `App/App.cpp`)

The top-level orchestrator. `App::run()` drives everything:

1. Creates `Window` and constructs `Renderer`
2. Registers `TextureManager`, `SceneManager`, `ModelManager` with Renderer
3. Calls `Renderer::init()` and sets up the active scene
4. Enters the main loop (see below)
5. On exit: `vkDeviceWaitIdle` → `Renderer::cleanup()` → destruct

<details>
<summary><strong>Main Loop Detail</strong></summary>

```
while (!window.should_close()) {
    t_now      = glfwGetTime()
    delta_time = min(t_now − t_last, 1/15 s)    // clamped
    t_last     = t_now

    window.pollEvents()

    // Input
    handle_escape_edge()       // edge-detected: fires once per press
    handle_cockpit_toggle()
    handle_camera_wasd()
    handle_car_arrows()

    // Update
    car.update(delta_time)
    if (cockpit) camera.sync_to_car(car)

    renderer.drawFrame()
}
```

### Delta-Time Cap

$$dt_\text{eff} = \min\!\left(t_\text{now} - t_\text{last},\ \tfrac{1}{15}\ \text{s}\right)$$

Without the cap, losing window focus or hitting a breakpoint accumulates wall-clock time and produces a single enormous physics step on return (car teleports, camera spins). The cap limits any single step to at most $\approx 67\ \text{ms}$.

### Escape Key Edge Detection

The Escape key toggles cursor lock — it must fire **once per press**, not every frame while held. This uses a previous-state flag:

```cpp
bool pressed = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
if (pressed && !m_esc_key_prev) {
    toggle_cursor_lock();
}
m_esc_key_prev = pressed;
```

Without edge detection, cursor lock would toggle at ~60 Hz while the key is held, making it impossible to release the cursor.

### Resize Flag Consumption

`wasResized()` returns and **clears** the flag in one call. This prevents double-recreation if both `pollEvents` and `drawFrame` check for resize in the same frame.

</details>

---

## Window (`Window/Window.h`, `Window/Window.cpp`)

Thin GLFW wrapper. Created with `GLFW_CLIENT_API = GLFW_NO_API` — no OpenGL context is created.

| Method | Behavior |
|--------|---------|
| `init(w, h, title)` | `glfwCreateWindow`; registers framebuffer resize callback |
| `pollEvents()` | `glfwPollEvents()` — returns immediately; used every frame |
| `waitEvents()` | `glfwWaitEvents()` — blocks CPU until any event arrives; used in minimize spin |
| `mark_resized()` | Called by GLFW resize callback; sets `m_resized = true` |
| `wasResized()` | Returns and clears `m_resized` |
| `getFramebufferSize()` | Returns actual pixel size (differs from window size on HiDPI displays) |

<details>
<summary><strong>Minimize Spin</strong></summary>

When the window is minimized, `getFramebufferSize()` returns `0×0`. A zero-extent swapchain cannot be recreated, so `Renderer::recreateSwapchain()` loops until the size is non-zero:

```cpp
while (width == 0 || height == 0) {
    m_window->waitEvents();            // blocks CPU — no busy-wait
    m_window->getFramebufferSize(&width, &height);
}
```

Using `waitEvents()` instead of `pollEvents()` here means the CPU idles at ~0% during minimize, rather than spinning at 100% re-checking a condition that only changes when the user un-minimizes the window.

</details>

---

## Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Free-fly camera (forward / left / back / right) |
| Shift + WASD | Sprint (3× speed) |
| Mouse | Free-fly look |
| Arrow Up / Down | Car throttle / brake |
| Arrow Left / Right | Car steer |
| C | Toggle cockpit / free camera |
| Escape | Toggle cursor lock |
