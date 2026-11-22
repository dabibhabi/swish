#!/usr/bin/env python3
"""queens_network.py: N1 Queens network bake (BQE, GCP, Van Wyck, Clearview, Cross Island).

    Overpass (one pull per road)  -> data/osm/queens_<id>.json
    USGS 3DEP (one pull per road) -> data/dem/queens_<id>_elev.csv
    LIE bake in memory (no write) -> the LIE (s, lateral) warp
      -> junctions placed through the warp, per-link similarity fit
        -> assets/corridor/queens_<id>_full.bin (v2 station format) + queens_network_full.bin

Every geometry helper comes from tools/lie_corridor.py; this file only adds the
multi-road parts. Config: config/queens_network.toml. Debug-only assets.

Subcommands: fetch_net, elev_net, bake_net.
"""

from __future__ import annotations

import argparse
import bisect
import cmath
import json
import math
import os
import re
import struct
import sys
import tempfile
import tomllib
import xml.etree.ElementTree as ET
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lie_corridor as lc  # noqa: E402

REPO = lc.REPO
NET_MAGIC = 0x4E4C5753  # "SWLN"
NET_VERSION = 1
ROAD_TAG = 0x44414F52   # "ROAD"
JNCT_TAG = 0x54434E4A   # "JNCT"
WALL_TAG = 0x4C4C4157   # "WALL"
RAMP_TAG = 0x504D4152   # "RAMP"
RPTS_TAG = 0x53545052   # "RPTS"
LMRK_TAG = 0x4B524D4C   # "LMRK": N8 landmarks
# N8 landmark kinds (== road::netmarks::Kind in RoadParams.h)
LMK_KIND = {"gas": 0, "arch": 1, "astoria": 2, "jamaica": 3, "fence": 4, "runway": 5, "terminal": 6, "citi": 7,
            "ashe": 8, "kew": 9, "throgs": 10}
NO_ROAD = 255           # a ramp end on another ramp or a local street
# N7 look columns (CorridorAsset.h): D1 pavement kinds, road::dressing::Lamp, CorridorEdgeKind
SURF_KIND = {"concrete": 1, "fresh": 2, "worn": 3, "patched": 4}
LAMP_KIND = {"none": 0, "twin": 1, "shoebox": 2, "cobra": 3}
EDGE_KIND = {"none": 0, "rail": 1, "wall": 2}
RSTEP = 5.0             # real resample spacing (m) of a road centreline before the fit


def osm_path(rid: str) -> str:
    return os.path.join(REPO, "data", "osm", f"queens_{rid}.json")


def dem_path(rid: str) -> str:
    return os.path.join(REPO, "data", "dem", f"queens_{rid}_elev.csv")


# ══════════════════════════════════════════════════════════════════════
# 1. Fetch
# ══════════════════════════════════════════════════════════════════════

def road_query(net: dict, r: dict) -> str:
    """Mainline motorway ways by tag selector, motorway_links near their nodes, and their junction nodes."""
    s, w, n, e = r["bbox"]
    sel = "".join(f'["{k}"~"{v}"]' for k, v in r["select"].items())
    sel += "".join(f'["{k}"!~"{v}"]' for k, v in r["exclude"].items())
    return (
        "[out:json][timeout:300];\n"
        f'way["highway"="motorway"]{sel}({s},{w},{n},{e})->.m;\n'
        "node(w.m)->.mn;\n"
        f'way(around.mn:{net["ramp_search_radius_m"]})["highway"="motorway_link"]->.l;\n'
        'node.mn["highway"="motorway_junction"]->.j;\n'
        "(.m; .l;);\n"
        "out geom;\n"
        ".j out;\n"
    )


def cmd_fetch_net(net: dict, force: bool) -> None:
    for rid, r in net["roads"].items():
        out = osm_path(rid)
        if os.path.exists(out) and not force:
            print(f"fetch_net: {rid} cached ({os.path.getsize(out)} B)")
            continue
        q = road_query(net, r)
        print(f"fetch_net: {rid} ->\n" + "".join("    " + ln + "\n" for ln in q.splitlines()))
        for k, url in enumerate(net["overpass_urls"]):
            try:
                lc.overpass_post(url, q, out)
                break
            except Exception as ex:  # a busy mirror: try the next one
                print(f"fetch_net: {url} failed ({ex})")
                if k == len(net["overpass_urls"]) - 1:
                    raise
        print(f"fetch_net: wrote {out} ({os.path.getsize(out)} B)")
    for rid, r in net["roads"].items():
        fetch_extra(net, rid, r, force)
    fetch_jlinks(net, force)


def jlinks_path(jid: str) -> str:
    return os.path.join(REPO, "data", "osm", f"queens_jlinks_{jid}.json")


def fetch_jlinks(net: dict, force: bool) -> None:
    """N4: every motorway_link way around each owned LIE junction. The LIE and road extracts only hold links near
    their own mainline nodes, so C/D pieces between them (exit 22's cloverleaf) were missing."""
    for jid in net.get("lie_links", []):
        out = jlinks_path(jid)
        if os.path.exists(out) and not force:
            print(f"fetch_net: jlinks {jid} cached ({os.path.getsize(out)} B)")
            continue
        _, _, lat, lon = net["junctions"][jid]
        q = (f'[out:json][timeout:120];\n(way["highway"="motorway_link"](around:{net["jlinks_radius_m"]},{lat},{lon});\n'
             f'way["highway"~"^(secondary|tertiary)$"]["oneway"="yes"](around:{net["jlinks_radius_m"]},{lat},{lon}););\n'
             "out geom;\n")
        for k, url in enumerate(net["overpass_urls"]):
            try:
                lc.overpass_post(url, q, out)
                with open(out) as fh:
                    json.load(fh)  # a busy mirror answers 200 with an HTML error page
                break
            except Exception as ex:
                print(f"fetch_net: {url} failed ({ex})")
                if os.path.exists(out):
                    os.remove(out)
                if k == len(net["overpass_urls"]) - 1:
                    raise
        print(f"fetch_net: wrote {out} ({os.path.getsize(out)} B)")


def fetch_extra(net: dict, rid: str, r: dict, force: bool) -> None:
    """N3: a road's Y legs (motorway ways by name) and what crosses over it, one Overpass pull each (cached)."""
    sel = "".join(f'["{k}"~"{v}"]' for k, v in r["select"].items())
    s, w, n, e = r["bbox"]
    qs = {}
    if "legs" in r:
        ls, lw, ln, le = r["legs_bbox"]
        lsel = "".join(f'["{k}"~"{v}"]' for k, v in r["legs"].items())
        qs[f"queens_{rid}_legs.json"] = f'[out:json][timeout:120];\nway["highway"="motorway"]{lsel}({ls},{lw},{ln},{le});\nout geom;\n'
    if r.get("crossings") == "bbox":  # N5: `around` a set of ways answers nothing today; the bake keeps only crossers
        qs[f"queens_{rid}_xing.json"] = (
            "[out:json][timeout:300];\n(\n"
            f'  way["bridge"]["bridge"!="no"]["highway"]({s},{w},{n},{e});\n'
            f'  way["bridge"]["bridge"!="no"]["railway"~"^(rail|light_rail|subway)$"]({s},{w},{n},{e});\n'
            ");\nout tags geom;\n")
    elif r.get("crossings"):
        qs[f"queens_{rid}_xing.json"] = (
            "[out:json][timeout:300];\n"
            f'way["highway"="motorway"]{sel}({s},{w},{n},{e})->.m;\n'
            "(\n"
            '  way(around.m:30)["bridge"]["bridge"!="no"]["highway"];\n'
            '  way(around.m:30)["bridge"]["bridge"!="no"]["railway"~"^(rail|light_rail|subway)$"];\n'
            ");\n"
            "out tags geom;\n")
    for name, q in qs.items():
        out = os.path.join(REPO, "data", "osm", name)
        if os.path.exists(out) and not force:
            print(f"fetch_net: {name} cached ({os.path.getsize(out)} B)")
            continue
        for k, url in enumerate(net["overpass_urls"]):
            try:
                lc.overpass_post(url, q, out)
                with open(out) as fh:
                    json.load(fh)  # a busy mirror answers 200 with an HTML error page
                break
            except Exception as ex:
                print(f"fetch_net: {url} failed ({ex})")
                if os.path.exists(out):
                    os.remove(out)
                if k == len(net["overpass_urls"]) - 1:
                    raise
        print(f"fetch_net: wrote {name} ({os.path.getsize(out)} B)")


