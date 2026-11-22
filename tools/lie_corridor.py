#!/usr/bin/env python3
"""lie_corridor.py — Phase 3a offline pipeline: real OSM data -> engine corridor asset.

    OSM extract (Overpass, one-time)
      -> SUMO netconvert (lane-level geometry, plain XML in lon/lat)
        -> local tangent-plane projection + landmark-anchored compaction
          -> assets/corridor/lie_corridor.bin  (checked in)

Everything is driven by config/lie_corridor.toml.  Nothing about the road's
shape is authored here: the centreline, the lane counts and the interchange
positions all come from OpenStreetMap.  The only authored numbers are the
cross-section dimensions and the compaction targets.

Subcommands
    fetch  one-time Overpass pull        -> data/osm/lie_i495.osm.xml
    fetch_srvr  one-time service-road pull -> data/osm/lie_service_roads.json
    fetch_xing  one-time crossings pull    -> data/osm/lie_crossings.json  (D4)
    net    run SUMO netconvert           -> data/osm/lie_i495.{net,edg,nod}.xml
    elev   one-time USGS 3DEP pull       -> data/dem/lie_i495_elev.csv
    bake   project + compact + serialise -> assets/corridor/lie_corridor.bin
    all    fetch (if missing) + net (if missing) + bake

Only `bake` is needed to reproduce the asset from the cached extract, and it
needs neither the network nor SUMO — that is the point of caching both.

Dependencies: Python 3.11+ stdlib only (tomllib, xml.etree, urllib).
"""

from __future__ import annotations

import argparse
import bisect
import json
import math
import os
import re
import shutil
import struct
import statistics
import subprocess
import sys
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from types import SimpleNamespace

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 1 scene-metre = 1000 world units (see CLAUDE.md "Units").
WORLD_SCALE = 1000.0
FT_M = 0.3048
MILE_M = 1609.344
# WGS84 semi-major axis; the local tangent plane below only needs one radius.
EARTH_R = 6378137.0

ASSET_MAGIC = 0x53574C43  # "SWLC"
EXIT_TAG = 0x54495845      # "EXIT"
DISTRICT_TAG = 0x54534944  # "DIST"
SRVR_TAG = 0x52565253      # "SRVR"
SURF_TAG = 0x46525553      # "SURF"
XING_TAG = 0x474E4958      # "XING"
XING_KIND = {"road_over": 0, "rail_over": 1, "lie_over": 2}  # == swish::CrossingKind
XING_STYLE = {"steel": 0, "concrete": 1, "stone": 2, "through_girder": 3}  # == swish::BridgeStyle
PAVEMENT = {"concrete": 1, "fresh": 2, "worn": 3, "patched": 4}  # == swish::Pavement in CorridorAsset.h
ASSET_VERSION = 2  # v2: station gains y (WU, relative to the start station) and bank (cross slope)


# ══════════════════════════════════════════════════════════════════════
# 1. Overpass extract
# ══════════════════════════════════════════════════════════════════════

def overpass_query(cfg: dict) -> str:
    """The exact, reproducible Overpass QL for the corridor extract.

    Mainline = OSM `highway=motorway` ways whose `ref` matches the I-495
    selector inside the bounding box.  Ramps = `motorway_link` ways within
    `ramp_search_radius_m` of that set (3b will need them; 3a ignores them).
    `(._;>;)` recurses down to the member nodes so the extract is
    self-contained; `out meta` keeps node tags (exit refs live there).
    """
    s, w, n, e = cfg["bbox"]
    return (
        "[out:xml][timeout:300];\n"
        f'way["highway"="motorway"]["ref"~"{cfg["ref_regex"]}"]'
        f"({s},{w},{n},{e})->.lie;\n"
        # Proximity is taken from the mainline's NODES: `around` on the way set
        # returned no ramps at all on the 2026-11-20 Riverhead pull.
        "node(w.lie)->.ln;\n"
        f'way(around.ln:{cfg["ramp_search_radius_m"]})["highway"="motorway_link"]->.ramps;\n'
        "(.lie; .ramps;);\n"
        "(._;>;);\n"
        "out meta;\n"
    )


def cmd_fetch(cfg: dict, force: bool) -> str:
    ocfg = cfg["osm"]
    out = os.path.join(REPO, ocfg["raw_extract"])
    if os.path.exists(out) and not force:
        print(f"fetch: {ocfg['raw_extract']} already cached ({os.path.getsize(out)} B) — use --force to re-pull")
        return out
    query = overpass_query(ocfg)
    print("fetch: Overpass query ->\n" + "".join("    " + l + "\n" for l in query.splitlines()))
    overpass_post(ocfg["overpass_url"], query, out)
    print(f"fetch: wrote {ocfg['raw_extract']} ({os.path.getsize(out)} B)")
    return out


def overpass_post(url: str, query: str, out: str) -> None:
    os.makedirs(os.path.dirname(out), exist_ok=True)
    req = urllib.request.Request(
        url,
        data=("data=" + urllib.parse.quote(query)).encode(),
        headers={"Content-Type": "application/x-www-form-urlencoded", "User-Agent": "corridor-fetch/1.0"},
    )
    for attempt in range(4):
        try:
            # Write to a temp file: a failed pull must never clobber a cached extract.
            with urllib.request.urlopen(req, timeout=300) as resp, open(out + ".part", "wb") as fh:
                shutil.copyfileobj(resp, fh)
            os.replace(out + ".part", out)
            return
        except urllib.error.HTTPError as e:
            if e.code not in (429, 503, 504) or attempt == 3:
                raise
            print(f"fetch: Overpass busy (HTTP {e.code}), retry in {60 * (attempt + 1)} s")
            time.sleep(60 * (attempt + 1))


def cmd_fetch_srvr(cfg: dict, force: bool) -> str:
    """C2a: one-time pull of the LIE service roads (NY 906A/906B), geometry inline."""
    scfg = cfg["service_roads"]
    out = os.path.join(REPO, scfg["extract"])
    if os.path.exists(out) and not force:
        print(f"fetch_srvr: {scfg['extract']} already cached ({os.path.getsize(out)} B) — use --force to re-pull")
        return out
    s, w, n, e = scfg["bbox"]
    query = f'[out:json][timeout:180];\nway["unsigned_ref"~"NY 906[AB]"]({s},{w},{n},{e});\nout geom;\n'
    print("fetch_srvr: Overpass query ->\n" + "".join("    " + l + "\n" for l in query.splitlines()))
    overpass_post(cfg["osm"]["overpass_url"], query, out)
    print(f"fetch_srvr: wrote {scfg['extract']} ({os.path.getsize(out)} B)")
    return out


def cmd_fetch_xing(cfg: dict, force: bool) -> str:
    """D4: one-time pull of what crosses the LIE: bridges over it (road / rail) and roads under its own bridges."""
    xcfg = cfg["crossings"]
    out = os.path.join(REPO, xcfg["extract"])
    if os.path.exists(out) and not force:
        print(f"fetch_xing: {xcfg['extract']} already cached ({os.path.getsize(out)} B) — use --force to re-pull")
        return out
    s, w, n, e = cfg["osm"]["bbox"]
    r = xcfg["search_m"]
    query = (
        "[out:json][timeout:300];\n"
        f'way["highway"="motorway"]["ref"~"{cfg["osm"]["ref_regex"]}"]({s},{w},{n},{e})->.lie;\n'
        'way.lie["bridge"]["bridge"!="no"]->.liebr;\n'
        "(\n"
        f'  way(around.lie:{r})["bridge"]["bridge"!="no"]["highway"];\n'
        f'  way(around.lie:{r})["bridge"]["bridge"!="no"]["railway"~"^(rail|light_rail|subway)$"];\n'
        f'  way(around.liebr:{r})["highway"]["bridge"!~"."]["tunnel"!~"."];\n'
        f'  way(around.liebr:{r})["railway"="rail"]["bridge"!~"."];\n'
        ");\n"
        "out tags geom;\n"
    )
    print("fetch_xing: Overpass query ->\n" + "".join("    " + l + "\n" for l in query.splitlines()))
    overpass_post(cfg["osm"]["overpass_url"], query, out)
    print(f"fetch_xing: wrote {xcfg['extract']} ({os.path.getsize(out)} B)")
    return out


# ══════════════════════════════════════════════════════════════════════
# 2. netconvert  (spec decision #2)
# ══════════════════════════════════════════════════════════════════════

