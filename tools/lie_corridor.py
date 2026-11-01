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
    net    run SUMO netconvert           -> data/osm/lie_i495.{net,edg,nod}.xml
    bake   project + compact + serialise -> assets/corridor/lie_corridor.bin
    all    fetch (if missing) + net (if missing) + bake

Only `bake` is needed to reproduce the asset from the cached extract, and it
needs neither the network nor SUMO — that is the point of caching both.

Dependencies: Python 3.11+ stdlib only (tomllib, xml.etree, urllib).
"""

from __future__ import annotations

import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tomllib
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 1 scene-metre = 1000 world units (see CLAUDE.md "Units").
WORLD_SCALE = 1000.0
FT_M = 0.3048
MILE_M = 1609.344
# WGS84 semi-major axis; the local tangent plane below only needs one radius.
EARTH_R = 6378137.0

ASSET_MAGIC = 0x53574C43  # "SWLC"
ASSET_VERSION = 1


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
        f'way["highway"="motorway_link"](around.lie:{cfg["ramp_search_radius_m"]})->.ramps;\n'
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
    os.makedirs(os.path.dirname(out), exist_ok=True)
    req = urllib.request.Request(
        ocfg["overpass_url"],
        data=("data=" + urllib.parse.quote(query)).encode(),
        headers={"Content-Type": "application/x-www-form-urlencoded", "User-Agent": "swish-lie-corridor/1.0"},
    )
    with urllib.request.urlopen(req, timeout=300) as resp, open(out, "wb") as fh:
        shutil.copyfileobj(resp, fh)
    print(f"fetch: wrote {ocfg['raw_extract']} ({os.path.getsize(out)} B)")
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


def compact(xy, tans, w, floor, step, c):
    """Integrate the compacted polyline: heading preserved, arc length scaled.

    p'_{i+1} = p'_i + f_i dx t_i, with the ORIGINAL unit tangent t_i.  Total
    turning is therefore preserved exactly, so the route keeps its real shape
    (its map silhouette) and only its scale varies.
    """
    out = [xy[0]]
    marks = [0.0]
    f = scale_profile(w, floor, c)
    for i in range(len(xy) - 1):
        d = f[i] * step
        out.append((out[-1][0] + tans[i][0] * d, out[-1][1] + tans[i][1] * d))
        marks.append(marks[-1] + d)
    return out, marks


# ══════════════════════════════════════════════════════════════════════
# 6. Bake
# ══════════════════════════════════════════════════════════════════════

def cmd_bake(cfg: dict) -> None:
    ccfg, ncfg, ocfg = cfg["corridor"], cfg["netconvert"], cfg["osm"]
    prefix = os.path.join(REPO, ncfg["plain_prefix"])
    osm_path = os.path.join(REPO, ocfg["raw_extract"])

    # ── real centreline + real lane counts (netconvert) ──────────────
    ll, raw_lanes = extract_mainline(prefix)
    tp = TangentPlane.for_route(ll)
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
    c = solve_compaction(w, floor, step, target)
    if not (0.0 < c < 1.0):
        sys.exit(f"bake: solved compression factor c = {c:.4f} is out of (0,1) — "
                 f"target_length_miles is inconsistent with a {real_len / MILE_M:.2f} mi route "
                 f"(landmark-preserved {step * sum(w) / MILE_M:.2f} mi; curvature floors alone "
                 f"force {step * sum(floor) / MILE_M:.2f} mi). Raise target_length_miles, or "
                 f"lower landmark_radius_m / curvature_floor_radius_m.")
    f_prof = scale_profile(w, floor, c)
    cxy, cs = compact(pts, tans, w, floor, step, c)
    binding = sum(1 for i in range(len(w)) if floor[i] > w[i] + c * (1.0 - w[i]) + 1e-9)
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
    fpts, fvals = resample(cxy, cs, [lanes_tap, w], step)
    # Put the west end (Queens-Midtown Tunnel portal) at the engine origin, so
    # the corridor runs 0 -> -L in Z exactly like the retired authored road did
    # and the car's start pose needs no offset.
    ox, oz = fpts[0]
    fpts = [(p[0] - ox, p[1] - oz) for p in fpts]
    ftans = headings(fpts)
    flanes = fvals[0]
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

    write_asset(cfg, tp, fpts, ftans, flanes, out_marks, total, real_len, c, start_s)


def write_asset(cfg, tp, pts, tans, lanes, marks, total_m, real_m, c, start_m) -> None:
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
    for (x, z), (tx, tz), ln in zip(pts, tans, lanes):
        body += struct.pack("<ddfff", x * WORLD_SCALE, z * WORLD_SCALE, tx, tz, ln)
    for sv, ref in marks:
        body += struct.pack("<d8s", sv * WORLD_SCALE, ref.encode()[:8])

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
    ap.add_argument("stage", choices=["fetch", "net", "bake", "all"])
    ap.add_argument("--config", default=os.path.join(REPO, "config", "lie_corridor.toml"))
    ap.add_argument("--force", action="store_true", help="re-pull / re-run even if the cache exists")
    a = ap.parse_args()

    with open(a.config, "rb") as fh:
        cfg = tomllib.load(fh)

    if a.stage in ("fetch", "all"):
        cmd_fetch(cfg, a.force and a.stage == "fetch")
    if a.stage in ("net", "all"):
        cmd_net(cfg, a.force and a.stage == "net")
    if a.stage in ("bake", "all"):
        cmd_bake(cfg)


if __name__ == "__main__":
    main()
