# Handoff: LIE full route C1-D6 (done) and what is next

> Written by AI (Claude) on 2026-11-21 for the owner and the next agent. Follows
> [`handoff-lie-full-route.md`](handoff-lie-full-route.md) (2026-11-20, state at C1). Most of it was built
> overnight: the owner was asleep and told the agent to take every recommended pick. Each pick is logged in
> [`autonomous-run-2026-11-20.md`](autonomous-run-2026-11-20.md).

## Summary

The compact 30-mile Long Island Expressway now runs from inside the Queens-Midtown Tunnel to the Riverhead
roundabout. It has these parts, all built debug-first:

- diamond interchanges and drivable ramps
- one-way service roads from OSM
- road surfaces and edge treatments
- MUTCD guide signs
- bridges from OSM
- 13 district kits
- 12 hand-detailed landmarks

The release build (`make run`) is unchanged: every feature is gated behind `#ifdef SWISH_DEBUG_UI` / `kDebugBuild`,
and the release `.spv` golden still passes.

- Commit: `8c17065 feat(corridor): LIE full route C1-D6 (...)` on `bug/fix-camera-ownership`, 257 files. **Not pushed.**
- Left out of the commit on purpose: `Untitled.md` (empty), `data/dem/lie_i495_elev.csv` (17 MB USGS cache),
  `.obsidian/workspace.json` (editor state). These keep `make check` failing on two old checks.
- Tests at handoff: `make test-debug` 516 pass, `make test` 491 pass (were 481 / 456 at the previous handoff).
- Draw calls: 8,710 debug (the peak was 23,840 at D4; D5 batched the lamps).

## What was built

| Item | What | Key files |
|------|------|-----------|
| C1 | Ramp, service-road and crossover sweeps over corridor frames (owner-grilled) | `src/scene/Corridor/CorridorRamps.{h,cpp}` |
| C2a | Overpass fetch of service-road ways (NY 906A EB / 906B WB, one-way), baked `SRVR` block | `tools/lie_corridor.py`, `CorridorAsset.h`, `data/osm/lie_service_roads.json` (git-ignored) |
| C2b | 30 crossroads (one per exit number), mainline dip 6.5 m / 350 m, ramps, gores, deck | `CorridorDiamonds.{h,cpp}`, `CorridorAlignment::dip` |
| C3 | Drivable surface over mainline, ramps, service roads and crossroad, for both car models | `CorridorSurface.h`, `CorridorBounds.cpp` |
| C3a | Cross-street stubs, span-wire signals, HOV buffer / access zones / diamonds | `CorridorDiamonds.cpp` |
| C3b | HUD strip and route minimap (second ImGui context, H key) | `src/debug/HudOverlay.*`, `src/renderer/Passes/HudOverlayPass.cpp`, `src/core/App/AppHud.cpp` |
| C4 | Tunnel tube interior, portal, Riverhead lane drop and roundabout | `CorridorEnds.{h,cpp}`, `CorridorEndsMesh.cpp` |
| D0 | Reference sheet: 12 landmark notes, corridor kit by district, 15 CC photos | `docs/lie-reference/` |
| D1 | Surface material per stretch (`SURF` block), concrete east of exit 64, pavement shader | `shaders/common/pavement.glsl`, `docs/surface/` |
| D2 | NJ median barrier, grass median + cable east of 64, W-beam, Nassau brick sound walls, cuts | `CorridorEdges.{h,cpp}` |
| D3 | Gantries at 1 mi / 1/2 mi / gore, gore signs, mile markers, sign atlases | `CorridorSigns.{h,cpp}`, `SignAtlas.h`, `tools/sign_corridor.py`, `assets/signs/` |
| D4 | 145 OSM crossings (`XING` block), 38 bridges (steel, concrete, Sagtikos stone arch, LIRR, LIE-over), restyled decks | `CorridorBridges.{h,cpp}`, `data/osm/lie_crossings.json` |
| D5 | 13 district kits: lamps per district (none east of 64), trees, buildings, far ground, Midtown skyline card | `CorridorDressing.{h,cpp}`, `CorridorLamps.h`, `tools/gen_dressing.py`, `assets/dressing/` |
| D6 | QMT vent tower, Calvary headstones, Unisphere + NYS Pavilion, stone exit 31 deck, county-line / HOV / Great Neck / 62N boards, Melville trees | `CorridorLandmarks.{h,cpp}` |