def load_osm(rid: str):
    with open(osm_path(rid)) as fh:
        els = json.load(fh)["elements"]
    ways = [e for e in els if e["type"] == "way"]
    juncs = [e for e in els if e["type"] == "node"]
    return ways, juncs


# ══════════════════════════════════════════════════════════════════════
# 2. Centreline: walk each carriageway, take the midpoint
# ══════════════════════════════════════════════════════════════════════

def way_layer(tg: dict) -> int:
    m = re.match(r"-?\d+", tg.get("layer", ""))
    if m:
        return int(m.group())
    return 1 if tg.get("bridge", "no") != "no" else -1 if tg.get("tunnel", "no") != "no" else 0


def way_lanes(tg: dict) -> int:
    m = re.match(r"\d+", tg.get("lanes", ""))
    return int(m.group()) if m else 3


def directed(ways: list) -> list:
    """Mainline ways as directed (node ids, lon/lat, lanes, layer); two-way ways count once per direction."""
    out = []
    for w in ways:
        tg = w["tags"]
        if tg.get("highway") != "motorway":
            continue
        nd, ll = w["nodes"], [(g["lon"], g["lat"]) for g in w["geometry"]]
        ow = tg.get("oneway", "yes")
        if ow == "-1":
            nd, ll = nd[::-1], ll[::-1]
        out.append({"id": w["id"], "nodes": nd, "ll": ll, "lanes": way_lanes(tg), "layer": way_layer(tg)})
        if ow == "no":
            out.append({"id": -w["id"], "nodes": nd[::-1], "ll": ll[::-1], "lanes": way_lanes(tg),
                        "layer": way_layer(tg)})
    return out


def bearing(a, b) -> float:
    return math.atan2(b[1] - a[1], b[0] - a[0])  # engine metres (lc._planar_bearing wants lon/lat)


def walk(dways: list, tp, frm, to) -> dict:
    """One carriageway from the seed nearest `frm` towards `to`, minimum-turn at every fork (as extract_mainline).

    Returns {"xy": engine metres, "ll": lon/lat, "lanes", "layer", "nodes": set of OSM node ids}.
    """
    E = lambda ll: tp.to_engine_m(*ll)  # noqa: E731
    f, t = E((frm[1], frm[0])), E((to[1], to[0]))
    goal = (t[0] - f[0], t[1] - f[1])
    by_first = defaultdict(list)
    for w in dways:
        w["xy"] = [E(p) for p in w["ll"]]
        by_first[w["nodes"][0]].append(w)

    def heading_ok(w):  # chord points towards `to`
        a, b = w["xy"][0], w["xy"][-1]
        return (b[0] - a[0]) * goal[0] + (b[1] - a[1]) * goal[1] > 0.0

    seeds = [w for w in dways if heading_ok(w)]
    cur = min(seeds, key=lambda w: min(math.dist(p, f) for p in w["xy"]))
    # Start the seed way at its vertex nearest `frm`.
    k0 = min(min(range(len(cur["xy"])), key=lambda i: math.dist(cur["xy"][i], f)), len(cur["xy"]) - 2)  # keep a heading
    chain = {"xy": cur["xy"][k0:], "ll": cur["ll"][k0:], "lanes": [cur["lanes"]] * (len(cur["xy"]) - k0),
             "layer": [cur["layer"]] * (len(cur["xy"]) - k0), "nodes": set(cur["nodes"][k0:])}
    used = {abs(cur["id"])}
    best = (math.dist(chain["xy"][-1], t), len(chain["xy"]))
    while True:
        here, hd = chain["xy"][-1], bearing(chain["xy"][-2], chain["xy"][-1])
        cands = [w for w in by_first.get(cur["nodes"][-1], []) if abs(w["id"]) not in used]
        if not cands:  # a hole in the extract (untagged way): bridge to a way starting <= 300 m dead ahead
            def ahead(w):
                d = math.dist(here, w["xy"][0])
                off = abs((bearing(here, w["xy"][0]) - hd + math.pi) % (2 * math.pi) - math.pi)
                return d if 0.0 < d < 300.0 and off < math.radians(20.0) else math.inf
            near = min((w for w in dways if abs(w["id"]) not in used), key=ahead, default=None)
            cands = [near] if near is not None and ahead(near) < math.inf else []

        def turn(w):
            b = bearing(w["xy"][0], w["xy"][1])
            return abs((b - hd + math.pi) % (2 * math.pi) - math.pi)
        cands = [w for w in cands if turn(w) < math.radians(100.0)]
        if not cands:
            break
        cur = min(cands, key=turn)
        used.add(abs(cur["id"]))
        skip = 1 if cur["xy"][0] == chain["xy"][-1] else 0
        chain["xy"] += cur["xy"][skip:]
        chain["ll"] += cur["ll"][skip:]
        chain["lanes"] += [cur["lanes"]] * (len(cur["xy"]) - skip)
        chain["layer"] += [cur["layer"]] * (len(cur["xy"]) - skip)
        chain["nodes"].update(cur["nodes"])
        d = min(math.dist(p, t) for p in cur["xy"])
        if d < best[0]:
            best = (d, len(chain["xy"]))
        elif d > best[0] + 2000.0:  # walked well past `to`
            break
    # Trim at the vertex nearest `to`.
    n = min(range(len(chain["xy"])), key=lambda i: math.dist(chain["xy"][i], t)) + 1
    for k in ("xy", "ll", "lanes", "layer"):
        chain[k] = chain[k][:n]
    return chain


def project_poly(xy, s, p):
    """Nearest point of polyline (xy, stations s) to p -> (distance, station, point, unit tangent)."""
    best = None
    for i in range(len(xy) - 1):
        a, b = xy[i], xy[i + 1]
        L2 = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
        if L2 < 1e-12:
            continue
        u = max(0.0, min(1.0, ((p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])) / L2))
        q = (a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1]))
        d = math.dist(p, q)
        if best is None or d < best[0]:
            L = math.sqrt(L2)
            best = (d, s[i] + u * L, q, ((b[0] - a[0]) / L, (b[1] - a[1]) / L))
    return best


def centreline(rid: str, r: dict, tp, step: float):
    """Median centreline of one road (real, engine metres) at `step` spacing, with per-sample lon/lat, lanes, layer.

    Forward carriageway A walks from -> to; B walks to -> from. Each A sample is paired with its nearest B point
    and the centreline is their midpoint, so lanes per side = min(A, B) and the median sits on the alignment.
    """
    ways, juncs = load_osm(rid)
    dw = directed(ways)
    A = walk(dw, tp, r["from"], r["to"])
    B = walk(dw, tp, r["to"], r["from"])
    sA, sB = lc.polyline_stations(A["xy"]), lc.polyline_stations(B["xy"])
    pts, vals = lc.resample(A["xy"], sA, [[p[0] for p in A["ll"]], [p[1] for p in A["ll"]],
                                          [float(v) for v in A["lanes"]], [float(v) for v in A["layer"]]], step)
    bl = [float(v) for v in B["lanes"]]
    by = [float(v) for v in B["layer"]]
    out, lanes, layer, seps, off = [], [], [], [], (0.0, 0.0)
    for i, p in enumerate(pts):
        d, sb, q, _ = project_poly(B["xy"], sB, p)
        j = min(range(len(sB)), key=lambda k: abs(sB[k] - sb))
        if d < 250.0 and 0.0 < sb < sB[-1]:  # B runs alongside here: keep its offset (carried past B's ends)
            off = ((q[0] - p[0]) * 0.5, (q[1] - p[1]) * 0.5)
            seps.append(d)
            lanes.append(min(vals[2][i], bl[j]))
            layer.append(max(vals[3][i], by[j]))
        else:
            lanes.append(vals[2][i])
            layer.append(vals[3][i])
        out.append((p[0] + off[0], p[1] + off[1]))
    # Uniform again along the centreline itself. shortcut: per-sample data keeps the A-sample index (the two
    # lengths differ by < 0.3 %), so a DEM sample drifts at most a few metres; re-pull per centreline if it matters.
    out, (lanes, layer, lon, lat) = lc.resample(out, lc.polyline_stations(out), [lanes, layer, vals[0], vals[1]], step)
    print(f"centreline {rid}: A {sA[-1]:.0f} m ({len(A['xy'])} pts), B {sB[-1]:.0f} m; carriageway separation "
          f"median {sorted(seps)[len(seps) // 2]:.0f} m, max {max(seps):.0f} m; real centreline {len(out) * step:.0f} m")
    return {"xy": out, "ll": list(zip(lon, lat)), "lanes": lanes, "layer": layer, "A": A, "B": B, "ways": ways,
            "juncs": juncs}