def cmd_net(cfg: dict, force: bool) -> str:
    ncfg = cfg["netconvert"]
    edg = os.path.join(REPO, ncfg["plain_prefix"] + ".edg.xml")
    if os.path.exists(edg) and not force:
        print(f"net: {ncfg['plain_prefix']}.edg.xml already cached — use --force to re-run netconvert")
        return edg

    exe = shutil.which("netconvert")
    if exe is None:
        sys.exit(
            "net: `netconvert` not on PATH.\n"
            "     Install with `pipx install eclipse-sumo` (the homebrew dlr-ts/sumo tap is stale).\n"
            "     netconvert is a DEV-MACHINE tool only: its output is cached in the repo, so\n"
            "     neither the build nor a player machine needs SUMO.  If you only want to rebuild\n"
            "     the engine asset from the cached network, run `bake` instead."
        )
    typemap = _find_typemap()
    args = [
        exe,
        "--osm-files", os.path.join(REPO, cfg["osm"]["raw_extract"]),
        "--type-files", typemap,
        "-o", os.path.join(REPO, ncfg["net_output"]),
        "--plain-output-prefix", os.path.join(REPO, ncfg["plain_prefix"]),
        # Keep plain-XML shapes in WGS84 lon/lat: the projection is ours (spec §4.1).
        "--proj.plain-geo",
    ] + list(ncfg["extra_args"])
    os.makedirs(os.path.dirname(os.path.join(REPO, ncfg["net_output"])), exist_ok=True)
    print("net: " + " ".join(args))
    res = subprocess.run(args, capture_output=True, text=True)
    tail = [l for l in res.stderr.splitlines() if not l.startswith("Warning:")]
    if res.returncode != 0:
        sys.exit("net: netconvert failed\n" + res.stderr[-4000:])
    print("net: netconvert ok" + ("\n" + "\n".join(tail[-8:]) if tail else ""))
    return edg


def _find_typemap() -> str:
    """SUMO's shipped OSM typemap (do not hand-roll edge types)."""
    home = os.environ.get("SUMO_HOME")
    cands = []
    if home:
        cands.append(os.path.join(home, "data", "typemap", "osmNetconvert.typ.xml"))
    exe = shutil.which("netconvert")
    if exe:
        root = os.path.dirname(os.path.dirname(os.path.realpath(exe)))
        for sub in ("sumo", ""):
            cands.append(os.path.join(root, "lib", sub, "data", "typemap", "osmNetconvert.typ.xml"))
        # pipx layout: .../site-packages/sumo/data/typemap/...
        for base, dirs, files in os.walk(root):
            if "osmNetconvert.typ.xml" in files:
                cands.append(os.path.join(base, "osmNetconvert.typ.xml"))
                break
    for c in cands:
        if c and os.path.exists(c):
            return c
    sys.exit("net: could not locate osmNetconvert.typ.xml — set SUMO_HOME")


# ══════════════════════════════════════════════════════════════════════
# 3. Mainline extraction from netconvert's plain XML
# ══════════════════════════════════════════════════════════════════════

def _shape(text: str) -> list[tuple[float, float]]:
    return [tuple(float(v) for v in p.split(",")[:2]) for p in text.split()]


def _planar_bearing(a, b) -> float:
    """Bearing of a->b in the lon/lat plane, latitude-corrected. Radians, CCW from east."""
    return math.atan2(b[1] - a[1], (b[0] - a[0]) * math.cos(math.radians(a[1])))


def extract_mainline(prefix: str) -> tuple[list[tuple[float, float]], list[float]]:
    """Walk netconvert's motorway edges into ONE eastbound I-495 carriageway.

    The extract holds both carriageways (OSM tags the LIE as two one-way
    ways).  We take the eastbound one as the reference alignment and build a
    symmetric cross-section about it — 3a needs one centreline, not two.

    The walk starts at the westernmost node with an outgoing motorway edge
    (the Queens-Midtown Tunnel's Manhattan portal) and at every junction
    takes the successor with the SMALLEST heading change.  That is what
    keeps it on the mainline: diverging ramps are `motorway_link` (excluded
    outright) and the opposite carriageway is a >100 deg turn away, so the
    minimum-turn rule cannot hop carriageways even where
    `--junctions.join` merged nodes.

    Returns (lon/lat points, per-point netconvert lane count).
    """
    nod = {
        n.get("id"): (float(n.get("x")), float(n.get("y")))
        for n in ET.parse(prefix + ".nod.xml").getroot().findall("node")
    }
    edges = []
    for e in ET.parse(prefix + ".edg.xml").getroot().findall("edge"):
        if e.get("type") != "highway.motorway":
            continue
        sh = e.get("shape")
        pts = _shape(sh) if sh else [nod[e.get("from")], nod[e.get("to")]]
        if len(pts) < 2:
            continue
        edges.append({"id": e.get("id"), "to": e.get("to"),
                      "lanes": int(e.get("numLanes")), "pts": pts,
                      "from": e.get("from")})
    if not edges:
        sys.exit("bake: no highway.motorway edges in the netconvert output")

    out = defaultdict(list)
    for e in edges:
        out[e["from"]].append(e)

    start = min(out.keys(), key=lambda k: nod[k][0])
    chain, cur, heading, used = [], start, None, set()
    while True:
        cands = [e for e in out.get(cur, []) if e["id"] not in used]
        if not cands and heading is not None:
            # Bridge a hole in the extract: a mainline way with no `ref` tag is
            # not pulled (one ~110 m hole near exit 68 on 2026-11-20).  Continue
            # on the nearest edge that starts within 300 m dead ahead.
            # shortcut: straight-line bridge, re-pull once OSM has the ref back.
            here = nod[cur]
            def ahead(e):
                d = math.hypot((e["pts"][0][0] - here[0]) * math.cos(math.radians(here[1])),
                               e["pts"][0][1] - here[1]) * math.radians(1.0) * EARTH_R
                b = _planar_bearing(here, e["pts"][0])
                off = abs((b - heading + math.pi) % (2 * math.pi) - math.pi)
                return d if (0.0 < d < 300.0 and off < math.radians(20.0)) else math.inf
            near = min((e for e in edges if e["id"] not in used), key=ahead, default=None)
            if near is not None and ahead(near) < math.inf:
                print(f"bake: bridged a {ahead(near):.0f} m hole in the mainline at {here}")
                cands = [near]
        if not cands:
            break
        if heading is None:
            pick = max(cands, key=lambda e: e["pts"][-1][0] - e["pts"][0][0])
        else:
            def turn(e):
                b = _planar_bearing(e["pts"][0], e["pts"][1])
                return abs((b - heading + math.pi) % (2 * math.pi) - math.pi)
            pick = min(cands, key=turn)
            if turn(pick) > math.radians(100.0):
                break
        used.add(pick["id"])
        chain.append(pick)
        heading = _planar_bearing(pick["pts"][-2], pick["pts"][-1])
        cur = pick["to"]

    pts, lanes = [], []
    for e in chain:
        for p in e["pts"]:
            if pts and p == pts[-1]:
                continue
            pts.append(p)
            lanes.append(e["lanes"])
    print(f"bake: mainline chain = {len(chain)} netconvert edges, {len(pts)} shape points")
    print(f"bake: netconvert lane counts along the chain: {dict(sorted(Counter(lanes).items()))}")
    return pts, lanes


def extract_landmarks(osm_path: str, ref_regex: str, wanted: list[str]) -> list[tuple[float, float, str]]:
    """Real interchange positions: OSM motorway_junction nodes ON the I-495 mainline.

    Restricting to nodes that are members of an I-495 `highway=motorway` way
    matters — the extract's ramps drag in junction nodes belonging to the
    Northern State / Grand Central / Cross Island, whose own exit numbering
    would otherwise alias onto ours (a node tagged `ref=27` that is not
    LIE exit 27).
    """
    root = ET.parse(osm_path).getroot()
    rx = re.compile(ref_regex)

    def tags(el):
        return {t.get("k"): t.get("v") for t in el.findall("tag")}

    mainline_nodes = set()
    for w in root.findall("way"):
        tg = tags(w)
        if tg.get("highway") == "motorway" and rx.match(tg.get("ref", "")):
            for nd in w.findall("nd"):
                mainline_nodes.add(nd.get("ref"))

    found = []
    for n in root.findall("node"):
        if n.get("id") not in mainline_nodes:
            continue
        tg = tags(n)
        if tg.get("highway") != "motorway_junction":
            continue
        ref = tg.get("ref", "")
        for want in wanted:
            # `27`, `27N`, `27S;27N`, `27N-S` all belong to exit 27; `27` must
            # not match `270`, hence the negative lookahead on a digit.
            if re.match(r"^%s(?![0-9])" % re.escape(want), ref):
                found.append((float(n.get("lon")), float(n.get("lat")), want))
                break
    print(f"bake: {len(found)} mainline motorway_junction nodes matched the {len(wanted)} landmark exits")
    return found


# ══════════════════════════════════════════════════════════════════════
# 4. Coordinate transform  (spec §4.1)
# ══════════════════════════════════════════════════════════════════════

