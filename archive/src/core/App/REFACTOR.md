# `App` modularization — status: DONE (as built 2026-09-29)

The original plan (split the ~505-line `App::run()` into named stages, extract the road/treadmill
systems, dedupe the key toggles, guard state behind setters) has been executed. This file is now
only the "as built" note; the rationale lives in git history and `review_2026-09-29/docs/app-build-and-cpp20.md`.

## What was executed

| Item | Where it landed |
|------|-----------------|
| Road geometry math | `src/scene/RoadGeometry/` |
| Endless-road treadmill + interchange follower | `src/scene/Treadmill/` (owned by `App::m_treadmill`, retired as the active road) |
| Real LIE corridor mechanics | `src/scene/Corridor/` (`App::m_corridor`) |
| State setters (`set_rain_level`, `set_weather_preset`, `set_wiper`, `set_camera_mode`, `set_cursor_captured`) | `App.cpp` |
| `run()` split into `init()` + per-frame stages | `App.cpp` (`run`, `frame_delta`, `poll_events`, `update_car`, `update_camera`, `shutdown`), `AppInit.cpp` (`init_window_and_renderer`, `init_textures`, `build_road_scene`, `init_scenes`, `load_car`) |
| Data-driven key table (reuses `KeyEdge`) | `AppInput.cpp`: `init_key_bindings()` builds `m_key_bindings` (Esc, C, R, V, G, backtick under `SWISH_DEBUG_UI`); `process_key_bindings()` polls it in row order |
| Camera block (~170 lines) | `src/core/CameraController/CameraController.{h,cpp}` — owns mode, look offsets, heli radius, cockpit yaw ease, FOV kick, shake clock |
| GLFW mouse / resize callbacks | `AppInput.cpp` |
| Dead accessors removed | `App::set_clear_day`, `clear_day()`, `cockpit()` (no callers) |

Per-frame order is unchanged: `frame_delta` → `poll_events` → key table → `CameraController::poll_input`
→ `update_car` (simulate, debug-panel sync, push to renderer) → `update_camera` → `Renderer::drawFrame`.
File sizes: `App.cpp` 715 -> 249, plus `AppInit.cpp` 208, `AppInput.cpp` 117, `CameraController.cpp` 292;
no function is longer than ~60 lines.

## What remains (not done, deliberately)

- **Input injection (review B4 / S8):** `Camera::process_keyboard` and `CarEntity::handle_input` still read
  GLFW directly. Gathering input into a plain struct in `core/` is scene-owned work (Camera.cpp) and not part of this pass.
- **Weather presets as data files (C4):** `WeatherPreset` is still an enum in `src/scene/WeatherPreset.h`.
- **`App` still owns the subsystem `unique_ptr`s and the scene-lambda wiring** (`init_scenes`); a
  `Subsystems` struct would be the next step only if a second scene appears.
- **No automated test drives `App`** (needs a window). Verify with a scripted screenshot run (`tasks/lessons.md`).
