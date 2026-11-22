#!/usr/bin/env python3
"""Generate the D5 roadside dressing textures (assets/dressing/, loaded in the debug build only).

dressing.png (2048^2 atlas, RGBA, alpha-tested): rows 0-1 four tree cards 512^2 (oak, maple, pitch pine, scrub oak),
row 2 four house backs (12 x 6 m each), rows 3-6 horizontally tileable facades (apartment brick 72 x 9 m, office
ribbon 64 x 8 m, glass tower 102.4 x 12.8 m, warehouse 64 x 8 m), row 7 roofs (shingle | flat membrane | barn siding).
skyline.png (4096 x 1024): Midtown Manhattan from Long Island City, 4.5 km x 560 m, buildings placed from memory.
Plus flat _normal and _roughness maps. Procedural, no third-party source: `python3 tools/gen_dressing.py` (seeded).
Layout constants are mirrored in src/scene/RoadScene/RoadParams.h (road::dressing).
"""
import os

import numpy as np
from PIL import Image, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "dressing")
rng = np.random.default_rng(495)
N = 2048
alb = np.zeros((N, N, 4), np.float64)
rough = np.full((N, N), 0.85)


def noise(h, w, scale, seed):
    """Smooth value noise in [0, 1]: random grid upsampled bicubic, tileable horizontally when w % scale == 0."""
    r = np.random.default_rng(seed)
    gh, gw = max(2, h // scale + 2), max(2, w // scale)
    g = r.random((gh, gw))
    g = np.concatenate([g, g[:, :3]], axis=1)  # wrap columns
    img = Image.fromarray((g * 255).astype(np.uint8)).resize(((gw + 3) * scale, gh * scale), Image.BICUBIC)
    return np.asarray(img)[:h, :w].astype(np.float64) / 255.0


# ── Trees: 512^2 cards, trunk at the bottom centre ────────────────────────────
def blob_crown(cell, centres, radii, base, light, seed, holes=0.35, needles=False):
    """Paint leaf clumps: spherical shading from the upper left, AO toward the crown core, ragged edges."""
    h, w = cell.shape[:2]
    r = np.random.default_rng(seed)
    hf = noise(h, w, 3, seed + 1)
    lf = noise(h, w, 12, seed + 2)
    cy0 = np.mean([c[1] for c in centres])
    for (cx, cy), rad in zip(centres, radii):
        x0, x1 = int(max(0, cx - rad * 1.4)), int(min(w, cx + rad * 1.4))
        y0, y1 = int(max(0, cy - rad * 1.4)), int(min(h, cy + rad * 1.4))
        if x1 <= x0 or y1 <= y0:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float64)
        dx, dy = (xx - cx) / rad, (yy - cy) / rad
        ang = np.arctan2(dy, dx)
        edge = 1.0 + 0.22 * np.sin(ang * r.integers(3, 7) + r.random() * 6) + 0.25 * (lf[y0:y1, x0:x1] - 0.5)
        d = np.sqrt(dx * dx + dy * dy) / edge
        inside = d < 1.0
        # leaves thin out toward the clump edge: holes where the fine noise is low
        keep = hf[y0:y1, x0:x1] > holes * np.clip((d - 0.55) / 0.45, 0.0, 1.0) ** 0.7
        m = inside & keep
        if needles:  # pine: streaky tufts
            m &= (np.sin((xx * 0.9 + yy * 0.5) + hf[y0:y1, x0:x1] * 9.0) > -0.6)
        nz = np.sqrt(np.clip(1.0 - np.minimum(d, 1.0) ** 2, 0.0, 1.0))
        shade = np.clip(0.45 + 0.55 * (-0.55 * dx - 0.65 * dy + 0.5 * nz), 0.25, 1.15)
        ao = 1.0 - 0.35 * np.clip((cy - cy0) / (h * 0.25) + 0.3, 0.0, 1.0)  # lower clumps sit in shade
        tint = base * (1.0 + 0.12 * (r.random(3) - 0.5)) * light
        speck = 0.75 + 0.5 * hf[y0:y1, x0:x1]
        col = tint[None, None, :] * (shade * ao * speck)[..., None]
        sub = cell[y0:y1, x0:x1]
        sub[m, :3] = col[m]
        sub[m, 3] = 1.0


