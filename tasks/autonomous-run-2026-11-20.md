# Autonomous run log — 2026-11-20 night

> Written by AI (Claude). Owner is away and asleep. Owner's instruction: "use every recommended task you
> prefer and document and note it… make sure when building it does follow photorealism, and attention to
> detail even though its a compact version of the long island expressway."

Rules kept from [`handoff-lie-full-route.md`](handoff-lie-full-route.md): one code agent at a time, debug-first
(release byte-identical), no commit/push, same branch, sliders + tuning notes + changelog + architecture note per item.
Downloads allowed by the owner: CC0/CC-BY textures (credits next to asset) and the Overpass service-road fetch.

## Decisions taken without the owner (pick = my recommendation)

| Item | Decision | Why |
|------|----------|-----|
| C2 | (see todo.md C2 grilled notes for owner-made picks) | |
| C2 | Split into C2a (fetch + bake service-road extents) and C2b (runtime geometry) | One code agent per item; asset format change isolated and testable first. |
| C2 | Service roads one-way with the mainline on their side: South SR (NY 906A) eastbound, North SR (NY 906B) westbound | Research: OSM 508/512 ways `oneway=yes`; Wikipedia + eastcoastroads. |
| C2 | Overpass select by `unsigned_ref~"NY 906[AB]"` (512 ways, Nassau line to ~exit 65); Queens (Horace Harding) and east of 65 filled by the exit-number rule | Research: only reliable key; name filter failed over full bbox. |
| C2 | Lanes per extent = OSM `lanes` median per stretch, default 2; speed not used | OSM: 2 (201 ways), 3 (184). |
| C2 | Grade: mainline dips under each crossroad (runtime cut, smoothstep, tunable depth ~6.5 m, ramp length ~350 m); service roads, ramps' far end and crossroad stay at original ground height | Typical Nassau/Suffolk: crossroad bridges a depressed LIE (UNSURE, research found no source). Keeps asset format; ramps climb to the service road as on the real road. |
| C2 | Exit ramp joins service road from its left, before the crossroad; entrance ramp leaves from its left after it | Research (eastcoastroads photo log). |
| C2 | One crossroad per exit number (EB/WB records paired by ref digits); no U-turn lanes | Research: none found for LIE. |
| Renderer | Fix sky-depth test in debug only, not release | Release byte-identical rule; owner decides promotion. |
| C2 | Photoreal detail in C2b: curbs, MUTCD markings (4 in lines, 10 ft dash / 30 ft gap), stop bars at crossroads, grass strip between mainline and SR; lights/stubs stay C3a, barriers/fence D2 | Owner asked for photoreal detail; scope split keeps items owned. |
| C3 | One ground plane under the car centre for both car models (no per-wheel piece query) | Seams coplanar within 13 cm worst case; `RigidChassis` takes one `GroundPlane`; per-wheel needs a corridor projection per wheel per substep. |
| C3 | Highest piece wins where pieces overlap (gore, ramp landing) | Overlaps are coplanar to a few WU; picking the higher never sinks a wheel into the drawn surface. |
| C3 | Dead end (side road ends under the car) holds the car at last frame's spot; lateral snap > 3 m while on a side road = dead end | Stops the clamp from throwing the car sideways across the grass onto the mainline; mainline never dead-ends laterally, so release is unchanged. |
| C3 | Crossroad drivable only straight along the service road | Task: no turning onto the crossroad (C3a stubs). |
| C3 | Ramp start 13 cm step on 5-6 % banks left as is | Comes from the C2b ramp mesh being level at the bank's high edge; physics matches the drawn surface. Fix = bank the ramp until the nose (mesh + physics), proposed follow-up. |
| C3a | Span wire on steel strain poles, black 3-section heads, no backplates | Commons photos of LIE service-road intersections (Lake Success 2024, Roslyn Heights 2021); brief offered mast arm/backplate but research decides. |
| C3a | One global signal phase (group 0 service road, group 1 crossroad), 60 s cycle, 4 s amber, 1 s all red | Lenses batch into 6 draws per crossroad; per-intersection offsets would multiply draws. |
| C3a | Lens luminance = ITE VTCSH minimum intensity / 12 in lens area; no new emissive material id needed (draw emissive exists) | Sourced; reads dim at dusk, flagged for the lighting phase. |
| C3a | Stub end hidden by a 1.8 m crest that falls 2.5 m below ground; grass apron +-50 m | Owner suggested "behind a small rise"; no shader fade needed. |
| C3a | Stubs not drivable | Turning off the 1D corridor frame is its own item; brief said props. |
| C3a | HOV: buffer-separated left lane, 2 ft buffer of two 8 in lines; solid, broken (10/30 ft) in a 1,000 ft access zone ending 1,000 ft before each exit's diverge | MUTCD 3D.02 drawing A/C; no zone data, fixed pattern as the brief allowed. |
| C3a | Diamonds MUTCD minimum 2.5 x 12 ft, 6 in lines, every 1,000 ft from the range start in travel direction | MUTCD 3D.01; no extra diamonds at zones (keeps spacing regular). |
| C3a | HOV signs deferred to D3 | Text signs belong to the sign generator. |
| C3a | Centre and lane lines on the crossroad now stop at crosswalks | Was paint over the bars (z-fight, unreal). |
| C3b | HUD on the game window through a second ImGui context + debug pass after Composite (LOAD pass on Composite's framebuffer) | Panel context belongs to the detached controls window; reusing the compatible framebuffer needs no resize handling. |
| C3b | H key toggles; tunables `hudShow` (on) and `hudMapLocal` (off) in the `corridor` group | H was free (T time of day, backtick edit/drive); no new panel header or test whitelist. |
| C3b | Exit distance in real miles (real/baked ratio); local map window in baked metres; map world-frame -z up | Brief asked for real miles; rotating to geographic north needs the frame pin sign, unverified. |
| C3b | Layout authored at 1080 px, scale floor 0.8; exit ticks fainter in whole-route view | 800x600 default window was too small at pure height scale; ~100 ticks crowd 220 px. |
| C4 | Portal = 80 m before the EB exit 13 diverge (s about 1,422 m); tube over [0, portal); spawn stays at the baked start, 1.08 km inside the tube | OSM tunnel way 658498546 ends there; station 0 is the Manhattan approach, so the old start was already under the river. Owner decision: drive the tube, emerge at the plaza. |
| C4 | Walls white glazed tile with blue and gold stripes (not cream with a dark band) | Wikipedia: 2017-18 re-tiling in white with gold and blue stripes. Stripe heights unverified. |
| C4 | Cashless gantry kept at the Queens plaza as the brief asked | Real gantries are at the Manhattan end (Wikipedia; OSM `toll_gantry` nodes). Remove the gantry block in `CorridorEndsMesh.cpp` to match reality. |
| C4 | Tube lit by its own spots (target road illuminance slider, CIE 88 threshold x10 / exit x5 stand-ins); only the car's tube inside | Spots are unshadowed and would leak through the dividing wall. |
| C4 | Debug lighting.frag: roof volumes also remove the shadow floor, haze and rain fog; closed / one-ended volumes hide the sun; tube EV100 floor 4.5 | CSM left the inner wall sunlit and the floor gave 25 % sun; the daylight EV clamp made the tube black. Release shader untouched (hashes pass). Also affects the lighting lab's closed / garage volumes. |
| C4 | New `MAT_TILE` slot + procedural tile textures (tools/gen_tunnel_tile.py) | No tile texture existed; procedural avoids licensing. Release loads the texture but draws nothing with it. |
| C4 | Per-lane asphalt bands on the graded (v2) asset, folded back into one draw | Lane-wide quads fold up to 13 WU on fast bank changes, hiding the 5 WU paint (the tube's solid lane line rendered dashed). Release (flat v1) path unchanged. |
| C4 | East end: lane drop (MUTCD L = WS), splitter, single-lane roundabout R 24 / 17 m (fictional); the mainline is the final exit; no separate exit ramp | The real LIE becomes CR 58 at a signal past exit 73, and no road exists past the last station. |
| C4 | U-turn opening in the plaza (fictional) replaces the west end pad; diamonds and service roads start 250 m past the portal (the first diamond near exits 13/14 is dropped) | The tube has no median to cross; ramps would pierce the tube walls. |
| C4 | Luminaire lens 1,500 cd/m² (physical about 9,000) | The bloom is wide; 4,000 to 15,000 blew a large halo. |
| D1 | Stretches baked into an optional `SURF` asset block from `config/lie_corridor.toml [surface]`, not a runtime table | One source of truth that is already the bake config; runtime TOML would need toml++ in release code plus an exit lookup. Old assets load as `Legacy`. |
| D1 | Concrete from exit 64 to the end; asphalt west of it as worn (start), patched (22), fresh (29), patched (33), worn (40), fresh (49), patched (57) | Exit 64 switch is sourced (Wikipedia I-495 route description); the asphalt grades are guesses for variety. |
| D1 | Kind snaps to 300 m tiles (taken at the tile middle) | Draws are per tile; km-long stretches do not need an exact seam. |
| D1 | Concrete lanes reuse `MAT_CONCRETE` (Concrete012 = existing `textures/concrete*`) with a grey tint; fresh asphalt gets a new `MAT_ASPHALT_FRESH` slot from Asphalt012; shoulders stay asphalt | Concrete012 was already wired (barriers); Asphalt012 was the unused set. Release loads the new texture but never draws it (MAT_TILE precedent). |
| D1 | Asphalt012 red crack stains filled from shifted clean copies (`tools/gen_asphalt_fresh.py`), all three maps | The stains read as rust-red crack networks; fresh LIE asphalt has none. |
| D1 | Joint spacing 15 ft; reflective cracks on asphalt at 4 x 15 ft; RPMs 80 ft mid-gap, lens facing traffic; rumble 7 x 16 in at 12 in, 4 in off the edge line | NYSDOT JPCP 15 ft (from memory, unverified); MUTCD 3B.11; FHWA T 5040.39; offsets and RPM size are guesses. |
| D1 | Detail in a debug-only push extension (`pave`, `pave2`, 160 B push in debug), not the set-4 UBO | Values are per draw (kind, tile station); the gbuffer pipeline has no set 4. Release struct and layout unchanged. |
| D1 | Sliders in the existing `corridor` group | No new panel header or test whitelist. |
| D1 | Ramps, service roads, crossroads and HOV buffer lines keep the legacy look | Scope: mainline only; each needs its own tag pass. |
| D2 | NJ shape 32 in (3 in reveal, 55° to 13 in, 84° to the top, 24 in base) everywhere west of the wide median, not 42 in "tall" | Brief's profile; reference says "tall" without a height. Height is one constant (`kNjHeightIn`). |
| D2 | Wide median = carriageways part (per-station `Sample::med`, shifted carriageway frames), 20 m slider default, from 1 km past the last exit 64 record to 600 m before the east lane drop | Only way to get a wide grass median; starting after the last exit 64 record keeps the HOV paint (unshifted) out of it. Width to check against aerials. |
| D2 | Three-cable barrier down the median swale (not W-beam) | Brief allowed either; reference "to check". Cheap, reads right at speed. |
| D2 | W-beam only at crossroad piers (45 m up / 15 m down); none at gores or "fill" | Gores are D3 sign territory; the bake has no fill sections (ground = profile). Continuous W-beam would cost ~380k quads. |
| D2 | Crossroad piers moved 1.5 to 2.0 m past the shoulder; median pier narrowed to the NJ toe + 1 cm | Rail and posts need room in front of the pier; the old 1 m median pier would have poked into the car at the new clamp. |
| D2 | Sound walls: every 400 m gap between diamonds in Nassau (both sides) + Queens WB east of exit 31; Suffolk tan 62-63 coded but no gap fits | "Houses back onto the expressway" between interchanges; photos exit-33, exit-31. Walls stand at the top of the cut, 1.2 m inside the service road curb. |
| D2 | Clamp = visible face minus `kCurbClear` (0.65 m, tunnel rule) for NJ toe and W-beam | One rule already in use; puts the 911's side at the face. Centre clamp, so an angled car's corner can overlap (C3 design). |
| D2 | Ditch 1V:6H, 0.45 m, 0.6 m flat, rounded backslope; verge one draw per tile side | AASHTO recoverable foreslope; also fewer draws. |
| D2 | Debug-only CPU mip chain for all material textures (sampler maxLod unclamped in debug) | Brick (1 m tile) was solid moire without mips; renderer had none. Changes every debug texture's far look (better). **Owner call:** promote to release (changes release images). |
| D2 | Bricks054 (ambientCG, CC0) as the precast form-liner texture, tinted slightly | Closest red-brown with variation of the three sets tried (101, 059, 054). |
| D3 | Font: Overpass Bold (SIL OFL 1.1, Red Hat), not the repo's FHWASeriesEF / HighwayGothicEMod | Brief asked for an openly licensed face; the two repo fonts have no licence or credit on file. Overpass is Highway Gothic-derived. |
| D3 | Two atlases (4096² panels + strips, 2048² gores / markers / boards), loaded in the debug build only from `assets/signs/` | ~116 panels need ~16 Mpx at 1.19 cm/px; release never draws them, so it never loads them (MAT_TILE loads in release; this avoids 85 MB there). |
| D3 | One sign per (exit number, direction), grouped by `atoi(ref)` like the crossroads; a missing direction borrows the other's text; merged suffixes as "49 N-S" | Diamonds are built per exit number with one ramp each way, so the sign must name what the ramp serves. |
| D3 | Faces show REAL miles at the baked equivalent (685.5 m per mile); collapse: 1 + 1/2 mile, else 1/2, else 1/4, else none, room from previous gore + 100 m | The HUD shows real miles; a face never lies about its distance. |
| D3 | Gantry steps upstream off a crossroad deck and out of a 150 m sight shadow past it | First shots: a gantry 50 m past a deck was hidden by the deck from 200 m (the mainline dips under each deck). |
| D3 | Advance guide only (no pull-through "495 EAST" panel on the left lanes, no exit-direction sign at the gore; the gore gets the E5-1a gore sign) | Scope and draws; brief listed the gore EXIT sign. Pull-through panels are a follow-up. |
| D3 | Destinations: two lines max, MUTCD abbreviations, OSM spelling fixes (Patchoque, Searington, Cuny); parkway refs (N East, SA) as text lines | MUTCD 2E.10; OSM wording unchecked against photos (reference sheet). |
| D3 | NY route shield: white with black border and an arched top; county route: blue point-down pentagon with yellow | Drawn from memory, unverified against photos. |
| D3 | Faces `retro = 1` (L8.2 lobe), metalness 0, roughness x `sign_sheen` 0.45; truss metalness 0.3 (zinc patina) | L8.3 does not exist yet; this is the hook it can refine. |
| D3 | Exit 73 boards: END 495 and EXIT 73 moved to 1 and 1/2 real mile (685 / 343 m) before the ring; W2-6 250 m, Riverhead / Orient 150 m | C4 placed them at 1,600 / 800 m (baked miles) but D3 faces use real miles. |
| D3 | Mile markers: D10 enhanced style (shield + direction, mile, tenth) every 0.2 real mile from the Manhattan end; skipped only where a ramp overlaps the spot or near a gore sign | Brief: every 0.2 mi; skipping whole ramp lengths left 1 km gaps. |
| D4 | Crossings = OSM `bridge` ways cutting the mainline chain (road/rail over) and roads/tracks cutting an LIE bridge way (LIE over); new `fetch_xing` file `data/osm/lie_crossings.json`, search radius 30 m | Brief; the cut test drops bridges near the LIE that do not cross it. |
| D4 | Same structure as a crossroad = footprint overlaps the diamond (ramps + join), not a fixed distance; nearest road-over donates its style, others dropped | A bridge inside a diamond would collide with ramps and the dip; compaction moves stations, a footprint test is robust. |
| D4 | Overlapping crossings: wider kept; sidewalk footway bridges absorbed into the street bridge; same-kind within 25 m merged in the bake | Compaction squeezes real neighbours; OSM maps sidewalks as separate bridges. |
| D4 | Overpasses are elevated over an at-grade mainline (no new dip); approach fills 120 m to grade then a C3a crest end; service roads pass under at grade | A new dip would change the drive profile; brief: drivability unchanged. Real cross streets meet the SR at grade (fiction noted). |
| D4 | Style: rail = through-girder; name regex Cross Island / Sagtikos / Northern State / Grand Central = stone, Van Wyck = concrete; else Queens/Nassau steel, Suffolk concrete; Queens green, Nassau grey paint | Reference sheet; OSM has no `bridge:structure` / material tags on these ways (all 407 empty). "Parkway" alone matched local streets (Utopia, Douglaston). |
| D4 | Skew clamped to ±40°, real angle (not compacted) | Sharper skews push abutments far along the mainline; the structure is drawn at real scale. |
| D4 | Steel = multi-column bents, concrete = wall piers, median pier always the NJ-toe wall (D2); spill-through abutments with concrete slope paving; F-shape parapet; chain-link as a frame only | Common NYSDOT types; no alpha-tested material for chain-link mesh. |
| D4 | Stone texture: ambientCG Bricks066 (CC0, coursed ashlar), debug-only load from `assets/structures/` | Reference says "ashlar pattern"; release never draws stone (white fallback). |
| D4 | LIE-over: deck in its own frame under the existing mainline surface, U-walls, under-road at 14 ft 6 in, verge cut on a straight station range with skew wedges filled; dropped in a dip or the wide median | Keeps the mainline mesh and drive untouched. |
| D4 | Debug CSM: light camera moved to the sun side (`center + sunDir * R`) | Found by looking: buried decks shadowed the road above them; lowering the caster moved the shadow toward the camera. Release keeps the old sign (byte-identical rule). |
| D4 | Debug ground bounce $E_b = \rho E_g (1 - N_y)/2$, $\rho$ = 0.15 slider, in a spare UBO lane | "Dark deck": every face turned from the sun read ~1 % of the road (IBL lower half is a dimmed horizon); material and normals were checked and correct. |
| D4 | Mile markers now skip bridge footprints (425 markers, was 493) | Same off-deck rule as crossroads. |
| D4 | Sound walls: the 400 m gap rule still counts only ramps and crossroads; bridges then cut the walls (40 m margin, pieces under 40 m dropped) | Counting bridges in the gap rule removed most Nassau walls (13.2 to 3.6 km); real walls run between overpasses. |
| D5 | Kit table by exit number in `road::dressing::kKits` (13 stretches), not TOML | Same precedent as D2 `kWideFromExit` / C3a HOV exits; runtime TOML needs toml++ in release code, a bake block needs an asset format change for 13 rows. |
| D5 | Lamps: Queens median twin arms on the existing 60 m grid + outer singles every 120 m; Nassau / west Suffolk right-shoulder singles every 65 m staggered between sides; shoebox west of exit 40, drop-lens cobra east; none from exit 65's first record | Reference sheet (photos `exit-22/`, `exit-33/`, `exit-44/`, `exit-62/`); 60 m grid kept so D3 gantry posts still dodge it; the exit 40 break is a pick (the sheet has one photo of each). |
| D5 | One spot per pole (twin = 32,000 lm at the pole, single = 16,000 lm at the head); lens 20,000 cd/m² kept | Light budget is the renderer's nearest-N; per-head spots double the list in Queens. 838 spots on the route. |
| D5 | Lamp steel, heads, lenses, pedestals batched per segment | Was 24 draws per pole (~15,000 draws); gives room for dressing. |
| D5 | Far ground: one strip in the verge draw out to `dress_far_m` 250 m, 0.2 m under level ground, cut at curve insides (0.8 R) and LIE-over trenches | Buildings and woods need ground; same draw; sinking it lets C3a aprons / stubs and D4 fills win the depth test without holes. |
| D5 | Trees as crossed double-sided alpha-tested cards, road-facing normals, no sun shadow (`DrawCall::noShadow`) | Existing MAT_TREE path; the depth-only shadow pipeline has no alpha test (cards would cast rectangles). Alpha-tested shadows = renderer item. |
| D5 | Procedural textures (tools/gen_dressing.py) for trees, facades and the skyline; no download | Licence-free, seeded, tuned to the photos; no CC0 tree / facade atlas or skyline found that fits one alpha atlas. |
| D5 | One `MAT_DRESSING` atlas (trees + facades + roofs), separate `MAT_SKYLINE` | At most two draws per tile side; skyline needs its own resolution. |
| D5 | Buildings: LIC glass towers 30-60 floors, Maspeth warehouses, LeFrak City brick slabs 14-18 floors, houses (hip roof, houses back onto the road), Lake Success / Melville offices, Riverhead barns; boxes with stacked facade bands | Reference "beyond the edge" row; boxes keep the vertex count low. |
| D5 | Skyline: one camera-facing card, Midtown 1.3 km past the tube's Manhattan end on the portal line, drawn at 2 km and scaled about the camera by 2 km / R each frame, hidden past 16 km | Far plane is 2.4 km; scaling about the camera keeps bearing and angular size exact (no parallax error); scene haze already near max at 2 km. Midtown placement from memory. |
| D5 | Pine barrens: trees 0.85, 35 % scrub oak 3-5.5 m understorey | First look showed bare trunks; photo `exit-68/` is a green wall. |
| D6 | Landmark exits (17 22 31 33 39 44 49 62 68) take the diamond slot from a closer plain neighbour; 53 excluded | Exits 17, 39, 44 had no diamond or signs (Glen Cove Road's gantry read EXIT 38); 53's diamond would swallow the D4 Sagtikos arch, which is the more recognisable feature. |
| D6 | Queens cashless gantry deleted; plaza paving and U-turn kept | Wikipedia (read 2026-11-21): plaza demolished with open-road tolling 2017-01-10, readers at the Manhattan portals. |
| D6 | Unisphere, NYS Pavilion and the Queens vent building as procedural geometry at real lateral distance (OSM), baked along offset | No model assets needed; side and distance are what a driver reads; along is compacted with the road. |
| D6 | Tree-free wedge (40 m at the globe, +350 m at the road) toward the Unisphere | The C2 dip hides the far view near the crossroad, so the sightline has to work from 400 m out; the park's central lawns justify it. 150 m hid it everywhere. |
| D6 | Calvary as a kit column (headstones) from exit 17 to 18; Maspeth kit moved from 15 to 18 | Data-driven like D5; the EB 17 record bakes before WB 15, so a 15 kit never showed anyway. |
| D6 | Boards: NASSAU / QUEENS COUNTY LINE just west of exit 33, HOV LANE BEGINS 1/2 MILE (ground-mounted, not on a gantry), Great Neck / Manhasset EXIT 33, ATTRACTIONS AT EXIT 62N with plain names, no logos | Photos exit-33 and exit-62 (2000); county-line wording NYSDOT style from memory; logos are trademarks. Overhead HOV panel would need gantry code (scope). |
| D6 | Boards placed at the nearest spot off decks and inside ramp inner edges (25 m steps, upstream first) | First rule parked two boards on crossroad 32's deck; past a ramp they stand on the cut slope out of sight. |
| D6 | Deck styles by exit: 22 and 44 concrete, 31 stone | Reference sheet (Van Wyck concrete piers, CIP stone-faced bridge); NY-135 material unverified. |
| D6 | No brown parkway signs at exit 53 | Interstate guide signs are green (MUTCD 2E); the brown-sign claim is unverified. |
| D6 | Not modelled: BQE two-level viaduct, exit 22 stacked ramps, exit 44 full freeway interchange and VMS gantry, QMT flood doors and portal facing | Each changes the drive profile or needs a ramp generator beyond the diamond template, or the appearance is unverified. |

## Open findings for the owner

- Final review (orchestrator): `d6-12-exit73-end.png` (exit 71) shows a narrow concrete median barrier, while exit 69 (`d5-pine-barrens-no-lamps.png`) has the wide grass median the reference sheet gives for east of exit 64. Check where the median narrows near Riverhead against Street View.
- Final review: `tests/golden/release_spv.sha256` differs from HEAD only in `ibl_irradiance` / `ibl_prefilter` (file dated 2026-11-20 20:51, before this run; the E1 prefilter work). No golden was touched by C1-D6.

- D6: the diamond priority always applies in debug (not behind the `landmarks` toggle): crossroads 30 to 29, and the LIC viaduct crossings near exits 15-16 became LIE-over bridges (38 to 44 bridges). Not viewed up close.
- D6: the Unisphere / Pavilion stand past the 250 m far ground (over haze, no ground under them); fine from the driver's eye, visible from a high camera. Unlit at night.
- D6: Calvary headstones read dark against the low sun (same backlit-dielectric finding as D3/D5); the wedge leaves a bare lawn north of exit 22.
- D6: unverified wording or shape: county-line boards, 62N panel names (2000 photo, logos replaced), NYS Pavilion paint, vent building footprint, NY-135 deck material, end-of-expressway wording at exit 73.
- D6: brief items not modelled: BQE two-level viaduct, stacked ramps at exit 22, full NY-135 interchange and VMS gantry, QMT flood doors / portal facing, overhead HOV panel, brown Sagtikos signs.
- D6: exit 53 still has no own diamond (gantries read EXIT 52 / 55) so the arch survives; owner call: arch vs diamond.

- D5: trees cast no sun shadow (alpha-tested cards, depth-only shadow pass has no alpha test). Renderer call: an alpha-tested shadow pipeline for `MAT_DRESSING`.
- D5: lamp poles and backlit trees read very dark against the shipped low sun (same as the D3 truss finding); twin-pole lenses (20,000 cd/m², pre-existing) bloom into large discs from the chase cam under a pole at night.
- D5: kit boundaries and building choices (exit 40 shoebox / cobra break, LIC towers both sides, Melville 4-8 floors) are picks from the reference sheet, not checked against Street View; skyline building positions are from memory.
- D5: houses and offices are only visible where there is no sound wall and no dip (Nassau mostly hides them, as on the real road); no lit windows at night.
- D5: the far ground ends 250 m out (0.34 deg below the horizon from the driver's eye); a high camera will see its edge.
- D5: the D0 "lamps east of exit 64" finding is fixed (none from exit 65's first record).

- D4: the debug sun shadow map was rendered from under the ground (`GeometryProducer::compute_cascades`, `eye = center - sunDir * R` with `sunDir` pointing at the sun). Fixed in debug only. **Owner call:** promote to release (changes release images; likely the root of the "near-cascade blackout" and "dark crossroad deck" findings).
- D4: ground bounce (debug, `ground_bounce` 0.15) lifts every face turned from the sun, not only bridges. **Owner call:** keep, retune, or promote.
- D4: a NaN normal exists on some car pixels (car glass?); the bounce term spread it into a black block until clamped. Source not traced.
- D4: overpasses cross the service roads in the air; real LIE cross streets meet the service road at grade with a signal. Only crossroads have intersections.
- D4: LIRR Main Line over the LIE (Queens) and three stone LIE-over parkways merged into diamonds; the only stone bridge left is Sagtikos, the only LIRR one the Oyster Bay Branch.

- D3: the HUD's "next exit" lists every OSM exit, but only 30 exit numbers have a diamond, so the HUD can name exit 23 while the gantry ahead says EXIT 24. Owner call: HUD shows only exits with ramps, or keep it.
- D3: steel (truss, lamp poles, W-beam posts) renders near black when backlit by the low sun, even as a dielectric test; renderer/lighting call.

- D2: no material texture had mips; debug now builds them (big far-field aliasing win). **Owner call:** promote to release.
- D2: the car clamp is on its centre point; an angled car's nose overlaps the NJ barrier while scraping (seen in `d2-rigid-scrape-median.png`).

- Sky test `depth < 1e-4` shades all geometry past 100 m as sky (near 10 WU, reverse-Z). Fixed in the debug build only (`depth <= 0.0`, 2026-11-21). **Owner call:** promote to release (changes release `.spv` golden, intended).

- C3a: crossroad decks render near black at the shipped low sun (42 in parapet shadow covers the deck, ambient in shadow is very low; same in C2b shots). Renderer/CSM call, not geometry: lowering the deck slab changed nothing.
- C3a: signal lenses read dim (ITE minimum luminance, no bloom on them); lighting-phase call.
- C4: inside the tube the depth < 1e-4 sky bug shows sky colour past about 100 m (C4 shots used a temporary `depth <= 0.0`, reverted). CSM does not shadow the tube interior (worked around with roof volumes). Backlit vertical faces (sign boards, lamp poles) render near black at the low sun.

- D0 research: Queens toll plaza is gone (cashless 2017, gantries at the Manhattan portals). C4 built a Queens-side gantry/plaza per brief; owner to choose period look vs today.
- D0: no lamps east of exit 64 (lamps still drawn there; D5 lamps-per-district will fix).

## Phase log

| Phase | State | Tests | Notes |
|-------|-------|-------|-------|
| C1 | Done | 485 / 460 | Owner-grilled. Slider rebuild not checked in app. |
| C2a | Done | 486 / 461 | 512 OSM ways; EB 6 osm + 2 rule ranges, WB 5 osm + 2 rule; SRVR block; nothing drawn. |
| C2b | Done | 489 / 464 | 30 crossroads, dip, SRs, ramps, gore, deck. SR left edge yellow (MUTCD 3A.05). Stubs where no OSM SR. Found far-sky bug in lighting.frag (not fixed). |
| C3 | Done | 492 / 467 | Drivable ramps, SRs, crossroad run; both car models driven by a removed script; dead-end hold seen; 13 cm seam step at banked ramp starts (C2b mesh). |
| C3a | Done | 495 / 470 | Stubs + crest, span-wire signals (cycling lenses), HOV buffer/zones/diamonds; +679 draws; deck renders near black at low sun (pre-existing shadow/ambient), lenses dim (ITE minimum, no bloom). |
| C3b | Done | 496 / 471 | Strip + minimap viewed in chase cam (asphalt + sky) and cockpit; chevron arrow fixed after first shot read backwards; speed only seen at 0 (car did not auto-accelerate this run). |
| C4 | Done | 500 / 475 | Tube, portal, plaza, gantry, U-turn opening, Riverhead lane drop + roundabout; viewed inside / exit / portal / approach / ring; found and fixed sunlit walls (CSM + shadow floor), daylight EV clamp, haze in the tube, lane paint under folded asphalt; ring not driven in the app. |
| D1 | Done | 502 / 477 | Concrete, patched, fresh, worn viewed in chase + cockpit at rain 0 and 1.0 (`docs/corridor/img/d1-*.png`); fixed Asphalt012 red stains, black paint-loss blotches, too-dark rumble; shimmer only judged from stills (box-filtered lines). |
| D2 | Done | 506 / 481 | NJ close-up + car contact, barrier end, Nassau walls (mainline + SR), cut + W-beam, grass median viewed; median hit with both models (scripted); fixed brick moire (debug mips), dark tint, wide median over HOV paint. 23,385 draws (walls + rails 180). |
| D3 | Done | 509 / 484 | Gantry 1 MILE at 45 m / 200 m, gore sign, mile marker 8.0, exit 73 board, W2-6 viewed (`docs/corridor/img/d3-*.png`); fixed small tab, deck-hidden gantry, marker gaps; truss near black against the low sun (pre-existing lighting); WB faces and night retro not viewed. 23,571 draws (+186). |
| D4 | Done | 512 / 487 | 145 OSM crossings, 38 bridges + 30 restyled decks; viewed steel (approach/under), concrete (under), Sagtikos arch, LIRR, crossroad deck, LIE-over (`docs/corridor/img/d4-*.png`); found and fixed debug CSM rendered from below, black undersides (bounce), NaN block, stretched shots. Trench interior, SR decks, ties/rails up close, night not viewed. 23,840 draws (+269); walls stop short of bridges (9.7 km). Wall ends at a bridge not viewed. |
| D5 | Done | 515 / 490 | 13 kits by exit, district lamps (none east of 64), trees + buildings + far ground, Midtown card; viewed LIC + skyline (WB), LeFrak, Flushing Meadows, Nassau walls + shoulder lamps, Nassau houses, Melville exit 49, pine barrens, Riverhead, night Nassau + Queens (`docs/corridor/img/d5-*.png`); fixed grey see-through trees (mip bleed, pinholes), tree shading (3 normal schemes), double haze on the skyline, thin pine understorey. 8,674 draws (-15,166: lamps batched). Not viewed: dusk, rain, high camera / far-ground edge, LIE-over trench with far ground, every kit boundary, at-speed shimmer. |
| D6 | Done | 516 / 491 | 12 landmarks; viewed QMT portal + vent (no gantry), Calvary headstones + exit 17 deck, Unisphere / Pavilion (road + high), stone exit 31 deck, HOV / county-line / Great Neck / 62N boards, exit 39 gantry, exit 44 deck, Melville street trees, Sagtikos arch, exit 68 barrens, exit 73 END + lane drop (`docs/corridor/img/d6-*.png`); fixed Unisphere hidden by trees (wedge), dark / tall headstones, boards on a deck or behind a ramp. 8,710 draws (+36). Not viewed: night, rain, WB board faces, LIC viaduct bridges up close, new diamonds' ramps, at speed. |