class TangentPlane:
    r"""Local tangent-plane projection + rotation into engine axes.

    Step 1 — equirectangular tangent plane about (lat0, lon0), the route
    midpoint.  Over a ~50 km corridor the plane's scale error is O((d/R)^2)
    ~ 6e-5, i.e. a few metres end to end: far below anything visible in a
    driving game, and the spec (§4.1) explicitly calls this sufficient.

        e = R * cos(lat0) * (lon - lon0)          [metres east]
        n = R *            (lat - lat0)           [metres north]

    Step 2 — rotate by -psi, where psi is the bearing of the route's
    start->end chord, so the corridor runs along +e'.

    Step 3 — into engine axes.  The engine drives along -Z with +X to the
    driver's right (cross((0,0,-1),(0,1,0)) = (1,0,0)), and driving east the
    right-hand side is south, so:

        x = -n' * WORLD_SCALE          (south -> +X)
        z = -e' * WORLD_SCALE          (east  -> -Z)

    That map has determinant -1 — a reflection, not a rotation.  This is
    expected: (x, z) with Y up is left-handed relative to a 2-D east/north
    map, so a handedness flip is exactly what belongs here.
    """

    def __init__(self, lat0: float, lon0: float, chord_bearing: float):
        self.lat0, self.lon0 = lat0, lon0
        self.psi = chord_bearing
        self._mx = EARTH_R * math.cos(math.radians(lat0)) * math.pi / 180.0
        self._my = EARTH_R * math.pi / 180.0

    def to_plane(self, lon: float, lat: float) -> tuple[float, float]:
        return ((lon - self.lon0) * self._mx, (lat - self.lat0) * self._my)

    def to_engine_m(self, lon: float, lat: float) -> tuple[float, float]:
        """-> (x, z) in METRES (pre-WORLD_SCALE), engine axes."""
        e, n = self.to_plane(lon, lat)
        c, s = math.cos(self.psi), math.sin(self.psi)
        ep = e * c + n * s
        np_ = -e * s + n * c
        return (-np_, -ep)

    @staticmethod
    def for_route(pts: list[tuple[float, float]]) -> "TangentPlane":
        lat0 = sum(p[1] for p in pts) / len(pts)
        lon0 = sum(p[0] for p in pts) / len(pts)
        tp0 = TangentPlane(lat0, lon0, 0.0)
        e0, n0 = tp0.to_plane(*pts[0])
        e1, n1 = tp0.to_plane(*pts[-1])
        return TangentPlane(lat0, lon0, math.atan2(n1 - n0, e1 - e0))


# ══════════════════════════════════════════════════════════════════════
# 5. Resampling, lane profile, compaction  (spec §4.2)
# ══════════════════════════════════════════════════════════════════════

def polyline_stations(xy: list[tuple[float, float]]) -> list[float]:
    s = [0.0]
    for a, b in zip(xy, xy[1:]):
        s.append(s[-1] + math.dist(a, b))
    return s


