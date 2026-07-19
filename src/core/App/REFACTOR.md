****# `App` modularization plan

`App.cpp` is ~630 lines with a **single 508-line `App::run()`** ([App.cpp:119-627](App.cpp#L119-L627)) that
does everything from window init to the per-frame endless-road treadmill. The Outline view is
useless because almost all logic lives inside that one function as local lambdas and inline blocks.

This README proposes **how to split `run()` into named methods / files** and **which raw member
fields should become getters/setters**. It is a plan only — no code has been moved yet.

---

## 1. The problem, concretely

`run()` currently interleaves five unrelated concerns:

| Concern                                                                    | Lines today                  | Why it doesn't belong in `run()`                                                      |
| -------------------------------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------------------- |
| Road/geometry math constants + `road_surface_y`, `drivable_bounds` lambdas | [120-177](App.cpp#L120-L177) | Pure functions of position; no reason to be closures capturing `this`.                |
| Subsystem bring-up (window, renderer, textures, models, scenes, car)       | [179-326](App.cpp#L179-L326) | One-time setup; belongs in an `init()` phase, not the loop function.                  |
| Input / key-edge handling (Esc, C, R, V, G, backtick)                      | [351-422](App.cpp#L351-L422) | Repetitive edge-detect blocks — the single biggest readability win to extract.        |
| Endless-road treadmill (rebase, chunk window, interchange instancing)      | [442-493](App.cpp#L442-L493) | Self-contained system with its own state (`m_originShift`, `m_introLen`, chunks).     |
| Interchange ribbon-follower                                                | [495-560](App.cpp#L495-L560) | Self-contained system with its own state (`m_carRibbon`, `m_ribbonS/T`, `m_ixTrueZ`). |

Because they're all in one scope, the Outline shows only `mouse_callback`,
`framebuffer_resize_callback`, and `run` — three entries for 630 lines.

---

## 2. Proposed function grouping

Goal: `run()` shrinks to a readable skeleton —

```cpp
int App::run() {
    init_subsystems();      // §A
    setup_scene_and_car();  // §B
    apply_initial_look();   // §C
    float last = glfwGetTime();
    while (!m_window->shouldClose()) {
        float dt = tick_delta(last);
        m_window->pollEvents();
        process_input(dt);          // §D
        update_car(dt);             // §E  (drive + treadmill + interchange)
        update_camera();            // §F  (cockpit vs free-fly)
        m_renderer->drawFrame(dt);
    }
    shutdown();                     // §G
    return 0;
}
```

### New files

Two of the five concerns are genuinely **separable systems with their own state**, so they deserve
their own translation units rather than just being private methods. The rest become private methods
on `App` (same file or a thin `App_input.cpp` / `App_setup.cpp` split).

| New file                                      | Moves out of `App.cpp`                                                                                                                                                                                                     | Owns / operates on                                                                                                                                                                                                                                                                                                                                           |
| --------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/scene/RoadGeometry/RoadGeometry.{h,cpp}` | the `namespace {}` block ([28-61](App.cpp#L28-L61)) + `road_surface_y` ([140-146](App.cpp#L140-L146)) + `drivable_bounds` ([156-177](App.cpp#L156-L177)) + all `k*` road constants ([120-133, 152-155](App.cpp#L120-L133)) | Free functions `ribbonLength`, `ribbonSample`, `road_surface_y(x)`, `drivable_bounds(...)`. Pure, unit-testable (ties into your Catch2 suite — none of these are covered today).                                                                                                                                                                             |
| `src/scene/Treadmill/Treadmill.{h,cpp}`       | endless-road block ([442-493](App.cpp#L442-L493)) + interchange follower ([495-560](App.cpp#L495-L560))                                                                                                                    | A `Treadmill` class owning `m_originShift`, `m_introLen`, `m_sceneLights`, `m_ixRibbons`, `m_carRibbon`, `m_ribbonS/T`, `m_ixTrueZ`. Exposes `void update(CarEntity&, Renderer&, float dt, bool debugEdit)`. This is the highest-value extraction: it's ~110 lines of self-contained state-machine that has nothing to do with `App`'s role as an app shell. |

### New private methods on `App` (stay in `App.cpp`, or split into `App_setup.cpp` / `App_input.cpp`)

| Method | Extracted from | Notes |
|---|---|---|
| `void init_subsystems()` | [179-239](App.cpp#L179-L239) | Window, Renderer, GLFW callbacks, TextureManager (incl. the rumble-strip texture block [212-230](App.cpp#L212-L230) — that itself could be a `make_rumble_texture()` helper), ModelManager, material descriptors. |
| `void setup_scene_and_car()` | [241-313](App.cpp#L241-L313) | The `road_scene` lambda ([242-278](App.cpp#L242-L278)) + car load ([289-313](App.cpp#L289-L313)). |
| `void apply_initial_look()` | [315-326](App.cpp#L315-L326) | The baked "lie" preset (overcast + rain 1.0). |
| `float tick_delta(float& last)` | [329-335](App.cpp#L329-L335) | Delta-time + the 67 ms clamp. |
| `void process_input(float dt)` | [351-428](App.cpp#L351-L428) | All six key toggles. **Each toggle is a candidate for a tiny `edge_pressed(key, prev)` helper** to kill the copy-pasted `x_down && !m_x_key_prev` pattern (6 repeats). |
| `void update_car(float dt, bool debugEdit)` | [431-593](App.cpp#L431-L593) | Drive + Y-snap; delegates the treadmill/interchange to the new `Treadmill`; pushes draw calls + velocity to renderer. |
| `void update_camera()` | [595-609](App.cpp#L595-L609) | Cockpit eye-follow vs free-fly. |
| `void shutdown()` | [614-624](App.cpp#L614-L624) | Reverse-order GPU cleanup. |

> **Edge-detect helper** — the single biggest line-count win. Replace six blocks of
> ```cpp
> bool x_down = glfwGetKey(w, KEY) == GLFW_PRESS;
> if (x_down && !m_x_key_prev) { ... }
> m_x_key_prev = x_down;
> ```
> with a small `KeyEdge` struct (`bool pressed(GLFWwindow*, int key)`) or a
> `std::array<bool, N>` of prev-states keyed by an enum. This collapses ~70 lines to ~25.

---

## 3. Fields that should become getters / setters

Right now **every member is a bare private field** mutated directly inside `run()`. Once logic moves
into methods (and especially into the new `Treadmill`), the state needs controlled access. Grouping:

### 3a. Should move OUT of `App` entirely (into the new systems)

These have no business being `App` members once the systems exist — they become private state of
`Treadmill`, accessed only through its `update()`:

| Field | New home |
|---|---|
| `m_introLen`, `m_originShift`, `m_sceneLights` | `Treadmill` (private). `App` keeps none of them. |
| `m_ixRibbons`, `m_carRibbon`, `m_ribbonS`, `m_ribbonT`, `m_ixTrueZ` | `Treadmill` (private). |

`m_sceneLights` is the one subtlety: it's produced by the scene-setup lambda ([247-248](App.cpp#L247-L248))
and consumed by the rebase ([455-457](App.cpp#L455-L457)). Hand it to `Treadmill` at construction /
`set_intro(...)` time.

### 3b. Should become setter-guarded state (invariants to enforce)

These are mutated together with a side effect on the renderer; a setter keeps the field and the
renderer in sync so callers can't set one without the other:

| Field(s) | Proposed accessor | Invariant the setter enforces |
|---|---|---|
| `m_rain_level` + rain intensity | `void set_rain_level(int)` / `int rain_level() const` | Maps level→intensity via `kRainLevels`, pushes to `m_renderer->set_rain_intensity(...)` **and** `m_car->set_rain_intensity(...)`, and cancels clear-day. Today this logic is duplicated between the launch preset ([322-326](App.cpp#L322-L326)) and the R-key handler ([371-386](App.cpp#L371-L386)). |
| `m_clear_day` | `void set_clear_day(bool)` | Calls `m_renderer->set_clear_day(...)`; when true, resets rain to 0. Dedupes G-key ([399-408](App.cpp#L399-L408)) vs launch. |
| `m_wiper_enabled` | `void set_wiper(bool)` / toggle | Pushes to `m_renderer->set_wiper_enabled(...)`. |
| `m_cockpit` | `void set_cockpit(bool)` | Zeros `m_look_yaw/pitch` on change ([363-365](App.cpp#L363-L365)). |
| `m_cursor_captured` | `void set_cursor_captured(bool)` | Calls `glfwSetInputMode(...)` + resets `m_first_mouse`. |

> The pattern to notice: **`set_rain_intensity` / `set_clear_day` are already called from three
> places with slightly different bookkeeping.** Centralizing them in `App` setters removes the risk
> of the "rain on but clear-day still true" inconsistency the current code hand-patches at
> [382-385](App.cpp#L382-L385).

### 3c. Fine to stay bare fields (no invariant, no external sync)

`m_first_mouse`, `m_last_mouse_x/y`, all the `m_*_key_prev` edge flags, `m_look_yaw/pitch`,
`m_debug_edit_mode`, `m_backtick_prev`. These are pure local UI state with no cross-object contract —
wrapping them in accessors would be ceremony. (If you adopt the `KeyEdge` helper from §2, the
`*_key_prev` flags disappear anyway.)

---

## 4. Suggested order of operations (one commit each, keep `make test` green)

1. **Extract `RoadGeometry`** (pure functions) — lowest risk, immediately unit-testable, and it's
   currently duplicated math. Add Catch2 tests for `road_surface_y` symmetry and `ribbonSample`
   clamping while you're there.
2. **Extract the six input toggles → `process_input()` + `KeyEdge` helper.** Pure mechanical move;
   biggest Outline win.
3. **Introduce the rain/clear-day/wiper/cockpit setters** (§3b) and route all three existing call
   sites through them — this removes the duplicated bookkeeping *before* you move code, so the moves
   are simpler.
4. **Extract `init_subsystems` / `setup_scene_and_car` / `apply_initial_look` / `shutdown`.**
5. **Extract `Treadmill`** (the big one) — move §3a fields into it last, once everything around it is
   already in methods.

Each step is behavior-preserving; verify per project convention (build **both** `SWISH_DEBUG_UI`
ON/OFF, run, drive far enough to trigger a rebase + an interchange, test rain at 0 and 1.0).

---

## 5. Expected Outline after refactor

`App.cpp` (or `App.cpp` + `App_input.cpp` + `App_setup.cpp`) Outline becomes:

```
App::App()
App::~App()
App::run()
App::init_subsystems()
App::setup_scene_and_car()
App::apply_initial_look()
App::process_input(float)
App::update_car(float, bool)
App::update_camera()
App::shutdown()
App::mouse_callback(...)
App::framebuffer_resize_callback(...)
+ set_rain_level / set_clear_day / set_wiper / set_cockpit / set_cursor_captured
```

…plus two new navigable units: `RoadGeometry` and `Treadmill`. That's the difference between an
Outline that helps you jump around and the current three-entry wall.
