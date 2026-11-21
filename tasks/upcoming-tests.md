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

## LIE full route (A1, A2; 2026-11-20)

In the app (`make debug`):
- [ ] Drive east past exit 49 (17.75 mi): the road continues, no seam or kink at the old end.
- [ ] Reach the Riverhead end (30 mi): turn-around pad is there and the car stops at the wall.
- [ ] Lane count drops 4 to 3 near 24.7 mi and 3 to 2 near 29.75 mi with a clean taper.
- [ ] fps near the start against the August build.

Unit tests for later:
- [ ] Python test for the two-zone solve: west length stays 18 mi when the east target changes.
- [ ] Python test for the hole bridge: a synthetic 100 m gap is bridged, a 400 m gap is not.
- [ ] A baked-asset check: minimum radius and total length inside the stated bands.

## LIE elevation (A3; 2026-11-20)

Nothing to see in the app yet: the mesh is still flat until A5.

Unit tests for later:
- [ ] Python: `vertical_profile` on a 20 m step input ends under the grade and acceleration limits.
- [ ] Python: `superelevation` sign: a right-hand arc gives bank > 0, a left-hand arc < 0, a straight 0.
- [ ] Python: `load_elevation` interpolation at and between cache rows.
- [ ] C++: the baked debug asset's max grade and max vertical acceleration at 70 mph stay inside the config bands.

## LIE exit table (A4; 2026-11-20)

In the app (`make debug`, terminal output):
- [ ] The `[corridor]` line names the right district and next exit as you drive; it switches to westbound exits after a U-turn.
- [ ] Nassau starts just before exit 33 (8.41 mi). Check the real city line on a map.

Unit tests for later:
- [ ] Python: `extract_exits` on a tiny hand-made OSM file (one eastbound and one westbound junction, one link with a destination).
- [ ] C++: the baked debug asset has exits sorted by station and every district index in range.

## LIE hills and banking (A5; 2026-11-20)

In the app (`make debug`):
- [ ] Hills are visible from about 10 mi on (Nassau); the steepest stretches feel like a hill, not a ramp. If too much, lower `max_grade` or raise `smooth_m` and re-bake.
- [ ] Curves lean into the turn; the barrier, paint and grass stay attached to the road, no cracks at tile seams.
- [ ] Lamps stand on the road, not under or above it.
- [ ] Known: on a slope the car stays level, so wheels clip or float by up to 6 cm. Note where it is obvious.
- [ ] Rain at 1.0 on a hill: splashes and puddles at the wrong height?
- [ ] `make run` looks exactly as before.

## Physics substep (B1; 2026-11-20)

In the app (`make debug`, Physics panel):
- [ ] Substep 0 against 240 in a slalom: 240 should feel the same at any frame rate.

Unit tests for later:
- [ ] A `CarEntity`-level 30 fps against 144 fps run (needs a headless entity; today the entity needs GLFW and a model).

## 6-DOF car (B2-B5c; 2026-11-20)

In the app (`make debug`, Physics panel, tick "Physics model: rigid body"):
- [ ] Launch, shift, brake to a stop: no shaking at rest, no creep on the flat.
- [ ] Slalom at 60 mph, then lift off mid-corner: tail steps out, catchable on arrow keys.
- [ ] ABS off: fronts lock and the car ploughs straight. Traction control off: rear spins on launch.
- [ ] On a hill (10 mi on): the car pitches with the road, wheels on the surface.
- [ ] Touch the median barrier at speed: slides along and slows, no bounce, no tunnelling.
- [ ] Parked on a slope with the brake held: known slow creep; note if it is visible.
- [ ] Above 80 mph: stable, not floaty.

Unit tests for later:
- [ ] Axle loads at 150 mph with downforce on.
- [ ] `wall_contact`: speed into the wall removed, sliding speed cut by 0.4 times that.
- [ ] `surface_slope` sign on a synthetic climb and a banked curve.
- [ ] Engine braking decelerates in gear and is zero with throttle.

