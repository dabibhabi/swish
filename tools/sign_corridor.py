#!/usr/bin/env python3
"""
D3: guide-sign atlases for the LIE corridor, from the baked exit table.

Reads the EXIT block of assets/corridor/lie_corridor_full.bin (same record as tools/lie_corridor.py writes and
src/scene/Corridor/CorridorAsset.h reads), groups the records per (exit number, direction) the way
corridor_crossroads pairs them, and draws MUTCD-style signs (2009 MUTCD Ch. 2E; FHWA Standard Highway Signs):

  assets/signs/sign_panels.png  4096 x 4096: per exit and direction an advance guide panel (exit tab, route
                                shields from destRef, two destination lines from dest) + the shared distance strips
  assets/signs/sign_kit.png     2048 x 2048: gore EXIT signs, mile-marker pieces, the C4 exit 73 boards
  src/scene/Corridor/SignAtlas.h  pixel rects + text-fit metadata the C++ side and tests read

Font: Overpass Bold (SIL OFL 1.1, Red Hat; a Highway Gothic-derived face), assets/fonts/overpass/.
Everything is drawn at 2x and box-downsampled for clean edges. Run:  python3 tools/sign_generator.py corridor
"""

import math
import os
import re
import struct

from PIL import Image, ImageDraw, ImageFont

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
ASSET = os.path.join(ROOT, "assets", "corridor", "lie_corridor_full.bin")
FONT = os.path.join(ROOT, "assets", "fonts", "overpass", "overpass-bold.otf")
OUT_DIR = os.path.join(ROOT, "assets", "signs")
HEADER = os.path.join(ROOT, "src", "scene", "Corridor", "SignAtlas.h")

EXIT_TAG = 0x54495845
SS = 2  # supersample

# FHWA colours (sRGB approximations of the sheeting chips)
GREEN = (0, 102, 64)
WHITE = (250, 250, 250)
BLACK = (12, 12, 12)
YELLOW = (255, 199, 0)
ORANGE = (240, 110, 30)  # MUTCD work-zone orange (docs/queens-network-reference/workzone.md section 9)
I_RED = (178, 30, 40)
I_BLUE = (0, 54, 120)
CR_BLUE = (0, 70, 150)

# Atlas layout (1x px)
PW, PH = 4096, 4096
CELL_W, TAB_H, MAIN_H, STRIP_H = 512, 64, 200, 64  # tab legend ~15 in (E1-5P), panel legend 16 in
CELL_H = TAB_H + MAIN_H
COLS = PW // CELL_W
STRIP_Y = 15 * CELL_H  # 3960
KW, KH = 2048, 4096  # N3: rows 2048+ hold the network guide boards and gores (N4: a fourth row; N5: 7 rows, 2 gore rows)
GORE = 128
MK_W, MK_TOP, MK_MID, MK_LOW = 96, 96, 64, 48
MK_Y = 1024
BOARD_Y = 1400
MIN_CAP = 26  # px: smallest destination cap height allowed (~12 in at the panel scale)

STRIPS = ["1 MILE", "1/2 MILE", "1/4 MILE"]  # SignDist order (CorridorSigns.h)

# OSM wording fixes (spelling) and MUTCD Table 1A-2 abbreviations
FIX = {"Patchoque": "Patchogue", "Searington": "Searingtown", "Cuny": "CUNY"}
ABBR = [("Expressway", "Expwy"), ("Parkway", "Pkwy"), ("Boulevard", "Blvd"), ("Avenue", "Ave"), ("Road", "Rd"),
        ("Street", "St"), ("Bridge", "Br"), ("Drive", "Dr"), ("Lane", "Ln")]
# destRef tokens that are parkways, not shields: shown as a destination line instead
PKWY = {"N East": "Northern State Pkwy", "Northern Parkway West": "Northern State Pkwy", "SA": "Sagtikos Pkwy"}
CARD = {"North": "NORTH", "No": "NORTH", "South": "SOUTH", "So": "SOUTH", "East": "EAST", "West": "WEST"}


def font(px):
    return ImageFont.truetype(FONT, px)


def cap_h(f):
    b = f.getbbox("H")
    return b[3] - b[1]


def read_exits(path=ASSET):
    d = open(path, "rb").read()
    i = d.find(struct.pack("<I", EXIT_TAG))
    n = struct.unpack_from("<I", d, i + 4)[0]
    out, o = [], i + 8
    for _ in range(n):
        s, ref, wb, _dist, dr, de = struct.unpack_from("<d8sBB2x24s76s", d, o)
        o += 120
        z = lambda b: b.split(b"\0")[0].decode().strip()  # noqa: E731
        out.append({"s": s, "ref": z(ref), "wb": wb, "destRef": z(dr), "dest": z(de)})
    return out


def clean(t):
    t = re.sub(r"\s+", " ", t).strip()
    for a, b in FIX.items():
        t = t.replace(a, b)
    for a, b in ABBR:
        t = re.sub(rf"\b{a}\b", b, t)
    return t


def shields(dref):
    """destRef -> ([(kind, number, cardinal)], [parkway lines])."""
    out, lines = [], []
    for tok in [t.strip() for t in dref.split(";") if t.strip()]:
        if tok in PKWY:
            lines.append(PKWY[tok])
            continue
        m = re.match(r"^(I|NY|CR|US)\s+(\w+)(?:\s+(\w+))?", tok)
        if m:
            out.append((m.group(1), m.group(2), CARD.get(m.group(3) or "", "")))
    return out, lines


def groups(exits):
    """One sign per (exit number, direction), numbers as corridor_crossroads pairs them (atoi of the ref).
    A direction with no record borrows the other direction's text (the diamond still builds its ramp)."""
    byno = {}
    for e in exits:
        m = re.match(r"\d+", e["ref"])
        if m:
            byno.setdefault(int(m.group()), {0: [], 1: []})[e["wb"]].append(e)
    out = []
    for no in sorted(byno):
        for wb in (0, 1):
            recs = byno[no][wb] or byno[no][1 - wb]
            recs = sorted(recs, key=lambda e: e["s"] * (-1 if wb else 1))  # travel order
            if len(recs) == 1:
                label = recs[0]["ref"].replace(";", "-")
            else:
                sfx = sorted({re.sub(r"^\d+", "", r["ref"]) for r in recs})  # one ramp serves them all: "49 N-S"
                label = f"{no} " + "-".join(sfx) if all(sfx) else str(no)
            sh, pk = [], []
            for r in recs:
                a, b = shields(r["destRef"])
                for x in a:
                    k = next((i for i, y in enumerate(sh) if y[:2] == x[:2]), -1)
                    if k < 0:
                        sh.append(x)
                    elif sh[k][2] != x[2]:
                        sh[k] = (x[0], x[1], "")  # both directions of one route: no cardinal
                pk += [p for p in b if p not in pk]
            names = []
            for r in recs:
                items = [clean(x) for x in r["dest"].split(";") if x.strip()]
                take = items[:1] if len(recs) > 1 else items[:2]
                names += [x for x in take if x not in names]
            lines = (pk + names)[:2]  # MUTCD 2E.10: at most two destinations
            out.append({"no": no, "wb": wb, "label": label, "shields": sh[:2], "lines": lines})
    return out


