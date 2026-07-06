#!/usr/bin/env python3
"""
Generate FHWA-compliant highway sign textures for the Swish renderer.

Uses Pillow to rasterize text with the FHWASeriesEF.otf font onto colored sign
backgrounds. The signage matches the Glen Cove / Nassau-County stretch of the
Long Island Expressway (I-495) from the reference photos: overhead green guide
gantries with exit tabs, an I-495 interstate shield, exit-only arrows, and
mixed-case destination legends (Glen Cove Rd, Northern Blvd, S Oyster Bay Rd …).

Output PNGs go into textures/ for auto-loading by TextureManager::load_directory().
Texture aspect ratios are chosen to match the panel geometry in
RoadScene::generate_sign_posts so legends are not stretched:
  overhead panels 20 ft x 6 ft  -> 1200 x 360 (3.33:1)
  roadside guide  8 ft x 5 ft   ->  512 x 320 (1.6:1)
  speed limit     3 ft x 4 ft   ->  300 x 400 (0.75:1)
  mile marker     2 ft x 3 ft   ->  200 x 300 (0.667:1)
  service (blue)  6 ft x 4 ft   ->  480 x 320 (1.5:1)

Usage:
    python3 tools/sign_generator.py
"""

import os
from PIL import Image, ImageDraw, ImageFont

# ── Paths ─────────────────────────────────────────────────────────────
SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
FONT_PATH   = os.path.join(PROJECT_DIR, "assets", "fonts", "FHWASeriesEF.otf")
OUTPUT_DIR  = os.path.join(PROJECT_DIR, "textures")

# ── FHWA Standard Colors ─────────────────────────────────────────────
GREEN_BG      = (0, 105, 62)      # FHWA guide-sign green
BLUE_BG       = (0, 67, 123)      # FHWA service-sign blue
WHITE         = (255, 255, 255)
YELLOW        = (255, 210, 0)      # FHWA advisory / EXIT ONLY yellow
BLACK         = (0, 0, 0)
SHIELD_RED    = (183, 34, 40)      # interstate shield top banner
SHIELD_BLUE   = (10, 49, 97)       # interstate shield body


def load_font(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_PATH, size)


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_w: int, start: int,
             min_size: int = 12) -> ImageFont.FreeTypeFont:
    """Return the largest font (<= start px) whose `text` fits within max_w."""
    size = start
    while size > min_size:
        font = load_font(size)
        if draw.textlength(text, font=font) <= max_w:
            return font
        size -= 2
    return load_font(min_size)


# ── Drawing helpers (Pillow anchors do the centering) ────────────────

def new_sign(w: int, h: int, bg: tuple) -> tuple:
    img  = Image.new("RGBA", (w, h), bg + (255,))
    return img, ImageDraw.Draw(img)


def border(draw: ImageDraw.ImageDraw, box, radius: int, color=WHITE, width: int = 5):
    draw.rounded_rectangle(box, radius=radius, outline=color, width=width)


def text_c(draw, cx, cy, s, font, fill=WHITE):
    draw.text((cx, cy), s, font=font, fill=fill, anchor="mm")


def text_l(draw, x, cy, s, font, fill=WHITE):
    draw.text((x, cy), s, font=font, fill=fill, anchor="lm")


def text_r(draw, x, cy, s, font, fill=WHITE):
    draw.text((x, cy), s, font=font, fill=fill, anchor="rm")


def arrow(draw, cx, cy, size, direction="down", fill=WHITE):
    """Chunky FHWA-style arrow centered at (cx,cy). direction: down|up|up_right."""
    s = size
    shaft_w = s * 0.28
    if direction in ("down", "up"):
        head_h = s * 0.5
        # shaft
        if direction == "down":
            draw.rectangle([cx - shaft_w / 2, cy - s / 2, cx + shaft_w / 2, cy + s / 2 - head_h], fill=fill)
            draw.polygon([(cx - s * 0.42, cy + s / 2 - head_h),
                          (cx + s * 0.42, cy + s / 2 - head_h),
                          (cx, cy + s / 2)], fill=fill)
        else:  # up
            draw.rectangle([cx - shaft_w / 2, cy - s / 2 + head_h, cx + shaft_w / 2, cy + s / 2], fill=fill)
            draw.polygon([(cx - s * 0.42, cy - s / 2 + head_h),
                          (cx + s * 0.42, cy - s / 2 + head_h),
                          (cx, cy - s / 2)], fill=fill)
    else:  # up_right diagonal
        import math
        ang = math.radians(-45)
        dx, dy = math.cos(ang), math.sin(ang)
        px, py = -dy, dx  # perpendicular
        tipx, tipy = cx + dx * s / 2, cy + dy * s / 2
        basex, basey = cx - dx * s / 2, cy - dy * s / 2
        hw = shaft_w / 2
        draw.line([(basex, basey), (tipx - dx * s * 0.4, tipy - dy * s * 0.4)], fill=fill, width=int(shaft_w))
        draw.polygon([(tipx, tipy),
                      (tipx - dx * s * 0.5 + px * s * 0.32, tipy - dy * s * 0.5 + py * s * 0.32),
                      (tipx - dx * s * 0.5 - px * s * 0.32, tipy - dy * s * 0.5 - py * s * 0.32)], fill=fill)