def trunk(cell, x, y0, y1, w0, w1, colour, seed, wobble=0.0):
    h, w = cell.shape[:2]
    r = np.random.default_rng(seed)
    bark = noise(h, w, 2, seed)
    ph = r.random() * 6
    for y in range(int(y1), int(y0)):
        t = (y0 - y) / max(1.0, (y0 - y1))
        cx = x + wobble * np.sin(t * 5.0 + ph) * 12.0
        hw = (w0 + (w1 - w0) * t) * 0.5
        xa, xb = int(cx - hw), int(cx + hw) + 1
        xs = np.arange(xa, xb)
        lit = 0.6 + 0.5 * np.clip((cx - xs) / max(hw, 1.0), -1, 1)  # rounded, lit from the left
        cell[y, xa:xb, :3] = colour * (lit * (0.7 + 0.5 * bark[y, xa:xb]))[:, None]
        cell[y, xa:xb, 3] = 1.0


def branch(cell, p, q, wid, colour):
    n = int(np.hypot(q[0] - p[0], q[1] - p[1])) + 1
    for i in range(n):
        t = i / max(1, n - 1)
        x, y = p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t
        hw = max(1.0, wid * (1.0 - 0.6 * t))
        cell[int(y - hw):int(y + hw) + 1, int(x - hw):int(x + hw) + 1, :3] = colour
        cell[int(y - hw):int(y + hw) + 1, int(x - hw):int(x + hw) + 1, 3] = 1.0