## Corridor ramps (C1; 2026-11-20)

In the app (`make debug`, header "Corridor"):
- [ ] At spawn, an exit ramp leaves the right shoulder and curves out to a two-lane service road with a double yellow.
- [ ] Drive along it (on the mainline): ramp and service road follow the hills, no gaps at the 1.2 km segment seams.
- [ ] `service_dy_m` to 5: ramp rises smoothly, service road floats 5 m up. Back to 0: flush, no grass through it.
- [ ] Move `ramp_ahead_m` to 10,000: one rebuild after release, the demo moves; the car stays where it was.
- [ ] Untick the demo: it disappears after the rebuild.
- [ ] Release (`make run`): no ramp.

Unit tests for later:
- [ ] Ramp on a curved synthetic alignment: band width stays square to the path.
- [ ] `build_corridor_segments` with a path crossing a segment edge emits into both segments.


## Corridor diamonds (C2b; 2026-11-21)

Note first: the stock debug and release builds draw everything past about 100 m as sky (`lighting.frag`
`depth < 0.0001` with reverse-Z and a 1 cm near plane). Distant bridges only show with that fixed.

In the app (`make debug`, header "Corridor"):
- [ ] Drive EB from the start: about every mile a crossroad bridge over a cut; the car drops into the dip and climbs out smoothly (no bump at the cut ends).
- [ ] At an exit: gore V with diagonal chevrons, ramp leaves the right shoulder, rises along the cut slope and merges into the service road's left lane before the crossroad.
- [ ] After the crossroad: entrance ramp leaves the service road's left side and merges onto the shoulder.
- [ ] Service road: curbs both sides, yellow left / white right edge lines, dashed lane lines, crosswalk + stop bar before the crossroad.
- [ ] WB side mirrors all of it (exit ramp east of the crossroad).
- [ ] No gaps at the 1.2 km segment seams on service roads or ramps; no z-fighting where a ramp overlays the shoulder or service road.
- [ ] Sliders: depth 0 flattens the cuts (bridges then sit at road level; expected); half-length 800 m; strip 5 and 20 m; untick diamonds.
- [ ] Release (`make run`): no diamonds, no dip.

Unit tests for later:
- [ ] Clearance under every crossroad deck >= 16 ft on the real asset (sample `surface_y` across the deck).
- [ ] Verge strips: no hole at a ramp start or end (point A..G continuity at the inserted stations).
- [ ] Neighbouring diamonds never overlap (ramp ranges disjoint) on the real asset.

## Drivable ramps and service roads (C3; 2026-11-21)

In the app (`make debug`, header "Corridor"; try the bicycle model and "rigid body" in the car panel):
- [ ] At an exit gore, steer right off the lane: the car crosses the shoulder onto the exit ramp with no wall. Past the nose it can no longer cut back across the gore.
- [ ] Down the exit ramp onto the service road: no height pop at the ramp start, the touch point or on the service road. On a banked curve a step of about 13 cm at the ramp start is expected (see tuning notes).
- [ ] Along the service road across the crossroad: no bump at the deck edges. The car cannot turn onto the crossroad (C3a).
- [ ] Entrance ramp back onto the mainline: merges with no wall and no jump.
- [ ] The grass strip between mainline and service road, and the median, still stop the car (push-out and scrape in rigid body).
- [ ] Drive to a service-road end that is not at a crossroad: the car stops there (nose over the grass). It does not jump sideways onto the mainline.
- [ ] Untick `Drive ramps + service roads`: the old clamp is back (the ramp is a wall again).
- [ ] The 1 Hz telemetry line (stdout) shows the piece: `main`, `ramp`, `service`, `cross`.
- [ ] WB side: the same, mirrored (exit ramp east of the crossroad).
- [ ] Release (`make run`): unchanged, mainline only.

