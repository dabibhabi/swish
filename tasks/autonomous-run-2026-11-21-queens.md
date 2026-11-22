# Autonomous run log: Queens network (2026-11-21)

> Written by AI (Claude). The owner is away. Owner's instruction (2026-11-21): "do what you do and aim for photo
> realism ... confirm any recommended tasks on your own." Plan: "Queens network" section in [`todo.md`](todo.md).

Rules kept from the LIE run: one code agent at a time (research may run in parallel), debug-first (release
byte-identical), no commit or push, same branch, and per item: sliders, tuning notes, changelog, architecture note.
Agents may launch `make debug` and take window-only screenshots (LIE plan precedent). Another session works on the
perf plan in the same checkout (renderer passes, `App.cpp`), so briefs keep to scene, tools, debug and HUD files.

## Decisions taken without the owner (pick = my recommendation)

| Item | Decision | Why |
|------|----------|-----|
| N0 | Work-zone advance signs at real MUTCD freeway spacing (A 1,000 / B 1,500 / C 2,640 ft, i.e. 1,000 / 2,500 / 5,140 ft before the taper) in compact metres, not scaled by compression; taper $L = WS$ uncompressed; clamp to the end link length if shorter | Sign spacing and taper are set by driver reaction at speed, not by map distance; the plan's 1,500 / 500 ft was a placeholder. |
| N0 | Astoria underpass = Astoria Blvd N/W elevated station over the sunken GCP; Jamaica underpass = Jamaica Yard green steel subway bridge on stone piers | Research picks with CC photos; most identifiable from the road. |
| N0 | 188th St gas station = Gulf median station between GCP exits 19 and 20, reached by left-lane exits; its U-turn link is not modelled (left-exit stub closed like local exits) | Exists per OSM; U-turn unverified. |
| N1 | Network bake lives in a new `tools/queens_network.py` that imports `lie_corridor.py` (no copied helpers); `lie_corridor.py` only gains `cmd_bake(write=False)` returning the warp frames and a shared `write_elev_csv` | Keeps the LIE tool unchanged in behaviour (asset re-baked byte-identical) and avoids a 1,700-line file |
| N1 | Centreline = midpoint of the two carriageways: each forward-carriageway sample paired with its nearest point on the reverse one (offset carried past the reverse chain's ends); lanes = min of the two sides | No netconvert run per road (the task fetch is JSON); the LIE uses one carriageway, but a median centreline is what a two-sided cross-section needs |
| N1 | Walls in config as `[junction, real metres past it]`, not lat/lon | The owner's ends are described relative to junctions (e.g. "Kew Gardens + 1 km"); one number per wall to tune in N0 |
| N1 | BQE north end and Van Wyck north end also get a wall (10 walls, owner list has 8) | Both roads end just past a junction in the network; the driver needs a closure there too |
| N1 | Junction = closest approach of the two real centrelines near a config hint (the crossing, or the BQE-into-GCP merge) | Data-derived; the hint only disambiguates |
| N1 | Seam fix: the two link fits at an interior junction cross-fade over +/- `blend_m` (250 m real) | A plain piecewise similarity kinks at every seam; the blend keeps the junction exact (both fits map it to the same point) |
| N1 | Ramp polylines use the mean of the incident (non-LIE) link fits at their junction; ends attach to the nearest mainline carriageway within 25 m (LIE: both carriageways from the cached LIE extract), else 255 | Ramp ends snap to the roads in N2 anyway; the mean fit keeps the ramp shape continuous |
| N1 | Ramp heights = DEM ground of the nearest road sample + layer x 6.5 m (no per-ramp USGS pull) | No extra network pulls; N2 snaps ramp ends to road heights |
| N1 | Network file blocks: ROAD (id -> asset path), JNCT, WALL, RAMP + RPTS (fixed-size ramp headers + one points block) | Fixed-size records keep the one-fread-per-block rule of CorridorAsset |
| N1 | Road heights use the LIE datum (DEM metres minus the LIE start-station height) | One vertical datum for all six alignments |

## Progress

- N0a done: [roads.md](../docs/queens-network-reference/roads.md) (lanes, medians, profiles, 9 junction layouts, exit lists). Aerial measurements and photos still "(to check)". The LIE exit 17 landmark coordinate in `docs/lie-reference/README.md` is ~1.9 km off the real LIE x BQE crossing.
- N0b done: [landmarks.md](../docs/queens-network-reference/landmarks.md), [workzone.md](../docs/queens-network-reference/workzone.md), 18 CC photos ([CREDITS](../docs/queens-network-reference/photos/CREDITS.md)). Street View not viewed.