def deciduous(seed, base, n, env, holes):
    c = np.zeros((512, 512, 4))
    r = np.random.default_rng(seed)
    bark = np.array([0.20, 0.16, 0.12])
    trunk(c, 256, 511, 300, 26, 12, bark, seed)
    ex, ey, rx, ry = env
    pts, rad = [], []
    for _ in range(n):
        a, d = r.random() * 2 * np.pi, np.sqrt(r.random())
        pts.append((ex + np.cos(a) * d * rx, ey + np.sin(a) * d * ry))
        rad.append(r.uniform(38, 70))
    for (px, py) in pts[: n // 3]:
        branch(c, (256, 330), (px, py + 10), 5, bark * 0.9)
    order = np.argsort([p[1] for p in pts])[::-1]  # back (low) clumps first, top clumps over them
    blob_crown(c, [pts[i] for i in order], [rad[i] for i in order], base, 1.0, seed + 7, holes)
    return c


def pitch_pine(seed):
    c = np.zeros((512, 512, 4))
    r = np.random.default_rng(seed)
    bark = np.array([0.30, 0.19, 0.13])  # plated red-brown bark
    trunk(c, 250, 511, 70, 22, 8, bark, seed, wobble=0.8)
    pts, rad = [], []
    for k in range(14):
        y = r.uniform(40, 290)
        side = -1 if k % 2 else 1
        x = 250 + side * r.uniform(30, 170) * (0.5 + 0.5 * (y - 40) / 250)
        branch(c, (250 + 4 * np.sin(y / 40), y + 25), (x, y), 3, bark * 0.8)
        for _ in range(7):  # tuft cluster at the branch end
            pts.append((x + r.normal(0, 24), y + r.normal(0, 14)))
            rad.append(r.uniform(22, 38))
    order = np.argsort([p[1] for p in pts])[::-1]
    blob_crown(c, [pts[i] for i in order], [rad[i] for i in order], np.array([0.13, 0.19, 0.07]), 1.0, seed + 3,
               0.35, needles=True)
    return c


def scrub(seed):
    c = np.zeros((512, 512, 4))
    r = np.random.default_rng(seed)
    pts, rad = [], []
    for _ in range(70):
        x = r.uniform(40, 472)
        top = 300 + 120 * ((x - 256) / 230) ** 2  # mound
        pts.append((x, r.uniform(top, 500)))
        rad.append(r.uniform(28, 50))
    order = np.argsort([p[1] for p in pts])[::-1]
    blob_crown(c, [pts[i] for i in order], [rad[i] for i in order], np.array([0.12, 0.18, 0.07]), 1.0, seed, 0.25)
    return c


cells = [
    deciduous(11, np.array([0.15, 0.24, 0.08]), 52, (256, 190, 200, 150), 0.30),  # oak: lumpy, dark
    deciduous(23, np.array([0.19, 0.30, 0.09]), 64, (256, 200, 180, 165), 0.22),  # maple: round, brighter
    pitch_pine(37),
    scrub(41),
]
def tidy(c):
    """Close pinholes (they sparkle under the alpha test), then bleed leaf colour into the clear texels so the box-filtered
    mips fade to foliage, not to black."""
    a = Image.fromarray((c[..., 3] * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    a = (np.asarray(a) > 110).astype(np.float64)
    rgb = c[..., :3].copy()
    known = c[..., 3] > 0.5
    for rad in (1, 2, 4, 8, 16, 32, 64):
        w = np.asarray(Image.fromarray((known * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(rad)), np.float64)
        acc = np.dstack([np.asarray(Image.fromarray((rgb[..., k] * known * 255).astype(np.uint8)).filter(
            ImageFilter.GaussianBlur(rad)), np.float64) for k in range(3)])
        fill = (~known) & (w > 2)
        rgb[fill] = acc[fill] / w[fill, None]
        known = known | fill
    c[..., :3] = rgb
    c[..., 3] = a
    return c


for i, c in enumerate(cells):
    c = tidy(c)
    c[:4, :, 3] = 0.0  # gutter: no mip bleed into the neighbour cell
    c[:, :4, 3] = 0.0
    c[:, -4:, 3] = 0.0
    alb[0:512, i * 512:(i + 1) * 512] = c

# ── Row 2 (y 512..767): house backs, 4 variants 512 x 256 = 12 x 6 m (42.7 px/m) ─────
Y = 512
PX = 512 / 12.0
sidings = [np.array([0.82, 0.80, 0.74]), np.array([0.74, 0.68, 0.55]), np.array([0.60, 0.66, 0.70]),
           np.array([0.55, 0.30, 0.22])]  # white, beige, grey-blue, brick
fine = noise(256, 2048, 2, 5)
for v, sc in enumerate(sidings):
    x0 = v * 512
    tile = np.ones((256, 512, 3)) * sc
    yy = np.arange(256)[:, None]
    if v < 3:
        tile *= (0.93 + 0.07 * ((yy % 9) > 1))[..., None]  # clapboard laps every 0.2 m
    else:
        tile *= (0.88 + 0.12 * (((yy % 4) > 0) & ((np.arange(512)[None, :] + (yy // 4) % 2 * 5) % 10 > 0)))[..., None]
    tile *= (0.92 + 0.12 * fine[:, x0:x0 + 512])[..., None]
    tile[:14] *= 0.55  # eave shadow
    tile[-17:] = np.array([0.55, 0.54, 0.52]) * (0.9 + 0.2 * fine[-17:, x0:x0 + 512, None])  # foundation
    for fl, wy in ((0, 160), (1, 50)):
        n_w = 3 if v % 2 else 4
        for k in range(n_w):
            if fl == 0 and k == 1:  # sliding door on the ground floor
                wx, ww, wh, yy0 = int(512 * (k + 0.5) / n_w - 40), 80, 90, 150
            else:
                wx, ww, wh, yy0 = int(512 * (k + 0.5) / n_w - 20), 40, 60, wy
            tile[yy0 - 4:yy0 + wh + 4, wx - 4:wx + ww + 4] = [0.92, 0.92, 0.90]  # trim
            g = np.linspace(0.32, 0.14, wh)[:, None, None] * np.array([0.75, 0.85, 1.0])
            tile[yy0:yy0 + wh, wx:wx + ww] = g
            tile[yy0 + wh // 2 - 1:yy0 + wh // 2 + 1, wx:wx + ww] = [0.9, 0.9, 0.88]  # sash
            if v == 1 and fl == 1:  # shutters
                tile[yy0 - 4:yy0 + wh + 4, wx - 18:wx - 6] = [0.12, 0.18, 0.14]
                tile[yy0 - 4:yy0 + wh + 4, wx + ww + 6:wx + ww + 18] = [0.12, 0.18, 0.14]
    alb[Y:Y + 256, x0:x0 + 512, :3] = tile
    alb[Y:Y + 256, x0:x0 + 512, 3] = 1.0
    rough[Y:Y + 256, x0:x0 + 512] = 0.7


# ── Rows 3-6: horizontally tileable facades, 2048 wide ───────────────────────
def band(y, h_m, w_m):
    return y, 256 / h_m, 2048 / w_m  # top, px per metre vertically, horizontally


def grid_windows(t, r_, px_x, px_y, bay, floor, win_w, win_h, sill, frame, glass, rgh, ac=False, seed=0):
    rr = np.random.default_rng(seed)
    nb, nf = int(round(2048 / (bay * px_x))), int(round(256 / (floor * px_y)))
    for f in range(nf):
        for b in range(nb):
            x = int(b * bay * px_x + (bay - win_w) * 0.5 * px_x)
            y = int(f * floor * px_y + (floor - sill - win_h) * px_y)
            w_, h_ = int(win_w * px_x), int(win_h * px_y)
            t[y - 2:y + h_ + 2, x - 2:x + w_ + 2] = frame
            lum = rr.uniform(0.6, 1.25)
            g = np.linspace(1.0, 0.55, h_)[:, None, None] * glass * lum
            t[y:y + h_, x:x + w_] = g
            if rr.random() < 0.3:  # blinds half down
                t[y:y + int(h_ * rr.uniform(0.2, 0.6)), x:x + w_] = np.array([0.70, 0.68, 0.62]) * lum
            r_[y:y + h_, x:x + w_] = 0.12
            if ac and rr.random() < 0.25:
                t[y + h_ + 2:y + h_ + 12, x + w_ // 4:x + w_ // 4 + 16] = [0.62, 0.62, 0.6]


# Row 3: apartment brick (LeFrak City / Rego Park), 3 floors x 3 m, 24 bays x 3 m
y3, py3, px3 = band(768, 9.0, 72.0)
t = np.ones((256, 2048, 3)) * np.array([0.52, 0.36, 0.27])
yy, xx = np.mgrid[0:256, 0:2048]
mortar = ((yy % 3) == 0) | (((xx + (yy // 3) % 2 * 3) % 7) == 0)
t *= (0.85 + 0.25 * noise(256, 2048, 4, 31))[..., None]
t[mortar] = t[mortar] * 0.6 + 0.3
r3 = np.full((256, 2048), 0.85)
for f in range(3):  # concrete floor slab band
    yb = int(f * 3.0 * py3)
    t[yb:yb + 6] = [0.72, 0.70, 0.66]
grid_windows(t, r3, px3, py3, 3.0, 3.0, 1.5, 1.5, 0.9, [0.80, 0.80, 0.78], np.array([0.20, 0.24, 0.28]), r3, True, 3)
alb[y3:y3 + 256, :, :3] = t
alb[y3:y3 + 256, :, 3] = 1.0
rough[y3:y3 + 256] = r3

# Row 4: office ribbon windows (Melville, Lake Success), 2 floors x 4 m, mullions 1.6 m
y4, py4, px4 = band(1024, 8.0, 64.0)
t = np.ones((256, 2048, 3)) * np.array([0.78, 0.76, 0.70]) * (0.94 + 0.08 * noise(256, 2048, 8, 41))[..., None]
r4 = np.full((256, 2048), 0.6)
for f in range(2):
    ya, yb = int((f * 4.0 + 0.4) * py4), int((f * 4.0 + 2.9) * py4)
    sky = np.linspace(0.30, 0.12, yb - ya)[:, None, None] * np.array([0.55, 0.72, 0.80])
    t[ya:yb] = sky * (0.9 + 0.2 * noise(yb - ya, 2048, 64, 43 + f))[..., None]
    r4[ya:yb] = 0.06
    for k in range(40):
        x = int(k * 51.2)
        t[ya:yb, x:x + 3] = [0.30, 0.31, 0.32]
alb[y4:y4 + 256, :, :3] = t
alb[y4:y4 + 256, :, 3] = 1.0
rough[y4:y4 + 256] = r4

# Row 5: glass curtain wall (Long Island City towers), 4 floors x 3.2 m, mullions 1.6 m
y5, py5, px5 = band(1280, 12.8, 102.4)
t = np.zeros((256, 2048, 3))
r5 = np.full((256, 2048), 0.08)
pv = np.random.default_rng(51).uniform(0.8, 1.2, (4, 64))
for f in range(4):
    for k in range(64):
        ya, xa = f * 64, k * 32
        g = np.linspace(0.34, 0.18, 64)[:, None, None] * np.array([0.62, 0.72, 0.80]) * pv[f, k]
        t[ya:ya + 64, xa:xa + 32] = g
    t[f * 64:f * 64 + 7] = [0.55, 0.57, 0.58]  # slab edge
    r5[f * 64:f * 64 + 7] = 0.5
t[:, ::32] = [0.45, 0.47, 0.48]
alb[y5:y5 + 256, :, :3] = t
alb[y5:y5 + 256, :, 3] = 1.0
rough[y5:y5 + 256] = r5

# Row 6: warehouse (Maspeth), corrugated metal, dock doors every 16 m
y6, py6, px6 = band(1536, 8.0, 64.0)
t = np.ones((256, 2048, 3)) * np.array([0.70, 0.69, 0.64])
t *= (0.86 + 0.14 * (np.sin(xx[:256] * 2 * np.pi / 6.0) > 0))[..., None]
t *= (0.9 + 0.15 * noise(256, 2048, 16, 61))[..., None]
streak = noise(256, 2048, 32, 63)
t *= (1.0 - 0.08 * np.clip((streak - 0.6) * 4, 0, 1))[..., None]  # weathering
for k in range(4):
    x = int((k * 16 + 6) * px6)
    t[256 - int(4.3 * py6):256 - int(0.5 * py6), x:x + int(3.6 * px6)] = [0.42, 0.44, 0.47]
    t[256 - int(4.3 * py6):256 - int(0.5 * py6):6, x:x + int(3.6 * px6)] *= 0.8
t[-16:] = [0.45, 0.45, 0.44]  # concrete base
alb[y6:y6 + 256, :, :3] = t
alb[y6:y6 + 256, :, 3] = 1.0
rough[y6:y6 + 256] = 0.45

# Row 7: roofs. u 0..0.5 asphalt shingle, 0.5..0.75 flat roof membrane, 0.75..1 red barn siding
y7 = 1792
t = np.zeros((256, 2048, 3))
sh = np.ones((256, 1024, 3)) * np.array([0.22, 0.21, 0.20]) * (0.8 + 0.35 * noise(256, 1024, 3, 71))[..., None]
sh[(np.arange(256) % 16) < 2] *= 0.55
t[:, :1024] = sh
fl = np.ones((256, 512, 3)) * np.array([0.62, 0.62, 0.60]) * (0.9 + 0.15 * noise(256, 512, 6, 73))[..., None]
for k in range(5):
    a, b = int(rng.uniform(20, 200)), int(rng.uniform(20, 450))
    fl[a:a + 30, b:b + 40] = [0.38, 0.39, 0.40]  # rooftop units
t[:, 1024:1536] = fl
bn = np.ones((256, 512, 3)) * np.array([0.50, 0.14, 0.10]) * (0.85 + 0.25 * noise(256, 512, 4, 75))[..., None]
bn[:, ::12] *= 0.6
bn[:10] = bn[-10:] = [0.88, 0.86, 0.80]
t[:, 1536:] = bn
alb[y7:y7 + 256, :, :3] = t
alb[y7:y7 + 256, :, 3] = 1.0
rough[y7:y7 + 256] = 0.9

os.makedirs(OUT, exist_ok=True)
a8 = np.clip(alb * 255, 0, 255).astype(np.uint8)
Image.fromarray(a8, "RGBA").save(os.path.join(OUT, "dressing.png"), optimize=True)
Image.fromarray((np.clip(rough, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, "dressing_roughness.png"))
flat = np.zeros((4, 4, 3), np.uint8) + np.array([128, 128, 255], np.uint8)
Image.fromarray(flat).save(os.path.join(OUT, "dressing_normal.png"))

# ── Skyline: 4096 x 1024 = 4.5 km x 560 m, ground on the bottom row ─────────
SW, SH, WM, HM = 4096, 1024, 4500.0, 560.0
sx, sy = SW / WM, SH / HM
sky = np.zeros((SH, SW, 4))
r = np.random.default_rng(7)
win = noise(SH, SW, 2, 77)


def tower(xc, w, h, col, crown=None, taper=0.0, setbacks=()):
    """One silhouette: x centre, width, height in metres (card scale); setbacks = [(height, width)]."""
    x0, x1 = int((xc + WM / 2 - w / 2) * sx), int((xc + WM / 2 + w / 2) * sx)
    ytop = SH - int(h * sy)
    steps = [(0.0, w)] + list(setbacks)
    for y in range(max(0, ytop), SH):
        hm = (SH - y) / sy
        ww = w
        for hh, sw in steps:
            if hm >= hh:
                ww = sw
        ww *= 1.0 - taper * hm / h
        a, b = int((xc + WM / 2 - ww / 2) * sx), int((xc + WM / 2 + ww / 2) * sx)
        a, b = max(0, a), min(SW, b)
        if b <= a:
            continue
        xs = np.arange(a, b)
        side = np.where(xs < (a + b) * 0.5 - (b - a) * 0.1, 0.82, 1.0)  # left face in shade
        floor = 0.92 + 0.08 * ((y % 6) > 1)
        c = col[None, :] * (side * floor * (0.9 + 0.2 * win[y, a:b]))[:, None]
        sky[y, a:b, :3] = c
        sky[y, a:b, 3] = 1.0
    if crown == "spire":
        sh_ = int(h * 0.16 * sy)
        xm = int((xc + WM / 2) * sx)
        sky[ytop - sh_:ytop, xm - 1:xm + 2, :3] = col * 0.9
        sky[ytop - sh_:ytop, xm - 1:xm + 2, 3] = 1.0
    if crown == "arch":  # Chrysler: stepped arches narrowing to a needle
        for k in range(7):
            ww = w * (0.8 - 0.11 * k)
            yy0 = ytop - int((k + 1) * 9 * sy)
            a, b = int((xc + WM / 2 - ww / 2) * sx), int((xc + WM / 2 + ww / 2) * sx)
            sky[yy0:yy0 + int(9 * sy), a:b, :3] = np.array([0.80, 0.80, 0.82])
            sky[yy0:yy0 + int(9 * sy), a:b, 3] = 1.0
        xm = int((xc + WM / 2) * sx)
        sky[ytop - int(110 * sy):ytop - int(60 * sy), xm - 1:xm + 1, :3] = 0.8
        sky[ytop - int(110 * sy):ytop - int(60 * sy), xm - 1:xm + 1, 3] = 1.0


stone, glass, dark, pale = (np.array([0.62, 0.58, 0.52]), np.array([0.42, 0.50, 0.58]), np.array([0.24, 0.27, 0.30]),
                            np.array([0.72, 0.74, 0.76]))
# filler: dense midrise, taller toward Midtown's centre (x ~ 0), Downtown cluster far left
for _ in range(900):
    xc = r.uniform(-2250, 2250)
    core = np.exp(-((xc + 100) / 1100) ** 2)
    dt = np.exp(-((xc + 2000) / 180) ** 2)
    h = r.uniform(25, 70) + core * r.uniform(0, 150) + dt * r.uniform(0, 120)
    w = r.uniform(25, 70)
    sb = ((h * r.uniform(0.65, 0.85), w * r.uniform(0.55, 0.8)),) if r.random() < 0.45 else ()
    tower(xc, w, h, [stone, glass, dark, pale][r.integers(4)] * r.uniform(0.85, 1.1), setbacks=sb)
# landmarks (heights at card scale: farther ones reduced by distance ratio)
tower(-2080, 40, 230, pale, "spire", taper=0.35)  # One World Trade Center, ~7 km
tower(-1150, 60, 258, glass, taper=0.15)  # 30 Hudson Yards
tower(-1050, 50, 205, stone)  # 35 Hudson Yards
tower(-700, 55, 381, stone, "spire", setbacks=((90, 45), (250, 35), (320, 22)))  # Empire State
tower(-420, 50, 300, glass, "spire", taper=0.25)  # Bank of America Tower
tower(-250, 90, 246, stone * 0.9)  # MetLife
tower(-150, 55, 427, dark, taper=0.3)  # One Vanderbilt
tower(50, 45, 282, pale, "arch", setbacks=((180, 38),))  # Chrysler
tower(250, 90, 210, glass * 0.85)  # UN Secretariat (close, at the river)
tower(600, 50, 279, pale)  # Citigroup Center
tower(650, 40, 280, glass, taper=0.4)  # 53 West 53rd
tower(800, 28, 426, pale)  # 432 Park Avenue
tower(1000, 18, 380, stone, setbacks=((300, 14),))  # 111 West 57th
tower(1060, 45, 400, glass)  # Central Park Tower
tower(1150, 40, 260, glass * 0.9)  # One57
sky[..., :3] = 0.9 * sky[..., :3] + 0.1 * np.array([0.52, 0.56, 0.60])  # a little airlight; the scene haze adds the rest
sky[-6:, :, :3] = sky[-7:-6, :, :3]  # solid ground row
sky[-6:, :, 3] = 1.0
Image.fromarray(np.clip(sky * 255, 0, 255).astype(np.uint8), "RGBA").save(os.path.join(OUT, "skyline.png"),
                                                                         optimize=True)
Image.fromarray(np.full((4, 4), 230, np.uint8)).save(os.path.join(OUT, "skyline_roughness.png"))
Image.fromarray(flat).save(os.path.join(OUT, "skyline_normal.png"))
print("wrote dressing + skyline textures to", os.path.normpath(OUT))