Unit tests for later:
- [ ] Dead-end hold: split the `update()` clamp into a pure function and test hold against lateral clamp.
- [ ] Every diamond route on the real asset stays inside a window with seam steps under 15 cm (the sweep used during C3).

## Cross-street stubs, signals, HOV lane (C3a; 2026-11-21)

In the app (`make debug`, header "Corridor"):
- [ ] At a crossroad, look out along a stub from the service road: crest at the end, no visible end or square cut; sidewalks both sides; double yellow and lane lines stop at the crosswalk.
- [ ] Stop bars: inbound lanes outside the outer crosswalk, outbound lanes before the service road (both sides, mirrored on WB).
- [ ] Signal from the service-road approach: heads over each service-road lane face you, crossroad heads side-on; wire sags between the two poles; no head floating off the wire.
- [ ] Watch one cycle (60 s): service-road heads green, then amber 4 s, then red; crossroad heads the opposite half; never green both ways.
- [ ] `Signal cycle (s)` slider: 20 s and 180 s change the timing at once without a rebuild.
- [ ] `Cross-street stub (m)` 60 and 150: rebuild after 0.5 s; the crest moves with the end; apron grass has no hole or z-fight where it tucks under the verge.
- [ ] HOV from the cockpit between exits 32 and 64: double white buffer right of the median lane; broken in the access zone before each exit; diamonds in the lane every 1,000 ft; plain dashed lane line outside the range.
- [ ] Night (T key): lit lenses glow; check whether they need more than the ITE minimum luminance.
- [ ] Release (`make run`): no stubs, signals or HOV paint.

Unit tests for later:
- [ ] Stub end below the line of sight from a 1.1 m eye at the service road, for the slider range.
- [ ] Every HOV diamond lies inside lane 1 and outside all access zones' buffer gaps (geometry, not just stations).

## HUD strip and route minimap (C3b; 2026-11-21)

In the app (`make debug`):
- [ ] Drive mode and edit mode (backtick): the strip and minimap show in both; H hides and shows them; the `HUD strip + minimap` checkbox follows.
- [ ] Drive to 60 mph: the strip reads 60 when the panel Telemetry reads 96.6 km/h.
- [ ] Next exit counts down while you drive and jumps to the following exit after the gore. On WB (U-turn at an end pad) it shows WB exits.
- [ ] Road piece changes Mainline, Ramp, Service road, Crossroad as you take an exit ramp, matching the stdout `[corridor]` line.
- [ ] District changes Queens, Nassau, Suffolk at the lines.
- [ ] Minimap: the chevron points along the road in both views; `Minimap: local 2 km` follows the car; amber service roads sit on the correct side.
- [ ] Readability in rain, at night (T), and in a large or fullscreen window (text crisp, strip not wider than the window).
- [ ] Long exit names truncate with "..." and never overlap the distance.
- [ ] Release (`make run`): no HUD.

Unit tests for later:
- [ ] `App::sync_hud` exit pick and miles on the real asset (needs the App split from GLFW).


## Tunnel, plaza and Riverhead roundabout (C4; 2026-11-21)

In the app (`make debug`):
- [ ] Spawn is inside the tube: white tile with blue and gold stripes, luminaire rows lit, walls lit evenly both sides, darker than outside but readable (eye adapted). No sunlit patches on the walls.
- [ ] Known renderer bug: past about 100 m the tube shows sky colour (`depth < 0.0001` in lighting.frag/ssao.frag). C4 shots used a temporary `depth <= 0.0`; check the look with and without, owner call.
- [ ] Drive EB to the portal: the exit glare and the exposure easing back to daylight look natural; no light from the other tube; the walk curbs act as walls (both car models, `rigidBodyChassis` on and off).
- [ ] From the plaza looking west: headwall, openings, cover and slopes, with no gaps or floating geometry. Gantry over the widened plaza. U-turn opening: barrier ends line up with the paved gap; U-turn into the WB tube.
- [ ] WB into the tube: the threshold zone is brighter than the interior, and the tube ends in darkness at the west cap (the car stops 8 m short).
- [ ] Riverhead: the right lane tapers, splitter and crash cushion, the roundabout drives round counter-clockwise onto WB, the island and outer edge stop the car (no driving through), the far rim holds the car. Both car models.
- [ ] Blank boards read as signs (they render near black when backlit by the low sun, like the lamp poles: a lighting-phase item).
- [ ] `Tunnel lighting (lux)` slider changes the tube brightness live.
- [ ] Release (`make run`): unchanged (no tube, old end pads).