Sizes live in `src/scene/RoadScene/RoadParams.h`, sliders in `src/debug/Tunables.def`, and config in
`config/lie_corridor.toml`. The D5/D6 tables went in `RoadParams.h`, not TOML: reading TOML at runtime would put
the parser in release, and baking would change the asset format.

Debug-only renderer fixes made along the way:
- **Sky test:** it reads only the clear depth (`depth <= 0.0`) in `lighting`, `ssao`, `godrays` and `dof`. Before,
  everything past 100 m was shaded as sky.
- **CSM light camera:** moved to the sun side. It was rendered from below the ground.
- **Ground bounce:** a 0.15 bounce term lights faces turned away from the sun.
- **Material mips:** material textures now get a mip chain.

Docs written per item: changelog in `docs/changelog/2026-11.md`, tuning notes in `docs/corridor/tuning-notes.md`,
architecture in `docs/endless-road.md`, in-app checks in `tasks/upcoming-tests.md` (C1-D6 sections), and
screenshots in `docs/corridor/img/{c2b,c3,c4,d1-d6}-*.png`.

## Owner calls (decisions waiting)

Full list with context: "Open findings for the owner" in [`autonomous-run-2026-11-20.md`](autonomous-run-2026-11-20.md).

1. **Promote the debug renderer fixes to release:** sky test, CSM sun side, material mips, ground bounce. Each one
   changes release images and the sky test also changes the `.spv` golden, so each needs a changelog entry.
2. **Tunnel look:** today (cashless, no Queens plaza; this is what D6 left) or a period look with the plaza.
3. **Exit 53:** keep the Sagtikos arch, or give exit 53 its own diamond.
4. **HUD next exit:** list only exits with ramps (30 numbers), or every OSM exit. Today the HUD can name exit 23
   while the gantry says 24.
5. **Lighting:** backlit steel and vertical faces (trusses, poles, headstones, sign backs) render near black at the
   shipped low sun.
6. **Tree shadows:** trees cast no sun shadow, because the shadow pass has no alpha test. A renderer task.
7. **Median near Riverhead:** narrow concrete barrier at exit 71 vs wide grass at exit 69. Check against Street View.
8. **Twin-pole lamp lenses:** at 20,000 cd/m² they bloom into large discs at night.

## Not viewed or unverified

- Night, rain, dusk and driving at speed were barely viewed after D4. The roundabout was never driven.
- Westbound sign faces, the LIE-over trench interior, every district-kit boundary, and the far-ground edge from a
  high camera.
- Sign wording is unconfirmed: county line, 62N panel names, end of expressway. Skyline building positions are
  from memory.
- A NaN normal on some car pixels (glass?), clamped but never traced.
- A 13 cm seam step at ramp starts on 5-6 % banks.
- Not modelled from the D6 brief: the BQE two-level viaduct, the exit 22 stacked ramps, the full NY-135
  interchange, the overhead HOV panel, and the brown Sagtikos signs.

## Next work

- **L4.4a-f bloom down/upsample chain** (added to [`todo.md`](todo.md) under L4.4 on 2026-11-21). It replaces the
  inside of the debug `BloomPyramidPass`: a 13-tap downsample and a 3x3 tent upsample (Jimenez 2014). This fixes
  shimmer and blocky wide halos at about the same cost.
- **B5b** road bumps can now read `CorridorAlignment::surface_at` (D1). Then B5d, then B6 / B7, which need a human
  driving.
- Owner checks in `tasks/upcoming-tests.md` (C1-D6 sections).
- Delete the retired `RoadScene` ramp generators once the golden mesh test and the lighting lab no longer need
  them (C1 decision).

## Traps (new since the last handoff)

- `data/osm/` is git-ignored. Back it up before any fetch (`data/osm_backup_*/` is ignored too). Never overwrite
  `lie_i495.osm.xml`. Never point the bake tool at the release asset `lie_corridor.bin`.
- Tunable counts are pinned in `tests/test_debug_params_io.cpp`. Every new `TUNE_*` line needs a bump.
- `tests/golden/release_spv.sha256` differs from the parent commit only in `ibl_irradiance` / `ibl_prefilter`.
  That change comes from the E1 work, not C1-D6.
- Screenshots: use `screencapture -l <window id>` on the `swish | <fps>` window, and revert any temporary spawn or
  camera hooks afterwards.
- clangd flags stale diagnostics (unknown types, unused includes) after big agent edits. Trust the build and the
  tests.