# ── drawing helpers (all at SS scale) ─────────────────────────────────

def text_cap(d, x, cy, s, f, fill=WHITE, anchor="l"):
    """Text whose cap height is centred on cy; anchor l / m / r."""
    w = d.textlength(s, font=f)
    x0 = x if anchor == "l" else x - w / 2 if anchor == "m" else x - w
    d.text((x0, cy + cap_h(f) / 2), s, font=f, fill=fill, anchor="ls")
    return w


def fit(d, s, max_w, cap_px):
    """Largest font with cap height <= cap_px whose width fits max_w."""
    size = int(cap_px / 0.70)
    while size > 8:
        f = font(size)
        if cap_h(f) <= cap_px and d.textlength(s, font=f) <= max_w:
            return f
        size -= 1
    return font(8)


def poly_curve(pts, n=16):
    """Cubic Bezier chain [p0, c1, c2, p1, c1, c2, p2, ...] -> polyline."""
    out = []
    for k in range(0, len(pts) - 1, 3):
        p0, c1, c2, p1 = pts[k:k + 4]
        for i in range(n):
            t = i / n
            a, b, c, e = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
            out.append((a * p0[0] + b * c1[0] + c * c2[0] + e * p1[0], a * p0[1] + b * c1[1] + c * c2[1] + e * p1[1]))
    out.append(pts[-1])
    return out


def interstate_outline(x, y, w, h):
    # top dips to a centre point; sides bulge and close to a pointed bottom (FHWA M1-1, approximated)
    right = poly_curve([(0.5, 0.09), (0.66, 0.0), (0.86, -0.01), (0.97, 0.0),
                        (1.04, 0.30), (1.02, 0.62), (0.5, 1.0)])
    left = [(1.0 - px, py) for px, py in reversed(right[:-1])]
    return [(x + px * w, y + py * h) for px, py in right + left]


def draw_interstate(img, box, num):
    x, y, w, h = box
    d = ImageDraw.Draw(img)
    d.polygon(interstate_outline(x, y, w, h), fill=WHITE)
    m = 0.06 * w
    inner = interstate_outline(x + m, y + m, w - 2 * m, h - 2 * m)
    d.polygon(inner, fill=I_BLUE)
    band = Image.new("L", img.size, 0)
    ImageDraw.Draw(band).polygon(inner, fill=255)
    ImageDraw.Draw(band).rectangle([0, y + 0.30 * h, img.size[0], img.size[1]], fill=0)
    img.paste(I_RED, (0, 0), band)
    d.line([(x + m, y + 0.30 * h), (x + w - m, y + 0.30 * h)], fill=WHITE, width=max(1, int(0.03 * h)))
    f = fit(d, "INTERSTATE", 0.62 * w, 0.075 * h)
    text_cap(d, x + w / 2, y + 0.19 * h, "INTERSTATE", f, anchor="m")
    f = fit(d, num, 0.66 * w, 0.30 * h)
    text_cap(d, x + w / 2, y + 0.57 * h, num, f, anchor="m")


def draw_ny(img, box, num):
    # NY state route: white route marker with a black border, slightly arched top (shape from memory, unverified)
    x, y, w, h = box
    d = ImageDraw.Draw(img)
    top = poly_curve([(0.0, 0.12), (0.25, 0.0), (0.75, 0.0), (1.0, 0.12)])
    pts = [(x + px * w, y + py * h) for px, py in top] + [(x + w, y + h), (x, y + h)]
    d.polygon(pts, fill=WHITE)
    m = 0.07 * w
    top = [(x + m + px * (w - 2 * m), y + m + py * (h - 2 * m)) for px, py in
           poly_curve([(0.0, 0.12), (0.25, 0.0), (0.75, 0.0), (1.0, 0.12)])]
    d.line(top + [(x + w - m, y + h - m), (x + m, y + h - m), top[0]], fill=BLACK, width=max(2, int(0.045 * w)))
    f = fit(d, num, 0.78 * w, 0.46 * h)
    text_cap(d, x + w / 2, y + 0.56 * h, num, f, fill=BLACK, anchor="m")


def draw_county(img, box, num):
    # MUTCD M1-6 county route: blue pentagon, yellow border and legend (point-down shape, unverified)
    x, y, w, h = box
    d = ImageDraw.Draw(img)
    pent = lambda m: [(x + m, y + m), (x + w - m, y + m), (x + w - m, y + 0.66 * h),  # noqa: E731
                      (x + w / 2, y + h - m), (x + m, y + 0.66 * h)]
    d.polygon(pent(0), fill=YELLOW)
    d.polygon(pent(0.07 * w), fill=CR_BLUE)
    f = fit(d, "COUNTY", 0.62 * w, 0.09 * h)
    text_cap(d, x + w / 2, y + 0.19 * h, "COUNTY", f, fill=YELLOW, anchor="m")
    f = fit(d, num, 0.66 * w, 0.34 * h)
    text_cap(d, x + w / 2, y + 0.50 * h, num, f, fill=YELLOW, anchor="m")


def draw_pkwy(img, box, name):
    # N4: NYSDOT parkway trailblazer (roads.md: "green guide signs plus parkway shield"): white-bordered green
    # rectangle, the parkway name over PKWY in white (shape from memory, unverified)
    x, y, w, h = box
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x, y, x + w, y + h], radius=0.12 * h, fill=WHITE)
    m = 0.06 * h
    d.rounded_rectangle([x + m, y + m, x + w - m, y + h - m], radius=0.09 * h, fill=GREEN)
    for k, ln in enumerate(name.upper().split(" ") + ["PKWY"]):
        f = fit(d, ln, 0.80 * w, 0.20 * h)
        text_cap(d, x + w / 2, y + h * (0.27 + 0.23 * k), ln, f, anchor="m")


def draw_shield(img, kind, num, x, y, h):
    """Shield of height h at (x, y); returns its width."""
    w = h * (1.0 if kind == "I" else 0.9 if kind == "CR" else 1.5 if kind == "PKWY" else
             (1.0 if len(num) <= 2 else 1.25))
    {"I": draw_interstate, "NY": draw_ny, "US": draw_ny, "CR": draw_county, "PKWY": draw_pkwy}[kind](img, (x, y, w, h),
                                                                                                  num)
    return w


def border(d, box, r, wdt, sides="tblr"):
    """White border; open sides are drawn past the box edge so they vanish."""
    x0, y0, x1, y1 = box
    e = 3 * r
    d.rounded_rectangle([x0, y0 - (0 if "t" in sides else e), x1, y1 + (0 if "b" in sides else e)], radius=r,
                        outline=WHITE, width=wdt)