Unit tests for later:
- [ ] A driver-level test: drive the bicycle model round the ring with a scripted steer (needs `CorridorDriver::update` without Renderer).
- [ ] Mesh test: no tube draw pokes through the cover (top of tube boxes < cover height everywhere).

## Road surface (D1; 2026-11-21)

In the app (`make debug`):
- [ ] Drive the concrete stretch east of exit 64 (about 25 mi): transverse joints every 15 ft and longitudinal joints at the lane lines read as thin dark lines near the car and fade smoothly with distance; no shimmer or crawling while moving.
- [ ] Patched stretches (exits 22-29, 33-40, 57-64): patches, sealed seams, transverse and longitudinal tar snakes look plausible, not too many or too black; nothing jumps at the 300 m tile seams.
- [ ] Fresh stretches (29-33, 49-57): dark clean asphalt, crisp paint, no red tint. Worn (start-22, 40-49): faded broken paint, polished wheel paths.
- [ ] Raised markers sit in the dash gaps every 80 ft and glow in the headlights at night (T to Midnight, lamps off).
- [ ] Rumble strips beside both edge lines; no moire band at distance. In lane tapers they may sit off the line (known limit).
- [ ] Rain 1.0 on concrete and asphalt: wet darkening, puddles, ruts, ripples still work; rain 0 dry.
- [ ] Each `surface_*` slider changes the look live; `surface_wear` 0 gives clean paint and few patches.
- [ ] Release (`make run`): road unchanged.

Unit tests for later:
- [ ] Mesh test: on the full asset, mainline lane draws east of exit 64 bind `MAT_CONCRETE` and carry a non-zero `surface` tag; release (v1) draws carry none.

## Corridor edges (D2; 2026-11-21)

In the app (`make debug`):
- [ ] Median west of exit 64: NJ barrier reads as a safety shape (lip, 55° and 84° faces) in sun and shade; no seam or flicker where tiles join; end caps at the U-turn opening, the wide median and the splitter.
- [ ] Drive into the median at speed with both car models (`rigid_body_chassis` off and on): the body side stops at the barrier toe, not short of it and not through it. A steep angle lets the nose corner overlap (centre clamp, known limit).
- [ ] Lamp poles on the median stand on their widened blocks; none east of exit 64.
- [ ] East of exit 64: carriageways part smoothly over 500 m, grass median with cables and posts, yellow attenuators at both NJ ends; ramps, service roads and crossroads follow the moved carriageways; no gap or overlap at the ease.
- [ ] W-beam at every crossroad pier, both sides: rail height, posts, blockouts, turned-down ends touch the ground; drive along it: the clamp is the rail face.
- [ ] Nassau sound walls: brick scale plausible from the mainline and the service road, no moire, wall feet buried on the slope, ends clear of ramps.
- [ ] Ditch and rounded cut slope near the shoulder and under crossroads; `edge_ditch_m` 0 gives a straight slope.
- [ ] Sliders `edge_wide_median_m`, `edge_wall_h_m`, `edge_ditch_m` rebuild the corridor after 0.5 s.
- [ ] Debug mips: distant asphalt, grass and paint calmer than before; nothing turns blurry up close.
- [ ] Release (`make run`): median, edges and textures unchanged.

