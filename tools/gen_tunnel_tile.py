#!/usr/bin/env python3
"""Generate the C4 tunnel wall tile texture set (textures/tile.jpg, tile_normal.png, tile_roughness.jpg).

Glazed 3 x 6 in subway tile in running bond with 1/8 in grout: 8 tiles x 16 courses per 1024 px square
(48 in = 1.219 m, road::ends::kTileRepeat). Albedo is near-white so the draw colour sets the white field or the blue and gold stripes.
Procedural, no third-party source: `python3 tools/gen_tunnel_tile.py` rewrites the three files (seeded).
"""
import os

import numpy as np
from PIL import Image

N, COLS, ROWS = 1024, 8, 16
TW, TH = N // COLS, N // ROWS  # 128 x 64 px tile
GROUT = 3.0                    # px, 1/8 in at 0.84 px/mm
BEVEL = 6.0                    # px of rounded glaze edge
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "textures")

rng = np.random.default_rng(495)
y, x = np.mgrid[0:N, 0:N].astype(np.float64)
row = (y // TH).astype(int)
xs = (x + (row % 2) * (TW / 2)) % N  # running bond: odd courses shift half a tile
col = (xs // TW).astype(int)
dx = np.minimum(xs % TW, TW - xs % TW)  # distance to the nearest vertical joint
dy = np.minimum(y % TH, TH - y % TH)
d = np.minimum(dx, dy) - GROUT / 2       # >0 on the tile face
tile = d > 0
# height: 0 in the grout, rounded shoulder up to 1 on the face
h = np.clip(d / BEVEL, 0.0, 1.0)
h = np.sin(h * np.pi / 2)
tid = row * COLS + col
var = rng.normal(0.0, 1.0, (ROWS * COLS, 3))[tid]  # per-tile glaze variation
h += 0.004 * var[..., 0] * (x - x.mean()) / N + 0.004 * var[..., 1] * (y - y.mean()) / N  # tiles not quite coplanar

gy, gx = np.gradient(h)
k = 2.2
n = np.dstack([-gx * k, gy * k, np.ones_like(h)])  # OpenGL convention: +green = up the image
n /= np.linalg.norm(n, axis=2, keepdims=True)
Image.fromarray(((n * 0.5 + 0.5) * 255).round().astype(np.uint8)).save(os.path.join(OUT, "tile_normal.png"))

noise = rng.normal(0.0, 1.0, (N, N))
glaze = np.array([246, 243, 236]) * (1.0 + 0.015 * var[..., 2:3]) + 2.0 * noise[..., None]
grout = np.array([150, 146, 138]) + 6.0 * noise[..., None]
alb = np.where(tile[..., None], glaze, grout)
Image.fromarray(np.clip(alb, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "tile.jpg"), quality=92)

rough = np.where(tile, 0.16 + 0.10 * (1.0 - h) + 0.02 * noise, 0.85 + 0.05 * noise)
Image.fromarray((np.clip(rough, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, "tile_roughness.jpg"), quality=92)
print("wrote tile.jpg, tile_normal.png, tile_roughness.jpg to", os.path.normpath(OUT))