# ══════════════════════════════════════════════════════════════════════
# 3. The LIE warp: real p -> (real LIE s, lateral d) -> compact LIE s -> compact pos + d * normal
# ══════════════════════════════════════════════════════════════════════

def lerp_at(P, T, step, sv):
    """Station table point and renormalised lerped unit tangent at sv: CorridorAlignment::sample, in metres."""
    f = min(max(sv / step, 0.0), len(P) - 1.0)
    i = min(int(f), len(P) - 2)
    u = f - i
    tx, tz = T[i][0] + u * (T[i + 1][0] - T[i][0]), T[i][1] + u * (T[i + 1][1] - T[i][1])
    n = math.hypot(tx, tz)
    return (P[i][0] + u * (P[i + 1][0] - P[i][0]), P[i][1] + u * (P[i + 1][1] - P[i][1])), (tx / n, tz / n)


def foot(P, T, step, p, sv):
    """(s, t) with sample(s) + t * right(s) = p, Newton from sv; right = (-tz, tx)."""
    for _ in range(20):
        (x, z), (tx, tz) = lerp_at(P, T, step, sv)
        ds = (p[0] - x) * tx + (p[1] - z) * tz
        sv += ds
        if abs(ds) < 1e-6:
            break
    (x, z), (tx, tz) = lerp_at(P, T, step, sv)
    return sv, (p[0] - x) * -tz + (p[1] - z) * tx


class LieWarp:
    r"""p -> (real LIE station s, lateral d) -> compact station s_c -> compact LIE point + d * (lateral axis).

    The lateral axis is the corridor frame's fixed cross axis (engine +X = right of eastbound), not the local
    normal: with the local normal, points a few km off the LIE sit past its centres of curvature and the
    nearest-point projection jumps (measured det -103 at the GCP 4 km north of exit 21). With a fixed axis,
    s(p) solves P_z(s) = p_z, unique while the LIE heads east (checked below), and det dW/dp = f(s) > 0.
    """

    def __init__(self, L, max_real_m: float, min_real_m: float = 1000.0):
        self.L = L
        idx = [i for i, v in enumerate(L.s_real) if min_real_m <= v <= max_real_m]  # past the tunnel portal loop
        self.xy, self.s = [L.xy_m[i] for i in idx], [L.s_real[i] for i in idx]
        bad = sum(1 for a, b in zip(self.xy, self.xy[1:]) if b[1] >= a[1])
        assert bad == 0, f"LIE z is not monotone over {min_real_m}..{max_real_m} m ({bad} segments): warp would fold"
        self._negz = [-p[1] for p in self.xy]

    def frame(self, p):
        """(real LIE station, signed lateral along +X, + = right of eastbound travel)."""
        i = min(max(bisect.bisect_left(self._negz, -p[1]) - 1, 0), len(self.xy) - 2)
        a, b = self.xy[i], self.xy[i + 1]
        u = (p[1] - a[1]) / (b[1] - a[1])  # extrapolates past the ends (not used there)
        return self.s[i] + u * (self.s[i + 1] - self.s[i]), p[0] - (a[0] + u * (b[0] - a[0]))

    def compact_s(self, s_real: float) -> float:
        f = min(max(s_real / self.L.step, 0.0), len(self.L.cs) - 1.0)
        i = min(int(f), len(self.L.cs) - 2)
        return self.L.cs[i] + (f - i) * (self.L.cs[i + 1] - self.L.cs[i])

    def at(self, sc: float):
        return lerp_at(self.L.fpts, self.L.ftans, self.L.step, sc)

    def __call__(self, p):
        s, d = self.frame(p)
        (x, z), _ = self.at(self.compact_s(s))
        return (x + d, z)

    def jacobian(self, p, h: float = 5.0) -> float:
        """det dW/dp by central differences: > 0 everywhere = no fold."""
        ax, bx = self((p[0] + h, p[1])), self((p[0] - h, p[1]))
        az, bz = self((p[0], p[1] + h)), self((p[0], p[1] - h))
        return ((ax[0] - bx[0]) * (az[1] - bz[1]) - (ax[1] - bx[1]) * (az[0] - bz[0])) / (4.0 * h * h)


# ══════════════════════════════════════════════════════════════════════
# 4. Bake
# ══════════════════════════════════════════════════════════════════════

def closest_approach(a_xy, a_s, b_xy, b_s, hint, radius):
    """Closest pair between two polylines near `hint` -> (point on a, s on a, s on b, gap)."""
    best = None
    for p in b_xy:
        if math.dist(p, hint) > radius:
            continue
        d, sa, q, _ = project_poly(a_xy, a_s, p)
        if best is None or d < best[0]:
            best = (d, sa, q, p)
    if best is None:
        sys.exit(f"bake_net: no road within {radius:.0f} m of junction hint {hint}")
    d, sa, q, p = best
    # The point ON road a: a crossing is within ~1 m either way; at a merge (the BQE's Y into the GCP) it is road
    # a's end, so road a lands exactly and road b keeps the real offset in its lateral t.
    return q, sa, project_poly(b_xy, b_s, q)[1], d