def interstate_shield(draw, box, number: str):
    """Draw a simplified 3-digit Interstate shield inside box=[x0,y0,x1,y1]."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    r = int(w * 0.12)
    # white silhouette / outline
    draw.rounded_rectangle(box, radius=r, fill=WHITE)
    m = max(3, int(w * 0.05))
    ix0, iy0, ix1, iy1 = x0 + m, y0 + m, x1 - m, y1 - m
    band_h = (iy1 - iy0) * 0.30
    # red top banner "INTERSTATE"
    draw.rounded_rectangle([ix0, iy0, ix1, iy0 + band_h], radius=int(r * 0.6), fill=SHIELD_RED)
    banner_font = fit_font(draw, "INTERSTATE", (ix1 - ix0) * 0.92, int(band_h * 0.7))
    text_c(draw, (ix0 + ix1) / 2, iy0 + band_h / 2, "INTERSTATE", banner_font, WHITE)
    # blue body with route number
    draw.rounded_rectangle([ix0, iy0 + band_h + 2, ix1, iy1], radius=int(r * 0.6), fill=SHIELD_BLUE)
    num_font = fit_font(draw, number, (ix1 - ix0) * 0.9, int((iy1 - iy0 - band_h) * 0.85))
    text_c(draw, (ix0 + ix1) / 2, (iy0 + band_h + iy1) / 2 + 4, number, num_font, WHITE)


def save(img, index: int, label: str):
    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — {label}")


# ── Sign definitions ─────────────────────────────────────────────────

def gen_overhead_mainline(index: int):
    """sign_00 — mainline reassurance gantry: I-495 shield + EAST + Long Island Expwy."""
    w, h = 1200, 360
    img, draw = new_sign(w, h, GREEN_BG)
    border(draw, [12, 12, w - 13, h - 13], radius=26, width=6)

    # I-495 shield on the left
    sh_w, sh_h = 210, 250
    interstate_shield(draw, [70, (h - sh_h) // 2, 70 + sh_w, (h - sh_h) // 2 + sh_h], "495")

    # EAST + subtitle to the right of the shield
    text_l(draw, 330, 140, "EAST", load_font(120))
    text_l(draw, 330, 250, "Long Island Expwy", fit_font(draw, "Long Island Expwy", 720, 70))

    # pull-through up arrow, far right
    arrow(draw, w - 110, h / 2, 150, "up")
    save(img, index, "overhead mainline: I-495 EAST / Long Island Expwy")


def gen_overhead_exit(index: int, exit_num: str, dest1: str, dest2: str):
    """sign_02 — overhead exit gantry with exit tab, EXIT ONLY plaque, down arrow."""
    w, h = 1200, 360
    img, draw = new_sign(w, h, GREEN_BG)

    tab_h = 96
    # exit tab, top-right, straddling the panel top
    tab_w = 300
    tx1 = w - 24
    tx0 = tx1 - tab_w
    draw.rounded_rectangle([tx0, 6, tx1, tab_h], radius=16, fill=GREEN_BG + (255,), outline=WHITE, width=5)
    text_c(draw, (tx0 + tx1) / 2, (6 + tab_h) / 2, f"EXIT {exit_num}", load_font(60))

    # main panel below the tab
    border(draw, [16, tab_h - 8, w - 17, h - 16], radius=24, width=6)

    # destinations, left-justified
    text_l(draw, 60, tab_h + 60, dest1, fit_font(draw, dest1, 700, 86))
    text_l(draw, 60, tab_h + 165, dest2, fit_font(draw, dest2, 700, 86))

    # down arrow over the exit lane (right side)
    arrow(draw, w - 170, tab_h + 95, 150, "down")

    # EXIT ONLY yellow plaque along the bottom-right
    ey0 = h - 66
    draw.rounded_rectangle([w - 330, ey0, w - 24, h - 20], radius=8, fill=YELLOW + (255,))
    text_c(draw, w - 177, (ey0 + h - 20) / 2, "EXIT ONLY", load_font(40), BLACK)
    save(img, index, f"overhead exit: EXIT {exit_num} / {dest1} / {dest2} (EXIT ONLY)")


def gen_overhead_pullthrough(index: int, dest1: str, dest2: str):
    """sign_07 — two-destination pull-through gantry, divider + up arrows."""
    w, h = 1200, 360
    img, draw = new_sign(w, h, GREEN_BG)
    border(draw, [12, 12, w - 13, h - 13], radius=26, width=6)

    # horizontal divider between the two legends
    draw.line([(40, h // 2), (w - 40, h // 2)], fill=WHITE, width=5)

    # top legend
    text_l(draw, 55, h * 0.28, dest1, fit_font(draw, dest1, 820, 92))
    arrow(draw, w - 130, h * 0.28, 120, "up")
    # bottom legend
    text_l(draw, 55, h * 0.72, dest2, fit_font(draw, dest2, 820, 92))
    arrow(draw, w - 130, h * 0.72, 120, "up")
    save(img, index, f"overhead pull-through: {dest1} / {dest2}")


def gen_exit_guide(index: int, exit_num: str, street: str, distance: str):
    """sign_01 — roadside advance guide sign: street / EXIT / distance."""
    w, h = 512, 320
    img, draw = new_sign(w, h, GREEN_BG)
    border(draw, [10, 10, w - 11, h - 11], radius=18, width=5)

    text_c(draw, w / 2, 90, street, fit_font(draw, street, w - 60, 66))
    text_c(draw, w / 2, 175, f"EXIT {exit_num}", load_font(56))
    text_c(draw, w / 2, 255, distance, load_font(48))
    save(img, index, f"roadside guide: {street} / EXIT {exit_num} / {distance}")


def gen_service_sign(index: int):
    """sign_03 — blue service sign: GAS / FOOD / LODGING."""
    w, h = 480, 320
    img, draw = new_sign(w, h, BLUE_BG)
    border(draw, [10, 10, w - 11, h - 11], radius=18, width=5)

    text_c(draw, w / 2, 55,  "SERVICES", load_font(52))
    text_c(draw, w / 2, 135, "GAS", load_font(46))
    text_c(draw, w / 2, 195, "FOOD", load_font(46))
    text_c(draw, w / 2, 255, "LODGING", load_font(46))
    save(img, index, "service: GAS / FOOD / LODGING")


def gen_speed_limit(index: int, speed: int):
    """sign_04 — white regulatory speed-limit sign, black legend."""
    w, h = 300, 400
    img, draw = new_sign(w, h, WHITE)
    draw.rounded_rectangle([12, 12, w - 13, h - 13], radius=14, outline=BLACK, width=6)

    text_c(draw, w / 2, 70,  "SPEED", load_font(46), BLACK)
    text_c(draw, w / 2, 130, "LIMIT", load_font(46), BLACK)
    text_c(draw, w / 2, 270, str(speed), load_font(150), BLACK)
    save(img, index, f"speed limit: {speed}")


def gen_mile_marker(index: int, mile: int):
    """sign_05 — small green mile marker."""
    w, h = 200, 300
    img, draw = new_sign(w, h, GREEN_BG)
    border(draw, [8, 8, w - 9, h - 9], radius=12, width=4)

    text_c(draw, w / 2, 70,  "MILE", load_font(38))
    text_c(draw, w / 2, 190, str(mile), load_font(96))
    save(img, index, f"mile marker: {mile}")


def gen_pavement_text(index: int, text: str):
    """sign_06 — white glyphs on transparent bg for the HOV pavement marking."""
    w, h = 256, 512
    img  = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    font = load_font(72)
    chars = list(text)
    total_h = len(chars) * 85
    start_y = (h - total_h) // 2
    for i, ch in enumerate(chars):
        text_c(draw, w / 2, start_y + i * 85 + 42, ch, font, WHITE)
    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — pavement text: {text}")


# ── Main ──────────────────────────────────────────────────────────────

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print("Generating Glen Cove-area LIE (I-495) sign textures...")

    gen_overhead_mainline(0)                                     # I-495 EAST / Long Island Expwy
    gen_exit_guide(1, "39", "Glen Cove Rd", "1 MILE")            # roadside advance guide
    gen_overhead_exit(2, "39", "Glen Cove Rd", "Northern Blvd")  # exit gantry (EXIT ONLY)
    gen_service_sign(3)                                          # blue services
    gen_speed_limit(4, 55)                                       # regulatory
    gen_mile_marker(5, 39)                                       # mile marker
    gen_pavement_text(6, "I-495")                                # HOV pavement text
    gen_overhead_pullthrough(7, "S Oyster Bay Rd", "Syosset  Bethpage")  # NEW — fills MAT_SIGN_7

    print("\nDone — 8 sign textures written to textures/")


if __name__ == "__main__":
    main()
