# Handoff: LIE full route + 6-DOF physics + real road

> Written by AI (Claude) on 2026-11-20 for the next agent. Read this, then the plan section
> "LIE full route" at the top of [`todo.md`](todo.md). Owner is away; run without asking.

## Standing rules from the owner (this plan)

- Agents build. **One code item at a time, in plan order. No parallel code agents** (parallel only for research).
- Code style: ponytail ultra + caveman. Shortest working diff, one-line why-comments only, no options nobody asked for.
- **Debug-first.** Release (`make run`) must stay byte-identical. Gate with `#ifdef SWISH_DEBUG_UI` / `kDebugBuild`.
- Stay on branch `bug/fix-camera-ownership`. **Do not commit or push.** About 66 files are uncommitted, roughly 40 of them from other work: do not revert or reformat files you did not need to touch.
- Each item ships: debug sliders (`src/debug/Tunables.def`), tuning notes, a changelog entry in `docs/changelog/2026-11.md` (then `python3 scripts/changelog_index.py`), and an architecture note with university sources.
- Simple unit tests only. Complex tests and in-app checks go to [`upcoming-tests.md`](upcoming-tests.md).
- For this plan agents may launch `make debug` and take window-only screenshots.
- Out of scope: traffic, manual shifting, scripted weather.

## State

| Step | State |
|------|-------|
| A1-A5 route data | Done. Debug asset `assets/corridor/lie_corridor_full.bin` (format v2): 70.41 real mi baked to 30.0 mi, USGS heights, banking, 108 exits, 3 districts. Mesh, lamps and car follow hills. |
| B1 substep | Done. Equal slices of at most 1/240 s, slider `carSubstepHz`. |
| B2-B5a, B5c 6-DOF car | Built in `src/scene/Entity/RigidChassis.h` + `TireCurve.h`. **Off by default** (`rigidBodyChassis` tunable). |
| B5b bumps | Not started. Waits for D1 (surface material id). |
| B5c grass drag, B5d skid marks/smoke | Not started. |
| B6 keyboard feel, B7 owner A/B | Not started. Need a human driving. |
| C1-C4 ramps, service roads, tunnel, HUD | Not started. **Next: C1.** |
| D0-D6 looks | Not started. Reference sheet: [`docs/lie-reference/README.md`](../docs/lie-reference/README.md). |

Tests at handoff: `make test-debug` 481 pass, `make test` 456 pass.

## Where things are

- Bake tool: `tools/lie_corridor.py` (`fetch`, `net`, `elev`, `bake`). Config: `config/lie_corridor.toml`. `bake` needs no network.
- Asset format + loader: `src/scene/Corridor/CorridorAsset.h` (v1 flat still loads; exits and districts are optional trailing blocks). Python packer and C++ structs must stay byte-identical.
- Queries: `CorridorAlignment::sample / surface_y / surface_slope / next_exit / district_at`.
- Mesh: `src/scene/Corridor/CorridorScene.cpp`, sweep primitives in `src/scene/RoadScene/MeshBuilder.cpp`.
- Car height: `CorridorDriver::update` calls `RoadGeometry::set_plane_surface` each frame.
- Retired generators to port in C1 (ramps, service roads, signs, guardrail, overpass): `src/scene/RoadScene/RoadScene_*.cpp`, `Treadmill.cpp`. OSM ramp ways (719) are in `data/osm/lie_i495.osm.xml`.
- Tuning notes: `docs/corridor/tuning-notes.md`, `docs/wheels/tuning-notes.md`.

## Traps found this session

- `data/osm/` is git-ignored. `fetch --force` overwrites the only copy. Back it up first. (`data/dem/` is tracked.)
- `lie_corridor.bin` is the release asset. The tool now writes `lie_corridor_full.bin`; never point it back.
- The map frame is pinned in the config (`frame_lat0`, `frame_lon0`, `frame_psi_deg`). Removing the pin rotates the whole road.
- `make format` realigns comments; re-read a line before a string-match edit.
- `tests/test_debug_params_io.cpp` pins tunable counts (now 74 total, 60 floats, 14 bools). Each new `TUNE_*` line needs them bumped.
- `RigidChassis` Euler angles: nose is +X, so the X angle is body roll.
- Screenshots: the window title is `swish | <fps>`; match the owner name `swish` and a title prefix, then `screencapture -x -o -l<id>`.
- Synthetic key input (CGEvent) was unreliable: it worked once, then stopped, and one attempt sent arrow keys to another app. Confirm the game window is key before sending, or script inputs inside a temporary debug block and remove it.
- Everything past about 100 m renders as sky: `lighting.frag` (and ssao/ssr/dof/godrays) test `depth < 0.0001`, and with reverse-Z and the 1 cm near plane that is z > 100 m. For screenshots temporarily use `depth <= 0.0`, then revert (renderer fix is an owner call). Spawn near a station with a temporary `SWISH_START_KM` read in `place_at_start`; the car starts accelerating by itself about 5 s after spawn, so shoot early.
- `make check` fails on two older items (`Untitled.md`, `.obsidian/workspace.json`). Not from this work.

## Unverified claims to treat with care

- Queens/Nassau line is placed at exit 33 from memory.
- Exit sign text is OSM's wording, unchecked against photos.
- The 6-DOF car was only run through a scripted launch and brake. Steering, slopes and wall contact were never exercised in the app.
- Only the first mile of the route was viewed. Hills (from about 10 mi) and the east end were not.
- Reading-list sources in the tuning notes were not opened.

## Suggested order from here

1. C1: port the ramp and service-road generators to corridor frames (heights and bank from `Sample`).
2. C2: one standard-diamond generator driven by `exits()`; fictional service road east of exit 64.
3. C3: drivable-bounds query covers ramps and service roads (`CorridorBounds.cpp`).
4. D3 signs can start once C2 places gores: `tools/sign_generator.py` reads the exit table.