def arrow_up_right(d, cx, cy, s, fill=WHITE):
    """FHWA-style diagonal arrow, size s, centred."""
    a = math.radians(-45)
    ux, uy = math.cos(a), math.sin(a)
    px, py = -uy, ux
    tip = (cx + ux * s / 2, cy + uy * s / 2)
    base = (cx - ux * s / 2, cy - uy * s / 2)
    hl, hw, sw = 0.42 * s, 0.30 * s, 0.11 * s
    neck = (tip[0] - ux * hl, tip[1] - uy * hl)
    d.polygon([(base[0] + px * sw, base[1] + py * sw), (neck[0] + px * sw, neck[1] + py * sw),
               (neck[0] + px * hw, neck[1] + py * hw), tip, (neck[0] - px * hw, neck[1] - py * hw),
               (neck[0] - px * sw, neck[1] - py * sw), (base[0] - px * sw, base[1] - py * sw)], fill=fill)


def distance(d, cx, cy, s, cap, fill=WHITE):
    """'1/2 MILE' with a stacked-style fraction (small raised numerator, slash, small denominator)."""
    f, fs = font(int(cap / 0.70)), font(int(0.62 * cap / 0.70))
    parts = s.split(" ")
    num = parts[0]
    word = " " + parts[1]
    if "/" in num:
        a, b = num.split("/")
        wa, wsl, wb = d.textlength(a, font=fs), d.textlength("/", font=f), d.textlength(b, font=fs)
        total = wa + wsl + wb + d.textlength(word, font=f)
        x = cx - total / 2
        text_cap(d, x, cy - 0.28 * cap, a, fs, fill)
        text_cap(d, x + wa, cy, "/", f, fill)
        text_cap(d, x + wa + wsl, cy + 0.28 * cap, b, fs, fill)
        text_cap(d, x + wa + wsl + wb, cy, word, f, fill)
    else:
        text_cap(d, cx, cy, num + word, f, fill, anchor="m")


def cell(w, h, bg=GREEN):
    img = Image.new("RGB", (w * SS, h * SS), bg)
    return img, ImageDraw.Draw(img)