def resample(xy, s, values, step):
    """Uniform-arc-length resample of a polyline; `values` series ride along.

    `values` is a list of per-vertex series; the return keeps that shape
    (one resampled series per input series), not a per-sample transpose.
    """
    outp = []
    outv = [[] for _ in values]
    j = 0
    for i in range(int(s[-1] // step) + 1):
        t = i * step
        while j + 2 < len(s) and s[j + 1] < t:
            j += 1
        seg = s[j + 1] - s[j]
        u = 0.0 if seg <= 1e-9 else min(max((t - s[j]) / seg, 0.0), 1.0)
        outp.append((xy[j][0] + (xy[j + 1][0] - xy[j][0]) * u,
                     xy[j][1] + (xy[j + 1][1] - xy[j][1]) * u))
        for k, v in enumerate(values):
            outv[k].append(v[j] + (v[j + 1] - v[j]) * u)
    return outp, outv


def headings(xy: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """Unit tangents, central-differenced (matches MeshBuilder::emit_ribbon)."""
    n = len(xy)
    out = []
    for i in range(n):
        a = xy[max(i - 1, 0)]
        b = xy[min(i + 1, n - 1)]
        dx, dz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dz)
        out.append((0.0, -1.0) if L < 1e-9 else (dx / L, dz / L))
    return out


def nominal_lanes(raw: list[float], step: float, window_m: float) -> list[float]:
    r"""Through-lane count from netconvert's per-edge lane counts.

    netconvert reports the *physical* lane count, which spikes at every ramp
    (auxiliary, deceleration and weaving lanes): along this chain it flips
    3<->4 through Queens and 4<->5 through Nassau, dozens of times.  The
    through-lane count is the value that PERSISTS, i.e. the minimum over a
    window wide enough to span an auxiliary lane but narrower than a real
    lane-count change:

        nominal(i) = min_{|j-i| <= W/(2 dx)} raw(j)

    Clamped to >= 2: the Queens-Midtown Tunnel is tagged `lanes=1` per tube
    in OSM, which is true per-tube but leaves a 1-lane drivable start.
    """
    half = max(1, int(window_m / (2.0 * step)))
    n = len(raw)
    return [max(2.0, min(raw[max(0, i - half): min(n, i + half + 1)])) for i in range(n)]


def taper(vals: list[float], step: float, taper_m: float) -> list[float]:
    """Box-smooth a step profile so a lane-count change becomes a real taper."""
    half = max(1, int(taper_m / (2.0 * step)))
    n = len(vals)
    out = []
    for i in range(n):
        lo, hi = max(0, i - half), min(n, i + half + 1)
        w = vals[lo:hi]
        out.append(sum(w) / len(w))
    return out


def smoothstep(t: float) -> float:
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def landmark_weights(s: list[float], marks: list[float], radius: float, blend: float) -> list[float]:
    r"""w(i) = 1 inside the landmark window, smoothstep to 0 over `blend`.

        d(i) = min_j |s_i - s_j|
        w(i) = 1                                       d <= N
               smoothstep(1 - (d - N)/W)               N < d < N + W
               0                                       d >= N + W
    """
    out = []
    for si in s:
        d = min(abs(si - m) for m in marks) if marks else 1e30
        if d <= radius:
            out.append(1.0)
        elif d >= radius + blend:
            out.append(0.0)
        else:
            out.append(smoothstep(1.0 - (d - radius) / blend))
    return out


def curve_radii(tans, step: float, baseline_m: float) -> list[float]:
    r"""Local radius of curvature (metres) from heading change.

        kappa = |dtheta| / ds        R = 1 / kappa

    Measured over a +/-`baseline_m` window rather than station-to-station:
    OSM centreline vertices are not smooth, and a one-station difference
    turns that digitising noise into fake curvature (measured: it drops the
    apparent minimum radius from 99 m to 79 m and inflates the amount of
    "tight" road by ~10%).
    """
    n = len(tans)
    b = max(1, int(baseline_m / step))
    out = []
    for i in range(n):
        lo, hi = max(0, i - b), min(n - 1, i + b)
        ta = math.atan2(tans[lo][1], tans[lo][0])
        tb = math.atan2(tans[hi][1], tans[hi][0])
        dth = abs((tb - ta + math.pi) % (2 * math.pi) - math.pi)
        L = (hi - lo) * step
        k = (dth / L) if L > 0.0 else 0.0
        out.append(1e18 if k < 1e-12 else 1.0 / k)
    return out


def curvature_floor(radii: list[float], floor_m: float, step: float, smooth_m: float) -> list[float]:
    r"""Lower bound on the scale factor that keeps every curve drivable.

    Compaction preserves heading and scales arc length, so curvature scales
    as kappa' = kappa / f and radius as R' = R f.  Requiring the COMPACTED
    radius to stay above a floor is therefore a bound on f directly:

        R_i f_i >= R_floor   =>   f_i >= min(1, R_floor / R_i)

    Without this the heuristic quietly manufactures corners that do not
    exist on the real road.  Measured on this corridor at c = 0.455: the real
    alignment's tightest curve is 99 m (the Queens-Midtown Tunnel portal
    ramp, genuinely that tight) and only 1.05 km of 50 km is under 500 m,
    but uniform compression produced a 46 m minimum and 6.05 km under
    500 m — a 6x inflation of tight-curve mileage that is pure artefact.

    This is the literal form of the heuristic's stated intent (compress the
    long STRAIGHTAWAYS between landmarks): landmark distance was a proxy for
    "nothing interesting here", and curvature is the direct measure.
    """
    raw = [min(1.0, floor_m / r) if r > 1e-9 else 1.0 for r in radii]
    half = max(0, int(smooth_m / (2.0 * step)))
    if half == 0:
        return raw
    n = len(raw)
    # Dilate (max, not mean): a floor must not be relaxed by averaging with
    # its straight neighbours, or the curve's own apex loses its protection.
    return [max(raw[max(0, i - half): min(n, i + half + 1)]) for i in range(n)]


def scale_profile(w: list[float], floor: list[float], c: float) -> list[float]:
    """Per-station arc-length scale: the heuristic, floored by drivability."""
    return [max(wi + c * (1.0 - wi), fl) for wi, fl in zip(w, floor)]


def solve_compaction(w: list[float], floor: list[float], step: float, target: float) -> float:
    r"""Solve the compression factor c for a target total length.

    Without the curvature floor, f_i = w_i + c (1 - w_i) makes the total

        L' = sum_i f_i dx = dx sum_i w_i  +  c dx sum_i (1 - w_i) = A + c B

    affine in c, invertible exactly as c = (L_target - A) / B.  The floor
    makes f_i = max(w_i + c(1 - w_i), g_i) piecewise linear — still
    continuous and monotonically non-decreasing in c, so a bisection finds
    the root just as reliably and needs no case analysis.

    Either way c is DERIVED, not authored: only the landmark radius N, the
    blend W and the target length are tuned, so the corridor always lands on
    the target rather than being hand-walked into the band.
    """
    def total(c: float) -> float:
        return step * sum(scale_profile(w, floor, c))

    lo, hi = 0.0, 1.0
    if total(hi) <= target:
        return 1.0  # even uncompacted the route is short enough
    if total(lo) >= target:
        return 0.0  # floors alone already exceed the target
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if total(mid) < target:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def compact(xy, tans, f, step):
    """Integrate the compacted polyline: heading preserved, arc length scaled.

    p'_{i+1} = p'_i + f_i dx t_i, with the ORIGINAL unit tangent t_i.  Total
    turning is therefore preserved exactly, so the route keeps its real shape
    (its map silhouette) and only its scale varies.
    """
    out = [xy[0]]
    marks = [0.0]
    for i in range(len(xy) - 1):
        d = f[i] * step
        out.append((out[-1][0] + tans[i][0] * d, out[-1][1] + tans[i][1] * d))
        marks.append(marks[-1] + d)
    return out, marks


def extract_exits(osm_path: str, ref_regex: str):
    """Every exit on the I-495 mainline, both directions, plus where each county starts.

    An exit is an OSM `motorway_junction` node on a mainline way.  Its sign
    text is not on the node: it is the `destination` / `destination:ref` of the
    `motorway_link` way that STARTS at the node.  Direction comes from the
    mainline way the node sits on (its last node is east or west of its first).

    Counties come from the mainline ways' `tiger:county` tag: a county starts
    at the westernmost node of any way carrying it.  Queens ways are mostly
    untagged, which is fine — Queens is simply "before Nassau starts".

    Returns (exits, county_starts):
        exits          [(lon, lat, ref, westbound, dest, dest_ref)]
        county_starts  {county name: (lon, lat)}
    """
    root = ET.parse(osm_path).getroot()
    rx = re.compile(ref_regex)
    tags = lambda el: {t.get("k"): t.get("v") for t in el.findall("tag")}  # noqa: E731
    nodes = {n.get("id"): n for n in root.findall("node")}
    pos = lambda nid: (float(nodes[nid].get("lon")), float(nodes[nid].get("lat")))  # noqa: E731

    node_dir: dict[str, bool] = {}
    link_from = defaultdict(list)
    county_starts: dict[str, tuple[float, float]] = {}
    for w in root.findall("way"):
        tg = tags(w)
        nds = [nd.get("ref") for nd in w.findall("nd")]
        if tg.get("highway") == "motorway" and rx.match(tg.get("ref", "")):
            westbound = pos(nds[-1])[0] < pos(nds[0])[0]
            for nid in nds:
                node_dir[nid] = westbound
            county = tg.get("tiger:county", "").split(",")[0]
            if county:
                west = min((pos(nid) for nid in nds), key=lambda q: q[0])
                if county not in county_starts or west[0] < county_starts[county][0]:
                    county_starts[county] = west
        elif tg.get("highway") == "motorway_link":
            link_from[nds[0]].append(tg)

    exits = []
    for nid, westbound in node_dir.items():
        tg = tags(nodes[nid])
        if tg.get("highway") != "motorway_junction" or not tg.get("ref"):
            continue
        dest = "; ".join(dict.fromkeys(d for l in link_from.get(nid, []) for d in l.get("destination", "").split(";") if d))
        dref = "; ".join(dict.fromkeys(d for l in link_from.get(nid, []) for d in l.get("destination:ref", "").split(";") if d))
        exits.append((*pos(nid), tg["ref"], westbound, dest, dref))
    return exits, county_starts


def route_frame(ccfg: dict, ll) -> "TangentPlane":
    if "frame_lat0" in ccfg:
        return TangentPlane(ccfg["frame_lat0"], ccfg["frame_lon0"], math.radians(ccfg["frame_psi_deg"]))
    return TangentPlane.for_route(ll)


# ══════════════════════════════════════════════════════════════════════
# 5b. Elevation  (USGS 3DEP -> vertical profile + superelevation)
# ══════════════════════════════════════════════════════════════════════

def cmd_elev(cfg: dict, force: bool) -> str:
    """One-time USGS 3DEP pull: ground height every `sample_spacing_m` of REAL route.

    Cached as CSV (s_real_m, lon, lat, elev_m) and checked in, like the OSM
    extract but tracked — a re-pull is never needed to bake.
    """
    ecfg, ccfg = cfg["elevation"], cfg["corridor"]
    out = os.path.join(REPO, ecfg["cache"])
    if os.path.exists(out) and not force:
        print(f"elev: {ecfg['cache']} already cached — use --force to re-pull")
        return out
    ll, _ = extract_mainline(os.path.join(REPO, cfg["netconvert"]["plain_prefix"]))
    tp = route_frame(ccfg, ll)
    s_real = polyline_stations([tp.to_engine_m(lon, lat) for lon, lat in ll])
    pts, _ = resample(ll, s_real, [], ecfg["sample_spacing_m"])
    write_elev_csv(ecfg["service_url"], pts, ecfg["sample_spacing_m"], out)
    print(f"elev: wrote {ecfg['cache']}")
    return out


def write_elev_csv(url: str, pts: list[tuple[float, float]], spacing: float, out: str) -> None:
    """USGS 3DEP ground height at each (lon, lat), `spacing` m apart, cached as CSV (s_real_m, lon, lat, elev_m)."""
    rows, batch = [], 200
    for b0 in range(0, len(pts), batch):
        chunk = pts[b0:b0 + batch]
        geom = json.dumps({"points": [[lon, lat] for lon, lat in chunk], "spatialReference": {"wkid": 4326}})
        data = urllib.parse.urlencode({
            "geometry": geom, "geometryType": "esriGeometryMultipoint", "returnFirstValueOnly": "true",
            "interpolation": "RSP_BilinearInterpolation", "f": "json"}).encode()
        req = urllib.request.Request(url, data=data, headers={"User-Agent": "corridor-fetch/1.0"})
        with urllib.request.urlopen(req, timeout=120) as resp:
            samples = json.load(resp)["samples"]
        got = {smp["locationId"]: float(smp["value"]) for smp in samples}
        if len(got) != len(chunk):
            sys.exit(f"elev: service returned {len(got)} of {len(chunk)} samples at batch {b0}")
        for k, (lon, lat) in enumerate(chunk):
            rows.append(((b0 + k) * spacing, lon, lat, got[k]))
        print(f"elev: {len(rows)}/{len(pts)}", end="\r", flush=True)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write("s_real_m,lon,lat,elev_m\n")
        for r in rows:
            fh.write(f"{r[0]:.1f},{r[1]:.7f},{r[2]:.7f},{r[3]:.3f}\n")
    print(f"elev: {len(rows)} samples, {min(r[3] for r in rows):.1f}..{max(r[3] for r in rows):.1f} m")


def load_elevation(cfg: dict, s_uni: list[float]) -> list[float]:
    """Ground height (m) at each uniform REAL station, linearly interpolated from the cache."""
    path = os.path.join(REPO, cfg["elevation"]["cache"])
    if not os.path.exists(path):
        sys.exit(f"bake: {cfg['elevation']['cache']} missing — run `python3 tools/lie_corridor.py elev`")
    with open(path) as fh:
        rows = [ln.split(",") for ln in fh.read().splitlines()[1:]]
    es, ez = [float(r[0]) for r in rows], [float(r[3]) for r in rows]
    out, j = [], 0
    for sv in s_uni:
        while j + 2 < len(es) and es[j + 1] < sv:
            j += 1
        u = min(max((sv - es[j]) / (es[j + 1] - es[j]), 0.0), 1.0)
        out.append(ez[j] + (ez[j + 1] - ez[j]) * u)
    return out


def box_smooth(v: list[float], half: int) -> list[float]:
    """Moving average over +/- `half` samples, ends clamped (O(n) prefix sum)."""
    n = len(v)
    pre = [0.0]
    for x in v:
        pre.append(pre[-1] + x)
    return [(pre[min(i + half, n - 1) + 1] - pre[max(i - half, 0)]) / (min(i + half, n - 1) - max(i - half, 0) + 1)
            for i in range(n)]


def vertical_profile(z: list[float], step: float, ecfg: dict) -> tuple[list[float], float, float]:
    r"""Turn raw ground heights (already on COMPACTED stations) into a drivable profile.

    Compaction scales arc length by f < 1 but leaves height alone, so a real
    grade g becomes g / f — up to 5.8x steeper.  Three steps fix that:

      1. low-pass (despike bridges, cuttings and DEM noise): box filter twice;
      2. grade clamp |dz/ds| <= G: a forward then a backward slope limiter
         (the tightest profile under a Lipschitz bound);
      3. vertical-curve limit: keep smoothing until the vertical acceleration
         a_v = v^2 |z''| at the design speed is under `max_vertical_accel`.

    Returns (profile, max |grade|, max a_v).
    """
    half = max(1, int(ecfg["smooth_m"] / (2.0 * step)))
    G, v, a_max = ecfg["max_grade"], ecfg["design_speed_mps"], ecfg["max_vertical_accel"]
    z = box_smooth(box_smooth(z, half), half)

    def limit(p):
        p = p[:]
        for i in range(1, len(p)):
            p[i] = min(max(p[i], p[i - 1] - G * step), p[i - 1] + G * step)
        for i in range(len(p) - 2, -1, -1):
            p[i] = min(max(p[i], p[i + 1] - G * step), p[i + 1] + G * step)
        return p

    def audit(p):
        g = max(abs(p[i + 1] - p[i]) / step for i in range(len(p) - 1))
        a = max(abs(p[i + 1] - 2.0 * p[i] + p[i - 1]) / (step * step) for i in range(1, len(p) - 1)) * v * v
        return g, a

    z = limit(z)
    for _ in range(200):
        g, a = audit(z)
        if a <= a_max and g <= G + 1e-9:
            break
        z = limit(box_smooth(z, max(1, half // 3)))
    g, a = audit(z)
    return z, g, a


def superelevation(tans, step: float, ecfg: dict) -> list[float]:
    r"""Cross slope from curvature, AASHTO-style: e = clamp(k v^2 / (g R), +/- e_max).

    Signed so that height(lateral t) = y - bank * t with t positive to the
    right of travel: a right-hand curve has bank > 0 (right edge lower).
    """
    n = len(tans)
    b = max(1, int(ecfg["bank_baseline_m"] / step))
    v, k, e_max = ecfg["design_speed_mps"], ecfg["bank_fraction"], ecfg["max_bank"]
    out = []
    for i in range(n):
        t0, t1 = tans[max(i - b, 0)], tans[min(i + b, n - 1)]
        ds = (min(i + b, n - 1) - max(i - b, 0)) * step
        # sin of the heading change, positive when turning toward the right vector (-tz, tx)
        kappa = (t1[0] * -t0[1] + t1[1] * t0[0]) / ds
        out.append(min(max(k * v * v * kappa / 9.80665, -e_max), e_max))
    half = max(1, int(ecfg["bank_smooth_m"] / (2.0 * step)))
    return box_smooth(box_smooth(out, half), half)


# ══════════════════════════════════════════════════════════════════════
# 6. Bake
# ══════════════════════════════════════════════════════════════════════

def cmd_bake(cfg: dict, write: bool = True):
    """Bake the LIE asset; `write=False` only returns the frames the Queens network warp needs (N1)."""
    ccfg, ncfg, ocfg = cfg["corridor"], cfg["netconvert"], cfg["osm"]
    prefix = os.path.join(REPO, ncfg["plain_prefix"])
    osm_path = os.path.join(REPO, ocfg["raw_extract"])

    # ── real centreline + real lane counts (netconvert) ──────────────
    ll, raw_lanes = extract_mainline(prefix)
    # The frame is pinned once the route is in use: for_route() depends on the
    # chain's extent, so widening the extract would rotate every station.
    tp = route_frame(ccfg, ll)
    xy_m = [tp.to_engine_m(lon, lat) for lon, lat in ll]
    s_real = polyline_stations(xy_m)
    print(f"bake: raw chain length {s_real[-1]:.0f} m = {s_real[-1] / MILE_M:.2f} mi")

    # ── real landmarks (OSM), snapped to the chain ──────────────────
    marks_ll = extract_landmarks(osm_path, ocfg["ref_regex"], ccfg["landmark_exits"])
    by_exit: dict[str, float] = {}
    for lon, lat, ref in marks_ll:
        px, pz = tp.to_engine_m(lon, lat)
        best = min(range(len(xy_m)), key=lambda i: (xy_m[i][0] - px) ** 2 + (xy_m[i][1] - pz) ** 2)
        off = math.dist(xy_m[best], (px, pz))
        if off > 300.0:  # not actually on this carriageway's shape
            continue
        # Keep the WESTERNMOST node per exit: an interchange has several
        # junction nodes (per ramp, per direction); the first is its start.
        if ref not in by_exit or s_real[best] < by_exit[ref]:
            by_exit[ref] = s_real[best]
    for ref in ccfg["landmark_exits"]:
        if ref not in by_exit:
            sys.exit(f"bake: landmark exit {ref} not found on the mainline — extract or bbox is wrong")
    print("bake: landmark stations (real): " +
          ", ".join(f"exit {r} @ {by_exit[r] / MILE_M:.2f} mi" for r in ccfg["landmark_exits"]))

    # ── trim the east end just past the terminus exit ────────────────
    # "end" = the whole chain (I-495 stops at Riverhead, exit 73).
    if ccfg["east_terminus_exit"] == "end":
        end_s = s_real[-1]
    else:
        end_s = by_exit[ccfg["east_terminus_exit"]] + ccfg["east_runout_m"]
    keep = max(2, sum(1 for v in s_real if v <= end_s))
    xy_m, raw_lanes, s_real = xy_m[:keep], raw_lanes[:keep], s_real[:keep]
    real_len = s_real[-1]
    print(f"bake: trimmed to {real_len:.0f} m = {real_len / MILE_M:.2f} mi "
          f"(east terminus = exit {ccfg['east_terminus_exit']} + {ccfg['east_runout_m']:.0f} m run-out)")

    # ── uniform resample of the REAL alignment ───────────────────────
    step = ccfg["station_spacing_m"]
    pts, vals = resample(xy_m, s_real, [[float(v) for v in raw_lanes]], step)
    tans = headings(pts)
    lanes_nom = nominal_lanes(vals[0], step, 2000.0)
    lanes_tap = taper(lanes_nom, step, ccfg["lane_taper_m"])
    s_uni = [i * step for i in range(len(pts))]

    # ── compaction ───────────────────────────────────────────────────
    marks = sorted(by_exit[r] for r in ccfg["landmark_exits"])
    w = landmark_weights(s_uni, marks, ccfg["landmark_radius_m"], ccfg["landmark_blend_m"])
    radii = curve_radii(tans, step, ccfg["curvature_baseline_m"])
    floor = curvature_floor(radii, ccfg["curvature_floor_radius_m"], step, ccfg["curvature_smooth_m"])
    target = ccfg["target_length_miles"] * MILE_M
    # Two zones: the west zone keeps the length it shipped with, so extending
    # the route east does not re-solve (and move) the road already built.
    k = len(w)
    if "west_zone_exit" in ccfg:
        k = min(len(w), int((by_exit[ccfg["west_zone_exit"]] + ccfg["east_runout_m"]) / step) + 1)
    west_target = ccfg.get("west_zone_length_miles", ccfg["target_length_miles"]) * MILE_M if k < len(w) else target
    c = solve_compaction(w[:k], floor[:k], step, west_target)
    c_east = solve_compaction(w[k:], floor[k:], step, target - west_target) if k < len(w) else c
    if k < len(w):
        print(f"bake: west zone {k * step / MILE_M:.2f} real mi -> {west_target / MILE_M:.2f} mi (c = {c:.4f}); "
              f"east zone {(len(w) - k) * step / MILE_M:.2f} real mi -> "
              f"{(target - west_target) / MILE_M:.2f} mi (c = {c_east:.4f})")
    if not (0.0 < c < 1.0 and 0.0 < c_east <= 1.0):
        sys.exit(f"bake: solved compression factor c = {c:.4f} is out of (0,1) — "
                 f"target_length_miles is inconsistent with a {real_len / MILE_M:.2f} mi route "
                 f"(landmark-preserved {step * sum(w) / MILE_M:.2f} mi; curvature floors alone "
                 f"force {step * sum(floor) / MILE_M:.2f} mi). Raise target_length_miles, or "
                 f"lower landmark_radius_m / curvature_floor_radius_m.")
    f_prof = scale_profile(w[:k], floor[:k], c) + scale_profile(w[k:], floor[k:], c_east)
    cxy, cs = compact(pts, tans, f_prof, step)
    binding = sum(1 for i in range(len(w)) if f_prof[i] <= floor[i] + 1e-9 and floor[i] > w[i] + 1e-9)
    print(f"bake: compaction  N = {ccfg['landmark_radius_m']:.0f} m, "
          f"W = {ccfg['landmark_blend_m']:.0f} m, solved c = {c:.4f}")
    print(f"bake: landmark-preserved {step * sum(w) / MILE_M:.2f} mi of {real_len / MILE_M:.2f} mi; "
          f"curvature floor binds over {binding * step / MILE_M:.2f} mi "
          f"(R_floor = {ccfg['curvature_floor_radius_m']:.0f} m)")
    print(f"bake: mean scale {sum(f_prof) / len(f_prof):.4f}, min {min(f_prof):.4f}")
    print(f"bake: compacted total {cs[-1]:.0f} m = {cs[-1] / MILE_M:.3f} mi (target "
          f"{ccfg['target_length_miles']:.2f} mi)")
    # Curvature audit: the compacted road must not be tighter than the real one.
    r_out = [radii[i] * f_prof[i] for i in range(len(radii))]
    tight_real = sum(1 for r in radii if r < 500.0) * step / 1000.0
    tight_out  = sum(1 for r in r_out if r < 500.0) * step / 1000.0
    print(f"bake: radius min real {min(radii):.0f} m -> compacted {min(r_out):.0f} m; "
          f"km under 500 m real {tight_real:.2f} -> compacted {tight_out:.2f}")

    # ── uniform resample of the COMPACTED alignment ──────────────────
    elev_real = load_elevation(cfg, s_uni)
    fpts, fvals = resample(cxy, cs, [lanes_tap, w, elev_real], step)
    # Put the west end (Queens-Midtown Tunnel portal) at the engine origin, so
    # the corridor runs 0 -> -L in Z exactly like the retired authored road did
    # and the car's start pose needs no offset.
    ox, oz = fpts[0]
    fpts = [(p[0] - ox, p[1] - oz) for p in fpts]
    ftans = headings(fpts)
    flanes = fvals[0]
    ecfg = cfg["elevation"]
    fy, g_max, a_max = vertical_profile(fvals[2], step, ecfg)
    fbank = superelevation(ftans, step, ecfg)
    print(f"bake: elevation raw {min(fvals[2]):.1f}..{max(fvals[2]):.1f} m -> profile "
          f"{min(fy):.1f}..{max(fy):.1f} m; max grade {100 * g_max:.2f} %, "
          f"max vertical accel {a_max:.2f} m/s^2 at {ecfg['design_speed_mps']:.0f} m/s; "
          f"max bank {100 * max(abs(b) for b in fbank):.2f} %")
    total = step * (len(fpts) - 1)

    lat_dev = max(abs(p[0]) for p in fpts)
    print(f"bake: engine extent  Z = {min(p[1] for p in fpts):.0f}..{max(p[1] for p in fpts):.0f} m, "
          f"|X| <= {lat_dev:.0f} m")
    lane_steps = []
    for i in range(1, len(flanes)):
        if round(flanes[i]) != round(flanes[i - 1]):
            lane_steps.append((i * step, round(flanes[i - 1]), round(flanes[i])))
    print("bake: nominal lane changes (compacted stations): " +
          ", ".join(f"{a / MILE_M:.2f} mi {b}->{cc}" for a, b, cc in lane_steps))

    # ── landmark stations in the compacted frame ─────────────────────
    out_marks = []
    for ref in ccfg["landmark_exits"]:
        i = min(range(len(s_uni)), key=lambda k: abs(s_uni[k] - by_exit[ref]))
        out_marks.append((cs[min(i, len(cs) - 1)], ref))
    print("bake: landmark stations (compacted): " +
          ", ".join(f"exit {r} @ {sv / MILE_M:.2f} mi" for sv, r in out_marks))

    # ── exit table + districts (A4) ───────────────────────────────────
    def to_index(lon: float, lat: float, tol: float):
        """(real station index, signed lateral m, + = right of eastbound) or None if off the chain."""
        px, pz = tp.to_engine_m(lon, lat)
        best = min(range(len(xy_m)), key=lambda i: (xy_m[i][0] - px) ** 2 + (xy_m[i][1] - pz) ** 2)
        if math.dist(xy_m[best], (px, pz)) > tol:
            return None
        a, b = xy_m[max(best - 1, 0)], xy_m[min(best + 1, len(xy_m) - 1)]
        lat_m = (px - xy_m[best][0]) * (a[1] - b[1]) + (pz - xy_m[best][1]) * (b[0] - a[0])  # dot with right = (-tz, tx)
        return min(int(round(s_real[best] / step)), len(cs) - 1), lat_m

    def to_compacted(lon: float, lat: float, tol: float):
        hit = to_index(lon, lat, tol)
        return None if hit is None else cs[hit[0]]

    raw_exits, county_starts = extract_exits(osm_path, ocfg["ref_regex"])
    # District 0 is the route's first county by config; the rest start where OSM says.
    out_districts = []
    for k, d in enumerate(ccfg["districts"]):
        nm = d["name"]
        if k == 0:
            d0 = 0.0
        elif "start_exit" in d:  # authored override where the county tags are missing
            hits = [to_compacted(lon, lat, 300.0) for lon, lat, ref, *_ in raw_exits
                    if re.match(r"^%s(?![0-9])" % re.escape(d["start_exit"]), ref)]
            d0 = min(h for h in hits if h is not None)
        elif nm in county_starts:
            d0 = to_compacted(*county_starts[nm], 1e9)
        else:
            sys.exit(f"bake: no mainline way is tagged tiger:county={nm} and no start_exit is set")
        out_districts.append((d0, nm))
    out_exits = []
    for lon, lat, ref, westbound, dest, dref in raw_exits:
        sv = to_compacted(lon, lat, 300.0)  # the westbound carriageway is up to ~100 m off the chain
        if sv is None:
            continue
        district = max(i for i, (d0, _) in enumerate(out_districts) if d0 <= sv)
        out_exits.append((sv, ref, westbound, district, dest, dref))
    out_exits.sort(key=lambda e: e[0])
    print(f"bake: {len(out_exits)} exits ({sum(1 for e in out_exits if not e[2])} eastbound, "
          f"{sum(1 for e in out_exits if e[2])} westbound, {sum(1 for e in out_exits if not e[4])} with no sign text); "
          "districts: " + ", ".join(f"{nm} from {d0 / MILE_M:.2f} mi" for d0, nm in out_districts))

    # Where the drive begins. The corridor's true station 0 is the Queens-Midtown
    # Tunnel's Manhattan portal, whose approach really is a ~100 m-radius loop
    # ramp — an awful first frame and an awful standing start. Start at the first
    # station past `min_start_m` where the road has straightened out, so the pose
    # comes from the data rather than a hand-picked offset.
    f_radii = curve_radii(ftans, step, ccfg["curvature_baseline_m"])
    start_s = ccfg["min_start_m"]
    for i in range(int(ccfg["min_start_m"] / step), len(f_radii)):
        if f_radii[i] >= ccfg["start_min_radius_m"]:
            start_s = i * step
            break
    print(f"bake: start station {start_s:.0f} m (first point past {ccfg['min_start_m']:.0f} m "
          f"with radius >= {ccfg['start_min_radius_m']:.0f} m)")

    # Heights are relative to the start station, so the start pose is unchanged.
    y0 = fy[int(start_s / step)]
    out_srvr = service_roads(cfg["service_roads"], to_index, cs, step, out_exits, out_districts, start_s)
    out_surf = pavement_ranges(cfg.get("surface", {}), out_exits)
    out_xing = crossings(cfg, xy_m, s_real, cs, step, tp, out_districts, total) if "crossings" in cfg else []
    if write:
        write_asset(cfg, tp, fpts, ftans, flanes, [v - y0 for v in fy], fbank, out_marks, out_exits, out_districts,
                    out_srvr, out_surf, total, real_len, c, start_s, out_xing)
    return SimpleNamespace(tp=tp, xy_m=xy_m, s_real=s_real, step=step, cs=cs, fpts=fpts, ftans=ftans, y0=y0, fy=fy)


def pavement_ranges(scfg, exits):
    """D1: [(start, kind)] in compacted metres; a row starts at its eastbound exit ("start" = 0)."""
    out = []
    for r in scfg.get("ranges", []):
        if r["from"] == "start":
            s0 = 0.0
        else:
            hits = [sv for sv, ref, wb, *_ in exits if not wb and re.match(r"^%s(?![0-9])" % re.escape(r["from"]), ref)]
            if not hits:
                sys.exit(f"bake: [surface] no eastbound exit {r['from']}")
            s0 = min(hits)
        out.append((s0, PAVEMENT[r["kind"]]))
    assert all(a[0] < b[0] for a, b in zip(out, out[1:])), "bake: [surface] rows must run west to east"
    print("bake: surface " + ", ".join(f"{k}@{s / MILE_M:.1f} mi" for s, k in out))
    return out


def service_roads(scfg, to_index, cs, step, exits, districts, start_s):
    """C2a: service-road extents per side as [(s0, s1, westbound, lanes, source)] in compacted metres.

    NY 906A = South SR (eastbound), NY 906B = North SR (westbound).  Each OSM way
    projects onto the mainline by its end nodes; spans merge per side across
    breaks shorter than merge_gap_m (cross-street intersections).  Where OSM has
    no ref, the exit-number rule fills: Queens (start to the Nassau line) and
    east of the last OSM stretch to end_exit.  source: 0 = OSM, 1 = rule.
    """
    with open(os.path.join(REPO, scfg["extract"])) as fh:
        ways = json.load(fh)["elements"]
    gap = int(scfg["merge_gap_m"] / step)
    idx = lambda sv: min(bisect.bisect_left(cs, sv), len(cs) - 1)  # noqa: E731 — compacted m -> real index
    nassau = idx(next(d0 for d0, nm in districts if nm == "Nassau"))
    out = []
    for wb in (0, 1):
        ref = "NY 906B" if wb else "NY 906A"
        spans, wrong_side = [], 0
        for w in ways:
            tg = w["tags"]
            if ref not in tg.get("unsigned_ref", "") or "Sunrise" in tg.get("name", ""):
                continue
            hits = [h for h in (to_index(p["lon"], p["lat"], 300.0) for p in (w["geometry"][0], w["geometry"][-1])) if h]
            if not hits:
                continue
            wrong_side += sum(1 for _, t in hits if (t < 0) != bool(wb))
            # Turn-bay counts (4-5) describe intersections, not the road: they get no vote.
            ln = [int(tg["lanes"])] if tg.get("lanes", "") in ("1", "2", "3") else []
            spans.append([min(i for i, _ in hits), max(i for i, _ in hits), ln])
        spans.sort()
        merged = []
        for i0, i1, ln in spans:
            if merged and i0 - merged[-1][1] < gap:
                merged[-1][1] = max(merged[-1][1], i1)
                merged[-1][2] += ln
            else:
                merged.append([i0, i1, ln])
        merged = [m for m in merged if m[1] - m[0] >= gap]  # lone stubs shorter than a block are not a road
        rows = [(i0, i1, int(statistics.median(ln)) if ln else 2, 0) for i0, i1, ln in merged]
        end = [sv for sv, r, w2, *_ in exits if r == scfg["end_exit"] and bool(w2) == bool(wb)]
        rule = [(idx(start_s), min(nassau, merged[0][0]) if merged else nassau)]
        if end and merged:
            rule.append((merged[-1][1], idx(max(end))))
        rows += [(i0, i1, 2, 1) for i0, i1 in rule if i1 - i0 >= gap]
        for src in (0, 1):
            rs = [r for r in rows if r[3] == src]
            print(f"bake: service roads {'WB/North' if wb else 'EB/South'} {'osm ' if src == 0 else 'rule'}: "
                  f"{len(rs)} ranges, {sum(r[1] - r[0] for r in rs) * step / MILE_M:.2f} real mi -> "
                  f"{sum(cs[r[1]] - cs[r[0]] for r in rs) / MILE_M:.2f} baked mi")
        print(f"bake:   {len(spans)} ways, {wrong_side} end nodes on the far side of the centreline")
        out += [(cs[i0], cs[i1], wb, ln, src) for i0, i1, ln, src in rows]
    out.sort()
    # Self-check: sorted, inside the route, both sides present.
    assert all(0.0 <= a[0] < a[1] <= cs[-1] for a in out) and {r[2] for r in out} == {0, 1}
    return out


def crossings(cfg, xy_m, s_real, cs, step, tp, districts, total):
    """D4: structures crossing the LIE as [(s, skew, width_m, kind, style, lanes, rail_under, name)], compacted metres.

    road/rail over = a bridge way that cuts the mainline chain; LIE over = a road or track that cuts one of the
    LIE's own bridge ways. Twin decks (one-way pairs, parallel tracks) within `twin_m` real metres merge.
    skew = signed angle (rad) of the crossing axis off the mainline normal, + toward travel on the right.
    """
    xcfg = cfg["crossings"]
    with open(os.path.join(REPO, xcfg["extract"])) as fh:
        ways = json.load(fh)["elements"]
    rx = re.compile(cfg["osm"]["ref_regex"])
    cell = 200.0
    grid = defaultdict(list)  # chain segments by 200 m cell
    for i in range(len(xy_m) - 1):
        (ax, az), (bx, bz) = xy_m[i], xy_m[i + 1]
        for gx in range(int(math.floor(min(ax, bx) / cell)), int(math.floor(max(ax, bx) / cell)) + 1):
            for gz in range(int(math.floor(min(az, bz) / cell)), int(math.floor(max(az, bz) / cell)) + 1):
                grid[(gx, gz)].append(i)

    def cut(p, q, a, b):  # segment p-q against a-b -> (u along p-q, v along a-b) or None
        d1 = (q[0] - p[0], q[1] - p[1])
        d2 = (b[0] - a[0], b[1] - a[1])
        den = d1[0] * d2[1] - d1[1] * d2[0]
        if abs(den) < 1e-12:
            return None
        w = (a[0] - p[0], a[1] - p[1])
        u = (w[0] * d2[1] - w[1] * d2[0]) / den
        v = (w[0] * d1[1] - w[1] * d1[0]) / den
        return (u, v) if 0.0 <= u <= 1.0 and 0.0 <= v <= 1.0 else None

    def near_segs(p, q):
        out = set()
        for gx in range(int(math.floor(min(p[0], q[0]) / cell)), int(math.floor(max(p[0], q[0]) / cell)) + 1):
            for gz in range(int(math.floor(min(p[1], q[1]) / cell)), int(math.floor(max(p[1], q[1]) / cell)) + 1):
                out.update(grid.get((gx, gz), ()))
        return out

    def chain_hit(pts):  # first cut of a polyline with the chain -> (real s, skew)
        for p, q in zip(pts, pts[1:]):
            for i in near_segs(p, q):
                h = cut(p, q, xy_m[i], xy_m[i + 1])
                if h:
                    a, b = xy_m[i], xy_m[i + 1]
                    L = math.dist(a, b)
                    tx, tz = (b[0] - a[0]) / L, (b[1] - a[1]) / L
                    dx, dz = q[0] - p[0], q[1] - p[1]
                    along, across = dx * tx + dz * tz, dx * -tz + dz * tx  # right of travel = (-tz, tx)
                    if across < 0.0:
                        along, across = -along, -across
                    return s_real[i] + h[1] * L, math.atan2(along, across)
        return None

    def project(pt):  # nearest chain station (real m) of a point
        best = None
        for i in near_segs(pt, pt) or range(len(xy_m) - 1):
            a, b = xy_m[i], xy_m[i + 1]
            L2 = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
            u = max(0.0, min(1.0, ((pt[0] - a[0]) * (b[0] - a[0]) + (pt[1] - a[1]) * (b[1] - a[1])) / L2))
            d = math.dist(pt, (a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1])))
            if best is None or d < best[0]:
                best = (d, s_real[i] + u * math.sqrt(L2))
        return best[1]

    def lanes_of(tg):
        m = re.match(r"\d+", tg.get("lanes", ""))
        return int(m.group()) if m else xcfg["default_lanes"].get(tg.get("highway", ""), 2)

    def width_of(tg):  # metres, curb to curb plus walks / shoulders
        if tg.get("railway"):
            return xcfg["track_m"] * max(1, int(tg.get("tracks", "1") or 1))
        hw = tg.get("highway", "")
        if hw in ("footway", "cycleway", "path", "pedestrian", "steps"):
            return xcfg["walk_bridge_m"]
        m = re.match(r"[\d.]+", tg.get("width", ""))
        if m:
            return float(m.group())
        extra = xcfg["motorway_extra_m"] if hw.startswith(("motorway", "trunk")) else xcfg["street_extra_m"]
        return lanes_of(tg) * xcfg["lane_m"] + extra

    def geo(w):
        return [tp.to_engine_m(g["lon"], g["lat"]) for g in w["geometry"]]

    def is_lie(tg):
        return tg.get("highway") == "motorway" and rx.match(tg.get("ref", ""))

    def bridged(tg):
        return tg.get("bridge", "no") != "no"

    lie_br = [geo(w) for w in ways if is_lie(w["tags"]) and bridged(w["tags"])]
    under_ok = set(xcfg["under_highways"])
    raw = []  # (real s, skew, width, kind, lanes, rail, name, id)
    for w in ways:
        tg = w["tags"]
        if is_lie(tg) or len(w.get("geometry", ())) < 2:
            continue
        name = tg.get("name") or tg.get("ref") or ""
        pts = geo(w)
        if bridged(tg):
            hit = chain_hit(pts)
            if hit:
                rail = bool(tg.get("railway"))
                raw.append((hit[0], hit[1], width_of(tg), 1 if rail else 0, 0 if rail else lanes_of(tg), 0, name, tg))
            continue
        if not (tg.get("highway") in under_ok or tg.get("railway") == "rail"):
            continue
        for br in lie_br:  # a road or track under one of the LIE's bridges
            hit = None
            for p, q in zip(pts, pts[1:]):
                for a, b in zip(br, br[1:]):
                    h = cut(p, q, a, b)
                    if h:
                        hit = (p[0] + h[0] * (q[0] - p[0]), p[1] + h[0] * (q[1] - p[1]))
                        break
                if hit:
                    break
            if hit:
                sr = project(hit)
                # skew from the under-road's direction at the chain
                ch = chain_hit(pts)
                skew = ch[1] if ch else 0.0
                rail = bool(tg.get("railway"))
                raw.append((sr, skew, width_of(tg), 2, 0 if rail else lanes_of(tg), int(rail), name, tg))
                break
    raw.sort(key=lambda r: r[0])
    # A sidewalk mapped as its own footway bridge belongs to the street bridge beside it.
    walk = lambda r: r[2] == xcfg["walk_bridge_m"]  # noqa: E731
    raw = [r for r in raw if not (walk(r) and any(
        q[3] == r[3] and not walk(q) and abs(q[0] - r[0]) < 0.5 * q[2] + xcfg["twin_m"] for q in raw))]

    # Twin decks and street corners: same kind (and same under-thing), footprints within twin_m -> one wider deck.
    merged = []
    for r in raw:
        m = merged[-1] if merged else None
        same = m and m[3] == r[3] and m[5] == r[5]
        if same and r[0] - m[0] < xcfg["twin_m"] + 0.5 * (m[2] + r[2]):
            lo = min(m[0] - 0.5 * m[2], r[0] - 0.5 * r[2])
            hi = max(m[0] + 0.5 * m[2], r[0] + 0.5 * r[2])
            merged[-1] = (0.5 * (lo + hi), 0.5 * (m[1] + r[1]), hi - lo, m[3], m[4] + r[4], m[5],
                          m[6] if m[2] >= r[2] else r[6], m[7])
        else:
            merged.append(r)

    styles = [(re.compile(k), XING_STYLE[v]) for k, v in xcfg["style_by_name"].items()]
    lim = math.radians(xcfg["max_skew_deg"])
    idx = lambda sv: min(sv / step, len(cs) - 1.0)  # noqa: E731 — real m -> fractional uniform index

    def to_cs(sv):
        f = idx(sv)
        i = min(int(f), len(cs) - 2)
        return cs[i] + (f - i) * (cs[i + 1] - cs[i])

    out = []
    for sv, skew, width, kind, lanes, rail, name, tg in merged:
        if sv > s_real[-1]:
            continue
        c = to_cs(sv)
        if c > total:  # past the last station (the east end's run-out)
            continue
        dist = max(i for i, (d0, _) in enumerate(districts) if d0 <= c)
        style = XING_STYLE["through_girder"] if kind == 1 else \
            XING_STYLE["concrete"] if districts[dist][1] == "Suffolk" else XING_STYLE["steel"]
        for rxn, st in styles:  # landmarks: parkway arches, rebuilt concrete crossings (reference sheet)
            if rxn.search(name) and kind != 1:
                style = st
        out.append((c, max(-lim, min(lim, skew)), width, kind, style, min(lanes, 255), rail, name))
    n = Counter((k, st) for _, _, _, k, st, *_ in out)
    print(f"bake: {len(out)} crossings ({len(raw)} raw ways): " + ", ".join(
        f"{[k for k, v in XING_KIND.items() if v == kk][0]}/{[k for k, v in XING_STYLE.items() if v == ss][0]} {c}"
        for (kk, ss), c in sorted(n.items())))
    assert all(0.0 <= r[0] <= total for r in out) and all(a[0] <= b[0] for a, b in zip(out, out[1:]))
    return out


def write_asset(cfg, tp, pts, tans, lanes, ys, banks, marks, exits, districts, srvr, surf, total_m, real_m, c,
                start_m, xing=()) -> None:
    ccfg = cfg["corridor"]
    ft = lambda k: ccfg[k] * FT_M * WORLD_SCALE  # noqa: E731 — feet -> world units

    # MUST stay byte-identical to CorridorHeader in
    # src/scene/Corridor/CorridorAsset.h (which is #pragma pack(1) and read
    # with a single fread) — same rule as toml_baker/RoadConfig.
    header = struct.pack(
        "<IIII" + "f" * 22 + "dddd",
        ASSET_MAGIC, ASSET_VERSION, len(pts), len(marks),
        ccfg["station_spacing_m"] * WORLD_SCALE,
        ccfg["segment_length_m"] * WORLD_SCALE,
        ccfg["tile_length_m"] * WORLD_SCALE,
        ft("lane_width_ft"), ft("median_half_width_ft"),
        ft("inner_shoulder_ft"), ft("outer_shoulder_ft"),
        ft("barrier_width_ft"), ft("barrier_height_ft"),
        ft("grass_extent_ft"), ft("edge_line_ft"),
        ft("dash_length_ft"), ft("dash_gap_ft"),
        ccfg["road_y_wu"], ccfg["grass_y_wu"], ccfg["marking_lift_wu"],
        ccfg["turnaround_length_m"] * WORLD_SCALE,
        start_m * WORLD_SCALE,
        ccfg["landmark_radius_m"] * WORLD_SCALE,
        ccfg["landmark_blend_m"] * WORLD_SCALE,
        c, math.degrees(tp.psi),
        total_m * WORLD_SCALE, real_m * WORLD_SCALE, tp.lat0, tp.lon0,
    )
    body = bytearray()
    for (x, z), (tx, tz), ln, y, bk in zip(pts, tans, lanes, ys, banks):
        body += struct.pack("<ddfffff", x * WORLD_SCALE, z * WORLD_SCALE, tx, tz, ln, y * WORLD_SCALE, bk)
    for sv, ref in marks:
        body += struct.pack("<d8s", sv * WORLD_SCALE, ref.encode()[:8])
    # Optional trailing blocks (tag, count, records) — CorridorExit / CorridorDistrict / CorridorServiceRoad
    # in CorridorAsset.h.  A file may simply end after the landmarks (v1 does).
    body += struct.pack("<II", EXIT_TAG, len(exits))
    for sv, ref, westbound, district, dest, dref in exits:
        body += struct.pack("<d8sBB2x24s76s", sv * WORLD_SCALE, ref.encode()[:7], int(westbound), district,
                            dref.encode()[:23], dest.encode()[:75])
    body += struct.pack("<II", DISTRICT_TAG, len(districts))
    for d0, nm in districts:
        body += struct.pack("<d16s", d0 * WORLD_SCALE, nm.encode()[:15])
    body += struct.pack("<II", SRVR_TAG, len(srvr))
    for s0, s1, westbound, ln, src in srvr:
        body += struct.pack("<ddBBB5x", s0 * WORLD_SCALE, s1 * WORLD_SCALE, westbound, ln, src)
    if surf:  # CorridorPavement
        body += struct.pack("<II", SURF_TAG, len(surf))
        for s0, kind in surf:
            body += struct.pack("<dB7x", s0 * WORLD_SCALE, kind)
    if xing:  # CorridorCrossing
        body += struct.pack("<II", XING_TAG, len(xing))
        for sv, skew, width, kind, style, lanes, rail, name in xing:
            body += struct.pack("<dffBBBB24s", sv * WORLD_SCALE, skew, width * WORLD_SCALE, kind, style, lanes, rail,
                                name.encode()[:23])

    out = os.path.join(REPO, cfg["output"]["asset"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(header)
        fh.write(body)
    print(f"bake: wrote {cfg['output']['asset']}  "
          f"({len(header)} B header + {len(body)} B body, {len(pts)} stations, {len(marks)} landmarks)")


# ══════════════════════════════════════════════════════════════════════

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["fetch", "fetch_srvr", "fetch_xing", "net", "elev", "bake", "all"])
    ap.add_argument("--config", default=os.path.join(REPO, "config", "lie_corridor.toml"))
    ap.add_argument("--force", action="store_true", help="re-pull / re-run even if the cache exists")
    a = ap.parse_args()

    with open(a.config, "rb") as fh:
        cfg = tomllib.load(fh)

    if a.stage in ("fetch", "all"):
        cmd_fetch(cfg, a.force and a.stage == "fetch")
    if a.stage == "fetch_srvr":
        cmd_fetch_srvr(cfg, a.force)
    if a.stage == "fetch_xing":
        cmd_fetch_xing(cfg, a.force)
    if a.stage in ("net", "all"):
        cmd_net(cfg, a.force and a.stage == "net")
    if a.stage in ("elev", "all"):
        cmd_elev(cfg, a.force and a.stage == "elev")
    if a.stage in ("bake", "all"):
        cmd_bake(cfg)


if __name__ == "__main__":
    main()
