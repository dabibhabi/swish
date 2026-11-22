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
NO_ROAD = 255           # a ramp end on another ramp or a local street
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
        here, hd = chain["xy"][-1], lc._planar_bearing(chain["xy"][-2], chain["xy"][-1])
        cands = [w for w in by_first.get(cur["nodes"][-1], []) if abs(w["id"]) not in used]
        if not cands:  # a hole in the extract (untagged way): bridge to a way starting <= 300 m dead ahead
            def ahead(w):
                d = math.dist(here, w["xy"][0])
                off = abs((lc._planar_bearing(here, w["xy"][0]) - hd + math.pi) % (2 * math.pi) - math.pi)
                return d if 0.0 < d < 300.0 and off < math.radians(20.0) else math.inf
            near = min((w for w in dways if abs(w["id"]) not in used), key=ahead, default=None)
            cands = [near] if near is not None and ahead(near) < math.inf else []

        def turn(w):
            b = lc._planar_bearing(w["xy"][0], w["xy"][1])
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
    print(f"centreline {rid}: A {sA[-1]:.0f} m ({len(A['xy'])} pts), B {sB[-1]:.0f} m; carriageway separation "
          f"median {sorted(seps)[len(seps) // 2]:.0f} m, max {max(seps):.0f} m; real centreline {len(out) * step:.0f} m")
    ll = list(zip(vals[0], vals[1]))
    return {"xy": out, "ll": ll, "lanes": lanes, "layer": layer, "A": A, "B": B, "ways": ways, "juncs": juncs}


# ══════════════════════════════════════════════════════════════════════
# 3. The LIE warp: real p -> (real LIE s, lateral d) -> compact LIE s -> compact pos + d * normal
# ══════════════════════════════════════════════════════════════════════

class LieWarp:
    def __init__(self, L, max_real_m: float):
        self.L = L
        n = sum(1 for v in L.s_real if v <= max_real_m) + 1  # Queens only: keeps the brute-force projection cheap
        self.xy, self.s = L.xy_m[:n], L.s_real[:n]

    def frame(self, p):
        """(real LIE station, signed lateral, + = right of eastbound travel)."""
        _, s, q, (tx, tz) = project_poly(self.xy, self.s, p)
        return s, (p[0] - q[0]) * -tz + (p[1] - q[1]) * tx

    def compact_s(self, s_real: float) -> float:
        f = min(max(s_real / self.L.step, 0.0), len(self.L.cs) - 1.0)
        i = min(int(f), len(self.L.cs) - 2)
        return self.L.cs[i] + (f - i) * (self.L.cs[i + 1] - self.L.cs[i])

    def at(self, sc: float):
        """Compact LIE centreline point and unit tangent at compact station sc (asset frame, metres)."""
        P, T = self.L.fpts, self.L.ftans
        f = min(max(sc / self.L.step, 0.0), len(P) - 1.0)
        i = min(int(f), len(P) - 2)
        u = f - i
        x = P[i][0] + u * (P[i + 1][0] - P[i][0])
        z = P[i][1] + u * (P[i + 1][1] - P[i][1])
        tx, tz = T[i][0] + u * (T[i + 1][0] - T[i][0]), T[i][1] + u * (T[i + 1][1] - T[i][1])
        n = math.hypot(tx, tz)
        return (x, z), (tx / n, tz / n)

    def __call__(self, p):
        s, d = self.frame(p)
        (x, z), (tx, tz) = self.at(self.compact_s(s))
        return (x - d * tz, z + d * tx)

    def jacobian(self, p, h: float = 5.0) -> float:
        """det dW/dp by central differences: > 0 everywhere = no fold."""
        ax, bx = self((p[0] + h, p[1])), self((p[0] - h, p[1]))
        az, bz = self((p[0], p[1] + h)), self((p[0], p[1] - h))
        return ((ax[0] - bx[0]) * (az[1] - bz[1]) - (ax[1] - bx[1]) * (az[0] - bz[0])) / (4.0 * h * h)


# ══════════════════════════════════════════════════════════════════════
# 4. Bake
# ══════════════════════════════════════════════════════════════════════

def closest_approach(a_xy, a_s, b_xy, b_s, hint, radius):
    """Closest pair between two polylines near `hint` -> (midpoint, s on a, s on b, gap)."""
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
    mid = ((p[0] + q[0]) * 0.5, (p[1] + q[1]) * 0.5)
    sb = project_poly(b_xy, b_s, mid)[1]
    return mid, project_poly(a_xy, a_s, mid)[1], sb, d


