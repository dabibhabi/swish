# Phase 3 — Real-world LIE corridor (OpenStreetMap + SUMO)

> **Path note (2026-09-29):** this design doc uses the constant name `WORLD_SCALE` (= 1000 WU per metre). That constant was
> removed in the deep-clean; the single definition is now `util::units::kWuPerMetre` in
> [`src/utils/units/Units.h`](../src/utils/units/Units.h). `RoadScene.cpp` is split into topic files and `MeshBuilder`
> now lives in [`src/scene/RoadScene/MeshBuilder.h`](../src/scene/RoadScene/MeshBuilder.h) / `.cpp`. The numbers below are unchanged.

**Owning agent:** `scene-road-engineer` (the existing endless-road/corridor specialist —
`roadside-props-engineer` for signage once real exit data exists, `traffic-engineer` for the later
TraCI sub-phase).
**Status:** design settled via grilling (2026-08-23). This is a full replacement of the shipped
endless-treadmill architecture — largest and riskiest of the three phases, staged internally.
**Stage 3a is BUILT and drivable (2026-08-23)** — see [§8 As-built](#8-as-built-stage-3a-2026-08-23)
for the measured numbers, the two design refinements it forced, and the corrections to §7's
precision estimate. 3b and 3c remain open.
**Diagrams:** [`archive/docs/diagrams/phase3-osm-pipeline-small.excalidraw`](../archive/docs/diagrams/phase3-osm-pipeline-small.excalidraw) ·
[`docs/diagrams/phase3-corridor-map-large.excalidraw`](diagrams/phase3-corridor-map-large.excalidraw).

## 1. Why this phase exists

The user wants a real-world-derived, compacted (16-20 mi) Long Island Expressway from Manhattan
through Nassau County to the Suffolk border, with drivable service/marginal roads — using
OpenStreetMap for the road data and SUMO for traffic tooling.

## 2. What we found

**Codebase facts.** There is currently **zero** OSM or SUMO tooling anywhere in this repo (a full
grep across `.md/.py/.cpp/.h/.json` for `OpenStreetMap|OSM|SUMO|osmnx|netconvert|.pbf|overpass`
returns no real hits — the only `overpass` matches are the English word for highway bridges). The
current road system (`docs/endless-road.md`) is an explicit, locked-in **treadmill**: a 4.2 km
hand-authored intro feeding one canonical 300 m chunk (`RoadGeometry::kChunkLen`), instanced
repeatedly with an origin-rebase scheme to keep float32 coordinates bounded. It is purely fictional
— not tied to any real-world coordinates. `WORLD_SCALE = 1000` (1 scene-metre = 1000 WU), so
1 mile = 1,609,340 WU.

**Real-world geography facts** (Manhattan/Midtown Tunnel → Nassau/Suffolk line):

| Fact | Value |
|---|---|
| Real driving distance | ≈ 31 mi (county line falls between exit 48 and exit 49) |
| Lanes | Queens: 3 each way. Nassau (to the county line): **4** each way, one HOV |
| Terrain | Essentially flat — no meaningful grade change on this stretch |
| Service/marginal roads | Present most of the way, **not continuous** (breaks at interchanges/ROW constraints) |
| Major interchanges, west→east | Exit 27 Clearview Expy (I-295) · Exit 31 Cross Island Pkwy · Exit 38 Northern State Pkwy/Meadowbrook (directional) · Exit 46 (2nd Northern State crossing) · Exit 48 Round Swamp Rd · Exit 49 NY-110, just past the county line |

## 3. Decisions from grilling (with reasoning)

1. **Full replacement, not a new mode.** The real corridor replaces the endless fictional treadmill
   entirely — the game becomes a finite, point-to-point drive. This retires the current procedural
   service-road and elevated-interchange generators as the mesh-emission machinery (see #5).
2. **SUMO's role, precisely scoped:** `netconvert` now, as an **offline** step converting OSM tags
   into clean lane-level network geometry (lane counts, turn lanes, ramp shapes). Full **TraCI**
   traffic microsimulation is deliberately deferred to a later sub-phase, once the road itself is
   drivable — these are genuinely different bodies of work (one is a build-time data-cleaning tool,
   the other is a live simulation process the game would need to interoperate with).
3. **OSM data via a one-time offline extract**, checked into the repo (or fetched once and cached),
   not a live Overpass query at build time — reproducible, no network dependency after the first
   pull, immune to upstream OSM edits changing the corridor underfoot.
4. **Compaction: automatic heuristic, anchored on the 6 real interchanges.** Segments beyond some
   threshold distance from the nearest named landmark get compressed; the landmarks themselves keep
   their real relative geometry. Chosen over uniform linear scaling specifically to avoid distorting
   interchange spacing — the whole point is that it reads as *recognizably* the real LIE.
5. **Real interchange geometry wins over the existing "elevated diamond" visual signature.** The
   real LIE through this stretch is mostly at-grade with directional ramps (e.g. exit 38's
   Northern State/Meadowbrook interchange) — not the towering elevated stacks the current shipped
   system generates every ~1.5 mi. Building it as-real means retiring `generate_interchange`
   (`RoadScene_Ramps.cpp`, Layer 4) as the geometry source for this corridor; a **new** OSM-driven mesh
   pipeline replaces it rather than adapting the old templated generator to irregular real geometry.
6. **Real signage + HOV lane markings.** Exit numbers/destination text sourced from the same OSM
   extract; HOV lane rendered with real diamond markings. Both are cosmetic only until the later
   TraCI sub-phase gives traffic (and therefore HOV-lane *function*) a reason to exist.
7. **Simple stop/turn-around at both ends.** No new start/finish game-flow for this phase — the
   corridor is just finite, the player turns around at either end.
8. **Staged internally (3a → 3b → 3c):**
   - **3a** — bare compacted alignment (centerline, lane counts, compaction) drivable, no
     interchange/signage detail. Proves the OSM→mesh pipeline and the finite-corridor mechanics work
     at all.
   - **3b** — real interchange ramp geometry + service-road connections layered in.
   - **3c** — real exit-number signage + HOV lane markings.

   Each stage is independently drivable and testable — this is the largest, riskiest phase of the
   three, and staging keeps any single verification pass bounded.

## 4. The pipeline

See `phase3-osm-pipeline-small.excalidraw` for the flow diagram:

```
OSM extract (Overpass, one-time)
  → SUMO netconvert (lane-level geometry)
    → compaction heuristic (landmark-anchored)
      → engine road-spline asset (checked in)
        → new OSM-driven mesh generator
          [later: SUMO TraCI traffic, same road-spline asset as its network]
```

### 4.1 Coordinate transform

Real-world data arrives as WGS84 lat/lon; the engine works in a flat, `WORLD_SCALE`-scaled
Cartesian frame. For a corridor this short (~31 real miles), a local tangent-plane (equirectangular
or transverse-Mercator-style) projection centered near the route's midpoint is sufficient —
full UTM-zone handling or anything geodesically exact is unnecessary overhead. This is treated as
an implementation detail for `scene-road-engineer`, not a design decision requiring further
grilling.

### 4.2 Compaction heuristic (sketch)

Given the real centerline as an ordered sequence of points $p_0 \ldots p_n$ with known landmark
indices $L = \{i_1, \ldots, i_6\}$ (the six interchanges above), for each point $p_i$ define its
distance to the nearest landmark:

$$
d(i) = \min_{j \in L} \big| \text{arclength}(p_i, p_j) \big|
$$

Points beyond a threshold $d(i) > N$ get compressed by a factor $c < 1$ along the route's own
tangent direction; points within $N$ of a landmark are left at real scale. $N$ and $c$ are tuned
together until total corridor length lands in the 16-20 mi target band — this tuning is exactly
what stage **3a** is for.

## 5. What gets retired vs. what gets reused

| Current system | Fate |
|---|---|
| 4.2 km hand-authored intro + 300 m canonical chunk (`RoadGeometry::kChunkLen`) | Retired — replaced by the real, finite alignment |
| Origin-rebase scheme (`Treadmill.cpp`) | Likely **still needed** in some form if the corridor's world-space extent exceeds float32-safe bounds at `WORLD_SCALE`, even though it's no longer infinite — needs a distance check early in 3a |
| `generate_service_roads` (Layer 2) | Retired as the mesh source; real service-road presence/absence now comes from OSM tags |
| `generate_interchange` (Layer 4, elevated diamonds) | Retired — real at-grade/directional ramp geometry replaces it (decision #5 above) |
| `emit_ribbon` primitive (3D centerline follower) | **Reused** — it's a generic centerline-to-mesh primitive, not tied to the fictional road's specific geometry, and is exactly what a real, irregular centerline needs |

## 6. Tools & resources

- **Overpass API / a bounded `.osm`/`.pbf` extract** for the Manhattan→Nassau/Suffolk-border
  bounding box — the actual one-time data pull.
- **SUMO's `netconvert`** — converts the OSM extract into lane-level network XML
  (`.net.xml`); this project's pipeline parses that output rather than raw OSM tags directly, since
  netconvert already resolves lane counts/turn lanes/ramp connectivity that OSM's tagging alone
  under-specifies.
- **A new offline conversion tool** (Python, matching this project's existing `tools/car_analyzer/` package
  / `toml_baker` conventions) that reads netconvert's `.net.xml`, applies the compaction heuristic,
  and emits the engine's own road-spline asset format.
- **`docs/endless-road.md`** — read fully before starting 3a; even though its architecture is being
  retired, `emit_ribbon` and the origin-rebase pattern are being carried forward and its "what makes
  a tile join seamlessly" lessons still apply to a real, irregular alignment.
- **Verification:** stage-by-stage, per this project's visual-verification convention — 3a proves
  drivability end-to-end before any interchange/signage work is judged.

## 7. Open risks / follow-ups

- **Float precision at real scale.** A ~20 mi corridor at `WORLD_SCALE=1000` is well within
  float32-safe range for a *static* level (unlike the old infinite treadmill, there's no unbounded
  accumulation) — but this should be explicitly confirmed in 3a, not assumed.
- **Variable lane count (3→4 lanes at the Queens/Nassau line).** The current chunk system has no
  concept of this at all; the new mesh generator must support it from the start, not retrofit it.
- **SUMO toolchain dependency.** `netconvert` needs to be available wherever the offline pipeline
  runs (a build-time/dev-machine tool, not a runtime or player-machine dependency) — confirm this
  doesn't conflict with the project's macOS/MoltenVK-only toolchain assumptions before relying on it.
- **This is the largest of the three phases by a wide margin** — if 3a reveals the compaction
  heuristic or coordinate transform needs real redesign, stop and re-grill rather than pushing
  through with a shaky foundation for 3b/3c.

---

## 8. As-built (stage 3a, 2026-08-23)

Stage 3a shipped the bare compacted alignment, drivable end to end, with the finite-corridor
mechanics working. No interchange ramps (3b), no exit signage or HOV markings (3c).

### 8.1 The real data

| | |
|---|---|
| Source | Overpass API, one-time pull, cached at [`data/osm/lie_i495.osm.xml`](../data/osm/lie_i495.osm.xml) (1.76 MB) |
| Bounding box | `(40.66, -74.02, 40.83, -73.38)` — Manhattan → just past the Nassau/Suffolk line |
| Selector | `highway=motorway` with `ref ~ "^I *495$"`, plus `motorway_link` within 200 m (kept for 3b; 3a ignores them) |
| Extract contents | 930 ways · 6,426 nodes · 128 `motorway_junction` nodes |
| `netconvert` | **Eclipse SUMO 1.27.1** (installed via `pipx install eclipse-sumo`) |
| netconvert output | cached at [`data/osm/lie_i495.net.xml`](../data/osm/) + `.edg/.nod/.con/.tll/.typ` plain XML |
| Mainline chain | 200 netconvert `highway.motorway` edges, 1,107 shape points, 33.95 mi raw |
| Trimmed corridor | **31.20 mi** (east terminus = exit 49 + 400 m run-out) |

The exact Overpass QL is generated by
[`tools/lie_corridor.py`](../tools/lie_corridor.py)`::overpass_query` and printed on every `fetch`,
so the pull is reproducible. Neither the build nor a player machine needs the network or SUMO —
both intermediates are checked in, and `bake` alone regenerates the engine asset.

> **Do not retry the Homebrew route for SUMO.** `brew tap dlr-ts/sumo` is stale against
> Homebrew 6.0.13 (`undefined method 'cxxstdlib_check'`). `pipx install eclipse-sumo` works and
> puts `netconvert`/`sumo`/`polyconvert`/`netedit` on `PATH` under `~/.local/bin`.
> `SUMO_HOME` is not exported; the tool locates `osmNetconvert.typ.xml` itself.

**netconvert flags** (`--proj.plain-geo` is the load-bearing one):

```
netconvert --osm-files data/osm/lie_i495.osm.xml \
           --type-files $SUMO_HOME/data/typemap/osmNetconvert.typ.xml \
           -o data/osm/lie_i495.net.xml \
           --plain-output-prefix data/osm/lie_i495 \
           --proj.plain-geo \
           --geometry.remove false --ramps.guess false \
           --junctions.join true --no-turnarounds
```

`--proj.plain-geo` keeps the plain-XML shapes in WGS84 lon/lat so the projection stays ours
(§4.1, unit-tested) rather than depending on netconvert's UTM zone choice. `--geometry.remove false`
preserves the real centreline vertices — we want the actual curvature, not a simplified graph.

### 8.2 Measured result

| Quantity | Value |
|---|---|
| Real (trimmed) length | 31.20 mi |
| **Compacted length** | **17.994 mi** (target 18.00, band 16–20 ✔) |
| $N$ (landmark radius) | **900 m** |
| $W$ (smoothstep blend) | **250 m** |
| $c$ (compression factor) | **0.1854** — *solved*, not authored |
| $R_\text{floor}$ (curvature floor) | **500 m** |
| Landmark-preserved length | 6.96 mi |
| Length where the curvature floor binds | 15.69 mi |
| Mean scale $\overline{f}$ | 0.577 |
| Stations | 2,896 at 10 m spacing |
| Engine extent | $Z \in [-26{,}521, 0]$ m, $\lvert X \rvert \le 1{,}835$ m |
| Segments / draw calls | 25 segments (1.2 km each) · 1,921 draw calls · 81 KB asset |

**Landmark stations** (real → compacted): exit 27 11.29 → 5.50 mi · 31 12.96 → 6.96 · 38 19.76 →
11.48 · 46 28.32 → 15.40 · 48 29.97 → 16.76 · 49 30.95 → 17.75. Note that *adjacent* landmark
spacing is preserved where the interchanges are close (48→49 is 0.98 mi real, 0.99 mi compacted)
and squeezed only across the long straightaways (31→38 is 6.80 mi real, 4.52 mi compacted) —
which is exactly what decision #4 was chosen for.

**Lane profile**, taken from netconvert's `numLanes` and *not* authored: 2 lanes through the
Queens-Midtown Tunnel and its approach, 3 through Queens, **3 → 4 at compacted station 7.90 mi**,
which is the Queens/Nassau county line. The spec's §2 claim fell straight out of the data.

### 8.3 Refinement 1 — the compaction heuristic needed a curvature floor

§4.2 as sketched compresses everything outside a landmark window by $c$. Because compaction
preserves heading and scales arc length, a compacted curve's radius is $R' = R f$ — **compressing a
curve makes it tighter by $1/f$.** Measured at $c = 0.455$ with no floor:

| | real alignment | compacted, no floor |
|---|---|---|
| tightest radius | 99 m (the Queens-Midtown Tunnel portal ramp — genuinely that tight) | **46 m** |
| road with $R < 500$ m | 1.05 km of 50.2 km | **6.05 km** |

That is the heuristic *inventing corners that do not exist*, which defeats the entire point of
"recognizably the real LIE". The fix is one extra term, and it is the literal form of the
heuristic's own stated intent — compress the long **straightaways**; landmark distance was only ever
a proxy for "nothing interesting here", and curvature measures it directly:

$$
f_i \;=\; \max\!\Big(\underbrace{w_i + c\,(1-w_i)}_{\text{§4.2 as sketched}},\;
\underbrace{\min\!\big(1,\; R_\text{floor}/R_i\big)}_{\text{curvature floor}}\Big)
$$

$R_i$ is measured over a $\pm 50$ m baseline (station-to-station differencing reads OSM digitising
noise as curvature: it drops the apparent minimum radius 99 → 79 m), and the floor is dilated by
$\pm 100$ m so a curve's apex keeps its protection.

The floor makes $L'(c)=\sum_i f_i\,\Delta s$ piecewise-linear instead of affine, so the closed form
$c = (L_\text{target}-A)/B$ becomes a bisection — still exact, since $L'$ stays continuous and
monotonically non-decreasing in $c$. $c$ remains **derived**, so the corridor always lands on the
target rather than being hand-walked into the band.

$R_\text{floor} = 500$ m was chosen by sweeping it against the real curvature distribution at the
18 mi target (km of road under 400 m / 500 m radius; real = 0.83 / 1.05):

| $R_\text{floor}$ | solved $c$ | km < 400 m | km < 500 m |
|---|---|---|---|
| 300 m | 0.416 | 10.30 | 12.66 |
| 400 m | 0.328 | 0.91 | 11.38 |
| **500 m** | **0.185** | **0.83** | **1.20** |
| 600 m | — | infeasible at 18 mi ($c \to 0$) | |

At 500 m the compacted curvature distribution matches the real road almost exactly, and the
tightest curve is the real 99 m ramp, untouched. The price is a smaller $c$ (straightaways compress
$5.4\times$) — the right place to spend it: a compressed straight still looks like a straight, a
compressed curve looks like a different road. **If curvature fidelity ever needs to be traded back
for a stronger sense of distance, raise `target_length_miles` toward 20** ($R_\text{floor}=500$ at
20 mi solves to $c = 0.369$ with the same curvature fidelity).

### 8.4 Refinement 2 — the drive cannot start at station 0

Station 0 is the tunnel's Manhattan portal, whose approach is a real ~100 m-radius loop ramp: a
confusing first frame and a hopeless standing start. The baker now emits a `startStation` — the
first station past `min_start_m` whose radius clears `start_min_radius_m` — so the start pose is
derived from the data rather than hand-placed. For the shipped bake that is **340 m**.

### 8.5 Correction to §7 — float32 was never at risk

§7 asked for this to be confirmed rather than assumed. Confirmed, and §7's own estimate was wrong:

> "A ~20 mi corridor at `WORLD_SCALE=1000` is well within float32-safe range" — correct conclusion,
> but the accompanying figure used elsewhere in this phase's planning (~3e10 WU) applied
> `WORLD_SCALE` twice. **1 mile = 1609.344 m × 1000 WU/m = 1.609e6 WU, so 18 mi is 2.897e7 WU, not
> 2.9e10.**

| Magnitude | float32 ULP |
|---|---|
| 2.897e7 WU (corridor east end, absolute) | **2.0 WU = 2.0 mm** |
| 2.4e6 WU (camera far plane) | 0.25 WU |
| 1.2e6 WU (rebase threshold) | 0.125 WU |
| 6e5 WU (segment-local vertex) | 0.06 WU |
| 3.0e10 WU (the figure that was assumed) | 2048 WU = 2.05 m |

So absolute float32 coordinates would have worked. **The origin rebase is therefore NOT required**
— but it is kept, because it costs nothing, removes any millimetre-scale shimmer at the east end,
and keeps the scheme valid if the corridor is lengthened. Segment-local vertices are kept for the
same reason plus a second, independent one that *is* load-bearing: arc-length UVs. Accumulated over
the whole corridor, arc/tileSize reaches ~1e4 UV units, where float32 quantises to a whole texture
repeat — hence the 300 m tile UV restart (§8.6).

One difference from the treadmill's rebase is worth recording: the old scheme could only shift by
whole chunk lengths, because it relied on the periodic tile looking identical afterwards. Here every
segment's render offset is recomputed from its double-precision origin every frame, so a shift of
**any** size moves all geometry together — invisible by construction rather than by periodicity.

### 8.6 What was built

| Concern | File |
|---|---|
| Offline pipeline (fetch · netconvert · project · compact · bake) | [tools/lie_corridor.py](../tools/lie_corridor.py) |
| Pipeline configuration (bbox, compaction, cross-section, tiling) | [config/lie_corridor.toml](../config/lie_corridor.toml) |
| Baked asset format + loader | [src/scene/Corridor/CorridorAsset.h](../src/scene/Corridor/CorridorAsset.h) |
| Alignment queries — `(station, lateral) ↔ (x, z)`, lanes, cross-section | [CorridorAlignment.h](../src/scene/Corridor/CorridorAlignment.h) · [.cpp](../src/scene/Corridor/CorridorAlignment.cpp) |
| Mesh generation (segments → tiles → cross-section bands) | [CorridorScene.h](../src/scene/Corridor/CorridorScene.h) · [.cpp](../src/scene/Corridor/CorridorScene.cpp) |
| Finite-corridor mechanics (rebase, visible window, clamp, termini) | [CorridorDriver.h](../src/scene/Corridor/CorridorDriver.h) · [.cpp](../src/scene/Corridor/CorridorDriver.cpp) · [CorridorBounds.cpp](../src/scene/Corridor/CorridorBounds.cpp) |
| Swept cross-section mesh primitives | `MeshBuilder::emit_sweep_band/_wall/_dashes` in [MeshBuilder.h](../src/scene/RoadScene/MeshBuilder.h) / [.cpp](../src/scene/RoadScene/MeshBuilder.cpp) (moved out of `RoadScene.cpp` 2026-09-29) |
| Tests (transform · compaction · alignment · bounds · precision) | [tests/test_corridor.cpp](../tests/test_corridor.cpp) |

**Two levels of subdivision, for two different reasons.** A *segment* (1.2 km) is one vertex buffer
and one render offset — vertices stored relative to a double-precision segment origin. A *tile*
(300 m) is one draw call per cross-section band and one arc-length UV restart; 300 m because it is a
multiple of every surface texture tile size (3000 / 4000 / 1500 / 1000 WU), the same reason the
retired canonical chunk was 300 m. Tiles share their `SweepFrame`s across every band in the
cross-section, so adjacent bands cannot open hairline slivers, and a tile's last station is the next
tile's first, so tiles join without a crack.

**Variable lane count** (§7's second risk) is supported from the start: `CorridorStation::lanes` is
**fractional**, so a lane-count change is baked as a real taper (`lane_taper_m`) rather than a step,
and every lateral offset downstream is a function of it. Dashed lane lines use the tile's *minimum*
lane count, so a line is never painted across a lane that is still opening up — which is how a real
lane-add is striped.

### 8.7 Deviations from the settled design

| Decision | As built | Why |
|---|---|---|
| #2 `netconvert` now | **Followed.** SUMO 1.27.1, flags in §8.1. | An earlier authorisation to parse raw OSM tags instead (the toolchain was absent) was withdrawn once `netconvert` was installed. |
| #2 pipeline parses `.net.xml` | Parses netconvert's **plain XML** (`.edg.xml`/`.nod.xml`), not `.net.xml`. | `--proj.plain-geo` only applies to plain output; `.net.xml` is always projected Cartesian, which would have meant inverting netconvert's UTM to get lon/lat back. Both files are produced and cached; §4.1's transform stays ours and testable. |
| §4.1 projection | Local tangent plane, ours, in Python; **not** netconvert's Proj. | Keeps the transform unit-testable in-repo and independent of netconvert's zone choice. Scale error over 50 km is $O((d/R)^2)\approx 6\times10^{-5}$. |
| §4.2 heuristic | Added a **curvature floor** (§8.3). | Refinement, not redesign: same $f_i = w_i + c(1-w_i)$ form and the same derived $c$; one extra `max`. Judged a refinement because it implements §4.2's own stated intent. Flagged here for review. |
| §5 "retired" generators | `generate_service_roads` / `generate_interchange` / `generate_chunk` are **unwired**, not deleted. | Deleting them is 3b's job (it replaces the interchange geometry). They upload nothing now, so they drive zero draws. `tests/test_road_scene.cpp` still exercises them. |
| §5 `emit_ribbon` reused | `emit_ribbon` is **untouched**; the corridor uses new `emit_sweep_*` siblings. | A real cross-section needs per-station *asymmetric* lateral bounds (lane tapers) and one draw call per band, not one per quad. `emit_ribbon`'s exact output still backs the legacy intro/interchange path, so it was generalised alongside rather than changed. |

### 8.8 Known gaps at the end of 3a

- The road surface is **flat** (no 2% crown). `RoadGeometry::set_flat_surface` points the physics
  surface query at the same single height the mesh uses, so geometry and physics agree exactly —
  which matters more than a crown. Re-introducing the crown means making it a function of *lateral*,
  not world X.
- Beyond the 120 ft grass verge there is **void**. No distant terrain, sound walls, trees, lamps or
  props — 3a is the bare alignment by definition.
- The lateral clamp is a **position** clamp (as the retired X-clamp was). A car held against a bound
  keeps its inward velocity and scrubs along it; it cannot cross, but it can stall there facing the
  wrong way. Zeroing the into-bound velocity component needs a `CarEntity` velocity setter.
- Free-fly camera collision bounds were **disabled** (they were a world-X window).
- The 2→3→2→3 lane wobble in the first 2.8 mi is real OSM data (tunnel tubes tagged `lanes=1`,
  clamped up to 2), not a bug, but it is cosmetically odd.
- No exit signage, no HOV diamonds, no ramps — 3b/3c.
