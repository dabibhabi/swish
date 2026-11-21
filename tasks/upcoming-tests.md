---
title: Upcoming tests (renderer roadmap R7-R10, E1-E5)
tags: [swish, renderer, tests, todo]
updated: 2026-11-15
branch: bug/fix-camera-ownership
ai-generated: true
---

# Upcoming tests

> [!warning] AI-generated
> Claude drafted this list from the renderer roadmap grilling session (2026-11-01). Nothing here has been run yet. Tick each item when you have done it yourself.

Decision from the grilling session: **no new unit tests are written now.** This file holds the manual app checks for each item, plus the unit tests worth adding later.

## Every item

- [ ] Kill the running app before rebuilding (it loads `.spv` at startup)
- [ ] `make build` and `make debug-build`
- [ ] `make test` and `make test-debug` green
- [ ] `make format-check` and `make check` clean

## R7: window assert

- [ ] Debug build: launch, resize, minimize and restore. No assert fires.
- [ ] Release build: same steps, same behaviour as before.

Unit test for later: none. An assert on a precondition is checked by reading the code.

## R8: safe teardown

- [ ] Launch and quit normally. No validation errors on quit and no crash in `~Renderer`.
- [ ] Quit while minimized (R1 path). It still exits.
- [ ] Debug build: quit with the ImGui panel open. It exits cleanly.

Unit tests for later (no GPU needed):
- [ ] `Renderer r; r.cleanup(); r.cleanup();` runs with no crash, and the destructor runs after it.
- [ ] `Renderer r;` with no `init()` and no `cleanup()` lets the destructor run with no crash.

## R9: wait_for_idle

- [ ] Resize the window several times. No hitch and no validation error.

## R10: named pass calls

- [ ] Resize the window. The picture rebuilds at the new size.
- [ ] Debug build: change the SSAA scale and press "Apply SSAA". The picture rebuilds at the new resolution.

## E1: weather-key stall

- [ ] Measure: press `G` once and write down load ___ ms, idle-wait ___ ms, bake ___ ms.
- [ ] If Option A ships: the `.spv` golden fails first, then is regenerated on purpose (`SWISH_UPDATE_GOLDENS=1`, see `tests/golden/README.md`).
- [ ] Look at a glossy panel on the car in clear weather and in overcast, before and after. Both must look the same.
- [ ] Measure again after the fix: ___ ms.
- [ ] Delete the timing code.

## E2: input latency

- [ ] Measure the average fence wait ___ ms and acquire ___ ms while driving.
- [ ] Feel test in `make run`, steering before and after. Revert if you can't feel a difference.
- [ ] If Option B ships: test minimize, resize and the SSAA button.

## E3: timeouts

- [ ] Minimize and restore. Nothing is thrown or logged.
- [ ] Cover the window with another app for 10 s, then uncover it. Note whether the acquire log line appears (once at most) and whether rendering resumes.
- [ ] Read through the fence-timeout path. A real GPU hang can't be tested.

## E4: fps readout

- [ ] The title updates once per second in both `make run` and `make debug`.
- [ ] Press `G`. The fps visibly drops during the bake, which shows the counter isn't using the clamped `dt`.
- [ ] Changelog notes the intended change to the release build.

## E5: atomic pipeline cache

- [ ] Find the cache file (`util::paths::user_cache_dir()` + `pipeline_cache.bin`).
- [ ] Delete it, launch, quit cleanly. The file exists and no `.tmp` is left behind.
- [ ] Launch again. Startup is faster than the first launch.

Unit test for later:
- [ ] Factor the write into a small `write_file_atomic(path, bytes)` helper and test that the target file holds the whole old content or the whole new content, and that no `.tmp` is left behind.

# Lighting L2 to L10 and the lab (added 2026-11-15)

> [!warning] AI-generated
> Claude drafted this list after building L2 to L10 debug first. None of it has been run in the app. L1 and the lab were run by the owner and looked right. Reference: `docs/lighting-l2-l10.md`, `docs/lighting-lab.md`.

## Release unchanged

- [ ] `make run`: the frame looks the same as before L2 (HDRI sky, no street lamps, no lens glow).
- [ ] `(cd build/shaders && shasum -a 256 -c ../../tests/golden/release_spv.sha256)` reports 33 OK.

## L2: sky and time of day (`make debug`)

- [ ] Start-up is the procedural sky at noon: blue overhead, paler horizon, a small sharp sun.
- [ ] T cycles all six presets; note how long each switch hitches.
- [ ] Dawn and golden hour: warm low sun, red toward the sun, long shadows.
- [ ] Evening: twilight glow, no sun disc, no sun shadows.
- [ ] Midnight: dark sky, moon disc, stars, moon shadows.
- [ ] G changes cloud cover: the sky greys, sun shadows fade at Cloudy and VeryCloudy.
- [ ] "Procedural sky" off returns the HDRI weather look, and on again restores the preset.
- [ ] Sun elevation slider below zero: the sun sets and the sky darkens smoothly.
- [ ] The sun reflected in the car's glass is the same small disc as in the sky.

## L3: shadows