Unit tests for later:
- [ ] Mesh test: on the full asset, median draws west of exit 64 contain no vertex above 32 in + bank over the road; wide-median tiles have no NJ draw.
- [ ] Bounds sweep: for every station in each W-beam run, `drive_spans` hi equals the rail face minus clearance and inset.

## Guide signs (D3; 2026-11-21)

In the app (`make debug`):
- [ ] Eastbound and westbound: every diamond exit has a gore sign (EXIT n + arrow) in the gore, and one or two gantries before it; the exit number on the tab matches the gore sign and the HUD's ramp.
- [ ] Westbound faces read correctly from the driver's seat (only eastbound faces and westbound backs were seen).
- [ ] Text spelled right on a sample of panels (Patchogue, Searingtown, Ronkonkoma, Sagtikos Pkwy, Northern State Pkwy); route shields: Interstate, NY route, county route shapes look right against photos (NY and county shapes drawn from memory).
- [ ] Night (T key) with headlights: sign faces glow back toward the car (L8.2 retro lobe), backs do not.
- [ ] Gantry columns never stand in a lane, on a ramp or through a lamp pole; no gantry hidden behind a crossroad deck; truss reads as light galvanised steel in full sun (it renders near black against the low sun, as lamp poles do).
- [ ] Mile markers: every 0.2 mile, numbers rise eastbound, E / W letter matches the side; none in a gore or on a ramp.
- [ ] Exit 73 boards: END 495 1 MILE, EXIT 73 1/2 MILE, W2-6 diamond upright, Riverhead / Orient, westbound I-495 WEST New York; chevrons point right.
- [ ] Sliders `signs` and `sign_sheen` rebuild the corridor after 0.5 s.
- [ ] Release (`make run`): no signs, nothing else changed.

Unit tests for later:
- [ ] Mesh test: every gantry's sign bottom is at least 17 ft above the surface under the whole sign width.
- [ ] Python test: generator output for a hand-made exit table (shield parsing, label merge, abbreviations).

## Overpasses (D4; 2026-11-21)

In the app (`make debug`):
- [ ] Drive the whole route both ways: every overpass clears the car and the lanes; no deck, cap or pier pokes into a lane, a shoulder, a ramp or a service road; no bridge sits inside a diamond.
- [ ] Steel overpasses (Queens green, Nassau grey): girders, stiffeners, cross-frames, bearings and caps read from below in full sun and against the low sun; fence frame on the parapet.
- [ ] Concrete overpasses (Suffolk): I-beams, diaphragms, wall piers; spill slopes with concrete paving under the deck ends; approach fills meet the ground without a gap at the toe.
- [ ] Sagtikos stone arch: ashlar scale plausible, arches spring at clearance height, stone parapet with coping, wing walls hide the fill.
- [ ] LIRR (Oyster Bay Branch): through-girders, stiffeners, ballast, ties square to the rails, level embankment ending in a slope.
- [ ] LIE-over bridges (15): mainline drives exactly as before (no bump, no height change); parapets on the deck edges; from the shoulder the trench, U-walls and under-road look right; service-road decks over the trench where one runs; no verge hole or grass wedge gap at the walls.
- [ ] Crossroad decks (30) in their style, not black, from the mainline and when crossing on the service road.
- [ ] Nassau sound walls end cleanly about 40 m before each overpass fill and resume after it (wall ends at a bridge were not viewed).
- [ ] Debug shadows (light camera on the sun side): bridges and gantries cast shadows onto the road, the car has a contact shadow; raise the sun (time of day) and the near road stays lit.
- [ ] `ground_bounce` 0 vs 0.15: undersides go from black to readable; nothing glows.
- [ ] Sliders `bridges`, `bridge_clear_ft` rebuild after 0.5 s; `bridge_clear_ft` 20 lifts every overpass and lengthens the fills.
- [ ] Release (`make run`): no bridges, crossroads look as before, shadows as before.

