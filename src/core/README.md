# src/core

Platform layer: window creation, the main loop, and input handling. No Vulkan knowledge lives here — all GPU work is delegated to `Renderer`.

---

## App (`App/App.h`, `App/App.cpp`, `App/AppInit.cpp`, `App/AppInput.cpp`)

The top-level orchestrator. `App::run()` is a short skeleton — `init()`, the frame loop, `shutdown()` —
and each concern is a named method (as-built notes: [`App/REFACTOR.md`](../../archive/src/core/App/REFACTOR.md)).

| File | Contents |
|------|----------|
| `App.cpp` | ctor/dtor, state setters (`set_rain_level`, `set_weather_preset`, `set_wiper`, `set_camera_mode`, `set_cursor_captured`), `run()` and the per-frame stages, car update + renderer push, `shutdown()` |
| `AppInit.cpp` | `init()` and its steps: window + renderer + callbacks, textures (incl. the procedural rumble strip), the road scene callback (corridor + POV camera), `SceneManager`, car load (`SWISH_CAR` override) |
| `AppInput.cpp` | `KeyEdge`, the data-driven key table, the key actions, and the GLFW mouse / resize callbacks |
| `../CameraController/` | `CameraController`: per-mode camera placement, cockpit FOV kick + shake, mouse-look offsets, free-fly WASD, helicopter Q/E |

<details>
<summary><strong>Main Loop Detail</strong></summary>

```
init()   // window, renderer, textures, scene, car, key table, rain = 0
while (!window.shouldClose()) {
    dt         = frame_delta(last)                // min(now - last, 1/15 s)
    debug_edit = debug_edit_mode()                // false in release

    poll_events()                                 // glfwPollEvents + consume resize flag
    process_key_bindings()                        // Esc, C, R, V, G (+ backtick in debug), row order
    camera.poll_input(...)                        // free-fly WASD / helicopter Q,E
    update_car(dt, debug_edit)                    // drive, Y-snap, corridor, wheel visuals, push draws
    update_camera(dt, debug_edit)                 // CameraController::update
    renderer.drawFrame(dt)
}
shutdown()   // wait_for_idle, reverse-order GPU cleanup
```

### Delta-Time Cap

$$dt_\text{eff} = \min\!\left(t_\text{now} - t_\text{last},\ \tfrac{1}{15}\ \text{s}\right)$$

Without the cap, losing window focus or hitting a breakpoint accumulates wall-clock time and produces a single enormous physics step on return (car teleports, camera spins). The cap limits any single step to at most $\approx 67\ \text{ms}$.

### Key Table and Edge Detection

Each toggle must fire **once per press**, not every frame while held. `KeyEdge::pressed()` returns true only on the
down transition and remembers the previous state. `m_key_bindings` holds one `{key, KeyEdge, action}` row per binding,
built in `init_key_bindings()` and polled in row order every frame:

```cpp
for (KeyBinding& b : m_key_bindings)
    if (b.edge.pressed(window, b.key))
        (this->*b.action)();
```

To add a binding: write an `on_*` method, declare it in `App.h`, add one row in `init_key_bindings()`.
The backtick row (`on_toggle_debug_edit`) exists only under `SWISH_DEBUG_UI`.

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
| W / A / S / D | Free-fly camera (forward / left / back / right; forward is flattened to XZ) |
| Q / E | Free-fly camera down / up (helicopter mode: orbit radius in / out) |
| Left Shift | Sprint (2× speed) while held |
| Mouse | Free-fly look; mouse-look offset in the car-attached camera modes |
| Arrow Up / Down | Car throttle / brake (reverse) |
| Arrow Left / Right | Car steer |
| C | Cycle camera preset (cockpit, chase, hood, bumper, roof, side, helicopter, free-fly) |
| R | Cycle rain (off, light, heavy) |
| V | Toggle windshield wiper |
| G | Cycle weather HDRI preset |
| Escape | Toggle cursor lock |
| Backtick (`` ` ``) | Debug builds only: toggle edit mode (free cursor, frozen sim) |