- [ ] Raise the sun to the zenith: the near road does not go black.
- [ ] Drive at speed: shadow edges do not crawl.
- [ ] No visible line where one cascade hands over to the next ("Cascade blend band" at 0 shows the line).
- [ ] Golden hour: no stripe pattern (acne) on the road.
- [ ] Shadows are not pitch black under open sky (the floor is 0; the sky fills them).

## L4: materials

- [ ] Body paint sparkles in direct sun ("Paint flake density"); tyres, glass and cabin do not.
- [ ] "Multiscatter energy compensation" on and off: rough metal parts get slightly brighter, nothing blows out.
- [ ] Wet road at rain 1.0: the far road is not over-bright.
- [ ] Bright points (sun glints, lamps) do not flicker in the bloom.

## L5: emissive

- [ ] Evening and midnight: headlight, tail and brake lenses glow and bloom; brake lenses follow the pedal.
- [ ] If a lens does not glow, note its name (a lens classified as glass cannot).
- [ ] Street-lamp heads glow on the corridor and in the lab.

## L6 and L8: lamps, beams, retroreflection

- [ ] Evening and midnight on the corridor: poles on the median every 60 m, orange pools on both carriageways.
- [ ] "Spot lights uploaded" stays at or under 40 and lamps do not pop on and off while driving.
- [ ] Headlights light the road ahead with a cutoff above the beam ("Headlight low-beam cutoff" off shows the difference).
- [ ] Lane markings brighten in the headlights and fade to the sides ("Lane-marking retroreflection" at 0 removes it).
- [ ] Noon: lamps and car lights are off.

## L10: wet

- [ ] Rain 0 and 1.0 at noon, golden hour and midnight.
- [ ] "Rain wets the car's exterior" on: judge whether the paint looks right; the cabin stays dry.
- [ ] Known: windshield rain and wet-lamp halos are too bright at night. Note how bad.

## Lab (`SWISH_LAB=1 make debug`)

- [ ] Still starts, now at the Noon preset; Day / Dusk / Night pick Noon / Evening / Midnight.
- [ ] Lit tunnel and lamp stretch at Midnight: luminaires and lamp heads glow.
- [ ] Unlit tunnel still goes dark and the exposure adapts.

## Performance

- [ ] fps at noon and at midnight on the corridor against `make run` (sky pixels march the atmosphere; 40 spot lights).

Unit tests for later (no GPU needed):
- [ ] `WeatherController` light choice as a pure function: sun above the horizon, moon when the sun is down, none in twilight.
- [ ] Cloud cover to direct-sun factor: 1.0 at 0.3, near 0.05 at 1.0.
- [ ] `build_corridor_segments(al, true)`: lamp count per segment is length / 60 m, none in turnarounds, all marking draws have `retro == 1`.
- [ ] `CameraUniforms` spot selection: car lights first, then nearest world lamps, never more than `MAX_SPOT_LIGHTS`.
- [ ] `car_spot_lights` with `headConeRule`: 1,200 lm at 16 degrees is about 4,930 cd.
- [ ] A GLSL-versus-C++ check of `atmSky` at a few directions (needs a shader test harness).

## Car showcase (`SWISH_LAB=1 make debug`, reference `docs/car-showcase.md`)

- [ ] The app starts and `make run` looks the same as before.
- [ ] Studio: the room is dark apart from three panels; the car shows a large soft highlight on top and a strip along each side.
- [ ] Studio: step through the five finishes and six colours. Gloss is mirror-sharp, metallic sparkles, satin is soft, matte has no highlight shape. "As the asset" restores the original.
- [ ] Turntable on: the camera orbits smoothly; radius, height and speed respond; off returns the normal camera.
- [ ] Grounding shadow on and off: a soft dark patch under the car, no hard box edge, no patch away from the car.
- [ ] Material fixes on and off: tyres go matte black, glass and chrome sharpen, the cabin loses any metallic look. Note anything that gets worse.
- [ ] Parking garage: twelve tubes glow; long thin reflections slide over the paint; daylight fades in from the open end at noon.
- [ ] Gas canopy: it is night after the teleport; a bright white pool under the canopy; the panels bloom.
- [ ] Night street: warm windows and sodium lamps; press R and check the wet road and "Wet" on the car (drops stay fixed to the body as the camera moves).
- [ ] Car lights Off / Parking / Low beam / Brake each do what they say while parked; Auto follows the time of day.
- [ ] Bloom pyramid on and off at the canopy: on is a wider, softer halo with the same overall brightness. Resize the window with it on.
- [ ] Known: area light passes through walls and pillars. Note where it is obvious.
- [ ] fps in the garage and under the canopy against the open pavement.

Unit tests for later (no GPU needed):
- [ ] `CameraUniforms::set_rect_lights`: nearest-first choice, never more than `MAX_RECT_LIGHTS`.
- [ ] A C++ mirror of `rectSolidAngle` and `carBoxBlock`, checked against known cases (a 1 m square at 1 m; a ray through and beside the box).
- [ ] The bloom pyramid weights sum to 1 as a compile-time check.
- [ ] The car-lights mode switch as a pure function of mode and time of day.