Unit tests for later:
- [ ] Mesh test: every overpass soffit vertex is at least `bridge_clear_ft` above the mainline surface under it.
- [ ] Python test: `crossings()` on a hand-made extract (cut detection, skew sign, sidewalk absorption, twin merge).

## District kits, lamps, skyline (D5; 2026-11-21)
- [ ] Drive the whole route EB and WB at speed: kit boundaries (exits 15, 19, 22, 24, 30, 32, 34, 40, 48, 51, 65, 71) change trees / buildings without popping mid-view; no tree or building on a ramp, service road, crossroad stub, apron or overpass approach.
- [ ] Lamps: Queens median twins + outer singles; Nassau shoebox heads west of exit 40, drop-lens cobras east; west Suffolk cobras; none from exit 65 east. No pole in a gore, on a ramp, through a gantry column, mile marker or gore sign.
- [ ] Night (T to midnight): lit stretches end at exit 64; the nearest-lights cap still lights the road ahead in Queens (median + outer) and Nassau (both shoulders); lens glare under a twin pole acceptable.
- [ ] Skyline: from the plaza and WB from exits 15-22 Midtown sits over the portal at a steady size and bearing while driving (no swimming, no popping at 16 km), never in front of nearer geometry, fades with haze; try dawn / noon / evening.
- [ ] Trees at speed: alpha-test shimmer on far crowns, cards edge-on, shading against the low sun and at noon; trees cast no shadow (known).
- [ ] Far ground: edge from a high free-fly camera (`dress_far_m` 0 / 250 / 500); no z-fight with crossroad aprons, stubs, bridge fills; LIE-over trenches still open; curves near the portal and at 10 km / 21 km without fold-over.
- [ ] Sliders `dressing`, `dress_density` (0, 1, 2), `dress_far_m` rebuild after 0.5 s; `skyline` toggles live.
- [ ] Frame time with dressing on vs off in Queens LIC and the pine barrens (vertex count, shadow pass).
- [ ] Release (`make run`): median lamps absent as before, no trees, buildings, far ground or skyline.

Unit tests for later:
- [ ] Mesh test: no dressing vertex inside $E + 12$ m or inside a path's paved width + pad.
- [ ] Kit table test: exits strictly rising, species weights sum > 0 where trees > 0.

## Landmarks (D6; 2026-11-21)
- [ ] Drive EB and WB past every landmark at speed: Unisphere / Pavilion visible from about 450 m before exit 22 (out of the dip), steady, no popping; the vent tower over the QMT portal from the WB approach; Calvary headstones from exit 17 to 18 (shimmer at speed, z-fight with the far ground).
- [ ] Unisphere and Pavilion at noon, dusk and night (no lighting on them at night: known); the far-ground edge under them from a high camera.
- [ ] Boards: county line both ways, HOV 1/2 MILE, Great Neck / Manhasset, 62N attractions legible at 100 m and 50 m in the cockpit, at night under headlights (retro), backs plain aluminium; none on a ramp, deck or cut slope; no lamp pole through a board.
- [ ] Exit 31 stone-faced deck and exit 22 / 44 concrete decks up close (texture scale, pier cladding).
- [ ] Crossroads 17, 39, 44: ramps, service-road joins, signals, gantry text match the exit; the HUD's next exit agrees with the gantry ahead.
- [ ] LIC viaduct (LIE-over bridges near exits 15-16, newly freed from crossroad 16): trench walls, under-roads, no holes in the verge.
- [ ] Melville street-tree row: no tree on a crossroad apron or stub, gaps look natural.
- [ ] Sliders `landmarks` and `landmark_graves` (0, 1, 2) rebuild after 0.5 s; `landmarks` off restores the D5 look (except the diamond priority, which always applies).
- [ ] Frame time in Calvary with headstones on vs off.
- [ ] Release (`make run`): unchanged (no landmarks, boards, headstones).