def cmd_bake_net(net: dict) -> None:
    with open(os.path.join(REPO, net["lie_config"]), "rb") as fh:
        lie_cfg = tomllib.load(fh)
    print("bake_net: re-running the LIE bake in memory (no write) for the warp")
    L = lc.cmd_bake(lie_cfg, write=False)
    tp, step = L.tp, L.step
    W = LieWarp(L, 30000.0)
    ecfg = lie_cfg["elevation"]
    rstep = RSTEP

    roads = {}
    for rid, r in net["roads"].items():
        c = centreline(rid, r, tp, rstep)
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
        sj = [j["s_real"][rid] for j in mine]
        # Junction points on this road and their placements.
        def at_s(sv, xy=c["xy"], s=c["s"]):
            i = min(max(bisect.bisect_right(s, sv) - 1, 0), len(xy) - 2)
            u = (sv - s[i]) / (s[i + 1] - s[i])
            return complex(xy[i][0] + u * (xy[i + 1][0] - xy[i][0]), xy[i][1] + u * (xy[i + 1][1] - xy[i][1]))
        P = [at_s(v) for v in sj]
        Q = [complex(*W((p.real, p.imag))) for p in P]
        # Link k maps [P_k, P_k+1] -> [Q_k, Q_k+1]: T(z) = a (z - P_k) + Q_k.
        tf = [((Q[k + 1] - Q[k]) / (P[k + 1] - P[k]), P[k], Q[k]) for k in range(len(P) - 1)]
        road_tf[rid] = (sj, tf)
        b = net["blend_m"]

        def T(z, sv):
            k = min(max(sum(1 for v in sj[1:-1] if v <= sv), 0), len(tf) - 1)
            ev = lambda t: t[0] * (z - t[1]) + t[2]  # noqa: E731
            for m in range(1, len(sj) - 1):  # cross-fade around interior junctions: no kink, J still exact
                if abs(sv - sj[m]) < b:
                    w = lc.smoothstep((sv - sj[m] + b) / (2.0 * b))
                    return (1.0 - w) * ev(tf[m - 1]) + w * ev(tf[m])
            return ev(tf[k])

        # Insert the junction points as vertices so they land exactly.
        rows = sorted([(c["s"][i], i, None) for i in range(len(c["xy"]))] + [(v, None, k) for k, v in enumerate(sj)])
        comp, sr, extra = [], [], {"lanes": [], "layer": [], "elev": []}
        for sv, i, k in rows:
            z = complex(*c["xy"][i]) if i is not None else P[k]
            comp.append(T(z, sv))
            sr.append(sv)
            ii = i if i is not None else min(bisect.bisect_right(c["s"], sv) - 1, len(c["xy"]) - 1)
            for key in ("lanes", "layer", "elev"):
                extra[key].append(c[key][ii])
        cxy = [(z.real, z.imag) for z in comp]
        cs = lc.polyline_stations(cxy)
        cs_of = lambda v: cs[min(range(len(sr)), key=lambda q: abs(sr[q] - v))]  # noqa: E731
        sj_c = [cs[rows.index((v, None, k))] for k, v in enumerate(sj)]

        # Walls (real metres past a junction, outward) and the tails past them.
        wl = []
        for end, (jid, run) in enumerate(r["walls"]):
            sv = next(j["s_real"][rid] for j in mine if j["id"] == jid) + (run if end else -run)
            wl.append(min(max(cs_of(sv), 0.0), cs[-1]))
        lo, hi = max(wl[0] - r["tail_m"], 0.0), min(wl[1] + r["tail_m"], cs[-1])

        # Elevation: USGS ground + OSM layer, on compact stations, LIE datum.
        raw_y = [e + ly * net["layer_m"] for e, ly in zip(extra["elev"], extra["layer"])]
        fpts, fv = lc.resample(cxy, cs, [extra["lanes"], extra["layer"], raw_y, sr], step)
        i0, i1 = int(math.ceil(lo / step)), int(hi / step)
        fpts = fpts[i0:i1 + 1]
        fv = [v[i0:i1 + 1] for v in fv]
        s0 = i0 * step
        ftans = lc.headings(fpts)
        lanes = lc.taper(lc.nominal_lanes(fv[0], step, 1000.0), step, lie_cfg["corridor"]["lane_taper_m"])
        fy, g_max, a_max = lc.vertical_profile(fv[2], step, ecfg)
        fbank = lc.superelevation(ftans, step, ecfg)
        total = step * (len(fpts) - 1)
        sjr = [v - s0 for v in sj_c]
        wlr = [v - s0 for v in wl]

        # Radius audit on the drivable range (between the walls): compact vs real.
        fr = lc.curve_radii(ftans, step, lie_cfg["corridor"]["curvature_baseline_m"])
        rr = lc.curve_radii(lc.headings(c["xy"]), rstep, lie_cfg["corridor"]["curvature_baseline_m"])
        real_at = [rr[min(bisect.bisect_right(c["s"], v), len(rr) - 1)] for v in fv[3]]
        drive = [i for i in range(len(fpts)) if wlr[0] <= i * step <= wlr[1]]
        invented = [i for i in drive if fr[i] < net["curvature_floor_radius_m"] and real_at[i] >= net["exempt_radius_m"]]
        kinks = [math.degrees(abs(cmath.phase(tf[k + 1][0] / tf[k][0]))) for k in range(len(tf) - 1)]
        report[rid] = {
            "scales": [abs(t[0]) for t in tf], "kinks": kinks,
            "min_r": min(fr[i] for i in drive), "min_r_real": min(real_at[i] for i in drive),
            "min_r_nonexempt": min((fr[i] for i in drive if real_at[i] >= net["exempt_radius_m"]), default=math.inf),
            "invented_km": len(invented) * step / 1000.0, "len": total, "drive": wlr[1] - wlr[0]}
        print(f"bake_net {rid}: links " + ", ".join(
            f"{mine[k]['id']}->{mine[k + 1]['id']} scale {abs(t[0]):.3f} rot {math.degrees(cmath.phase(t[0])):+.1f} deg"
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
        for n in c["juncs"]:
            tg = n.get("tags", {})
            onA, onB = n["id"] in c["A"]["nodes"], n["id"] in c["B"]["nodes"]
            if not tg.get("ref") or not (onA or onB):
                continue
            sv = cs_of(project_poly(c["xy"], c["s"], tp.to_engine_m(n["lon"], n["lat"]))[1]) - s0
            if not 0.0 <= sv <= total:
                continue
            dest = "; ".join(dict.fromkeys(d for t in link_from[n["id"]] for d in t.get("destination", "").split(";") if d))
            dref = "; ".join(dict.fromkeys(d for t in link_from[n["id"]] for d in t.get("destination:ref", "").split(";") if d))
            exits.append((sv, tg["ref"], not onA, 0, dest, dref))
        exits.sort(key=lambda e: e[0])
        marks = sorted((v, names[j["b"] if j["a"] == rid else j["a"]]) for v, j in zip(sjr, mine))
        rcfg = {**lie_cfg, "output": {"asset": f"assets/corridor/queens_{rid}_full.bin"}}
        ys = [v - L.y0 for v in fy]  # the LIE's datum: heights relative to the LIE start station
        lc.write_asset(rcfg, tp, fpts, ftans, lanes, ys, fbank, marks, exits, [(0.0, "Queens")], [], [], total,
                       fv[3][-1] - fv[3][0], sum(report[rid]["scales"]) / len(tf), wlr[0])
        print(f"bake_net {rid}: {len(exits)} exits")

        walls += [(ids[rid], 0, wlr[0]), (ids[rid], 1, wlr[1])]
        c["out"] = {"s0": s0, "sj": dict(zip((j["id"] for j in mine), sjr)), "fpts": fpts, "ftans": ftans, "ys": ys}

    # ── JNCT: (road, s, lateral) per side, placed point ──────────────
    jrec = []
    for k, j in enumerate(junctions):
        x = out_j[j["id"]]["x"]
        side = []
        for rd in (j["a"], j["b"]):
            if rd == "lie":
                sv, d = W.frame(j["p"])
                side.append((W.compact_s(sv), d))
            else:
                o = roads[rd]["out"]
                sv = o["sj"][j["id"]]
                i = min(int(sv / step), len(o["fpts"]) - 2)
                u = sv / step - i
                q = [o["fpts"][i][m] + u * (o["fpts"][i + 1][m] - o["fpts"][i][m]) for m in (0, 1)]
                t = o["ftans"][i]
                side.append((sv, (x[0] - q[0]) * -t[1] + (x[1] - q[1]) * t[0]))
        jrec.append((k, ids[j["a"]], ids[j["b"]], side, x, j["id"]))

    # ── RAMP: motorway_link ways per junction, through that junction's mean link fit ──
    links, seen = [], set()
    for rid, c in roads.items():
        for w in c["ways"]:
            if w["tags"].get("highway") == "motorway_link" and w["id"] not in seen:
                seen.add(w["id"])
                links.append(w)
    ground = [gp for c in roads.values() for gp in zip(c["xy"][::4], c["elev"][::4])]  # (real point, DEM ground m)

    # Both LIE carriageways, straight from the cached LIE extract (the warp chain is the EB one only).
    root = ET.parse(os.path.join(REPO, lie_cfg["osm"]["raw_extract"])).getroot()
    nodes = {n.get("id"): (float(n.get("lon")), float(n.get("lat"))) for n in root.findall("node")}
    rx = re.compile(lie_cfg["osm"]["ref_regex"])
    lie_cw = []
    for w in root.findall("way"):
        tg = {t.get("k"): t.get("v") for t in w.findall("tag")}
        if tg.get("highway") == "motorway" and rx.match(tg.get("ref", "")):
            xy = [tp.to_engine_m(*nodes[nd.get("ref")]) for nd in w.findall("nd")]
            if min(math.dist(p, j["p"]) for p in xy for j in junctions) < 3000.0:
                lie_cw.append(xy)
    chains = [(0, xy) for xy in lie_cw] + [(ids[rid], cw["xy"]) for rid, c in roads.items() for cw in (c["A"], c["B"])]

    def attach(p):  # nearest mainline carriageway within tolerance -> road id, else NO_ROAD
        d, rd = min((project_poly(xy, lc.polyline_stations(xy), p)[0], r) for r, xy in chains if len(xy) > 1)
        return rd if d < net["road_end_tol_m"] else NO_ROAD

    ramps, rpts = [], []
    for w in links:
        xy = [tp.to_engine_m(g["lon"], g["lat"]) for g in w["geometry"]]
        dj = [min(math.dist(p, j["p"]) for p in xy) for j in junctions]
        k = min(range(len(junctions)), key=lambda q: dj[q])
        if dj[k] > net["junction_ramp_radius_m"]:
            continue
        j = junctions[k]
        # Mean of the incident links' similarity fits of the non-LIE roads: each maps J (or its foot) to its place.
        tfs = []
        for rd in (j["a"], j["b"]):
            if rd == "lie":
                continue
            sj, tf = road_tf[rd]
            m = sj.index(j["s_real"][rd])
            tfs += [tf[q] for q in (m - 1, m) if 0 <= q < len(tf)]
        layer = way_layer(w["tags"])
        first = len(rpts)
        for p in xy:
            z = complex(*p)
            q = sum(t[0] * (z - t[1]) + t[2] for t in tfs) / len(tfs)
            g = min(ground, key=lambda gg: (gg[0][0] - p[0]) ** 2 + (gg[0][1] - p[1]) ** 2)[1]
            rpts.append((q.real, q.imag, g + layer * net["layer_m"] - L.y0, layer))
        ramps.append((w["id"], first, len(xy), k, attach(xy[0]), attach(xy[-1]), layer))
    per = defaultdict(int)
    for rp in ramps:
        per[junctions[rp[3]]["id"]] += 1
    print(f"bake_net: {len(ramps)} ramps, {len(rpts)} points: " + ", ".join(f"{k} {v}" for k, v in per.items()))

    write_network(net, ids, jrec, walls, ramps, rpts)

    # ── self-check: every link endpoint within 0.5 m of its placed junction ──
    worst = 0.0
    for k, ra, rb, side, x, jid in jrec:
        for rd, (sv, t) in zip((junctions[k]["a"], junctions[k]["b"]), side):
            if rd == "lie":
                (px, pz), (tx, tz) = W.at(sv)
            else:
                o = roads[rd]["out"]
                i = min(int(sv / step), len(o["fpts"]) - 2)
                u = sv / step - i
                px = o["fpts"][i][0] + u * (o["fpts"][i + 1][0] - o["fpts"][i][0])
                pz = o["fpts"][i][1] + u * (o["fpts"][i + 1][1] - o["fpts"][i][1])
                tx, tz = o["ftans"][i]
            worst = max(worst, math.dist((px - t * tz, pz + t * tx), x))
    print(f"bake_net: worst junction miss {worst:.3f} m (limit 0.5)")
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


def write_network(net, ids, jrec, walls, ramps, rpts) -> None:
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
    for osm, first, n, k, fr, to, layer in ramps:
        body += struct.pack("<qIIBBBb4x", osm, first, n, k, fr, to, layer)
    body += struct.pack("<II", RPTS_TAG, len(rpts))
    for x, z, y, layer in rpts:
        body += struct.pack("<ddfb3x", x * lc.WORLD_SCALE, z * lc.WORLD_SCALE, y * lc.WORLD_SCALE, layer)
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
