# Queens network landmarks (N0)

> Written by AI (Claude), 2026-11-21. Owner to review. Items marked "(to check)" come from memory, one
> unclear photo, or an inference; confirm them on Street View before building.

Reference for decision 9 of the Queens network plan ([`tasks/todo.md`](../../tasks/todo.md), section
"Queens network"). One note per landmark, in the template from
[`docs/lie-reference/README.md`](../lie-reference/README.md), with a lat/lon and dimensions added. The work-zone
kit is in [`workzone.md`](workzone.md).

## Rules for images

Same rules as the LIE sheet. Street View is look-only: keep links and notes, never save screenshots or use
them as textures. Saved photos are openly licensed Commons files in `photos/<topic>/`, listed in
[`photos/CREDITS.md`](photos/CREDITS.md). Street View was not opened for this draft. The `pano` links below are
built from the coordinates and still need a look.

## Source keys

- **[OSM]** OpenStreetMap, read through the Overpass API on 2026-11-21. Way and node ids are given inline.
- **[WP-GCP]** Wikipedia, [Grand Central Parkway](https://en.wikipedia.org/wiki/Grand_Central_Parkway)
- **[WP-CIP]** Wikipedia, [Cross Island Parkway](https://en.wikipedia.org/wiki/Cross_Island_Parkway)
- **[WP-KGI]** Wikipedia, [Kew Gardens Interchange](https://en.wikipedia.org/wiki/Kew_Gardens_Interchange)
- **[WP-TNB]** Wikipedia, [Throgs Neck Bridge](https://en.wikipedia.org/wiki/Throgs_Neck_Bridge)
- **[WP-CF]** Wikipedia, [Citi Field](https://en.wikipedia.org/wiki/Citi_Field)
- **[ECR]** eastcoastroads.com, [GCP eastbound photo gallery](https://eastcoastroads.com/states/ny/parkways/grand/photogal/east) (April 2022 photos)
- **[NYCP]** NYC Parks, [Flushing Meadows Corona Park highlights](https://www2.nycgovparks.org/parks/Q099/highlights/19834), and the GCP gas-station concession listing (search result only; the page returned HTTP 401)
- nycroads.com refused the TLS handshake again (as on the LIE sheet), so it was not read.

## Summary

| # | Landmark | Road | Lat, lon | Status |
|---|----------|------|----------|--------|
| 1 | Cunningham Park Gulf station (median service area) | GCP, between exits 19 and 20 | 40.7250, -73.7718 | Verified (OSM + 2 photos) |
| 2 | Stone-arch bridge carrying the GCP over the CIP | CIP southbound at exits 29E-W | 40.7434, -73.7334 (to check) | Photo verified, exact bridge to check |
| 3 | Astoria underpass: Astoria Blvd (N/W) station over the GCP | GCP / I-278 at 31st St | 40.7700, -73.9180 | Verified (OSM + 2 photos) |
| 4 | Jamaica underpass: Jamaica Yard bridge over the GCP | GCP at Kew Gardens / Briarwood | 40.7171, -73.8235 | Verified (2 photos, photo GPS) |
| 5 | LaGuardia Airport edge | GCP exits 5-8 | 40.7729, -73.8716 (Terminal B) | Verified (OSM + 2 photos) |
| 6 | Citi Field and the USTA tennis centre | GCP exits 9-9P | 40.7570, -73.8459 (Citi Field) | Verified (OSM + 2 photos) |
| 7 | Kew Gardens Interchange | GCP x Van Wyck x Jackie Robinson | 40.7146, -73.8296 | Verified (WP + 1 photo) |
| 8 | Throgs Neck Bridge view | Clearview Expwy north end / CIP | 40.7853, -73.7867 (viewpoint) | Verified (WP + 1 photo) |
| 9 | Flushing Bay Promenade and World's Fair Marina | GCP between exits 8 and 9 | 40.7619, -73.8596 to 40.7663, -73.8629 | Verified (OSM + 2 photos) |

## 1. Cunningham Park Gulf station (GCP median service area)

- **Location:** 40.7250, -73.7718 [OSM way 166707836, `amenity=fuel`, `brand=Gulf`, `building=roof`, `layer=1`].
  It is in the **centre median** of the GCP inside Cunningham Park, between exit 19 (188th St, Jamaica
  Estates-Holliswood) and exit 20 (Francis Lewis Blvd) [WP-GCP], [ECR]. The plan said "by the 188th St exit":
  this is it, about 0.4 mi east of exit 19 (to check against the exit gore).
- **Access:** both directions, by **left-lane exits**. Eastbound the GCP is 5 lanes here with lane 1 tagged
  `Gas` (`turn:lanes` = `slight_left|none|none|none|slight_right`) [OSM way 1085410475]. Westbound has its own
  `destination=Gas` link [OSM way 1085410480]. Exit links back out are signed "Parkway East" and "Parkway West"
  [OSM ways 1085410474, 524565335]. **A driver can re-enter the opposite direction**, so the station is a
  legal U-turn (to check on Street View). This matches the plan's U-turn need.
- **Speed and lanes:** `maxspeed=50 mph`, 4 lanes each way either side of the station [OSM ways 49180750, 995506931].
- **What it looks like (photos):**
  - Canopy: long flat canopy with an **orange fascia band and a blue Gulf panel** with white "Gulf" lettering,
    on thin white columns. About 4 pump islands (to check).
  - Building: small **stone cottage-style kiosk** with a grey hipped roof and gable, lit by the canopy.
    This is the parkway's 1930s service-building style (to check: era).
  - Signs: pole sign with the round orange-and-blue Gulf disc, a price board, "LOW GAS" boards, a magenta
    "OPEN 24 HR" Dunkin' banner, and a white "WELCOME TO DUNKIN' DONUTS" board on the lawn.
  - Surroundings: mature oaks of Cunningham Park on both sides. A W-beam guardrail separates the station
    lawn from the through lanes. Tall single-arm galvanised lamp poles. A green guide gantry for exit 20 is
    just beyond.
- **Dimensions (to check on the aerial):** canopy about 30 m x 10 m; pad about 120 m long including the
  accel/decel lanes.
- **Visible from the road:** from the left lanes in both directions; the canopy is the landmark.
- **Concession:** NYC Parks lists a GCP gas station concession in the centre mall at Cunningham Park near the
  188th St exit, term 2023-2038 [NYCP search result; page not readable].
- **Second station (not the landmark):** a Gulf station with a Dunkin' on the **westbound side at LaGuardia**,
  40.7677, -73.8652 [OSM way 277479210], and a Mobil at 40.7650, -73.8617 [OSM way 277479207] (which side to
  check). Commons: "GCP WB Gas Station at LaGuardia.jpg" (DanTD, CC BY-SA 4.0, not saved).
- **Photos:** `photos/gcp-gas-station/` (2022-01-13 eastbound and 2018-11-12 eastbound, both Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7250,-73.7718)

## 2. Stone-arch bridge carrying the GCP over the Cross Island Parkway

- **Location:** the GCP / CIP interchange at Alley Pond Park, CIP exits 29E-W [WP-CIP]. Best estimate
  40.7434, -73.7334, where the GCP mainline bridges sit [OSM ways 40943971, 40943973]. Which of the stacked
  structures is the stone arch is **to check** on the aerial; OSM tags them `layer=3`, which does not fit an
  old arch.
- **How it was identified:** three Dougtone photos taken 24 Dec 2011, 13:18:22 to 13:18:49, in sequence on the
  **CIP southbound**. The first shows the gantry "Cross Island Pkwy / To Belt Pkwy / Kennedy Airport" (three
  down arrows) and "EXITS 29E-W / Grand Central Pkwy / Hauppauge / Triboro Br / EXIT ONLY". The next two show
  the arch, with a green **"GRAND CENTRAL PARKWAY"** name plate on its face.
- **What it looks like (photos):**
  - **Segmental (shallow) arch** over the full roadway (3 lanes plus shoulders, about 15-18 m clear span, to
    check). There is a ring of cut-stone voussoirs around the arch.
  - **Rough-faced ashlar** facing in grey, buff and brown random-course stone. A flat stone parapet with a
    coping course runs on top, and square stone-clad abutment piers sit at each side.
  - Yellow clearance plates: **10'-10"** at the crown and **10'-6"** near the haunches.
  - A plain steel-girder bridge is visible through the arch behind it.
- **Road under it:** CIP with a concrete Jersey median (double-yellow left edge line) and bare deciduous woods.
  A brick tower building with a cupola is on the right skyline (Creedmoor, to check).
- **Related stone bridge already built:** the stone-faced CIP ramp bridge at LIE exit 31
  ([LIE sheet, exit 31](../lie-reference/README.md)). Reuse the D4 stone parapet and pier cladding. Add
  an **arch profile** for this one.
- **Photos:** `photos/cip-stone-bridge/` (6817776221 gantry, 6817777037 and 6817777409 arch).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7434,-73.7334) (to
  check: may need to move north onto the CIP southbound).

## 3. Astoria underpass: Astoria Boulevard (N/W) station over the GCP

Chosen as the most iconic Astoria underpass. The GCP (co-signed I-278 here, the RFK/Triborough approach)
dips under 31st St and the elevated BMT Astoria Line, and the station spans it.

- **Location:** 40.7700, -73.9180. The station is [OSM node 3575982154] at 40.7704, -73.9177. Photo GPS is
  40.7700, -73.9182.
- **What it looks like (photos):**
  - The GCP runs in a **depressed cut** between tall concrete retaining walls (board-formed, water-stained,
    some graffiti). A green-painted steel-girder overpass with chain-link fencing carries 31st St and Astoria
    Blvd over the parkway.
  - Above, the **elevated station**: a dark green steel structure with a long **red-brown roof**. The 2008 photo
    shows an older red canopy. The 2023 photo shows the station after its 2019-2020 rebuild (to check).
  - Astoria Blvd north and south run as service roads at street level on both sides, with brick
    apartment buildings, shops and a red bike lane (2023 photo).
  - Lighting: tall double-arm galvanised poles in the median and at the edges.
- **Work-zone note:** the 2008 photo shows a **NYSDOT drum taper** (orange drums with white bands) and an
  orange diamond sign on the eastbound side here. This is useful for the RFK-approach work zone (decision 1
  puts a work zone at the GCP west end).
- **Other Astoria-area candidate:** the **East Elmhurst pillars**: tall square concrete pillars on both sides of
  the GCP and of the Astoria Blvd service roads in East Elmhurst (Commons description). In the photo they are
  about 8-9 m tall, flanking the road like gateposts. Origin and exact location: to check.
- **Photos:** `photos/astoria-underpass/` (jim.henderson 2008, public domain; Tdorante10 2023 from the
  station; DanTD pillars 2019).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7700,-73.9180)

## 4. Jamaica underpass: Jamaica Yard bridge over the GCP

Chosen as the most iconic Jamaica-area underpass. The NYCTA Jamaica Yard lead tracks cross over the GCP
just north of the Kew Gardens Interchange [WP-GCP]. The Van Wyck / Hillside Ave underpasses were not
researched (to check if the owner prefers one of those).

- **Location:** 40.7171, -73.8235 (photo GPS, 2019 eastbound photo). Jamaica Yard itself is at
  40.7190, -73.8290 [OSM node 7476947205]. A second photo was taken at 40.7169, -73.8300.
- **What it looks like (photos):**
  - **Two-span riveted steel plate-girder bridge** painted green, now weathered and rust-streaked. A
    walkway railing and a tall chain-link fence run on top.
  - **Granite ashlar abutments and a centre pier** in large rough-faced blocks.
  - Yellow clearance plates: **12'-7"** on one span, and about **10'-7"** on another (to check). A small green
    "NYCTA" plate.
  - A J Robinson Pkwy sign nearby reads "CLEARANCE 8'-8" / EXIT 20 MPH".
- **Road:** GCP concrete slab surface, W-beam guardrail, woods (Forest Park / Kew Gardens edge) on both sides.
  A guide sign "EXITS 11E-W / Jewel Ave / 69 Rd / 1/2 MILE" stands just beyond (westbound photo).
- **Photos:** `photos/jamaica-underpass/` (2017 and 2019, both Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7171,-73.8235)

## 5. LaGuardia Airport edge

- **Location:** the GCP runs along the south edge of LGA from about exit 5 (Astoria Blvd / 82nd St) to exit 8
  (111th St) [WP-GCP]. Exit 7 serves Terminals B and C [WP-GCP], [ECR].
  - Terminal B: 40.7729, -73.8716 [OSM way 644536343, 4 levels].
  - Terminal C: 40.7701, -73.8621 [OSM relation 20056349, `height=13.2` m].
  - Runway 4/22: 2134 m x 45.7 m. The runway 4 end is at 40.7692, -73.8841 [OSM way 5782325].
  - Runway 13/31: 2135 m x 46 m. The **runway 31 end is at 40.7721, -73.8571**, close to the GCP [OSM way 5782302].
  - OSM has `man_made=pier` ways at 40.7719, -73.8500 and 40.7800, -73.8505 east of the runway 31 end. These
    are probably the approach-light piers over Flushing Bay (to check).
- **What it looks like (photos):**
  - **Airport side:** a long low **white concrete barrier wall** topped by **chain-link fence**, with a grass
    strip and young trees between it and the GCP. The tail fins of parked jets (an American Airlines fin in
    the 2022 photo) show above the fence. The **Manhattan skyline** (Midtown supertalls) sits on the western
    horizon.
  - **Lamps:** tall tapered galvanised poles with a single curved arm; the overhead sign bridges are
    galvanised trusses.
  - **Terminal B (2020):** a long grey metal-panel building with a glass curtain-wall tower block. Giant
    vinyl ad banners hang on the facade. In front of it run **concrete arrivals/departures viaducts** on
    round piers, crossing over the GCP-side frontage road, with steel sign trusses.
- **Speed and signs:** GCP 50 mph (to check here). The plan's GCP west end (RFK approach) is about 3 km west.
- **Photos:** `photos/laguardia/` (044 Terminal B, 048 airport wall and skyline, both 2022-01-14, Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7700,-73.8700)

## 6. Citi Field and the USTA Billie Jean King National Tennis Center

- **Location:**
  - Citi Field: 40.7570, -73.8459 [OSM way 604023701]. Wikipedia gives 40.75694, -73.84583 [WP-CF].
  - Louis Armstrong Stadium: 40.7510, -73.8454 [OSM way 840573223, capacity 14,053].
  - Arthur Ashe Stadium: about 40.7499, -73.8470 [OSM way 284859611, name to check].
  - The GCP passes **west of Arthur Ashe Stadium**. Exit 9 (Northern Blvd / Whitestone Expwy) is just west of
    Citi Field. Exit 9P (Shea Rd) serves the USTA centre and has no eastbound exit [WP-GCP].
- **What it looks like (photos):**
  - **Citi Field:** a brick-and-steel ballpark with a dark steel upper structure, light towers along the roof
    line, and a low wide profile behind a park lawn and bare trees. It is seen across the opposite carriageway
    and a double W-beam guardrail. The facade recalls Ebbets Field [WP-CF]. Facade materials: red brick and
    limestone (to check). Capacity 41,922 [WP-CF].
  - **USTA:** the big round stadium beside the GCP, with a **pale concrete ring facade** with vertical ribs
    and a **white roof** structure. The photo caption says "Grandstand", but the shape and roof fit
    **Arthur Ashe Stadium** (to check). Tall stadium-light masts and dark pines are in front.
- **Road at this point (2022-01-14 photo, westbound/northbound):** 4 lanes plus a left exit. Overhead
  gantry: "EXIT 9W / WEST 25A Northern Blvd / 3/4 MILE / LEFT EXIT", a yellow "PKWY DIVIDES" panel, and
  "EXIT 9E / EAST 25A Northern Blvd / 678 Whitestone Expwy / 1/2 MILE". A concrete overpass is ahead. A
  yellow "10'-7"" low-clearance sign stands near Citi Field.
- **Photos:** `photos/citi-field-usta/` (025 Citi Field, 017 USTA, both 2022-01-14, Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7520,-73.8490)

## 7. Kew Gardens Interchange

- **Location:** 40.7146, -73.8296 (Wikipedia and photo GPS 40.7154, -73.8296).
- **Roads:** GCP, I-678 Van Wyck, Jackie Robinson Pkwy, Union Tpke and Queens Blvd (NY 25). On the GCP these
  are exits 13-15 [WP-GCP], [WP-KGI].
- **Layout:** an incomplete junction, nicknamed "The Pretzel" and "The Maze". The original 1930s layout was a
  distorted trumpet between the GCP and the Jackie Robinson (then Interboro) Pkwy, with Union Tpke as a
  service road; the Van Wyck links came later [WP-KGI].
- **Rebuild:**
  - Phase 1: 2010-2016, $159M. Queens Blvd viaduct, four new bridges.
  - Phase 2: a new viaduct and a **new flyover for the northbound I-678**.
  - Phases 3-4: ramps and widening.
  - All done 2022, $739M total, carrying about 600,000 vehicles a day [WP-KGI].
- **What it looks like (Phase 4 photo, 2021):** new **curved steel plate-girder flyovers painted light blue**
  with plain concrete decks and parapets, on concrete hammerhead piers. Concrete Jersey barriers, MSE
  retaining walls, galvanised single-arm lamps. Under construction in the photo: drainage structures and
  cones.
- **Build note:** this is decision 6's "Kew Gardens interchange" junction. Take the levels from OSM `layer`
  tags (N1).
- **Photos:** `photos/kew-gardens/` (2021-02-17 093, Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7146,-73.8296)

## 8. Throgs Neck Bridge view (Clearview Expressway north end / CIP)

- **Bridge facts [WP-TNB]:**
  - Suspension bridge, opened 11 Jan 1961.
  - **Main span 1,800 ft (549 m)**; 2,910 ft between anchorages; 11,250 ft total.
  - **Towers 326 ft above the tower islands** (346 ft above mean high water). They are closed-box steel
    with arched top struts and blocky tops.
  - 142 ft clearance below. Six lanes (3 each way).
  - Centre 40.802, -73.793.
- **Queens approach:** joins the Clearview Expwy (I-295) southbound and the CIP southbound. There is no direct
  link to the CIP northbound [WP-TNB]. The approach viaduct starts at 40.7902, -73.7922 [OSM way 40595062,
  2 lanes]; the main bridge way is 3 lanes [OSM way 40595084].
- **The view (2019 photo, Clearview northbound at 40.7853, -73.7867):**
  - Galvanised overhead truss: "NORTH 295 Throgs Neck Br Bronx" with two down arrows. Under it, a yellow
    "TOLL BRIDGE" panel with a white "E-ZPass ACCEPTED" panel, and a white toll-rate sign (2019: cars $6.12
    NY E-ZPass, $9.50 others).
  - The road rises onto the **elevated approach** between concrete barriers. Tall galvanised single-arm
    lamps line both sides.
  - Red-roofed low-rise apartment blocks are on the left; a residential tower is on the right; ivy-covered
    walls.
  - The bridge towers themselves are not in frame. The tower paint colour is to check.
- **Build note:** the Clearview north end is a work zone (decision 1). Put the work zone before the toll gantry
  and show the towers on the horizon card beyond the wall (decision 12).
- **Photos:** `photos/throgs-neck/` (2019-05-25 30, Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7853,-73.7867)

## 9. Flushing Bay Promenade and World's Fair Marina

- **Location:** the promenade runs between the GCP and Flushing Bay, from 40.7619, -73.8596 to
  40.7663, -73.8629 in OSM [OSM ways 24806250, 25335572, 46958819; `highway=cycleway`, paving stones and
  asphalt]. World's Fair Marina is at 40.7623, -73.8544 [OSM relation 5713700], with Piers 1-3.
- **Facts:** about **1.4 mi** long, from LaGuardia (27th Ave overpass) to Harper St and the boat ramp. It was
  renovated in 1999. The marina began as the 1939 World's Fair boat basin and was enlarged to about 800 boats
  for 1964 [NYCP]. Pier 1 and Pier 3 reconstruction is planned or underway (date to check).
- **What it looks like (photos):**
  - **From the GCP:** a black steel picket fence behind a W-beam guardrail. Rows of **shrink-wrapped white
    boats on cradles** (winter storage) and sailboat masts. Bare trees and a single-arm lamp.
  - **From the water's edge:** a wide flat grey bay with rip-rap at the shore. Citi Field's light towers and
    sign on the far right shore, and the low skyline of Corona / Jackson Heights across the water.
- **Build note:** a water plane plus a marina strip on the north side of the GCP between exits 8 and 9.
  Reuse the shoreline material from the LIE kit if it has one (to check).
- **Photos:** `photos/flushing-bay/` (Flushing_Bay.jpg 2010 Transpoman; 032 marina 2022 Tdorante10).
- **Street View:** [pano](https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=40.7640,-73.8610)

## Open checks for the owner (Street View)

1. Gulf station (#1): confirm the opposite-direction re-entry (U-turn) and the canopy size.
2. Stone arch (#2): pin down which GCP structure it is, and measure its span.
3. Jamaica (#4): confirm the clearance on the second span. Say whether a Van Wyck / Hillside underpass is
   preferred instead.
4. USTA (#6): confirm the stadium in the photo is Arthur Ashe.
5. LGA (#5): confirm that the piers east of runway 31 are the approach lights, and whether they are visible
   from the GCP.