def cmd_bake_net(net: dict) -> None:
    with open(os.path.join(REPO, net["lie_config"]), "rb") as fh:
        lie_cfg = tomllib.load(fh)
    print("bake_net: re-running the LIE bake in memory (no write) for the warp")
    L = lc.cmd_bake(lie_cfg, write=False)
    tp, step = L.tp, L.step
    W = LieWarp(L, 30000.0)
    ecfg, ccfg = lie_cfg["elevation"], lie_cfg["corridor"]

    roads = {}
    for rid, r in net["roads"].items():
        c = centreline(rid, r, tp, RSTEP)
        c["s"] = lc.polyline_stations(c["xy"])
        c["cfg"] = r
        # The DEM cache is indexed by forward-carriageway sample (index x RSTEP), not by centreline arc length.
        c["elev"] = lc.load_elevation({"elevation": {"cache": dem_path(rid)}}, [i * RSTEP for i in range(len(c["xy"]))])
        roads[rid] = c

    # ── junctions (real) ─────────────────────────────────────────────
    jcfg = net["junctions"]
    junctions = []  # dicts
    for jid, (ra, rb, lat, lon) in ((k, v) for k, v in jcfg.items() if isinstance(v, list)):
        hint = tp.to_engine_m(lon, lat)
        a_xy, a_s = (W.xy, W.s) if ra == "lie" else (roads[ra]["xy"], roads[ra]["s"])
        mid, sa, sb, gap = closest_approach(a_xy, a_s, roads[rb]["xy"], roads[rb]["s"], hint, jcfg["search_m"])
        junctions.append({"id": jid, "a": ra, "b": rb, "p": mid, "s_real": {ra: sa, rb: sb}, "gap": gap})
        print(f"junction {jid}: {ra} x {rb} gap {gap:.1f} m, real s {ra} {sa:.0f} m, {rb} {sb:.0f} m, "
              f"{math.dist(mid, hint):.0f} m from the hint")

    # ── warp fold check over every road's real points ────────────────
    jmin, jworst = math.inf, None
    for rid, c in roads.items():
        for p in c["xy"][::10]:
            dj = W.jacobian(p)
            if dj < jmin:
                jmin, jworst = dj, (rid, p, W.frame(p))
    jj = [W.jacobian(j["p"]) for j in junctions]
    print(f"warp: Jacobian det min {jmin:.3f} over all road points ({jworst[0]}, LIE s {jworst[2][0]:.0f} m, "
          f"lateral {jworst[2][1]:.0f} m); at junctions {min(jj):.3f}..{max(jj):.3f} "
          f"-> {'FOLDS' if jmin <= 0.0 else 'no fold'}")

    # ── per-road fit, resample, profile ──────────────────────────────
    ids = {"lie": 0, **{rid: r["id"] for rid, r in net["roads"].items()}}
    names = {"lie": "LIE", **{rid: r["name"] for rid, r in net["roads"].items()}}
    out_j = {j["id"]: {"x": W(j["p"])} for j in junctions}
    walls, road_tf, report = [], {}, {}
    for rid, c in roads.items():
        r = c["cfg"]
        mine = sorted((j for j in junctions if rid in (j["a"], j["b"])), key=lambda j: j["s_real"][rid])
        n = len(c["xy"])
        ij = [min(int(round(j["s_real"][rid] / RSTEP)), n - 1) for j in mine]  # junction = nearest vertex (<= 2.5 m)
        P = [complex(*c["xy"][i]) for i in ij]
        Q = [complex(*W((p.real, p.imag))) for p in P]

        # Arc length first, as on the LIE: heading kept, f_i = max(c_k, R_floor / R_i), with c_k solved per link
        # so the compacted chord matches the placed one. The similarity left over is then ~a pure rotation.
        tans = lc.headings(c["xy"])
        radii = lc.curve_radii(tans, RSTEP, ccfg["curvature_baseline_m"])
        floor = lc.curvature_floor(radii, net["curvature_floor_radius_m"], RSTEP, ccfg["curvature_smooth_m"])

        def chord(i0, i1, ck):
            x = z = 0.0
            for i in range(i0, i1):
                d = max(ck, floor[i]) * RSTEP
                x, z = x + tans[i][0] * d, z + tans[i][1] * d
            return math.hypot(x, z)
        cks = []
        for k in range(len(ij) - 1):
            target, lo_, hi_ = abs(Q[k + 1] - Q[k]), 0.0, 1.0
            if chord(ij[k], ij[k + 1], 1.0) <= target or chord(ij[k], ij[k + 1], 0.0) >= target:
                cks.append(1.0 if chord(ij[k], ij[k + 1], 1.0) <= target else 0.0)  # similarity scale takes the rest
                continue
            for _ in range(50):
                mid = 0.5 * (lo_ + hi_)
                lo_, hi_ = (mid, hi_) if chord(ij[k], ij[k + 1], mid) < target else (lo_, mid)
            cks.append(0.5 * (lo_ + hi_))
        link_of = lambda i: min(max(bisect.bisect_right(ij, i) - 1, 0), len(cks) - 1)  # noqa: E731 (end links: neighbour)
        f = [max(cks[link_of(i)], floor[i]) for i in range(n)]
        C = [complex(*p) for p in lc.compact(c["xy"], tans, f, RSTEP)[0]]
        # Link k maps [C_k, C_k+1] -> [Q_k, Q_k+1]: T(z) = a (z - C_k) + Q_k.
        tf = [((Q[k + 1] - Q[k]) / (C[ij[k + 1]] - C[ij[k]]), C[ij[k]], Q[k]) for k in range(len(ij) - 1)]
        # Ramps work in real space: near J the compaction is ~ a scale f_J about P_J, so the local map is a f_J.
        road_tf[rid] = {j["id"]: [(tf[k][0] * f[ij[m]], P[m], Q[m]) for k in (m - 1, m) if 0 <= k < len(tf)]
                        for m, j in enumerate(mine)}
        b = net["blend_m"] / RSTEP
        assert all(min(ij[m] - ij[m - 1], ij[m + 1] - ij[m]) > b for m in range(1, len(ij) - 1)), \
            f"{rid}: blend_m reaches a neighbour junction"

        def T(i):
            ev = lambda t: t[0] * (C[i] - t[1]) + t[2]  # noqa: E731
            for m in range(1, len(ij) - 1):  # cross-fade around interior junctions: no kink, J still exact
                if abs(i - ij[m]) < b:
                    w = lc.smoothstep((i - ij[m] + b) / (2.0 * b))
                    return (1.0 - w) * ev(tf[m - 1]) + w * ev(tf[m])
            return ev(tf[link_of(i)])
        cxy = [(z.real, z.imag) for z in map(T, range(n))]
        # One straight station past the data end, so a junction at the very end (BQE into the GCP) has a station.
        e = (cxy[-1][0] - cxy[-2][0], cxy[-1][1] - cxy[-2][1])
        cxy.append((cxy[-1][0] + e[0] / math.hypot(*e) * step, cxy[-1][1] + e[1] / math.hypot(*e) * step))
        cs = lc.polyline_stations(cxy)
        cs_of = lambda v: cs[min(max(int(round(v / RSTEP)), 0), n - 1)]  # noqa: E731 (real s -> compact s)
        sj_c = [cs[i] for i in ij]
        ext = lambda v: v + v[-1:]  # noqa: E731
        sr = [i * RSTEP for i in range(n + 1)]

        # Walls ([end, junction, real metres past it, outward]) and the tails past them; no wall = data end.
        wl, wall_ends = [0.0, cs[n - 1]], []
        for end, jid, run in r["walls"]:
            sv = next(j["s_real"][rid] for j in mine if j["id"] == jid) + (run if end else -run)
            wl[end] = min(max(cs_of(sv), 0.0), cs[n - 1])
            wall_ends.append(end)
        lo = max(wl[0] - r["tail_m"], 0.0) if 0 in wall_ends else 0.0
        hi = min(wl[1] + r["tail_m"], cs[n - 1]) if 1 in wall_ends else cs[n - 1]

        # Elevation: USGS ground + OSM layer, on compact stations, LIE datum. N3: a `profile` row (real lat range,
        # roads.md) forces a viaduct (ground + h, at least the OSM layer) or a trench (ground - h) over its range.
        lat = [ll[1 if r.get("profile_axis", "lat") == "lat" else 0] for ll in c["ll"]]  # N4: an E-W road keys by lon
        prof = [next((p for p in r.get("profile", []) if p[0] <= v <= p[1]), None) for v in lat]
        raw_y = ext([e + max(ly * net["layer_m"], p[3]) if p and p[2] == "viaduct" else e - p[3] if p else
                     e + ly * net["layer_m"] for e, ly, p in zip(c["elev"], c["layer"], prof)])
        # N3: lane floor per real lat range (OSM tags 2 where a ramp lane splits off; roads.md has 3 through)
        lanes_in = [max(v, max((n for a, b_, n in r.get("lane_floor", []) if a <= la <= b_), default=0))
                    for v, la in zip(c["lanes"], lat)]
        via = ext([1.0 if p and p[2] == "viaduct" else 0.0 for p in prof])
        # N7: roadside look row per real sample (roads.md per-stretch table), first match wins; -1 = none
        lrows = r.get("look", [])
        lk = ext([float(next((k for k, w in enumerate(lrows) if w[0] <= v <= w[1]), -1)) for v in lat])
        fpts, fv = lc.resample(cxy, cs, [ext(lanes_in), ext(c["layer"]), raw_y, sr, ext(c["elev"]), via, lk], step)
        i0, i1 = int(math.ceil(lo / step - 1e-9)), min(int(math.ceil(hi / step - 1e-9)), len(fpts) - 1)
        fpts = fpts[i0:i1 + 1]
        fv = [v[i0:i1 + 1] for v in fv]
        s0 = i0 * step
        ftans = lc.headings(fpts)
        lanes = lc.taper(lc.nominal_lanes(fv[0], step, 1000.0 if "lane_floor" not in r else 200.0), step,
                         ccfg["lane_taper_m"])
        fy, g_max, a_max = lc.vertical_profile(fv[2], step, ecfg)
        fbank = lc.superelevation(ftans, step, ecfg)
        total = step * (len(fpts) - 1)
        sjr = [v - s0 for v in sj_c]
        wlr = [v - s0 for v in wl]
        # Radius audit on the drivable range (between the walls): compact vs real.
        fr = lc.curve_radii(ftans, step, ccfg["curvature_baseline_m"])
        real_at = [radii[min(int(round(v / RSTEP)), n - 1)] for v in fv[3]]
        drive = [i for i in range(len(fpts)) if wlr[0] <= i * step <= wlr[1]]
        invented = [i for i in drive if fr[i] < net["curvature_floor_radius_m"] and real_at[i] >= net["exempt_radius_m"]]
        kinks = [math.degrees(abs(cmath.phase(tf[k + 1][0] / tf[k][0]))) for k in range(len(tf) - 1)]
        eff = [(cs[ij[k + 1]] - cs[ij[k]]) / ((ij[k + 1] - ij[k]) * RSTEP) for k in range(len(tf))]
        report[rid] = {
            "scales": eff, "kinks": kinks,
            "min_r": min(fr[i] for i in drive), "min_r_real": min(real_at[i] for i in drive),
            "min_r_nonexempt": min((fr[i] for i in drive if real_at[i] >= net["exempt_radius_m"]), default=math.inf),
            "invented_km": len(invented) * step / 1000.0, "len": total, "drive": wlr[1] - wlr[0]}
        print(f"bake_net {rid}: links " + ", ".join(
            f"{mine[k]['id']}->{mine[k + 1]['id']} scale {eff[k]:.3f} (c {cks[k]:.3f}, similarity x{abs(t[0]):.3f} "
            f"rot {math.degrees(cmath.phase(t[0])):+.1f} deg)"
            for k, t in enumerate(tf)) + (f"; seam kinks {', '.join(f'{v:.1f}' for v in kinks)} deg (blended)" if kinks else ""))
        print(f"bake_net {rid}: {total:.0f} m compact ({wlr[1] - wlr[0]:.0f} m drivable, walls at "
              f"{wlr[0]:.0f} / {wlr[1]:.0f} m); radius min {report[rid]['min_r']:.0f} m (real {report[rid]['min_r_real']:.0f}); "
              f"min where real >= {net['exempt_radius_m']:.0f} m: {report[rid]['min_r_nonexempt']:.0f} m; "
              f"{report[rid]['invented_km']:.2f} km under the floor that is not really that tight")
        print(f"bake_net {rid}: elevation {min(fy):.1f}..{max(fy):.1f} m, grade {100 * g_max:.2f} %, "
              f"a_v {a_max:.2f} m/s^2; lanes {min(lanes):.1f}..{max(lanes):.1f}")

        # Exits: motorway_junction nodes on this road's carriageways; westbound = against +s (carriageway B).
        link_from = defaultdict(list)
        for w in c["ways"]:
            if w["tags"].get("highway") == "motorway_link":
                link_from[w["nodes"][0]].append(w["tags"])
        exits = []
        for nd in c["juncs"]:
            tg = nd.get("tags", {})
            onA, onB = nd["id"] in c["A"]["nodes"], nd["id"] in c["B"]["nodes"]
            if not tg.get("ref") or not (onA or onB):
                continue
            sv = cs_of(project_poly(c["xy"], c["s"], tp.to_engine_m(nd["lon"], nd["lat"]))[1]) - s0
            if not 0.0 <= sv <= total:
                continue
            dest = "; ".join(dict.fromkeys(d for t in link_from[nd["id"]] for d in t.get("destination", "").split(";") if d))
            dref = "; ".join(dict.fromkeys(d for t in link_from[nd["id"]] for d in t.get("destination:ref", "").split(";") if d))
            exits.append((sv, tg["ref"], not onA, 0, dest, dref))
            if wlr[0] < sv < wlr[1]:
                c.setdefault("exit_nodes", []).append((nd["id"], sv, tg["ref"]))  # N6: local-exit stubs below
        exits.sort(key=lambda e: e[0])
        marks = sorted((v, names[j["b"] if j["a"] == rid else j["a"]]) for v, j in zip(sjr, mine))
        # N3: per-road cross-section overrides (BQE: no shoulders where real, roads.md)
        rcfg = {**lie_cfg, "corridor": {**ccfg, **r.get("corridor", {})},
                "output": {"asset": f"assets/corridor/queens_{rid}_full.bin"}}
        ys = [v - L.y0 for v in fy]  # the LIE's datum: heights relative to the LIE start station
        surf, ground = [], []
        look = []
        if "profile" in r:  # N3: concrete deck on the viaduct (D1 kinds: 1 concrete, 3 worn asphalt); DEM ground
            for i, (v, li) in enumerate(zip(fv[5], fv[6])):
                row = lrows[int(round(li))] if lrows and li > -0.5 else None
                k = SURF_KIND[row[5]] if row and row[5] != "auto" else 1 if v > 0.5 else 3  # N7: per-stretch override
                if not surf or surf[-1][1] != k:
                    surf.append((i * step, k))
                if row:  # N7: (start, kit exit, lamp, edge), merged
                    rec = (row[2], LAMP_KIND[row[3]], EDGE_KIND[row[4]])
                    if not look or look[-1][1:] != rec:
                        look.append((i * step, *rec))
            ground = [g - L.y0 for g in lc.box_smooth(fv[4], max(1, int(12.5 / step)))]
        xing = []
        if r.get("crossings"):  # N3: what crosses over this road, the LIE's D4 rule (its own ramps and legs dropped)
            with open(os.path.join(REPO, "data", "osm", f"queens_{rid}_xing.json")) as fh:
                els = json.load(fh)["elements"]
            ex = r.get("crossing_exclude")  # N4: drawn by another structure (D4 LIE bridge decks, Y legs)
            keep = [e for e in els if not e["tags"].get("highway", "").startswith("motorway") and
                    not (ex and re.search(ex, e["tags"].get("name", "")))]
            with tempfile.TemporaryDirectory() as td:
                path = os.path.join(td, "xing.json")
                with open(path, "w") as fh:
                    json.dump({"elements": keep}, fh)
                xc = {**lie_cfg, "crossings": {**lie_cfg["crossings"], "extract": path,  # N4: per-road styles first
                                               "style_by_name": {**r.get("crossing_styles", {}),
                                                                 **lie_cfg["crossings"]["style_by_name"]}},
                      "osm": {**lie_cfg["osm"], "ref_regex": "^$"}}
                xing = [(v - s0, *rest) for v, *rest in
                        lc.crossings(xc, c["xy"], c["s"], cs, RSTEP, tp, [(0.0, "Queens")], cs[-1]) if 0.0 <= v - s0 <= total]
        lc.write_asset(rcfg, tp, fpts, ftans, lanes, ys, fbank, marks, exits, [(0.0, "Queens")], [], surf, total,
                       fv[3][-1] - fv[3][0], sum(cks) / len(cks), wlr[0], xing=xing, ground=ground, look=look)
        print(f"bake_net {rid}: {len(exits)} exits")

        walls += [(ids[rid], e, wlr[e]) for e in wall_ends]
        c["out"] = {"s0": s0, "sj": dict(zip((j["id"] for j in mine), sjr)), "fpts": fpts, "ftans": ftans, "ys": ys,
                    "cs": cs,
                    "q": {j["id"]: (q.real, q.imag) for j, q in zip(mine, Q)}}

    # ── JNCT: (road, s, lateral) per side, placed point ──────────────
    jrec = []
    for k, j in enumerate(junctions):
        x = out_j[j["id"]]["x"]
        side = []
        for rd in (j["a"], j["b"]):  # (s, t) on each road's station table that lands on the placed point
            if rd == "lie":
                side.append(foot(L.fpts, L.ftans, step, x, W.compact_s(j["s_real"]["lie"])))
            else:
                o = roads[rd]["out"]
                side.append(foot(o["fpts"], o["ftans"], step, x, o["sj"][j["id"]]))
        jrec.append((k, ids[j["a"]], ids[j["b"]], side, x, j["id"]))

    # ── RAMP: motorway_link chains per junction, through that junction's mean link fit ──
    # N3: an end attaches to a road only through a shared OSM node (the N1 25 m rule tied fragments in a tangle to
    # the wrong road), and links joined end to start with no fork are merged into one ramp (no fragments).
    ground = [gp for c in roads.values() for gp in zip(c["xy"][::4], c["elev"][::4])]  # (real point, DEM ground m)
    root = ET.parse(os.path.join(REPO, lie_cfg["osm"]["raw_extract"])).getroot()
    nodes = {int(n.get("id")): (float(n.get("lon")), float(n.get("lat"))) for n in root.findall("node")}
    rx = re.compile(lie_cfg["osm"]["ref_regex"])
    lie_cw, node_road, links = [], {}, {}
    lie_link_j = [j for j in junctions if j["id"] in net.get("lie_links", [])]
    for w in root.findall("way"):
        tg = {t.get("k"): t.get("v") for t in w.findall("tag")}
        nd = [int(n.get("ref")) for n in w.findall("nd")]
        if any(n not in nodes for n in nd):
            continue
        xy = [tp.to_engine_m(*nodes[n]) for n in nd]
        if tg.get("highway") == "motorway" and rx.match(tg.get("ref", "")):
            if min(math.dist(p, j["p"]) for p in xy for j in junctions) < 3000.0:
                lie_cw.append(xy)
                node_road.update({n: 0 for n in nd})
        elif tg.get("highway") == "motorway_link" and lie_link_j and \
                min(math.dist(p, j["p"]) for p in xy for j in lie_link_j) < net["chain_radius_m"]:
            links[int(w.get("id"))] = {"nodes": nd, "xy": xy, "tags": tg, "lie": True}  # the LIE's own ramps where N3+ owns it
    # N4: one-way service-road node -> real point, and its next / previous node (the LIE's C/D at exit 22: Horace Harding)
    srv, srv_nx, srv_pv = {}, {}, {}
    for jid in net.get("lie_links", []):  # N4: the junction's whole link set (serves owned junctions only, as the LIE's)
        if os.path.exists(jlinks_path(jid)):
            with open(jlinks_path(jid)) as fh:
                els = [w for w in json.load(fh)["elements"] if w["type"] == "way"]
            # N5: an unnamed signed connector tagged secondary / tertiary (LIE EB -> GCP EB: 25419995 "Grand Central
            # Parkway East" off the Horace Harding; LIE WB -> Van Wyck SB: "Van Wyck Expressway; Kennedy Airport") is a
            # ramp where it meets a motorway_link, directly or through another such connector
            ends = {n for w in els if w["tags"].get("highway") == "motorway_link" for n in (w["nodes"][0], w["nodes"][-1])}
            conn, grew = set(), True
            while grew:
                grew = False
                for w in els:
                    if w["id"] not in conn and w["tags"].get("highway") != "motorway_link" and "destination" in w["tags"] \
                            and "name" not in w["tags"] and (w["nodes"][0] in ends or w["nodes"][-1] in ends):
                        conn.add(w["id"])
                        ends.update((w["nodes"][0], w["nodes"][-1]))
                        grew = True
            for w in els:
                if w["id"] in conn:
                    w = {**w, "tags": {**w["tags"], "highway": "motorway_link"}}
                if w["tags"].get("highway") != "motorway_link":  # a one-way service road
                    if jid in net["srv_junctions"]:
                        srv.update({n: tp.to_engine_m(g["lon"], g["lat"]) for n, g in zip(w["nodes"], w["geometry"])})
                        srv_nx.update(zip(w["nodes"], w["nodes"][1:]))
                        srv_pv.update(zip(w["nodes"][1:], w["nodes"]))
                elif w["id"] not in links:
                    links[w["id"]] = {"nodes": w["nodes"], "tags": w["tags"], "lie": True,
                                      "xy": [tp.to_engine_m(g["lon"], g["lat"]) for g in w["geometry"]]}
    for rid, c in roads.items():
        for w in c["ways"]:
            hw = w["tags"].get("highway")
            if hw == "motorway":
                for n in w["nodes"]:
                    node_road.setdefault(n, ids[rid])
            elif hw == "motorway_link" and w["id"] in links:
                links[w["id"]]["lie"] = False  # N4: a network road's own link too (kept wherever it lands)
            elif hw == "motorway_link":
                links[w["id"]] = {"nodes": w["nodes"], "tags": w["tags"],
                                  "xy": [tp.to_engine_m(g["lon"], g["lat"]) for g in w["geometry"]]}
    legs = {}  # N3: Y-leg way id -> (its road, the other road at its junction, junction index)
    for rid, r in net["roads"].items():
        if "legs" not in r:
            continue
        with open(os.path.join(REPO, "data", "osm", f"queens_{rid}_legs.json")) as fh:
            for w in json.load(fh)["elements"]:
                if w["type"] == "way":
                    links[w["id"]] = {"nodes": w["nodes"], "tags": w["tags"],
                                      "xy": [tp.to_engine_m(g["lon"], g["lat"]) for g in w["geometry"]]}
                    k = next(i for i, j in enumerate(junctions) if j["id"] == r["legs_junction"])
                    legs[w["id"]] = (rid, junctions[k]["b"] if junctions[k]["a"] == rid else junctions[k]["a"], k)
    for w in links.values():
        if w["tags"].get("oneway") == "-1":
            w["nodes"], w["xy"] = w["nodes"][::-1], w["xy"][::-1]
    chains = [(0, xy) for xy in lie_cw] + [(ids[rid], cw["xy"]) for rid, c in roads.items() for cw in (c["A"], c["B"])]

    lie_d = lambda p: min(project_poly(xy, lc.polyline_stations(xy), p)[0] for xy in lie_cw if len(xy) > 1)  # noqa: E731

    def attach(n, p, owned=False):  # the road owning end node n, if its carriageway is really there; else NO_ROAD
        rd = node_road.get(n)
        if rd is None and owned and n in srv:  # N4: a link ending on the LIE's service road (its C/D) merges into the LIE
            return 0 if lie_d(p) < net["srv_attach_m"] else NO_ROAD
        if rd is None:
            return NO_ROAD
        d = min(project_poly(xy, lc.polyline_stations(xy), p)[0] for r, xy in chains if r == rd and len(xy) > 1)
        return rd if d < net["road_end_tol_m"] else NO_ROAD

    def junction_of(xy):
        dj = [min(math.dist(p, j["p"]) for p in xy) for j in junctions]
        k = min(range(len(junctions)), key=lambda q: dj[q])
        return k if dj[k] <= net["junction_ramp_radius_m"] else None

    jof = {i: legs[i][2] if i in legs else junction_of(w["xy"]) for i, w in links.items()}
    # N4: the LIE's own links serve only the junctions that own them; one nearer an unowned junction (the Van Wyck, N5)
    # joins an owned one only through its chain below
    lie_k = {k for k, j in enumerate(junctions) if j in lie_link_j}
    jof = {i: None if links[i].get("lie") and k not in lie_k else k for i, k in jof.items()}
    # A movement's far pieces (an exit ramp leaving the LIE a kilometre upstream) join their junction through the
    # chain: a link sharing an end node with an assigned one, within chain_radius_m of that junction, is assigned too.
    grew = True
    while grew:
        grew = False
        at_node = defaultdict(set)
        for i, w in links.items():
            if jof[i] is not None:
                at_node[w["nodes"][0]].add(jof[i])
                at_node[w["nodes"][-1]].add(jof[i])
        for i, w in links.items():
            if jof[i] is not None:
                continue
            for k in at_node.get(w["nodes"][0], set()) | at_node.get(w["nodes"][-1], set()):
                if min(math.dist(p, junctions[k]["p"]) for p in w["xy"]) < net["chain_radius_m"]:
                    jof[i], grew = k, True
                    break

    # N4: a link of a service-road junction ending on the service road away from the LIE follows the service road
    # (downstream from its end, upstream to its start) to where it runs beside the LIE, so it merges there
    srv_k = {k for k, j in enumerate(junctions) if j["id"] in net["srv_junctions"]}
    for w in (w for i, w in links.items() if jof[i] in srv_k):
        for e, step_ in ((-1, srv_nx), (0, srv_pv)):
            n, run = w["nodes"][e], []
            if n not in srv or n in node_road or lie_d(srv[n]) < net["srv_attach_m"]:
                continue
            while n in step_ and len(run) < 200 and step_[n] not in run:
                n = step_[n]
                run.append(n)
                if lie_d(srv[n]) < net["srv_attach_m"]:
                    nd, xy = (run, [srv[q] for q in run]) if e else (run[::-1], [srv[q] for q in run[::-1]])
                    w["nodes"], w["xy"] = (w["nodes"] + nd, w["xy"] + xy) if e else (nd + w["nodes"], xy + w["xy"])
                    break

    # N4: a link merging into the middle of another (exit 22: 49504 into 64574) splits that one there, so they weld
    ends_at = {n for i, w in links.items() if jof[i] in srv_k for n in (w["nodes"][0], w["nodes"][-1])}
    for i in [i for i in links if jof[i] in srv_k]:
        w = links[i]
        cut = [k for k, n in enumerate(w["nodes"][1:-1], 1) if n in ends_at]
        if cut:
            k = cut[0]
            links[-i] = {**w, "nodes": w["nodes"][k:], "xy": w["xy"][k:]}  # the tail keeps the tags; id = -way id
            w["nodes"], w["xy"] = w["nodes"][:k + 1], w["xy"][:k + 1]
            jof[-i] = jof[i]

    def road_map(rid, p):  # real p -> compact through road rid's own station/lateral frame (lateral unscaled)
        c, o = roads[rid], roads[rid]["out"]
        _, sr, q, (tx, tz) = project_poly(c["xy"], c["s"], p)
        t = (p[0] - q[0]) * -tz + (p[1] - q[1]) * tx
        f = min(max(sr / RSTEP, 0.0), len(o["cs"]) - 1.0)
        i = min(int(f), len(o["cs"]) - 2)
        (x, z), (ux, uz) = lerp_at(o["fpts"], o["ftans"], step, o["cs"][i] + (f - i) * (o["cs"][i + 1] - o["cs"][i]) - o["s0"])
        return complex(x - t * uz, z + t * ux), abs(t)

    # N3: the LIE's frame for ramp points. Its compact alignment is the real EB carriageway line, which the compact
    # cross-section puts on the median, so a lateral d off that line maps piecewise: right of EB -> past the EB lane
    # centre c, between the carriageways (real separation sep) -> squeezed to [-c, c], left of WB -> past -c.
    cc = ccfg["median_half_width_ft"] * lc.FT_M + ccfg["inner_shoulder_ft"] * lc.FT_M + 1.5 * ccfg["lane_width_ft"] * lc.FT_M
    seps = defaultdict(list)  # 100 m LIE-station bins -> WB line laterals (left of the EB line)
    for xy in lie_cw:
        for pt in xy:
            sv, dv = W.frame(pt)
            if -80.0 < dv < -4.0:
                seps[int(sv // 100.0)].append(-dv)
    sep_at = lambda sv: sorted(seps[int(sv // 100.0)])[len(seps[int(sv // 100.0)]) // 2] if seps[int(sv // 100.0)] else 20.0  # noqa: E731

    def unsqueeze(sv, j):
        # N4: across a road crossing the LIE, the LIE's compaction squeezed its C/D roads onto its lanes (the C/D runs
        # along the LIE axis there). Within r0 of the crossing the LIE station keeps real scale, easing back to the
        # compaction by r1: s_c = C(s) + sign(d) (1 - c) int_0^|d| (1 - smooth((x - r0) / (r1 - r0))) dx.
        r0, r1 = net["unsqueeze_m"].get(j["id"], (0.0, 0.0))
        if r1 <= 0.0:
            return W.compact_s(sv)
        sj = j["s_real"]["lie"]
        c = (W.compact_s(sj + 50.0) - W.compact_s(sj - 50.0)) / 100.0
        d, x, acc = abs(sv - sj), 0.0, 0.0
        while x < min(d, r1):  # 1 m quadrature
            acc += 1.0 - lc.smoothstep((x - r0) / (r1 - r0)) if x > r0 else 1.0
            x += 1.0
        return W.compact_s(sv) + math.copysign((1.0 - c) * acc, sv - sj)

    def lie_map(p, j=None):
        sv, dv = W.frame(p)
        sp_ = sep_at(sv)
        dd = dv + cc if dv >= 0.0 else cc + dv * 2.0 * cc / sp_ if dv >= -sp_ else -cc + (dv + sp_)
        (x, z), _ = W.at(unsqueeze(sv, j) if j else W.compact_s(sv))
        return complex(x + dd, z), min(abs(dv), abs(dv + sp_))
    links = {i: w for i, w in links.items() if jof[i] is not None}
    starts, ends = defaultdict(list), defaultdict(list)
    for i, w in links.items():
        starts[w["nodes"][0]].append(i)
        ends[w["nodes"][-1]].append(i)

    def nxt(i):  # the one link continuing i through a plain join (no fork, no merge, not on a mainline), else None
        n = links[i]["nodes"][-1]
        if n in node_road or len(starts[n]) != 1 or len(ends[n]) != 1:
            return None
        k = starts[n][0]
        return k if k != i and (jof[k] == jof[i] or {jof[k], jof[i]} <= srv_k) else None  # N5: exit 22 and the Van Wyck share C/D pieces

    cont = {k for i in links for k in [nxt(i)] if k is not None}
    order = [i for i in sorted(links) if i not in cont] + sorted(cont)  # chain heads first; pure loops last
    ramps, rpts, done = [], [], set()
    for i in order:
        if i in done:
            continue
        run = [i]
        done.add(i)
        while (k := nxt(run[-1])) is not None and k not in done:
            run.append(k)
            done.add(k)
        k = jof[i]
        j = junctions[k]
        # Mean of the incident links' similarity fits of the non-LIE roads: each maps J (or its foot) to its place.
        tfs = [t for rd in (j["a"], j["b"]) if rd != "lie" for t in road_tf[rd][j["id"]]]
        first, layers, lanes = len(rpts), [], []
        for m, li in enumerate(run):
            w = links[li]
            ly = way_layer(w["tags"])
            layers.append(ly)
            lm = re.match(r"\d+", w["tags"].get("lanes", ""))
            lanes.append(int(lm.group()) if lm else 1)
            for p in w["xy"][1 if m else 0:]:
                z = complex(*p)
                q = sum(t[0] * (z - t[1]) + t[2] for t in tfs) / len(tfs)
                if li in legs:  # a leg runs between two roads: each road's own frame, weighted by nearness
                    (qa, da), (qb, db) = road_map(legs[li][0], p), road_map(legs[li][1], p)
                    q = qa + (qb - qa) * (da / max(da + db, 1e-9))
                elif j in lie_link_j:  # N3: an owned LIE junction blends the LIE's frame and the other road's
                    other = j["b"] if j["a"] == "lie" else j["a"]
                    (qa, da), (qb, db) = lie_map(p, j), road_map(other, p)
                    q = qa + (qb - qa) * (da / max(da + db, 1e-9))
                g = min(ground, key=lambda gg: (gg[0][0] - p[0]) ** 2 + (gg[0][1] - p[1]) ** 2)[1]
                rpts.append((q.real, q.imag, g + ly * net["layer_m"] - L.y0, ly))
        a, z = links[run[0]], links[run[-1]]
        fr, to = attach(a["nodes"][0], a["xy"][0], k in srv_k), attach(z["nodes"][-1], z["xy"][-1], k in srv_k)
        if fr == to == 0 and k in srv_k:  # N4: a LIE <-> service-road slip ramp; the service road is folded into the LIE
            del rpts[first:]
            continue
        ramps.append((run[0], first, len(rpts) - first, k, fr, to, max(layers), min(lanes), 0))
    # N6 (decision 8): a local exit on a network road's drivable span (a motorway_junction whose link is no network
    # ramp) is signed but closed: its link chain from the node (plain joins, up to stub_m real metres) through the road's
    # own frame becomes a ramp from the road to nowhere; the runtime puts the EXIT CLOSED zone on it
    taken = {w["nodes"][0] for w in links.values()}
    stubs = defaultdict(int)
    for rid, c in roads.items():
        out_links = defaultdict(list)
        for w in c["ways"]:
            if w["tags"].get("highway") == "motorway_link":
                nd_, g_ = (w["nodes"], w["geometry"]) if w["tags"].get("oneway") != "-1" else (w["nodes"][::-1], w["geometry"][::-1])
                out_links[nd_[0]].append((w["id"], nd_, [tp.to_engine_m(q["lon"], q["lat"]) for q in g_], w["tags"]))
        for node, sv, ref in c.get("exit_nodes", []):
            if node in taken or not out_links.get(node):
                continue
            wid, nd_, xy, tg = out_links[node][0]
            run_xy, layers, lanes = list(xy), [way_layer(tg)], [int(m.group()) if (m := re.match(r"\d+", tg.get("lanes", ""))) else 1]
            while len(out_links.get(nd_[-1], [])) == 1 and sum(map(math.dist, run_xy, run_xy[1:])) < net["stub_m"]:
                _, nd_, xy, tg = out_links[nd_[-1]][0]
                run_xy += xy[1:]
                layers.append(way_layer(tg))
            k = min((q for q, j in enumerate(junctions) if rid in (j["a"], j["b"])),
                    key=lambda q: abs(c["out"]["sj"][junctions[q]["id"]] - sv))  # its road's nearest junction (mesh group)
            first, acc = len(rpts), 0.0
            for q, p in enumerate(run_xy):
                acc += math.dist(p, run_xy[q - 1]) if q else 0.0
                if acc > net["stub_m"]:
                    break
                z = road_map(rid, p)[0]
                g = min(ground, key=lambda gg: (gg[0][0] - p[0]) ** 2 + (gg[0][1] - p[1]) ** 2)[1]
                rpts.append((z.real, z.imag, g + max(layers) * net["layer_m"] - L.y0, max(layers)))
            ramps.append((wid, first, len(rpts) - first, k, ids[rid], NO_ROAD, max(layers), min(lanes), 1))
            stubs[rid] += 1
    print("bake_net: N6 closed local-exit stubs: " + ", ".join(f"{k} {v}" for k, v in stubs.items()))
    per = defaultdict(int)
    for rp in ramps:
        per[junctions[rp[3]]["id"]] += 1
    print(f"bake_net: {len(links)} links -> {len(ramps)} ramps, {len(rpts)} points: " +
          ", ".join(f"{k} {v}" for k, v in per.items()))

    # N8: landmarks (landmarks.md) as (road, compact s, real lateral) per point, the ramps' rule (lateral unscaled);
    # past a road's data end the along offset is kept real. y = the DEM under the nearest road sample (sea for water).
    lmks = []
    for row in net.get("landmarks", []):
        kind, rid, pts = row[0], row[1], [(row[2], row[3])] + ([(row[4], row[5])] if len(row) > 5 else [])
        c, o = roads[rid], roads[rid]["out"]
        st = []
        for lat_, lon_ in pts:
            p = tp.to_engine_m(lon_, lat_)
            _, sr, q, (tx, tz) = project_poly(c["xy"], c["s"], p)
            f = min(max(sr / RSTEP, 0.0), len(o["cs"]) - 1.0)
            i = min(int(f), len(o["cs"]) - 2)
            sc = o["cs"][i] + (f - i) * (o["cs"][i + 1] - o["cs"][i]) - o["s0"]
            st.append((sc + (p[0] - q[0]) * tx + (p[1] - q[1]) * tz, (p[0] - q[0]) * -tz + (p[1] - q[1]) * tx, sr))
        if len(st) == 1:
            st.append(st[0])
        y = -L.y0 if kind == "throgs" else c["elev"][min(int(round(st[0][2] / RSTEP)), len(c["elev"]) - 1)] - L.y0
        lmks.append((LMK_KIND[kind], ids[rid], st[0][0], st[1][0], st[0][1], st[1][1], y))
        print(f"bake_net: landmark {kind} on {rid}: s {st[0][0]:.0f}..{st[1][0]:.0f} m, lateral {st[0][1]:+.0f} / "
              f"{st[1][1]:+.0f} m, y {y:.1f} m")

    write_network(net, ids, jrec, walls, ramps, rpts, lmks)

    # ── self-check: each road's link endpoint lands on its warped junction foot (the fit is exact) ──
    worst = max(math.dist(lerp_at(c["out"]["fpts"], c["out"]["ftans"], step, c["out"]["sj"][jid])[0], q)
                for c in roads.values() for jid, q in c["out"]["q"].items())
    print(f"bake_net: worst link-endpoint miss {worst:.3f} m (limit 0.5); junction laterals t (m): " + ", ".join(
        f"{jid} {sd[0][1]:+.1f}/{sd[1][1]:+.1f}" for _, _, _, sd, _, jid in jrec))
    assert worst < 0.5
    print("bake_net: junction height gaps (road a y - road b y, m): " + ", ".join(
        f"{junctions[k]['id']} {hgt(roads, L, junctions[k], side):+.1f}" for k, _, _, side, _, _ in jrec))


def hgt(roads, L, j, side):
    """Profile height difference at a junction: the grade separation (LIE heights from the LIE bake are not kept)."""
    def y(rd, sv):
        if rd == "lie":
            return L.fy[min(int(sv / L.step), len(L.fy) - 1)] - L.y0
        o = roads[rd]["out"]
        return o["ys"][min(int(sv / L.step), len(o["ys"]) - 1)]
    return y(j["a"], side[0][0]) - y(j["b"], side[1][0])


def write_network(net, ids, jrec, walls, ramps, rpts, lmks) -> None:
    """MUST match CorridorNetworkAsset.h (#pragma pack(1))."""
    body = struct.pack("<II", NET_MAGIC, NET_VERSION)
    body += struct.pack("<II", ROAD_TAG, len(ids))
    for rid, i in sorted(ids.items(), key=lambda kv: kv[1]):
        path = "corridor/lie_corridor_full.bin" if rid == "lie" else f"corridor/queens_{rid}_full.bin"
        body += struct.pack("<B7x16s40s", i, rid.encode()[:15], path.encode()[:39])
    body += struct.pack("<II", JNCT_TAG, len(jrec))
    for k, ra, rb, side, x, jid in jrec:
        body += struct.pack("<ddddffBBB5x16s", side[0][0] * lc.WORLD_SCALE, side[1][0] * lc.WORLD_SCALE,
                            x[0] * lc.WORLD_SCALE, x[1] * lc.WORLD_SCALE, side[0][1] * lc.WORLD_SCALE,
                            side[1][1] * lc.WORLD_SCALE, ra, rb, k, jid.encode()[:15])
    body += struct.pack("<II", WALL_TAG, len(walls))
    for rd, end, sv in walls:
        body += struct.pack("<dBB6x", sv * lc.WORLD_SCALE, rd, end)
    body += struct.pack("<II", RAMP_TAG, len(ramps))
    for osm, first, n, k, fr, to, layer, lanes, stub in ramps:
        body += struct.pack("<qIIBBBbBB2x", osm, first, n, k, fr, to, layer, lanes, stub)
    body += struct.pack("<II", RPTS_TAG, len(rpts))
    for x, z, y, layer in rpts:
        body += struct.pack("<ddfb3x", x * lc.WORLD_SCALE, z * lc.WORLD_SCALE, y * lc.WORLD_SCALE, layer)
    body += struct.pack("<II", LMRK_TAG, len(lmks))
    for kind, rd, s0, s1, t0, t1, y in lmks:
        body += struct.pack("<ddfffBB2x", s0 * lc.WORLD_SCALE, s1 * lc.WORLD_SCALE, t0 * lc.WORLD_SCALE,
                            t1 * lc.WORLD_SCALE, y * lc.WORLD_SCALE, kind, rd)
    out = os.path.join(REPO, net["network_asset"])
    with open(out, "wb") as fh:
        fh.write(body)
    print(f"bake_net: wrote {net['network_asset']} ({len(body)} B: {len(jrec)} junctions, {len(walls)} walls, "
          f"{len(ramps)} ramps)")


def cmd_elev_net(net: dict, force: bool) -> None:
    with open(os.path.join(REPO, net["lie_config"]), "rb") as fh:
        lie_cfg = tomllib.load(fh)
    tp = lc.route_frame(lie_cfg["corridor"], None)
    ecfg = lie_cfg["elevation"]
    for rid, r in net["roads"].items():
        out = dem_path(rid)
        if os.path.exists(out) and not force:
            print(f"elev_net: {rid} cached")
            continue
        c = centreline(rid, r, tp, RSTEP)
        # CSV station = sample index x spacing, i.e. centreline index x RSTEP (see road_elev).
        lc.write_elev_csv(ecfg["service_url"], c["ll"][::int(ecfg["sample_spacing_m"] / RSTEP)],
                          ecfg["sample_spacing_m"], out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["fetch_net", "elev_net", "bake_net"])
    ap.add_argument("--config", default=os.path.join(REPO, "config", "queens_network.toml"))
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    with open(a.config, "rb") as fh:
        net = tomllib.load(fh)
    {"fetch_net": lambda: cmd_fetch_net(net, a.force), "elev_net": lambda: cmd_elev_net(net, a.force),
     "bake_net": lambda: cmd_bake_net(net)}[a.stage]()


if __name__ == "__main__":
    main()
