# Freeway full-closure work zone: specs (N0)

> Written by AI (Claude), 2026-11-21. Owner to review. Numbers are quoted from the MUTCD and NYSDOT texts
> listed below unless marked "(industry typical)" or "(to check)".

This is the spec for the dead-end work zones in decision 2 of the Queens network plan
([`tasks/todo.md`](../../tasks/todo.md)): ROAD WORK AHEAD and LANE CLOSED signs, a cone or drum taper that
merges the lanes, then a full-width concrete-barrier wall with ROAD CLOSED boards and an arrow board, plus a
median U-turn gap. Landmarks are in [`landmarks.md`](landmarks.md).

## Sources and editions

- **[M09]** MUTCD 2009 Part 6, read on [mutcd.fhwa.dot.gov](https://mutcd.fhwa.dot.gov/htm/2009/part6/part6c.htm)
  (chapters 6C, 6F, 6H and Figure 6F-6).
- **[M23]** MUTCD **11th Edition (December 2023)** Part 6, from the [FHWA PDF](https://mutcd.fhwa.dot.gov/pdfs/11th_Edition/part6.pdf),
  text extracted with `pdftotext`. FHWA now lists the **11th Edition with Revision 1 (December 2025)** as
  current. Revision 1 changes to Part 6 were not checked (to check).
- **[NY]** NYSDOT Section 619 standard sheets (Westchester County copy:
  [NYSDOTSec619Details.pdf](https://publicworks.westchestercountyny.gov/images/stories/pdfs/NYSDOTSec619Details.pdf)).
  Sheets 619-01 (Temporary Concrete Barrier, effective 01/07/10), 619-10 (Legends and Notes, effective
  01/08/09), 619-24 and 619-30 (lane closures). This is a secondary copy, and current revisions are to check
  on the [NYSDOT 619 standard sheets page](https://www.dot.ny.gov/part-ii-619-standard-sheets-filter).
- The 2023 edition **renumbered Part 6**. The values below were checked in both editions unless marked.

| Topic | 2009 number | 2023 number |
|-------|-------------|-------------|
| Advance warning sign spacing | Table 6C-1 (also 6H-3) | Table 6B-1 |
| Buffer space (stopping sight distance) | Table 6C-2 | Table 6B-2 |
| Taper length criteria / formulas | Tables 6C-3 / 6C-4 (also 6H-4) | Tables 6B-3 / 6B-4 |
| Sign sizes | Table 6F-1 | Tables 6G-1 (regulatory), 6H-1 (warning) |
| Channelizing devices | 6F.63-6F.71 | Chapter 6K (6K.01-6K.09) |
| Arrow boards | 6F.61, Figure 6F-6 | 6L.06, Figure 6L-3 |
| Warning lights | 6F.83 | 6L.07 |
| Temporary traffic barrier / crash cushions | 6F.85 / 6F.86 | 6M.02 / 6M.05 |
| Typical applications (lane closure on divided highway, median crossover) | TA-33, TA-39 | Figures 6P-33, 6P-37, 6P-39 |

## 1. Layout of a freeway lane closure, upstream to downstream

```mermaid
graph LR
  S1["W20-1 ROAD WORK 1 MILE"] -->|C 2640 ft| S2["W20-5 RIGHT LANE CLOSED 1/2 MILE"]
  S2 -->|B 1500 ft| S3["W4-2 lane ends"]
  S3 -->|A 1000 ft| T["Arrow board (Type C) on shoulder + merging taper L"]
  T --> BUF["Longitudinal buffer (Table 6B-2)"]
  BUF --> WZ["Work space / closure: Type 3 barricades + R11-2, barrier wall"]
```

### 1.1 Advance warning sign spacing (freeway)

[M09] Table 6C-1 and [M23] Table 6B-1, same values. [NY] Table NY6H-3 has the same values.

| Road type | A | B | C |
|-----------|---|---|---|
| Urban (low speed) | 100 ft | 100 ft | 100 ft |
| Urban (high speed) | 350 ft | 350 ft | 350 ft |
| Rural | 500 ft | 500 ft | 500 ft |
| **Expressway / Freeway** | **1,000 ft (304.8 m)** | **1,500 ft (457.2 m)** | **2,640 ft (804.7 m)** |

- **A** is the distance from the transition (start of the taper) to the **nearest** sign. **B** is the
  distance from that sign to the next one, and **C** from the second sign to the third. On a freeway the
  three signs therefore stand at 1,000, 2,500 and 5,140 ft (304.8, 762.0 and 1,566.7 m) before the taper.
- [NY] gives sign legends by road type. On an expressway or freeway, XX = "1 MILE" and YY = "1/2 MILE".
- [M23] 6C: on freeways "the advance warning sign placement should extend ... as far as ½ mile or more".
- **Plan note:** decision 2 uses 1,500 ft and 500 ft. That is a compact choice, shorter than MUTCD (5,140 /
  2,500 / 1,000 ft). For the 1.7:1 compression, a scaled spacing would be about 3,020 / 1,470 / 590 ft. This
  is the owner's call.

### 1.2 Taper length

[M09] Table 6C-4 and [M23] Table 6B-4:

$$
L = \begin{cases}
\dfrac{W\,S^{2}}{60} & S \le 40\ \text{mph} \\[2ex]
W\,S & S \ge 45\ \text{mph}
\end{cases}
$$

- $L$ is the taper length in ft, $W$ the width of the offset in ft (one lane, usually 12 ft), and $S$ the
  posted speed limit, the off-peak 85th-percentile speed before work, or the expected operating speed, in mph.
- [NY] uses the **pre-construction posted speed limit** for $S$.

Taper types ([M09] Table 6C-3, [M23] Table 6B-3):

| Taper | Length |
|-------|--------|
| **Merging** | at least $L$ |
| Shifting | at least $0.5L$ |
| Shoulder | at least $0.33L$ |
| One-lane, two-way | 50 ft min, 100 ft max |
| Downstream | 2009: about 100 ft per lane. **2023: 50 ft min, 100 ft max**, devices about 20 ft apart |

Merging taper for one 12 ft lane ($W = 12$):

| $S$ (mph) | $L = 12S$ (ft) | metres | Max device spacing in taper ($S$ ft) | Devices per taper |
|-----------|----------------|--------|--------------------------------------|-------------------|
| 45 | 540 | 164.6 | 45 ft | 13 |
| 50 (GCP posted, OSM) | 600 | 182.9 | 50 ft | 13 |
| 55 | 660 | 201.2 | 55 ft | 13 |
| 60 | 720 | 219.5 | 60 ft | 13 |
| 65 | 780 | 237.7 | 65 ft | 13 |

Devices per taper is $L / (S \text{ spacing}) + 1 = 12 + 1$ at any speed when $W = 12$ ft.

**Closing several lanes** ([M23] 6N.11 ¶12, Figure 6P-37): move traffic **one lane at a time**. Separate the
tapers by a tangent of **$2L$**, and use **one arrow board per closed lane** ([M23] 6L.06 ¶21). The first
board goes on the shoulder at the start of the first taper. The second goes in the first closed lane at the
start of the second taper (¶22). Example: 3 lanes down to 1 at 50 mph needs $L + 2L + L = 4L = 2{,}400$ ft
(731.5 m) of transition.

**Full closure of a divided freeway:** MUTCD has no "dead end" application. The real-world pattern is a
**median crossover** to two-lane, two-way operation ([M23] 6N.15, Figure 6P-39), or an exit-ramp detour. The
plan's U-turn gap is a crossover. [M23] 6N.15 says:
- Keep the lane-drop tapers separate from the crossover.
- Design the crossover for no less than 10 mph below the posted speed.
- Extend channelizing devices $2S$ ft past the downstream end of the transition ([M23] 6K.01 ¶05).

### 1.3 Channelizing device spacing

| Where | Max spacing | Source |
|-------|-------------|--------|
| Taper | $1.0 \times S$ ft (50 ft at 50 mph) | [M09] 6C.08, [M23] 6K.01 ¶04 |
| Tangent | $2.0 \times S$ ft (100 ft at 50 mph) | [M23] 6K.01 ¶04 |
| Downstream taper | about 20 ft | [M23] 6B.08 ¶12 |
| Devices replacing lane lines | $S/2$ ft | [M09] TA-32 note 3 |
| **NYSDOT, active work space** | **40 ft centre to centre** | [NY] 619-24, 619-30 notes |
| Device extension past a crossover | $2.0 \times S$ ft | [M23] 6K.01 ¶05, [M09] TA-39 |

### 1.4 Longitudinal buffer space

[M09] Table 6C-2 and [M23] Table 6B-2, same values. [NY] Table 6C-2 is the same.

| Speed (mph) | 45 | 50 | 55 | 60 | 65 | 70 |
|-------------|----|----|----|----|----|----|
| Buffer (ft) | 360 | 425 | 495 | 570 | 645 | 730 |

[NY] "No work activity, equipment, or storage ... shall occur within the buffer space at any time."

## 2. Signs

Warning signs are **black on orange** (fluorescent orange is allowed). ROAD CLOSED (R11-2) is a white
regulatory sign with black text ([M23] 6F.01).

| Code | Legend | Freeway size (in) | Freeway size (m) | Conventional | Source |
|------|--------|-------------------|------------------|--------------|--------|
| W20-1 | ROAD WORK (AHEAD / XX FT / 1 MILE) | 48 x 48 diamond | 1.219 x 1.219 | 36 x 36 | [M09] Table 6F-1, [M23] Table 6H-1 |
| W20-5 / W20-5a | RIGHT (LEFT) LANE CLOSED (AHEAD / distance); 5a = lanes | 48 x 48 | 1.219 x 1.219 | 36 x 36 | same |
| W4-2 (R/L) | Lane Ends (symbol) | 48 x 48 | 1.219 x 1.219 | 36 x 36 | same |
| W20-3 | ROAD CLOSED (AHEAD / distance), warning | 48 x 48 | 1.219 x 1.219 | 36 x 36 | same |
| **R11-2** | **ROAD CLOSED**, regulatory, rectangle | no freeway column: **48 x 30** | 1.219 x 0.762 | 48 x 30 | [M09] Table 6F-1, [M23] Table 6G-1 |
| W1-6 | Large Arrow, one direction | 60 x 30 | 1.524 x 0.762 | 48 x 24 | [M23] Table 6H-1 |
| W1-8 | Chevron Alignment | 30 x 36 | 0.762 x 0.914 | 18 x 24 | [M23] Table 6H-1 |
| G20-2 | END ROAD WORK | (to check) | | | [NY] 619-30 |
| **NYR9-11** | **LICENSE SUSPENDED AFTER TWO WORK ZONE SPEEDING TICKETS** (NY only) | **48 x 84** | 1.219 x 2.134 | 24 x 42 | [NY] 619-10/11 sign table |

- **R11-2 use** ([M23] 6G.04): place it "at or near the center of the roadway on or above a Type 3
  Barricade". It "shall not be used where road user flow is maintained through the TTC zone with a
  reduced number of lanes ... or where the actual closure is some distance beyond the sign". For the plan:
  put the R11-2 boards **on the wall**, not on the approach. On the approach use W20-3 ROAD CLOSED 1/2 MILE
  (to check against the plan's sign set).
- **Mounting height** ([M23] 6F.02):
  - At least **5 ft (1.52 m)** to the bottom of the sign at the roadside in rural areas.
  - **7 ft (2.13 m)** in urban areas or where parking or pedestrians are likely.
  - At least **1 ft** above the travelled way when on a barricade.
  - [NY] 619 portable sign supports: 5 ft minimum, measured from the bottom of the skid base.
- **Freeway practice:** signs on **both** shoulders for a multi-lane closure ([M23] 6F.02 ¶02 allows both sides
  "where special emphasis is needed").

## 3. Channelizing devices

### 3.1 Cones ([M09] 6F.64, [M23] 6K.03)

- Predominantly **orange**, and must not damage a vehicle that hits them.
- **At least 28 in (0.711 m) tall on freeways**, on other high-speed roads, and **at night on all roads**. The
  18 in minimum applies only to daytime and low-speed roads.
- **28-36 in cone:** a **6 in (152 mm) white band 3-4 in from the top**, plus a **4 in (102 mm) white band about
  2 in below** the 6 in band.
- **Cone taller than 36 in:** alternating orange and white stripes, 4-6 in wide, at least 2 of each, **top
  stripe orange**. Gaps between stripes are at most 3 in.
- Cones may be doubled up or ballasted (sandbag rings) to resist wind ([M23] 6K.03 ¶05-08).
- Base size (industry typical, to check): about 14 in square for a 28 in cone and 15 in for a 36 in cone. A 28
  in cone weighs 7-10 lb.

### 3.2 Drums ([M09] 6F.67, [M23] 6K.06)

- **At least 36 in (0.914 m) tall and at least 18 in (0.457 m) wide** in any orientation.
- Lightweight, deformable plastic. **Metal drums are not allowed.** NYSDOT banned steel drums earlier, in an
  engineering instruction (search result; the document was not read).
- **Horizontal, circumferential, alternating orange and white retroreflective stripes, 4-6 in wide.** At least
  2 orange and 2 white, **top stripe orange**. Non-reflective gaps are at most 3 in.
- **Closed top.** No ballast on top. Drain holes are used in freezing regions.
- Typical product (industry typical, to check): about 36-39 in tall, about 23 in wide at the base ring, with 4
  bands of 6 in (orange-white-orange-white from the top) on an orange body.
- **NY practice:**
  - Drums are the common freeway channelizer in New York. The 2008 public-domain photo at the GCP / Astoria
    Blvd underpass ([`photos/astoria-underpass/GCP_Astoria_Blvd_31st_St_BMT_jeh.JPG`](photos/astoria-underpass/GCP_Astoria_Blvd_31st_St_BMT_jeh.JPG))
    shows an eastbound drum taper.
  - The [NY] sheets call all of these "channelizing devices" and do not mandate drums on freeways in the
    text read. "Drums required on NY freeways" is therefore **observed practice, not a spec** (to check
    against the NYSDOT 619 Standard Specifications).
  - NYSDOT puts warning lights on the first two channelizers of a night lane-closure taper (search summary
    from workzonesafety.org, to check).

### 3.3 Tubular markers ([M23] 6K.04)

- Orange, at least 18 in tall and 2 in wide facing traffic. **At least 28 in on freeways and at night.**
- Under 42 in tall: two 3 in white bands, the top one within 2 in of the top, at most 6 in apart.
- 42 in or taller: four 4-6 in alternating stripes, top stripe orange.
- Use only where space is tight ([M23] 6K.04 ¶04).

### 3.4 Vertical panels ([M23] 6K.05)

- Striped face 8-12 in wide and at least 24 in tall.
- Orange and white diagonal stripes at 45 degrees, sloping down toward the side traffic passes on.
- 6 in stripes when the face is 36 in tall or more; 4 in allowed below that.

### 3.5 Type 3 barricades ([M09] 6F.68, [M23] 6K.07)

- **Rails 8-12 in wide**, **at least 48 in (1.219 m) long** for Type 3. Type 3 has three rails.
- Stripes are alternating orange and white retroreflective, **6 in (152 mm) wide**, **sloping down at 45
  degrees** toward the side road users pass on. 4 in stripes are allowed only on rails under 36 in.
- At least **270 sq in** of retroreflective area facing traffic on freeways.
- **Full closure stripe direction** (¶06-08):
  - Across a whole roadway, the stripes slope down toward the side drivers must turn to.
  - If both turns are allowed, they slope down both ways from the centre.
  - **If no turn is intended, they slope down toward the centre** of the barricades. **This is the case for
    the plan's dead-end wall.**
- Type 2 or Type 3 on freeways; Type 3 to close or partly close a road ([M23] 6K.07 ¶18).
- Signs on a Type 3 should not cover more than 50% of the top two rails or 33% of the total rail area
  ([M23] 6F.02 ¶14).
- Overall height: MUTCD gives none. [NY] barricade detail shows a **4'-0" minimum** rail length, an **8 in
  minimum** rail width and an overall height of about **5 ft** (to check: the sheet extraction is garbled).
  The warning light is fastened on top.
- Ballast with sandbags on the lower frame on high-speed roads ([M23] 6K.07 ¶13-14).

## 4. Arrow board ([M09] 6F.61 + Figure 6F-6, [M23] 6L.06 + Figure 6L-3)

| Type | Min size | Min legibility | Min elements | Use |
|------|----------|----------------|--------------|-----|
| A | 48 x 24 in | 1/2 mile | 12 | low-speed urban |
| B | 60 x 30 in | 3/4 mile | 13 | intermediate speed; mobile work on high speed |
| **C** | **96 x 48 in (2.438 x 1.219 m)** | **1 mile** | **15** | **high-speed, high-volume (this plan)** |
| D | arrow 48 in long, head 24 in wide | 1/2 mile | 12 | vehicle-mounted, arrow-shaped |

- **Flash rate: 25-40 flashes per minute.** Minimum element on-time is **50%** in flashing mode. Sequential
  modes use equal **25%** phases ([M23] 6L.06 ¶15).
- Elements are **yellow**. The board face is **non-reflective black**. Types A-C are solid rectangles (¶07-08,
  ¶13).
- **Dimming:** at least 50% dimming; the dimmed mode is used at night (¶11).
- **Mounting:** the bottom of the board at least **7 ft (2.13 m)** above the road (¶09), on a trailer or vehicle.
- **Placement:** on the **shoulder at the start of the merging taper**, or in the closed lane if the
  shoulder is narrow (¶19-20). One board per closed lane (¶21).
- **Modes** (¶16): (1) flashing arrow, sequential arrow or sequential chevron; (2) flashing double arrow;
  (3) flashing caution or alternating diamond. Arrow or chevron mode only for lane closures on multi-lane
  roads (¶17).
- [NY] 619-33 notes: the "mainline merging taper with the arrow panel at its starting point" is placed
  relative to the ramp (wording partly garbled). Shoulder-closure sheets show the arrow panel in caution mode.
- **Shader parameters:** a 15-lamp minimum matrix; period $T = 60/f$ s with $f \in [25, 40]$, so
  $T \in [1.5, 2.4]$ s; duty $\ge 0.5$. For example $f = 30$ gives $T = 2.0$ s, on for 1.0 s.

## 5. Warning lights ([M09] 6F.83, [M23] 6L.07)

- **Type A** low-intensity flashing: night use, visible at **3,000 ft** on a clear night. Typical on
  barricades and the first drums of a taper.
- **Type B** high-intensity flashing: day and night.
- **Type C** steady-burn: delineates the edge of the travelled way at night (also Type D, 360-degree).
- Minimum mounting height **30 in (0.762 m) to the bottom of the lens**.
- Max spacing equals the channelizing device spacing.

## 6. Temporary (portable) concrete barrier

- **MUTCD** ([M23] 6M.02):
  - Not a TTC device by itself.
  - When used to channelize, it **shall be supplemented with delineation, markings or channelizers**, with
    the delineation colour matching the lane line.
  - **It and its end treatments shall be crashworthy.**
  - Short intermittent segments are not allowed; segments must be connected.
  - Steady-burn lights may be mounted on it.
  - Details are in the AASHTO Roadside Design Guide (4th ed., 2011), chapters 8-9.
- **NYSDOT TCB, sheet 619-01** [NY]:
  - Height **2'-8" (32 in, 0.813 m)**, base width **2'-0" (24 in, 0.610 m)**. The profile appears to be a safety
    shape (New Jersey or F); which one is to check, because the sheet text is garbled.
  - Segment lengths **8, 10, 12, 14, 16, 18, 20 ft** (20 ft shown). Pinned connections.
  - Minimum installation radius 92 ft for 8 ft segments, 115 ft for 10 ft and 138 ft for 12 ft.
  - A **tapered end section** drops to a **6 in** end height.
  - Rebar A615 Grade 420.
- **NY flare rates for a positive barrier** ([NY] Table 619-4):

  | Speed (mph) | 30 | 40 | 50 | 55 | 65 |
  |-------------|----|----|----|----|----|
  | Temporary concrete barrier | 8:1 | 11:1 | 14:1 | 16:1 | 20:1 |
  | Box beam / heavy-post corrugated beam | 7:1 | 9:1 | 11:1 | 12:1 | 15:1 |

- [NY] 619-22 and 619-32 notes: **"THE TEMPORARY CONCRETE BARRIER SHALL NOT BE PLACED ALONG THE MERGING TAPER."** Use
  channelizers in the taper, then the wall after the buffer.
- **Repo reuse:** [`nj_profile()`](../../src/scene/Corridor/CorridorEdges.h) already gives the NJ shape. The
  TCB only needs segment joints every 20 ft and a 6 in tapered end.

## 7. Crash cushions and barrier vehicles

- **MUTCD** ([M23] 6M.05):
  - **Stationary crash cushions** protect the exposed ends of barriers and fixed objects. They are "designed for
    the specific application".
  - **Truck-mounted attenuators (TMA)** are energy absorbers on the rear of shadow trucks or trailers, placed
    upstream of the work.
  - All must be crashworthy and must be inspected and repaired after a hit.
- **NY barrier vehicle placement** ([NY] Table NY2-A, distance from the barrier vehicle to the work space):

  | Speed (mph) | 18,000 lb vehicle min / max | 24,000 lb vehicle min / max |
  |-------------|-----------------------------|-----------------------------|
  | > 55 | 165 / 200 ft | 100 / 200 ft |
  | 45-55 | 100 / 165 ft | 85 / 100 ft (to check: column order garbled) |
  | < 45 | 85 / 100 ft | 50 / 100 ft (to check) |

  Shadow vehicles (mobile work, Table NY2-B) sit farther back, 180-330 ft (to check).
- **Look:**
  - A TMA is a yellow-and-black chevron-striped cartridge on a dump truck that carries a Type C or B arrow
    board.
  - A stationary cushion at the wall end is a yellow sand-barrel array or a yellow-and-black striped
    telescoping attenuator (industry typical, to check).
  - The repo's attenuator colour and end boards can be reused (see the plan's code facts).

## 8. NYSDOT differences in one list

1. **NYR9-11 "LICENSE SUSPENDED AFTER TWO WORK ZONE SPEEDING TICKETS"** sign, 48 x 84 in on freeways, at the
   start of freeway work zones ([NY] 619-24, 619-30).
2. **G20-2 END ROAD WORK** at the end of every zone ([NY] 619-30).
3. **40 ft** maximum channelizer spacing in the active work space ([NY]).
4. **Barrier vehicles** (with TMA) are required in stationary freeway closures, with the placement table above
   ([NY] Tables NY1-A, NY2-A).
5. **Drums** are the common NY freeway channelizer (photo evidence; spec to check). Steel drums are banned.
6. **TCB not along the merging taper**, plus the flare rates above.
7. "Transverse devices" are required when a paved shoulder of 8 ft or more is closed for over 1,500 ft
   ([NY] 619-24 note 6).
8. $S$ in the taper formula is the **pre-construction posted speed** ([NY] 619-10).
9. **Advance warning sign spacing (Table NY6H-3):** NY splits urban roads into three bands, at 30 mph or less,
   35-40 mph and 45 mph or more (100 / 200 / 350 ft). The freeway row is the same as MUTCD.
10. [NY] standard-taper table (619-10): for a 12 ft shift the merging taper is 540 / 600 / 660 / 720 / 780 ft
    at 45 / 50 / 55 / 60 / 65 mph. This matches $L = WS$.

## 9. Build cheat sheet (GCP, 50 mph, 12 ft lanes; real units, before the 1.7:1 compression)

| Item | Value | WU (1 m = 1000 WU) |
|------|-------|--------------------|
| W20-1 ROAD WORK 1 MILE | 5,140 ft before the taper | 1,566,700 |
| W20-5 RIGHT LANE CLOSED 1/2 MILE | 2,500 ft before | 762,000 |
| W4-2 lane ends | 1,000 ft before | 304,800 |
| Arrow board Type C 96 x 48 in, 30 fpm | shoulder, start of each taper | 2,438 x 1,219 |
| Merging taper $L$ | 600 ft per lane | 182,880 |
| Tangent between tapers | $2L$ = 1,200 ft | 365,760 |
| Taper device spacing | 50 ft (NY: 40 ft in the work space) | 15,240 (12,192) |
| Tangent device spacing | 100 ft | 30,480 |
| Buffer | 425 ft | 129,540 |
| Drum | 36 in tall, 18 in min width, 4 x 6 in bands, top orange | 914 tall, 457 wide, 152 bands |
| 28 in cone | 6 in band 3-4 in below the top, then a 4 in band 2 in lower | 711 tall |
| Type 3 barricade | rails 8-12 in x 48 in min, 6 in stripes at 45 degrees sloping down to the centre | 203-305 x 1,219 |
| R11-2 ROAD CLOSED | 48 x 30 in, at the centre on the barricade, bottom at least 1 ft up | 1,219 x 762 |
| TCB | 32 in high, 24 in base, 20 ft segments, 6 in tapered ends | 813 x 610 x 6,096 |
| Warning light | Type A on the barricades and first drums, lens bottom at least 30 in up | 762 |

Use `utils/units/Units.h` (`ft()`) in code. Do not retype the factors.

Approximate render colours (to check against the FHWA colour tolerance charts, 23 CFR 655): orange sRGB about
(235, 100, 30); retroreflective white sheeting about (235, 235, 230); arrow-board amber lamp about
(255, 180, 40) emissive.
