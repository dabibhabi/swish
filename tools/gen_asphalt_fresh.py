#!/usr/bin/env python3
"""Derive the D1 fresh-asphalt set (textures/asphalt_fresh*.jpg) from ambientCG Asphalt012 (CC0).

Asphalt012 has rust-red stained cracks; fresh LIE asphalt has none. Red pixels are filled with aggregate from shifted
clean copies of the same texture, so only the black-and-grey aggregate stays; normal and roughness get the same fill.
`python3 tools/gen_asphalt_fresh.py` rewrites the three files.
"""
import os

import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "assets", "Asphalt012_1K-JPG", "Asphalt012_1K-JPG_")
OUT = os.path.join(ROOT, "textures", "asphalt_fresh")

rgb = np.asarray(Image.open(SRC + "Color.jpg").convert("RGB")).astype(np.float64) / 255.0
red = np.clip((rgb[..., 0] - 0.5 * (rgb[..., 1] + rgb[..., 2])) * 12.0, 0.0, 1.0)  # rust stain mask
red = np.asarray(Image.fromarray((red * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5))) / 255.0
soft = np.asarray(Image.fromarray((red * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5)))[..., None] / 255.0


def fill(img):
    """Each stained pixel takes aggregate from a shifted copy that is clean there (stays tileable)."""
    out, todo = img.copy(), red > 0.05
    for dy, dx in ((317, 211), (149, 433), (401, 97), (263, 389), (71, 157)):
        src, ok = np.roll(img, (dy, dx), (0, 1)), np.roll(red, (dy, dx), (0, 1)) <= 0.05
        take = todo & ok
        out[take] = src[take]
        todo &= ~take
    return np.clip(img * (1.0 - soft) + out * soft, 0, 1)


Image.fromarray((fill(rgb) * 255).astype(np.uint8)).save(OUT + ".jpg", quality=92)
for suffix, name in (("NormalGL", "_normal"), ("Roughness", "_roughness")):  # the crack grooves go too
    img = np.asarray(Image.open(SRC + suffix + ".jpg").convert("RGB")).astype(np.float64) / 255.0
    Image.fromarray((fill(img) * 255).astype(np.uint8)).save(OUT + name + ".jpg", quality=92)
assert red.mean() < 0.25, "stain mask ate the texture"  # self-check: Asphalt012 stains (dilated) cover ~20 %
print(f"wrote {OUT}*.jpg (stain mask {red.mean():.1%})")