def paste(atlas, img, x, y):
    atlas.paste(img.resize((img.width // SS, img.height // SS), Image.LANCZOS), (x, y))


# ── panels ───────────────────────────────────────────────────────────

def draw_panel(g):
    """Tab strip (top TAB_H) + main panel (MAIN_H). Returns image and metadata."""
    s = SS
    img, d = cell(CELL_W, CELL_H)
    bw, r, ins = 4 * s, 18 * s, 3 * s
    # exit tab, right-aligned (exit to the right): full border
    label = "EXIT " + g["label"]
    ft = fit(d, label, 999 * s, 32 * s)
    tab_w = int(d.textlength(label, font=ft) / s + 52)
    tx0 = (CELL_W - tab_w) * s
    border(d, [tx0 + ins, ins, CELL_W * s - ins, TAB_H * s + bw], r, bw)  # bottom hides under the panel border
    text_cap(d, (tx0 + CELL_W * s) / 2, TAB_H * s / 2 + 2 * s, label, ft, anchor="m")
    d.rectangle([0, 0, tx0 - 1, TAB_H * s], fill=GREEN)  # outside the tab: never shown
    # main panel: open bottom (the distance strip continues it)
    y0 = TAB_H * s
    border(d, [ins, y0 + ins, CELL_W * s - ins, CELL_H * s], r, bw, sides="tlr")
    inner_w = (CELL_W - 2 * 30) * s
    caps, widths = [], []
    y = y0 + 14 * s
    if g["shields"]:
        sh_h = 60 * s
        cards = [c for _, _, c in g["shields"]]
        same = len(set(cards)) == 1
        items = []
        for k, (kind, num, c) in enumerate(g["shields"]):
            items.append(("shield", kind, num))
            if c and (not same or k == len(g["shields"]) - 1):
                items.append(("card", c))
        fc = font(int(24 * s / 0.70))
        tot = sum((sh_h * (1.0 if it[1] == "I" else 0.9 if it[1] == "CR" else
                           (1.0 if len(it[2]) <= 2 else 1.25)) if it[0] == "shield"
                   else d.textlength(it[1], font=fc)) + 12 * s for it in items) - 12 * s
        x = (CELL_W * s - tot) / 2
        for it in items:
            if it[0] == "shield":
                x += draw_shield(img, it[1], it[2], x, y, sh_h) + 12 * s
            else:
                x += text_cap(d, x, y + sh_h / 2, it[1], fc) + 12 * s
        widths.append(tot / s)
        y += sh_h + 8 * s
    n = len(g["lines"])
    room = CELL_H * s - y - 10 * s
    pitch = room / max(n, 1)
    for k, line in enumerate(g["lines"]):
        f = fit(d, line, inner_w, min(36 * s, 0.62 * pitch))
        caps.append(cap_h(f) / s)
        widths.append(text_cap(d, CELL_W * s / 2, y + pitch * (k + 0.5), line, f, anchor="m") / s)
    meta = {"tab_w": tab_w, "min_cap": int(min(caps) if caps else 99), "max_w": int(max(widths) if widths else 0)}
    return img, meta


def draw_strip(text):
    s = SS
    img, d = cell(CELL_W, STRIP_H)
    border(d, [3 * s, -1, CELL_W * s - 3 * s, STRIP_H * s - 3 * s], 18 * s, 4 * s, sides="blr")
    distance(d, CELL_W * s / 2, STRIP_H * s / 2 - 2 * s, text, 30 * s)
    return img


def draw_gore(label):
    s = SS
    img, d = cell(GORE, GORE)
    border(d, [3 * s, 3 * s, GORE * s - 3 * s, GORE * s - 3 * s], 12 * s, 4 * s)
    text_cap(d, GORE * s / 2, 24 * s, "EXIT", font(int(20 * s / 0.70)), anchor="m")
    f = fit(d, label, (GORE - 16) * s, 34 * s)
    text_cap(d, GORE * s / 2, 60 * s, label, f, anchor="m")
    arrow_up_right(d, GORE * s / 2, 98 * s, 40 * s)
    return img, cap_h(f) / s


# ── mile markers (enhanced reference location sign, MUTCD D10-4/5 style) ──

def marker_top(card):
    s = SS
    img, d = cell(MK_W, MK_TOP)
    border(d, [2 * s, 2 * s, MK_W * s - 2 * s, MK_TOP * s], 8 * s, 3 * s, sides="tlr")
    draw_interstate(img, (20 * s, 8 * s, 56 * s, 56 * s), "495")
    text_cap(d, MK_W * s / 2, 80 * s, card, font(int(18 * s / 0.70)), anchor="m")
    return img


def marker_mid(n):
    s = SS
    img, d = cell(MK_W, MK_MID)
    border(d, [2 * s, -1, MK_W * s - 2 * s, MK_MID * s], 8 * s, 3 * s, sides="lr")
    text_cap(d, MK_W * s / 2, MK_MID * s / 2, str(n), fit(d, str(n), 80 * s, 40 * s), anchor="m")
    return img


def marker_low(t):
    s = SS
    img, d = cell(MK_W, MK_LOW)
    border(d, [2 * s, -1, MK_W * s - 2 * s, MK_LOW * s - 2 * s], 8 * s, 3 * s, sides="blr")
    text_cap(d, MK_W * s / 2, MK_LOW * s / 2 - 2 * s, f".{t}", font(int(26 * s / 0.70)), anchor="m")
    return img


# ── C4 exit 73 boards (CorridorEndsMesh kSigns order, then the W1-8 chevron) ──

def board(w, h, lines, shield=None, plaque=None, card=None, dist=None, tab=None):
    s = SS
    img, d = cell(w, h)
    border(d, [4 * s, 4 * s, w * s - 4 * s, h * s - 4 * s], 20 * s, 5 * s)
    y = 18 * s
    if tab:
        f = font(int(26 * s / 0.70))
        tw = d.textlength(tab, font=f) + 40 * s
        d.rounded_rectangle([w * s - tw - 18 * s, y - 6 * s, w * s - 18 * s, y + 38 * s], radius=10 * s,
                            outline=WHITE, width=4 * s)
        text_cap(d, w * s - 18 * s - tw / 2, y + 16 * s, tab, f, anchor="m")
        y += 52 * s
    if shield:
        sh_h = (74 if lines else 150) * s  # a shield-only board (END 495) carries a big shield
        x = w * s / 2 - (sh_h + (d.textlength(card, font=font(int(28 * s / 0.70))) + 14 * s if card else 0)) / 2
        if plaque:
            pf = font(int(22 * s / 0.70))
            text_cap(d, x + sh_h / 2, y + 12 * s, plaque, pf, anchor="m")
            y += 30 * s
        sw = draw_shield(img, shield[0], shield[1], x, y, sh_h)
        if card:
            text_cap(d, x + sw + 14 * s, y + sh_h / 2, card, font(int(28 * s / 0.70)))
        y += sh_h + 12 * s
    bottom = h * s - (56 * s if dist else 14 * s)
    pitch = (bottom - y) / max(1, len(lines))
    for k, ln in enumerate(lines):
        f = fit(d, ln, (w - 50) * s, min(40 * s, 0.62 * pitch))
        text_cap(d, w * s / 2, y + pitch * (k + 0.5), ln, f, anchor="m")
    if dist:
        distance(d, w * s / 2, h * s - 32 * s, dist, 30 * s)
    return img


def roundabout_w2_6(px):
    """Yellow diamond W2-6 (three circular arrows), content pre-rotated -45 deg: the quad is turned 45 deg."""
    s = SS
    n = px * s
    img = Image.new("RGB", (n, n), YELLOW)
    d = ImageDraw.Draw(img)
    d.rectangle([5 * s, 5 * s, n - 5 * s, n - 5 * s], outline=BLACK, width=5 * s)
    sym = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sym)
    c, R, t = n / 2, 0.24 * n, 0.07 * n
    for k in range(3):  # counter-clockwise ring: three arcs with heads
        a0 = 90 + k * 120 + 25
        sd.arc([c - R, c - R, c + R, c + R], start=-(a0 + 80), end=-a0, fill=BLACK, width=int(t))
        ah = math.radians(a0 + 80)
        hx, hy = c + R * math.cos(ah), c - R * math.sin(ah)
        tx, ty = -math.sin(ah), -math.cos(ah)  # CCW tangent in image coords
        nx, ny = math.cos(ah), -math.sin(ah)
        L, W = 0.11 * n, 0.09 * n
        sd.polygon([(hx + tx * L, hy + ty * L), (hx + nx * W, hy + ny * W), (hx - nx * W, hy - ny * W)], fill=BLACK)
    sym = sym.rotate(-45, resample=Image.BICUBIC)
    img.paste(sym, (0, 0), sym)
    return img


def chevron_w1_8(w, h):
    s = SS
    img = Image.new("RGB", (w * s, h * s), YELLOW)
    d = ImageDraw.Draw(img)
    d.rectangle([4 * s, 4 * s, w * s - 4 * s, h * s - 4 * s], outline=BLACK, width=4 * s)
    x0, x1, t = 0.28 * w * s, 0.72 * w * s, 0.20 * w * s
    yc = h * s / 2
    d.polygon([(x0, 0.16 * h * s), (x0 + t, 0.16 * h * s), (x1, yc), (x0 + t, 0.84 * h * s), (x0, 0.84 * h * s),
               (x1 - t, yc)], fill=BLACK)
    return img


def hov_board(w, h):
    """White HOV advance sign (photo exit-33): black diamond panel, three lines, hours strip with a down arrow."""
    s = SS
    img, d = cell(w, h, WHITE)
    d.rounded_rectangle([4 * s, 4 * s, w * s - 4 * s, h * s - 4 * s], radius=12 * s, outline=BLACK, width=4 * s)
    split = int(0.58 * h) * s
    d.line([(4 * s, split), (w * s - 4 * s, split)], fill=BLACK, width=3 * s)
    pw = int(0.24 * w) * s
    d.rectangle([10 * s, 10 * s, 10 * s + pw, split - 6 * s], fill=BLACK)
    cx, cy, r = 10 * s + pw / 2, (10 * s + split - 6 * s) / 2, 0.36 * (split - 16 * s)
    d.polygon([(cx, cy - r), (cx + 0.55 * r, cy), (cx, cy + r), (cx - 0.55 * r, cy)], outline=WHITE, width=5 * s)
    x0 = 10 * s + pw
    f = fit(d, "HOV LANE BEGINS", w * s - x0 - 34 * s, 22 * s)
    for k, ln in enumerate(["HOV LANE BEGINS", "LEFT LANE"]):
        text_cap(d, (x0 + w * s) / 2, 30 * s + k * 40 * s, ln, f, fill=BLACK, anchor="m")
    f, fs = font(int(24 * s / 0.70)), font(int(15 * s / 0.70))
    xm = (x0 + w * s) / 2
    text_cap(d, xm - 30 * s, 106 * s, "1", fs, fill=BLACK)
    text_cap(d, xm - 20 * s, 116 * s, "/", f, fill=BLACK)
    text_cap(d, xm - 8 * s, 126 * s, "2", fs, fill=BLACK)
    text_cap(d, xm + 6 * s, 116 * s, "MILE", f, fill=BLACK)
    rows = ["BUSES AND CAR POOLS ONLY", "6-10 AM   3-8 PM   MON-FRI"]
    for k, ln in enumerate(rows):
        f = fit(d, ln, w * s - 70 * s, 17 * s)
        text_cap(d, w * s / 2 + 22 * s, split + 30 * s + k * 38 * s, ln, f, fill=BLACK, anchor="m")
    ax, ay = 34 * s, split + 50 * s  # down arrow
    d.rectangle([ax - 5 * s, ay - 34 * s, ax + 5 * s, ay + 8 * s], fill=BLACK)
    d.polygon([(ax - 18 * s, ay), (ax + 18 * s, ay), (ax, ay + 28 * s)], fill=BLACK)
    return img


def attraction_board(w, h, names):
    """Blue motorist-attraction sign (photo exit-62, 2000): title + 2 x 3 white panels. Plain names, no logos."""
    s = SS
    img, d = cell(w, h, (0, 60, 135))
    border(d, [4 * s, 4 * s, w * s - 4 * s, h * s - 4 * s], 14 * s, 4 * s)
    f = fit(d, "ATTRACTIONS AT EXIT 62N", (w - 40) * s, 22 * s)
    text_cap(d, w * s / 2, 30 * s, "ATTRACTIONS AT EXIT 62N", f, anchor="m")
    pw, ph, gx, gy, y0 = (w - 56) / 3, (h - 78) / 2, 10, 10, 54
    colours = [(150, 20, 30), (0, 70, 40), (20, 40, 110), (120, 20, 20), (10, 10, 10), (0, 90, 60)]
    for k, nm in enumerate(names):
        x = 18 + (k % 3) * (pw + gx)
        y = y0 + (k // 3) * (ph + gy)
        d.rectangle([x * s, y * s, (x + pw) * s, (y + ph) * s], fill=WHITE)
        ws = nm.split(" ")
        cut = min(range(1, len(ws)), key=lambda c: max(len(" ".join(ws[:c])), len(" ".join(ws[c:]))), default=1)
        ln = [" ".join(ws[:cut]), " ".join(ws[cut:])] if len(ws) > 1 else [nm]
        ff = min((fit(d, t, (pw - 12) * s, 16 * s) for t in ln), key=cap_h)  # one size per panel
        for j, t in enumerate(ln):
            text_cap(d, (x + pw / 2) * s, (y + ph / 2 + (j - (len(ln) - 1) / 2) * 26) * s, t, ff,
                     fill=colours[k], anchor="m")
    return img


# D6 landmark boards (RoadParams road::landmarks::kBoards cell order): (w, h, image) at 0.84 cm/px
LANDMARK_BOARDS = [
    (320, 112, lambda w, h: board(w, h, ["NASSAU", "COUNTY LINE"])),
    (320, 112, lambda w, h: board(w, h, ["QUEENS", "COUNTY LINE"])),
    (320, 256, hov_board),
    (320, 224, lambda w, h: board(w, h, ["Great Neck", "Manhasset"], dist="EXIT 33")),
    (480, 288, lambda w, h: attraction_board(w, h, ["Long Island Museum", "Stony Brook Village", "Stony Brook University",
                                                    "Suffolk Comm College", "Sports Plus", "Port Jefferson Ferry"])),
]



# ── N2b work-zone faces (docs/queens-network-reference/workzone.md section 2) ──
# Kit atlas: free gore cells 116..127 (row y 896, x 512..2048) and the column right of the landmark boards.
WZ_Y, WZ_D = 896, 128  # row top, diamond cell (48 in -> ~1 cm/px)
WZ_LANES = ["RIGHT LANE", "RIGHT 2 LANES", "RIGHT 3 LANES"]  # W20-5 / W20-5a by lanes closed


def wz_diamond(px, lines=(), dist=None, symbol=None):
    """Orange warning diamond, content pre-rotated -45 deg like W2-6 (the quad is turned 45 deg)."""
    s = SS
    n = px * s
    img = Image.new("RGB", (n, n), ORANGE)
    d = ImageDraw.Draw(img)
    d.rectangle([4 * s, 4 * s, n - 4 * s, n - 4 * s], outline=BLACK, width=4 * s)
    sym = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sym)
    inner = 0.62 * n  # legend box that fits inside the turned square
    if symbol == "lane_ends":  # W4-2R: right lane line bends into the left lane; dashed lane line stops
        c, t = n / 2, 0.06 * n
        lx, rx, y0, y1 = c - 0.16 * n, c + 0.16 * n, c + 0.30 * n, c - 0.30 * n
        sd.rectangle([lx - t / 2, y1, lx + t / 2, y0], fill=BLACK)
        sd.line([(rx, y0), (rx, c + 0.02 * n), (c - 0.02 * n, y1 + 0.06 * n)], fill=BLACK, width=int(t), joint="curve")
        for k in range(2):
            ya = y0 - k * 0.14 * n
            sd.rectangle([c - t / 3, ya - 0.08 * n, c + t / 3, ya], fill=BLACK)
    else:
        rows = list(lines) + ([dist] if dist else [])
        pitch = inner / len(rows)
        y = n / 2 - inner / 2
        for k, ln in enumerate(rows):
            cy = y + pitch * (k + 0.5)
            if ln == dist and "/" in ln:
                distance(sd, n / 2, cy, ln, min(0.55 * pitch, 0.13 * n), BLACK)
            else:
                f = fit(sd, ln, inner, min(0.55 * pitch, 0.13 * n))
                text_cap(sd, n / 2, cy, ln, f, BLACK, anchor="m")
    sym = sym.rotate(-45, resample=Image.BICUBIC)
    img.paste(sym, (0, 0), sym)
    return img


def wz_rect(w, h, lines, bg, fg=BLACK, cap=None):
    """Rectangular work-zone / regulatory board with a border and centred lines."""
    s = SS
    img, d = cell(w, h, bg)
    d.rectangle([3 * s, 3 * s, w * s - 3 * s, h * s - 3 * s], outline=fg, width=3 * s)
    pitch = (h - 16) * s / len(lines)
    for k, ln in enumerate(lines):
        f = fit(d, ln, (w - 18) * s, cap * s if cap else 0.62 * pitch)
        text_cap(d, w * s / 2, 8 * s + pitch * (k + 0.5), ln, f, fg, anchor="m")
    return img


def work_zone_faces(K):
    """Paste the N2b faces; returns their rects in WzFace order (CorridorWorkZone.h)."""
    faces = [(wz_diamond(WZ_D, ["ROAD", "WORK"], "1 MILE"), WZ_D, WZ_D)]
    faces += [(wz_diamond(WZ_D, [t, "CLOSED"], "1/2 MILE"), WZ_D, WZ_D) for t in WZ_LANES]
    faces.append((wz_diamond(WZ_D, symbol="lane_ends"), WZ_D, WZ_D))
    faces.append((wz_rect(205, 128, ["ROAD", "CLOSED"], WHITE), 205, 128))  # R11-2, 48 x 30 in
    faces.append((wz_rect(205, 128, ["EXIT", "CLOSED"], ORANGE), 205, 128))  # E5-2a style panel
    faces.append((wz_rect(256, 128, ["END ROAD WORK"], ORANGE, cap=34), 256, 128))  # G20-2, 48 x 24 in
    rects, x = [], 512
    for img, w, h in faces:
        paste(K, img, x, WZ_Y)
        rects.append((x, WZ_Y, w, h))
        x += w
    assert x <= KW
    # NYR9-11 (NY only), 48 x 84 in, in the free column right of the landmark boards
    paste(K, wz_rect(160, 280, ["LICENSE", "SUSPENDED", "AFTER TWO", "WORK ZONE", "SPEEDING", "TICKETS"], WHITE),
          1728, 1760)
    rects.append((1728, 1760, 160, 280))
    return rects

# ── main ─────────────────────────────────────────────────────────────

# ── N3 network guide boards (RoadParams road::network::kGuideSigns board index order) ──
# Legends from the BQE exit list (docs/queens-network-reference/roads.md section 1, WP-BQE) and the I-278 control
# cities (Bronx eastbound, Brooklyn westbound). 512 x 256 px = a 20 x 10 ft overhead panel.
NET_Y = 2048
NET_BOARDS = [
    dict(tab="EXIT 39", shield=("NY", "25"), lines=["Queens Blvd"], dist="1/2 MILE"),
    dict(tab="EXIT 40", lines=["Broadway", "Roosevelt Av"], dist="1/2 MILE"),
    dict(tab="EXIT 41", shield=("NY", "25A"), lines=["Northern Blvd"], dist="1/2 MILE"),
    dict(shield=("I", "278"), card="WEST", lines=["Grand Central Pkwy W", "RFK Bridge"]),
    dict(tab="EXIT 42", lines=["Grand Central Pkwy E", "LaGuardia Airport"]),
    dict(shield=("I", "278"), card="EAST", lines=["Bronx", "Grand Central Pkwy"], dist="3 MILES"),
    dict(shield=("I", "278"), card="WEST", lines=["Brooklyn", "Long Island Expwy"], dist="2 MILES"),
    dict(tab="EXIT 41", shield=("NY", "25A"), lines=["Northern Blvd"], dist="1/2 MILE"),
    dict(tab="EXIT 40", lines=["Broadway", "Roosevelt Av"], dist="1/2 MILE"),
    dict(tab="EXIT 39", shield=("NY", "25"), lines=["Queens Blvd"], dist="1/2 MILE"),
    dict(tab="EXIT 35 W", shield=("I", "495"), card="WEST", lines=["Midtown Tunnel"], dist="1/2 MILE"),
    # N4 GCP, parkway style (roads.md section 2, WP-GCP exit list): the Interstates it meets by their shields, the
    # parkway itself by its trailblazer on the confirmation boards past each junction
    dict(tab="EXIT 10", shield=("I", "495"), lines=["Long Island Expwy"], dist="1/2 MILE"),  # EB
    dict(tab="EXIT 10", shield=("I", "495"), lines=["Long Island Expwy"], dist="1/2 MILE"),  # WB
    dict(tab="EXIT 4", shield=("I", "278"), card="WEST", lines=["Brooklyn Queens Expwy"], dist="1/2 MILE"),
    dict(shield=("PKWY", "Grand Central"), card="EAST", lines=["LaGuardia Airport", "Eastern Long Island"]),
    dict(shield=("PKWY", "Grand Central"), card="WEST", lines=["LaGuardia Airport", "RFK Bridge"]),
    # N5 rungs (roads.md sections 3-5, legends from the OSM destination tags of each exit): the Van Wyck and the
    # Clearview in Interstate style, the Cross Island in parkway style (the roads met by their shields)
    dict(tab="EXIT 7", shield=("NY", "25"), card="WEST", lines=["Union Tpke", "Jackie Robinson Pkwy W"],
         dist="1/2 MILE"),                                                                              # 16 VW SB
    dict(tab="EXIT 10", shield=("PKWY", "Grand Central"), lines=["LaGuardia Airport", "RFK Bridge"],
         dist="1/2 MILE"),                                                                              # 17 VW NB
    dict(tab="EXIT 12 A-B", shield=("I", "495"), lines=["Eastern Long Island", "Midtown Tunnel"],
         dist="1/2 MILE"),                                                                              # 18 VW NB
    dict(tab="EXIT 4 E-W", shield=("I", "495"), lines=["Manhattan", "Eastern Long Island"], dist="1/2 MILE"),  # 19 CLV
    dict(tab="EXIT 1", shield=("PKWY", "Grand Central"), lines=["Eastern Long Island", "RFK Bridge"],
         dist="1/2 MILE"),                                                                              # 20 CLV SB
    dict(tab="EXIT 30 E", shield=("I", "495"), card="EAST", lines=["Eastern Long Island"],
         dist="1/2 MILE"),                                                                              # 21 CIP SB
    dict(tab="EXIT 29 E-W", shield=("PKWY", "Grand Central"), lines=["Hauppauge", "Triborough Br"],
         dist="1/2 MILE"),                                                                              # 22 CIP SB
    dict(tab="EXIT 29 E-W", shield=("PKWY", "Grand Central"), lines=["RFK Bridge", "Eastern Long Island"],
         dist="1/2 MILE"),                                                                              # 23 CIP NB
    dict(shield=("PKWY", "Cross Island"), card="SOUTH", lines=["Belt Pkwy", "Kennedy Airport"]),        # 24 CIP SB
    dict(tab="EXIT 13", shield=("I", "678"), card="SOUTH", lines=["Van Wyck Expwy", "Kennedy Airport"],
         dist="1/2 MILE"),                                                                              # 25 GCP EB
    dict(tab="EXIT 21", shield=("I", "295"), card="NORTH", lines=["Throgs Neck Br", "Bronx"],
         dist="1/2 MILE"),                                                                              # 26 GCP
    dict(tab="EXIT 23", shield=("PKWY", "Cross Island"), lines=["Whitestone Br", "Belt Pkwy"],
         dist="1/2 MILE"),                                                                              # 27 GCP
]
NET_GORES = ["35 W", "39", "40", "41", "42", "43", "44", "10", "4",
             "7", "12 A-B", "4 E-W", "1", "30 E", "29 E-W", "13", "21", "23"]  # N5 from 9


def net_signs(K):
    """N3 boards (4 per row) then a row of gore signs; returns (board rects, gore rects)."""
    out = []
    for k, kw in enumerate(NET_BOARDS):
        x, y = (k % 4) * 512, NET_Y + (k // 4) * 256
        paste(K, board(512, 256, **kw), x, y)
        out.append((x, y, 512, 256))
    gy = NET_Y + 256 * ((len(NET_BOARDS) + 3) // 4)
    gores, per = [], KW // GORE
    for k, lb in enumerate(NET_GORES):  # N5: rows of 16
        x, y = (k % per) * GORE, gy + (k // per) * GORE
        paste(K, draw_gore(lb)[0], x, y)
        gores.append((x, y, GORE, GORE))
    assert gores[-1][1] + GORE <= KH, "N3 boards do not fit the kit atlas"
    return out, gores


# ── N6 local exits on the network roads (decision 8: signed but closed) ──
# Every exit record on a network road's drivable span (between its WALL records) gets a kNetExits row: the N3-N5 board
# and gore of its kGuideSigns row (RoadParams.h) when it has one (baked = 1: its board carries "1/2 MILE"), else a new
# board without a distance (the gantry hangs the D3 1 MILE / 1/2 MILE strip under it) and a gore sign, shared by label.
NET_ASSET = os.path.join(ROOT, "assets", "corridor", "queens_network_full.bin")
PARAMS = os.path.join(ROOT, "src", "scene", "RoadScene", "RoadParams.h")
N6_Y = 4096  # below the N3-N5 rows; the atlas grows to fit


def exit_label(ref):
    m = re.match(r"^(\d+)(.*)$", ref)
    return f"{m.group(1)} {m.group(2)}" if m and m.group(2) else ref


def net_exits():
    """[(road, ref, wb, board kwargs or (board, gore) of its row)] in road / station order."""
    d = open(NET_ASSET, "rb").read()
    tag = lambda t: d.find(struct.pack("<I", t)) + 8  # noqa: E731
    o, roads = tag(0x44414F52), {}
    for _ in range(struct.unpack_from("<I", d, o - 4)[0]):
        rid, _key, path = struct.unpack_from("<B7x16s40s", d, o)
        roads[rid], o = path.split(b"\0")[0].decode(), o + 64
    o, span = tag(0x4C4C4157), {r: [-1e30, 1e30] for r in roads}
    for _ in range(struct.unpack_from("<I", d, o - 4)[0]):
        sv, rid, end = struct.unpack_from("<dBB6x", d, o)
        span[rid][end], o = sv, o + 16
    src = open(PARAMS).read()
    block = src[src.index("kGuideSigns[] = {"):]
    rows = [(int(a), b, c == "true", int(e), int(g)) for a, b, c, e, _f, g, mi in re.findall(
        r'\{(\d+), "([^"]+)", (true|false), (-?\d+), (-?\d+), (-?\d+), (-?[\d.]+)f\}', block[:block.index("};")])
        if float(mi) < 0.0]  # advance rows: an exit's board
    num = lambda ref: re.match(r"\d*", ref).group()  # noqa: E731
    out = []
    for rid in sorted(r for r in roads if r):
        for e in sorted(read_exits(os.path.join(ROOT, "assets", roads[rid])), key=lambda e: e["s"]):
            if not span[rid][0] < e["s"] < span[rid][1]:
                continue
            row = next((r for r in rows if r[0] == rid and r[2] == bool(e["wb"]) and num(r[1]) == num(e["ref"])), None)
            if row:
                out.append((rid, e["ref"], e["wb"], (row[3], row[4])))
                continue
            sh, pk = shields(e["destRef"])
            names = [clean(x) for x in e["dest"].split(";") if x.strip()]
            kw = dict(tab=f"EXIT {exit_label(e['ref'])}", lines=((pk + names)[:2] or ["Local Streets"]))
            if sh:
                kw.update(shield=sh[0][:2], card=sh[0][2] or None)
            out.append((rid, e["ref"], e["wb"], kw))
    return out


def exit_arrow():
    """Exit-direction strip under a board (MUTCD 2E.37): an up-right arrow, the open top joins the board."""
    s = SS
    img, d = cell(CELL_W, STRIP_H)
    border(d, [3 * s, -1, CELL_W * s - 3 * s, STRIP_H * s - 3 * s], 18 * s, 4 * s, sides="blr")
    arrow_up_right(d, CELL_W * s - 70 * s, STRIP_H * s / 2 - 2 * s, 46 * s)
    return img


def n6_signs(K, ex):
    """Paste the N6 boards, new gores and the arrow strip from N6_Y; returns (boards, gores, arrow, rows)."""
    boards, gores, table, seen = [], [], [], {}
    labels = list(NET_GORES)
    for rid, ref, wb, kw in ex:
        if isinstance(kw, tuple):
            table.append((rid, ref, wb, kw[0], kw[1], 1))
            continue
        k = seen.setdefault(repr(sorted(kw.items())), len(boards))  # one cell per legend (both directions share it)
        if k == len(boards):
            x, yy = (k % 4) * 512, N6_Y + (k // 4) * 256
            paste(K, board(512, 256, **kw), x, yy)
            boards.append((x, yy, 512, 256))
        lb = exit_label(ref)
        if lb not in labels:
            labels.append(lb)
        table.append((rid, ref, wb, len(NET_BOARDS) + k, labels.index(lb), 0))
    y = N6_Y + 256 * ((len(boards) + 3) // 4)
    per = KW // GORE
    for k, lb in enumerate(labels[len(NET_GORES):]):
        gx, gy = (k % per) * GORE, y + (k // per) * GORE
        paste(K, draw_gore(lb)[0], gx, gy)
        gores.append((gx, gy, GORE, GORE))
    y += GORE * ((len(gores) + per - 1) // per)
    paste(K, exit_arrow(), 0, y)
    assert y + STRIP_H <= KH, "N6 signs do not fit the kit atlas"
    return boards, gores, (0, y, CELL_W, STRIP_H), table, labels


def build():
    global KH
    ex = net_exits()
    nb = len({repr(sorted(e[3].items())) for e in ex if not isinstance(e[3], tuple)})
    KH = N6_Y + 256 * ((nb + 3) // 4) + GORE * 3 + STRIP_H  # N6: room for the boards, up to 3 gore rows, the arrow
    gs = groups(read_exits())
    assert len(gs) <= COLS * 15, f"{len(gs)} panels do not fit the atlas"
    A = Image.new("RGB", (PW, PH), GREEN)
    K = Image.new("RGB", (KW, KH), GREEN)
    panels = []
    for i, g in enumerate(gs):
        x, y = (i % COLS) * CELL_W, (i // COLS) * CELL_H
        img, meta = draw_panel(g)
        paste(A, img, x, y)
        gx, gy = (i % (KW // GORE)) * GORE, (i // (KW // GORE)) * GORE
        gimg, gcap = draw_gore(g["label"])
        paste(K, gimg, gx, gy)
        panels.append((g, x, y, gx, gy, meta, gcap))
    for k, t in enumerate(STRIPS):
        paste(A, draw_strip(t), k * CELL_W, STRIP_Y)
    # mile markers: mids 0..79 in rows of 21, then tops (E, W) and lows (.0 .2 .4 .6 .8)
    mids, per = [], KW // MK_W
    for n in range(80):
        x, y = (n % per) * MK_W, MK_Y + (n // per) * MK_MID
        paste(K, marker_mid(n), x, y)
        mids.append((x, y))
    ty = MK_Y + 4 * MK_MID
    tops = [(k * MK_W, ty) for k in range(2)]
    for k, c in enumerate(["E", "W"]):
        paste(K, marker_top(c), *tops[k])
    lows = [((2 + k) * MK_W, ty) for k in range(5)]
    for k in range(5):
        paste(K, marker_low(2 * k), *lows[k])
    # exit 73 boards, sized to CorridorEndsMesh kSigns (w x h metres at ~0.84 cm/px)
    specs = [
        (512, 286, dict(lines=[], shield=("I", "495"), plaque="END", dist="1 MILE")),
        (512, 357, dict(lines=["Old Country Rd", "Riverhead"], shield=("CR", "58"), card="EAST", dist="1/2 MILE",
                        tab="EXIT 73")),
        None,  # roundabout W2-6
        (512, 341, dict(lines=["Riverhead", "Orient"], shield=("CR", "58"), card="EAST")),
        (512, 341, dict(lines=["New York"], shield=("I", "495"), card="WEST")),
    ]
    boards, bx = [], 0
    for sp in specs:
        if sp is None:
            boards.append(None)
            continue
        w, h, kw = sp
        paste(K, board(w, h, **kw), bx, BOARD_Y)
        boards.append((bx, BOARD_Y, w, h))
        bx += w
    yy = BOARD_Y + 360
    paste(K, roundabout_w2_6(160), 0, yy)
    boards[2] = (0, yy, 160, 160)
    paste(K, chevron_w1_8(128, 154), 160, yy)
    boards.append((160, yy, 128, 154))
    # D6 landmark boards in the free strip right of the chevron; column of short boards stacks
    lmk, lx, ly, cw = [], 288, yy, 0
    for w, h, draw in LANDMARK_BOARDS:
        if ly + h > KH:
            lx, ly, cw = lx + cw, yy, 0
        cw = max(cw, w)
        assert lx + w <= KW, "landmark boards do not fit the kit atlas"
        paste(K, draw(w, h), lx, ly)
        lmk.append((lx, ly, w, h))
        ly = ly + h if h <= 128 else KH  # only short boards share a column
    boards.append(lmk)
    assert len(gs) <= 116, "gore cells reach the N2b work-zone row"
    assert max(x + w for x, y, w, h in lmk) <= 1728, "landmark boards reach the NYR9-11 column"
    boards.append(work_zone_faces(K))
    nets = net_signs(K)
    boards.append(nets)
    n6 = n6_signs(K, ex)
    boards.append(n6)
    return gs, panels, mids, tops, lows, boards, A, K


def write_header(panels, mids, tops, lows, boards):
    r = lambda t: "{%d, %d, %d, %d}" % t  # noqa: E731
    L = ["// Generated by tools/sign_generator.py corridor (tools/sign_corridor.py) - do not edit.",
         "// Pixel rects into assets/signs/sign_panels.png (panels, strips) and sign_kit.png (gores, markers, boards).",
         "#pragma once", "", "// clang-format off", "namespace swish::sign_atlas {", "",
         "struct Rect { int x, y, w, h; };",
         "// Advance guide panel per (exit number, direction): tab (right-aligned, tabW wide) over the main panel;",
         "// gore = the E5-1a gore sign. minCap / maxTextW: smallest destination cap height and widest legend (px).",
         "struct Panel { int exitNo, westbound; Rect tab, main, gore; int minCap, maxTextW; const char* label; };",
         f"inline constexpr int kPanelsW = {PW}, kPanelsH = {PH}, kKitW = {KW}, kKitH = {KH};",
         f"inline constexpr int kCellW = {CELL_W}, kTabH = {TAB_H}, kMainH = {MAIN_H}, kStripH = {STRIP_H};",
         f"inline constexpr int kMinCap = {MIN_CAP}, kInnerW = {CELL_W - 60};",
         "inline constexpr Panel kPanels[] = {"]
    for g, x, y, gx, gy, m, gc in panels:
        tab = (x + CELL_W - m["tab_w"], y, m["tab_w"], TAB_H)
        L.append(f'    {{{g["no"]}, {g["wb"]}, {r(tab)}, {r((x, y + TAB_H, CELL_W, MAIN_H))}, {r((gx, gy, GORE, GORE))},'
                 f' {m["min_cap"]}, {m["max_w"]}, "{g["label"]}"}},')
    L.append("};")
    L.append("// Distance strips under the main panel: 0 = 1 MILE, 1 = 1/2 MILE, 2 = 1/4 MILE (SignDist).")
    L.append("inline constexpr Rect kStrips[] = {" + ", ".join(r((k * CELL_W, STRIP_Y, CELL_W, STRIP_H))
                                                         for k in range(len(STRIPS))) + "};")
    L.append("// Mile marker: top (shield + E/W), whole mile (0..79), tenth (.0 .2 .4 .6 .8), stacked.")
    L.append("inline constexpr Rect kMarkerTop[] = {" + ", ".join(r((x, y, MK_W, MK_TOP)) for x, y in tops) + "};")
    L.append("inline constexpr Rect kMarkerMid[] = {")
    for k in range(0, len(mids), 8):
        L.append("    " + ", ".join(r((x, y, MK_W, MK_MID)) for x, y in mids[k:k + 8]) + ",")
    L.append("};")
    L.append("inline constexpr Rect kMarkerLow[] = {" + ", ".join(r((x, y, MK_W, MK_LOW)) for x, y in lows) + "};")
    L.append("// C4 exit 73 boards in CorridorEndsMesh kSigns order, then the W1-8 chevron.")
    n6b, n6g, arrow, table, labels = boards[-1]
    L.append("inline constexpr Rect kBoards[] = {" + ", ".join(r(b) for b in boards[:-4]) + "};")
    L.append("// D6 landmark boards in RoadParams road::landmarks::kBoards cell order.")
    L.append("inline constexpr Rect kLandmarkBoards[] = {" + ", ".join(r(b) for b in boards[-4]) + "};")
    L.append("// N2b work-zone faces in WzFace order (CorridorWorkZone.h).")
    L.append("inline constexpr Rect kWorkZone[] = {" + ", ".join(r(b) for b in boards[-3]) + "};")
    L.append("// N3 network guide boards (RoadParams road::network::kGuideSigns board order) and gore signs; N6 boards")
    L.append("// (no distance line) and gores after them.")
    L.append("inline constexpr Rect kNetBoards[] = {" + ", ".join(r(b) for b in boards[-2][0] + n6b) + "};")
    L.append("inline constexpr Rect kNetGores[] = {" + ", ".join(r(b) for b in boards[-2][1] + n6g) + "};")
    L.append("inline constexpr const char* kNetGoreLabels[] = {" + ", ".join(f'"{g}"' for g in labels) + "};")
    L.append("// N6 exit-direction strip (an up-right arrow) hung under a board at the exit.")
    L.append(f"inline constexpr Rect kNetArrow = {r(arrow)};")
    L.append("// N6 every exit on a network road's drivable span: kNetBoards / kNetGores cells; baked = 1 when the board")
    L.append("// is an N3-N5 one with its distance line (an exit-direction sign crops it off).")
    L.append("struct NetExit { int road; const char* ref; int westbound, board, gore, baked; };")
    L.append("inline constexpr NetExit kNetExits[] = {")
    for rid, ref, wb, b, g, bk in table:
        L.append(f'    {{{rid}, "{ref}", {wb}, {b}, {g}, {bk}}},')
    L.append("};")
    L += ["", "}  // namespace swish::sign_atlas", "// clang-format on", ""]
    open(HEADER, "w").write("\n".join(L))


def check(panels):
    """Self-check: every legend fits its panel at a legible size."""
    bad = [(g["no"], g["wb"], m) for g, *_, m, _gc in panels
           if m["min_cap"] < MIN_CAP or m["max_w"] > CELL_W - 60 or m["tab_w"] > CELL_W - 40]
    assert not bad, f"legend does not fit: {bad}"


def main():
    gs, panels, mids, tops, lows, boards, A, K = build()
    check(panels)
    os.makedirs(OUT_DIR, exist_ok=True)
    A.save(os.path.join(OUT_DIR, "sign_panels.png"), optimize=True)
    K.save(os.path.join(OUT_DIR, "sign_kit.png"), optimize=True)
    write_header(panels, mids, tops, lows, boards)
    print(f"{len(gs)} panels + gores, {len(STRIPS)} strips, {len(mids)} marker numbers -> {OUT_DIR}, {HEADER}")
    for g, *_, m, gc in panels:
        print(f'  {g["no"]:>3} {"WB" if g["wb"] else "EB"} EXIT {g["label"]:<8} cap {m["min_cap"]:>2}px  '
              f'{" / ".join(k + " " + n + (" " + c if c else "") for k, n, c in g["shields"])}  | '
              f'{" / ".join(g["lines"])}')


if __name__ == "__main__":
    main()
