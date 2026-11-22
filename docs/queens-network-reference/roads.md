# Queens network: road reference

> Written by AI (Claude), 2026-11-21. Owner to review. Facts marked "(to check)" come from memory, one blurry
> view, or an inference from OSM tags. They are not confirmed by a photo or a written source.

This sheet feeds N0 to N7 of the Queens network plan ([`tasks/todo.md`](../../tasks/todo.md), section "Queens
network"). It covers five roads, nine junctions and the local exits on each modelled stretch. The image rules in
[`docs/lie-reference/README.md`](../lie-reference/README.md) still apply. Street View is look-only. This round
saved no photos.

## Source keys

| Key | Source |
|-----|--------|
| **[WP-BQE]** | Wikipedia, [Interstate 278](https://en.wikipedia.org/wiki/Interstate_278) (route description + exit list) |
| **[WP-GCP]** | Wikipedia, [Grand Central Parkway](https://en.wikipedia.org/wiki/Grand_Central_Parkway) |
| **[WP-VW]** | Wikipedia, [Interstate 678](https://en.wikipedia.org/wiki/Interstate_678) (Van Wyck) |
| **[WP-CLV]** | Wikipedia, [Interstate 295 (New York)](https://en.wikipedia.org/wiki/Interstate_295_(New_York)) |
| **[WP-CIP]** | Wikipedia, [Cross Island Parkway](https://en.wikipedia.org/wiki/Cross_Island_Parkway) |
| **[NYCR-x]** | nycroads.com pages: [BQE](https://www.nycroads.com/roads/brooklyn-queens/), [GCP](https://www.nycroads.com/roads/grand-central/), [Van Wyck](https://www.nycroads.com/roads/van-wyck/), [Clearview](https://www.nycroads.com/roads/clearview/), [Cross Island](https://www.nycroads.com/roads/cross-island/). Some text dates from the 2000s. "Recent" there can mean 2005. |
| **[OSM]** | OpenStreetMap via Overpass, fetched 2026-11-21. Main-line `motorway`/`trunk` ways, bbox 40.70,-73.95,40.80,-73.69. The figures are lanes per direction (ways are `oneway=yes`), weighted by length, binned every 0.01°. |
| **[OSM-L]** | Same fetch, `motorway_link`/`trunk_link` ways within 600 m of a junction (800 m at Kew Gardens). The count is of OSM way fragments, not of ramps. The maximum `layer` on `bridge=yes` gives the number of levels. |
| **[SV-n]** | Google Street View panorama checked by AI on 2026-11-21 (links in the last section). It gives a look only, not a measurement. |

## Summary table (build defaults)

| Road | Modelled stretch | Lanes each way | Surface | Median | Edge | Speed (OSM) | Signs |
|------|------------------|----------------|---------|--------|------|-------------|-------|
| BQE I-278 | LIE exit 35 (LIE 17) north to the GCP | 3, splitting to 2+2 in the Astoria "Y" | Concrete on the viaduct, asphalt elsewhere | NJ barrier | NJ parapet (viaduct), concrete retaining walls (trench) | 45 mph | Interstate green, overhead trusses |
| GCP | RFK approach to the Nassau line | 3 (Astoria), 4-5 (LaGuardia to the LIE), 3 (Kew Gardens to Nassau) | Asphalt (concrete patches at Kew Gardens) | NJ barrier with lamps on the median | Grass banks, W-beam, trench walls in Briarwood, woods east | 40 mph Astoria, 50 mph elsewhere | Green guide signs plus parkway shield; no commercial vehicles east of exit 4 |
| Van Wyck I-678 | LIE exit 12 (LIE 22) south to Kew Gardens | 3 | Asphalt in Flushing Meadows; concrete on the KGI bridges | NJ barrier (to check in the park) | W-beam, park lawn and trees | 50 mph | Interstate green |
| Clearview I-295 | Throgs Neck approach to Hillside Ave | 3 (2-3 south of the GCP) | Asphalt | NJ barrier (to check) | W-beam, oaks, service roads beside it | 45 mph at the bridge, 40 mph at Hillside (WP), otherwise not tagged | Interstate green, truss gantries |
| Cross Island Pkwy | Whitestone/Throgs end south to the GCP and beyond | 3 (4 at interchange auxiliary lanes) | Asphalt | NJ barrier | Low concrete wall along Little Neck Bay, woods in Alley Pond Park | 50 mph | Parkway; green guide signs; passenger cars only |

Lane width is 12 ft (3.66 m) unless a row below says otherwise. That is the design width on the Clearview, the
BQE after its rebuild, the original Cross Island and the Van Wyck [NYCR]. The 1933 GCP slab was 44 ft for 4 lanes,
or 11 ft each [NYCR-GCP]. Its later widening probably moved it to 12 ft (to check).

## 1. BQE (I-278), LIE to the GCP

Northbound from LIE exit 17, the BQE runs north past Calvary Cemetery and turns east over New Calvary Cemetery.
It turns northeast to Queens Blvd, drops into a depressed section under the LIRR Main Line, and comes back onto a
viaduct near Broadway/Roosevelt. Past Northern Blvd it splits into east and west legs that join the GCP [WP-BQE].

| Stretch | Lanes/dir | Profile | Median / edge | Surface | Notes |
|---------|-----------|---------|---------------|---------|-------|
| LIE interchange to Queens Blvd (lat 40.73-40.74) | 3 (4 in places) | Viaduct, OSM `layer=1` [OSM] | Low NJ median barrier, NJ parapets [SV-2] | Concrete or asphalt in a mix [OSM]; light grey asphalt [SV-2] | Open views of low-rise Maspeth/Woodside and cemeteries. The Park Ave viaduct was rebuilt as steel and concrete [NYCR-BQE] |
| Queens Blvd to Broadway/Roosevelt (40.74-40.75) | 3 | Depressed: OSM `cutting`, at grade, and short `layer=1` sections | NJ median; concrete retaining walls with brick apartments above [SV-3] | Concrete slabs [SV-3] | Passes under the LIRR Main Line [WP-BQE]. Graffiti on the rail bridge [SV-3] |
| Broadway to Northern Blvd (40.75-40.76) | 3 | Back onto a viaduct [WP-BQE] | NJ | Asphalt [OSM] | Exit 41 is a SPUI, with a 9-span bridge [NYCR-BQE] |
| East and west legs (40.76-40.77) | 2 + 2 | At grade, mostly | Legs are separate roadways | Asphalt [OSM] | Only the west leg is signed I-278 [WP-BQE]. WP says "four lanes each" leg; OSM tags 2 per direction |
| GCP overlap to the RFK (40.77-40.78) | 4 | `layer=1` [OSM] | NJ | Asphalt, 40 mph [OSM] | Roadbed lowered in 2017 for a 14 ft clearance [WP-GCP] |

- **Shoulders:** most of the length has none. The 1960s-70s rebuild "did not bring breakdown shoulders". The
  2000s Woodside project (12-ft lanes) added shoulders, retaining walls, lights and signs [NYCR-BQE].
- **Ramps:** many ramps have no acceleration lane and use STOP signs [NYCR-BQE] (could be from an older era; to
  check).
- **Lamps:** single-arm davits on the parapets and the median [SV-2]. Height is about 40 ft (to check).
- **Overpasses:** steel or concrete girder street bridges. The design of the arches and brick walls "very varied
  from section to section" [NYCR-BQE] (that is mostly Brooklyn; Queens is to check).

**Local exits in the modelled stretch (northbound numbering) [WP-BQE]:**

| Exit | Destination | Notes |
|------|-------------|-------|
| 35 | I-495 LIE / 48th St / Greenpoint Ave | Signed 35A (west), 35B (east). This is LIE exits 17W/17E |
| 39 | NY 25 Queens Blvd / 65th Pl / 58th St | 39E/39W westbound |
| 40 | Broadway / Roosevelt Ave | |
| 41 | NY 25A Northern Blvd | SPUI (formerly exit 38) |
| 42 | To GCP east, LaGuardia | EB exit, WB entrance (eastern leg) |
| 43 | 30th Ave | EB exit, WB entrance |
| 44 | Astoria Blvd west | EB exit, WB entrance |
| 4 (GCP) | GCP east, LaGuardia | WB exit; the I-278/GCP overlap starts here, exit number not signed |
| 45 | 31st St / Astoria Blvd | GCP western end; last EB exit before the toll |

## 2. Grand Central Parkway, RFK approach to the Nassau line

History [NYCR-GCP]: Kew Gardens to Glen Oaks opened in 1933 with 4 lanes and no divider. The Triborough to Kew
Gardens link opened in 1936 with 6 lanes and a pedestrian esplanade. The 1960-63 World's Fair rebuild widened
Astoria to Kew Gardens from 6 to 8 lanes and Kew Gardens to Glen Oaks from 4 to 6. Through Briarwood the service
roads were **cantilevered over the parkway**, and the old wooden lamp posts were replaced with metal "crookarm"
and "bigloop" posts.

| Stretch | Lanes/dir [OSM] | Profile | Median / edge | Surface | Scenery |
|---------|-----------------|---------|---------------|---------|---------|
| RFK to exit 4 (I-278 overlap, lon -73.92 to -73.90) | 3 (+1-2 aux) | At grade between grass banks and stone/concrete walls (to check) | Tall NJ median with single-arm lamps on it [SV-1] | Light concrete with asphalt patches [SV-1] | Astoria rowhouses above the banks; Astoria Blvd as a service road |
| Exit 4 to LaGuardia (-73.89 to -73.86) | 4-5 | At grade | NJ median; NJ barrier between the main line and the ramps [SV-5] | Asphalt | LaGuardia Terminal C garage beside the road, truss sign gantries, airport lamp masts [SV-5]. Twin stone-faced gas stations east of LGA, in East Elmhurst [NYCR-GCP] |
| Flushing Meadows / LIE (-73.85 to -73.84) | 4 | At grade through the park; C/D splits | NJ | Asphalt | Park, Unisphere sightline (to check) |
| Kew Gardens interchange (-73.83 to -73.82) | 2-4, split roadways | Bridges, OSM `layer` up to 3 | NJ | Concrete [OSM] | 40 mph limit [NYCR-GCP]; old "Pretzel" |
| Briarwood / Jamaica Hills (-73.81 to -73.79) | 3 | **Trench**: tall tan concrete retaining walls (about 5-6 m, to check), with service roads cantilevered overhead [SV-4][NYCR-GCP] | NJ median | Dark asphalt | Trees and a railing on top of the walls |
| Utopia Pkwy to the Clearview stack (-73.78 to -73.76) | 3 (4 at exits) | At grade, `layer=3` at the stack | NJ | Asphalt | Median gas station (stone motif) just west of exit 20, Francis Lewis Blvd [NYCR-GCP]. This is the plan's "188th St" gas station (exit 19 is 188th St) |
| Hollis Hills to Alley Pond (-73.75 to -73.73) | 3 | At grade; viaduct at the CIP | NJ median with lamps; W-beam on the right [SV-6] | Asphalt | Dense deciduous woods on both sides [SV-6] |
| Alley Pond to the Nassau line (-73.72 to -73.71) | 3 | At grade | NJ | Asphalt | Sound walls near the county line [NYCR-GCP]. Continues as the Northern State Pkwy |

- **Bridges:** a 1930s-style triple-arch overpass at exit 11 (Jewel Ave / 69th Rd) [NYCR-GCP photo caption]. The
  Steinway St bridge in Astoria was a concrete arch, since replaced [NYCR-GCP]. The CIP interchange reuses many
  original 1930s structures [NYCR-GCP]. Stone facing on those is to check. Elsewhere there are 1960s steel and
  concrete girder spans with plain abutments (to check).
- **Lamps:** modern single-arm davits on the median NJ barrier [SV-1][SV-5][SV-6]. Height is about 40 ft (to check).
- **Signs:** NYSDOT green guide signs, ground-mounted or on trusses [SV-5][SV-6]. A parkway shield, and commercial
  vehicles banned east of exit 4 [WP-GCP]. Small trucks were allowed on the overlap from 2003 [NYCR-GCP].

**Exits [WP-GCP]:**

| Exit | Destination | Notes |
|------|-------------|-------|
| 45 | 31st St / Astoria Blvd | West end, at the RFK toll |
| 4 | I-278 west, BQE | All trucks exit here |
| 5 | Astoria Blvd / 82nd St, Terminal A | |
| 6 | 94th St, Terminal B parking | |
| 7 | LaGuardia, Terminals B and C | |
| 8 | 111th St | EB only |
| 9 | NY 25A Northern Blvd / Whitestone Expwy | |
| 9P | Flushing Meadows-Corona Park, USTA | No EB exit |
| 10 | I-495 LIE | Cloverleaf |
| 11 | 69th Rd / Jewel Ave | |
| 12 | NY 25 Queens Blvd / 78th Ave | EB only |
| 13 | I-678 south, Van Wyck, JFK | Kew Gardens interchange |
| 14 | Jackie Robinson Pkwy / Union Tpke / NY 25 west | |
| 15 | Union Tpke | WB |
| 16 | Parsons Blvd / 164th St | EB only |
| 17 | Parsons Blvd / 168th St to I-678 south | WB only |
| 18 | Utopia Pkwy | No WB exit |
| 19 | 188th St | |
| 20A/B | Francis Lewis Blvd north/south | |
| 21 | I-295 north, Clearview / NY 24 / NY 25 Hillside Ave | Stack |
| 22 | Union Tpke | EB exit, WB entrance |
| 23 | Cross Island Pkwy / Winchester Blvd | |
| 24 | Little Neck Pkwy | Last exit before the Nassau line |

## 3. Van Wyck (I-678), LIE to Kew Gardens

| Stretch | Lanes/dir [OSM] | Profile | Median / edge | Surface | Scenery |
|---------|-----------------|---------|---------------|---------|---------|
| North of the LIE (Flushing, lat 40.74-40.76) | 3 | **Viaduct**, OSM `layer` 1-3, concrete. The SB viaduct is 40 ft high [NYCR-VW] | Parapets | Concrete | Citi Field and Flushing Bay. Outside the modelled stretch, but it is the LIE junction backdrop |
| LIE to Jewel Ave (40.73) | 3 + shoulder | At grade along the east edge of Flushing Meadows Park | W-beam separates the parallel ramp/C-D; wide grass [SV-7] | Asphalt | Park lawn and trees; tall single-arm davit lamps [SV-7] |
| Jewel Ave to KGI (40.72) | 3 | At grade, then onto KGI bridges (`layer=2`) | NJ (to check), W-beam [SV-8] | Asphalt, then concrete on the bridges [OSM] | Park trees; cantilever sign structures [SV-8] |

- South of KGI the road is depressed through Kew Gardens and Richmond Hill. Bridges there have "stone and brick
  facades" on their supports, and the retaining walls have facades too [NYCR-VW]. Only the work-zone tail sees this.
- Design: six 12-ft lanes with 10-ft shoulders. Steel median guardrail in the early 1960s, concrete Jersey barrier
  from the mid-1970s [NYCR-VW].

**Exits, northbound [WP-VW]:**

| Exit | Destination | Notes |
|------|-------------|-------|
| 6 | Jamaica Ave / Hillside Ave | South of KGI (work-zone side) |
| 7B (SB) / 8 (NB) | Main St / Union Tpke | |
| 7C (SB) / 9 (NB) | NY 25 west, Queens Blvd | No NB entrance |
| 10 | GCP west, LaGuardia, RFK | NB exit, SB entrance; this is GCP exit 13 |
| 7A | Jackie Robinson Pkwy west | SB exit, NB entrance |
| 11 | 69th Rd / Jewel Ave, Flushing Meadows | |
| 12A/12B | I-495 east/west, College Point Blvd | LIE exits 22C-D |

## 4. Clearview (I-295), Throgs Neck approach to Hillside Ave

Design [NYCR-CLV]: six 12-ft lanes. Planned depressed below street level from the LIE north to the bridge. Opened
in 1960 from the bridge to 73rd Ave (including the LIE junction), and in 1963 from 73rd Ave to Hillside (including
the GCP stack).

| Stretch | Lanes/dir [OSM] | Profile | Median / edge | Surface | Scenery |
|---------|-----------------|---------|---------------|---------|---------|
| Throgs Neck approach (lat 40.79-40.81) | 3 | Bridge approach viaduct, `layer=1`, 45 mph | Parapets | Asphalt, with concrete on the bridges | Water views of the East River and Little Neck Bay; Fort Totten |
| CIP to Northern Blvd (40.77-40.78) | 3 | Depressed below street level per the design; how deep is to check | NJ (to check) | Asphalt | Bayside houses; the Bay Terrace shopping centre to the east |
| Northern Blvd to the LIE (40.76) | 3 | Near grade at exit 5 [SV-9] | NJ median; W-beam on the right; service road alongside [SV-9] | Asphalt | Oak rows; truss gantry "EXIT 5 Northern Blvd, RIGHT LANE" [SV-9] |
| LIE to the GCP (40.73-40.75) | 3 (4 at the LIE) | At grade | NJ | Asphalt | Fresh Meadows, Cunningham Park |
| GCP to Hillside (40.72-40.73) | 2-3 | At grade, ends at a signalised intersection | | Asphalt | 40 mph speed trap at Hillside [WP-CLV] |

Lamps: the 2000s exit 4 rebuild upgraded lighting, signs and guardrail [NYCR-CLV]. Single-arm davits (to check).
Trucks park on the shoulders north of the LIE before dawn [NYCR-CLV]. This is optional scenery.

**Exits, northbound [WP-CLV]:**

| Exit | Destination | Notes |
|------|-------------|-------|
| — | NY 25 Hillside Ave / NY 24 | South end, at-grade |
| 1 | GCP | Stack; GCP exit 21 |
| 2 | Union Tpke | |
| 3 | 73rd Ave | SB exit, NB entrance |
| 4E/4W | I-495 LIE | LIE exits 27S-N |
| 5 | NY 25A Northern Blvd | |
| 6A | 35th Ave | Signed 6 NB |
| 6B | 26th Ave | SB exit, NB entrance |
| 7 | Willets Point Blvd | NB exit; last NB exit before the toll |
| 8 | Cross Island Pkwy south | No NB exit; CIP exit 33 |

## 5. Cross Island Parkway, Whitestone to the GCP and beyond

Built in the late 1930s with four 12-ft lanes and a wide grass median. Widened to 6 lanes in the late 1940s (WP
says 1952). Original details were dark main lanes, light-coloured ramps, and timber lamp posts. Twin stone-faced gas
stations stood south of exit 30 and were demolished in the late 1970s [NYCR-CIP][WP-CIP]. Exits continue the Belt
Parkway numbering, 25A to 36.

| Stretch | Lanes/dir [OSM] | Profile | Median / edge | Surface | Scenery |
|---------|-----------------|---------|---------------|---------|---------|
| Whitestone to Utopia Pkwy (lat 40.79) | 3 (2 on some ramps) | At grade, built on the old LIRR Whitestone branch [NYCR-CIP] | NJ | Asphalt | East River shore, Whitestone and Throgs Neck bridge views |
| Little Neck Bay, Fort Totten to Bayside (40.77-40.78) | 3 | At grade on the shore | NJ median with lamps; low white concrete wall on the bay side; pedestrian bridges overhead [SV-10] | Asphalt | Bay to the east, the Joe Michaels Mile bike path, Bayside Marina |
| Northern Blvd to the LIE (40.75-40.76) | 3 | At grade | NJ | Asphalt | Alley Pond Park wetlands |
| Alley Pond Park, LIE to the GCP (40.74-40.75) | 3 (+aux) | At grade, wooded; flyovers at both junctions | Low concrete barrier and W-beam; woods to the curb [SV-11] | Asphalt | Dense deciduous forest [SV-11] |
| South of the GCP (40.70-40.73) | 3 | At grade | NJ | Asphalt | Glen Oaks, Bellerose, Belmont (work-zone tail) |

**Exits, northbound [WP-CIP]:**

| Exit | Destination | Notes |
|------|-------------|-------|
| 28A | NY 25B Hillside Ave | South of the modelled end |
| 28B | Union Tpke | |
| 29E/29W | GCP | GCP exit 23 |
| 30E/30W | I-495 LIE | No SB access to I-495 west; LIE exits 31S-N |
| 31E/31W | NY 25A Northern Blvd | |
| 32 (NB) / 33 (SB) | I-295 south, Clearview / Bell Blvd | No NB access to I-295 |
| 33 | I-295 north, Throgs Neck Bridge | NB exit, SB entrance; Clearview exit 8 |
| 34 | 160th St / Utopia Pkwy | |
| 35 | 14th Ave to Francis Lewis Blvd | No NB exit |
| 36N/36S | I-678 Whitestone Bridge / Expwy | North end |

## Junctions

The coordinates are the closest points between main-line OSM geometries. They replace the exit 17 coordinate in
the LIE landmark table (40.7261,-73.9026), which is about 1.9 km off (to check).

| Junction | Centre (OSM) | Layout | Levels | Link ways [OSM-L] | Flyovers / notes |
|----------|--------------|--------|--------|-------------------|------------------|
| LIE x BQE (LIE 17 / BQE 35) | 40.7344,-73.9224 | Full freeway-to-freeway; rebuilt 1966-on as "the largest such undertaking on the Interstate system" [NYCR-BQE]. Directional ramps mixed with 48th St / Greenpoint Ave and Laurel Hill Blvd ramps | 3 (`layer` up to 2) | 64 (mostly 1-lane, some 2-3) | Exact ramp set to check against the aerial: the LIE is elevated through Sunnyside/Maspeth, and the BQE viaduct crosses it |
| BQE x GCP, Astoria | 40.7678,-73.9047 (west leg joins) | "Y": BQE splits into east and west legs that merge into the EB and WB GCP. Astoria Blvd reachable from both [WP-BQE] | 2 | 10 | Legs are main-line roadways, not ramps. GCP exit 4 to I-278 is a 1-lane ramp [NYCR-BQE] |
| LIE x GCP (LIE 22A / GCP 10) | 40.7396,-73.8460 | **Cloverleaf**, opened 1937 for the 1939 Fair, widened 1963 [WP-GCP][NYCR-GCP] | 3 (`layer` 2) | 32 | Loops plus C/D lanes; the LIE service roads meet College Point Blvd at grade [NYCR-GCP] |
| LIE x Van Wyck (LIE 22C-D / VW 12) | 40.7432,-73.8376 | Stacked directional ramps beside the GCP cloverleaf; the Van Wyck viaduct rises to the north | 4 (`layer` up to 3) | 25 | The plan's "stacked ramps at exit 22". Ramp set to check against the aerial |
| GCP x Van Wyck, Kew Gardens (GCP 13-15 / VW 7-10) | 40.7163,-73.8263 | Kew Gardens Interchange ("the Pretzel"): five roads (GCP, Van Wyck, Jackie Robinson, Union Tpke, Queens Blvd). I-678 crosses over all of them [WP-VW] | 3 (`layer` up to 2) | 57 (15 two-lane) | Rebuilt in phases from 2010 [WP-GCP]: the NB VW to GCP exit moved from the left side to the right, stop-sign merges removed, GCP-VW ramps widened from 2 to 3 lanes [NYCR-VW][WP-VW]. Final flyover list to check |
| LIE x Clearview (LIE 27 / CLV 4) | 40.7444,-73.7713 | Late-1950s compact cloverleaf: tight single-lane loops, **no flyovers**, LIE service roads curve through it [NYCR-CLV]. 8 ramps rebuilt in the 2000s | 2 (`layer` 1) | 29 (27 one-lane) | Narrow footprint |
| GCP x Clearview (GCP 21 / CLV 1) | 40.7262,-73.7572 | **Four-level stack**, 1963, the only one in NYC [NYCR-CLV][WP-GCP]. Hillside Ave access was added in 2001 | 4 (`layer` up to 3 on the GCP main line) | 25 | Every movement is a directional ramp |
| LIE x CIP (LIE 31 / CIP 30) | 40.7533,-73.7449 | Rebuilt 2000-05 ($112-130 M) with **new flyover ramps and C/D roads** [NYCR-CIP][WP-CIP] | 3 (`layer` up to 2) | 38 | No SB CIP to WB LIE [WP-CIP] |
| GCP x CIP (GCP 23 / CIP 29) | 40.7439,-73.7323 | Inside Alley Pond Park. Original 1930s structures reused, plus a 1960s GCP main-line viaduct [NYCR-GCP]. Winchester Blvd ramps share it | 4 (`layer` up to 3) | 42 | Mostly loops and directional ramps; flyover set to check |

Ramp shapes for N1 come from the `motorway_link` fetch itself (the plan's decision 7). The way counts above only
give a rough measure of complexity.

## Street View checks (look-only, 2026-11-21)

| Key | Where | Capture | Link |
|-----|-------|---------|------|
| SV-1 | GCP EB, Astoria, at the BQE leg | Sep 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.768297,-73.905391&heading=80) |
| SV-2 | BQE NB viaduct, Maspeth | Sep 2022 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.736381,-73.908867&heading=20) |
| SV-3 | BQE NB, Woodside depressed section | Sep 2024 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.749970,-73.897086&heading=10) |
| SV-4 | GCP EB trench, Briarwood | Sep 2025 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.715864,-73.809976&heading=70) |
| SV-5 | GCP EB at LaGuardia Terminal C | Sep 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.769413,-73.867098&heading=100) |
| SV-6 | GCP EB, Hollis Hills (near the stack) | Sep 2025 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.730234,-73.749340&heading=60) |
| SV-7 | Van Wyck SB, Flushing Meadows | Jun 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.730328,-73.834205&heading=200) |
| SV-8 | Van Wyck NB, north of KGI | Jun 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.723809,-73.828579&heading=20) |
| SV-9 | Clearview SB at exit 5 | Nov 2024 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.764356,-73.780023&heading=180) |
| SV-10 | CIP SB, Little Neck Bay | Oct 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.77896,-73.76838&heading=165) |
| SV-11 | CIP SB, Alley Pond Park | Oct 2026 | [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.749905,-73.740419&heading=180) |

No aerial measurements were taken in this round. Shoulder widths, lamp heights and spacing, and wall heights all
remain to check with the Google Maps measure tool.

## Open-licence photo sources (not yet pulled)

| Commons category | Note |
|------------------|------|
| [Grand Central Parkway](https://commons.wikimedia.org/wiki/Category:Grand_Central_Parkway) | 13 sub-categories |
| [Cross Island Parkway](https://commons.wikimedia.org/wiki/Category:Cross_Island_Parkway) | 272 files, many 2024-06 road views |
| [Kew Gardens Interchange](https://commons.wikimedia.org/wiki/Category:Kew_Gardens_Interchange) | 96 files, includes 2024-06 views from the 82nd Ave footbridge |
| Interstate 278 / 678 / 295 in Queens | Category names to check; `Van_Wyck_Expressway` and `Clearview_Expressway` are empty on Commons |

Existing LIE photos already cover two junctions: [`../lie-reference/photos/exit-17/`](../lie-reference/photos/exit-17/)
(BQE) and [`../lie-reference/photos/exit-22/`](../lie-reference/photos/exit-22/) (`I-495_NY_x_I-678_1.jpg`).
