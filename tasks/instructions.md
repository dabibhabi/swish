# Handoff instructions (2026-11-20)

Read this first in a new session. Then: `docs/user-goals/CURRENT.md`, `tasks/todo.md`, `git log --oneline -10`.

## State

- Branch `bug/fix-camera-ownership`. Last commit `698c335`. **Everything below is uncommitted.** Commit and push only when the owner asks (the hook blocks agent pushes; the owner pushes).
- Uncommitted work: L2.4 rain-onset hitch, Spray v2, L10.2 wet streaks and lens rain, small items (`VK_CHECK_LOG`, F3 render scale, hydroplane film lag), Wheel Phase C slip and B2 weight transfer. Each has an entry in `docs/changelog/2026-11.md`.
- Gates were green at hand-off: `make test` 444/444, `make test-debug` 469/469, `make format-check` clean, release `.spv` golden unchanged.

## Owner to-dos (agents cannot do these)

1. Delete `Untitled.md` from the repo root.
2. Run `git rm --cached .obsidian/workspace.json`.
3. In the app (`make debug`), check:
   - Braking dips the nose; corners lean the body; full throttle spins the rear wheels.
   - Rain at 0 and at 1.0: a wet launch spins longer.
   - Tyre spray reads as water.
   - Lamp and brake-light streaks show on the wet road.
   - F3 cycles render scale 1x / 1.5x / 2x.

`make check` fails until items 1 and 2 are done.

## Open next (see `tasks/todo.md`)

- Rain at night (rain always uses the daytime Misty HDRI).
- The car drifts left into the barrier when driving straight (pre-existing).
- SSAA: a better downsample kernel above 1.5x.
- Hydroplane: per-side water sampling or a combined-slip tyre model.
- Phase C follow-ups: per-wheel slip angle, skid marks, tyre load sensitivity.
- The remaining ~12 ms `vkQueueWaitIdle` in the IBL bake could become a fence-based async submit.
- `CarEntity.cpp` is 842 lines, over the 800-line soft limit; split it when next touched.

## Rules that bite

- After each change: `make format`, `make test`, `make test-debug`. Add a changelog entry to `docs/changelog/2026-11.md`, then run `python3 scripts/changelog_index.py`.
- Debug-only code goes behind `#ifdef SWISH_DEBUG_UI`. Release `.spv` must stay byte-identical unless the change is intended. In that case update `tests/golden/release_spv.sha256` and say so in the changelog.
- After every realism phase, append knobs, ranges, LaTeX and sources to `docs/<topic>/tuning-notes.md` (`docs/rain/`, `docs/wheels/`).
- Units: 1 m = 1000 WU. Use `utils/units/Units.h` and never re-type the constants.
- No apostrophes inside the single-quoted `ALLOWED` list in `scripts/check_root.sh`.

## App testing (the owner allowed screenshots)

- Kill the app before rebuilding: `pkill -x swish`.
- Target the game window by the title ending in `fps`. Never use `starts with "swish"`: it also matches the "Swish Lighting Controls" panel.
- Key presses: post CGEvents with a small Swift script, for example `swift key.swift <keycode> <hold_ms>`. Keycodes: Up 126, Down 125, Right 124, C 8, F3 99.
- Screenshots: capture the window only, by CGWindow id: `screencapture -x -o -l<id>`.
- Temporary forces (for example `set_rain_level(2)` in `AppInit.cpp`) must be reverted after the test.
