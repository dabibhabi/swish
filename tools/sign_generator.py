#!/usr/bin/env python3
"""
Generate FHWA-compliant highway sign textures for the Swish renderer.

Uses Pillow to rasterize text with the FHWASeriesEF.otf font onto
colored sign backgrounds. Output PNGs go into textures/ for auto-loading
by TextureManager::load_directory().

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
GREEN_BG    = (0, 105, 62)       # FHWA guide sign green
BLUE_BG     = (0, 67, 123)       # FHWA service sign blue
WHITE       = (255, 255, 255)
YELLOW      = (255, 210, 0)      # FHWA advisory yellow
BLACK       = (0, 0, 0)


def load_font(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_PATH, size)


def make_sign(width: int, height: int, bg_color: tuple, border_width: int = 6) -> tuple:
    """Create a sign image with colored background and white border."""
    img  = Image.new("RGBA", (width, height), bg_color + (255,))
    draw = ImageDraw.Draw(img)

    # White border inset
    bw = border_width
    draw.rectangle([bw, bw, width - bw - 1, height - bw - 1], outline=WHITE, width=3)

    return img, draw


def center_text(draw: ImageDraw.Draw, y: int, text: str, font: ImageFont.FreeTypeFont,
                fill: tuple, img_width: int):
    """Draw centered text at vertical position y."""
    bbox = font.getbbox(text)
    tw = bbox[2] - bbox[0]
    x = (img_width - tw) // 2
    draw.text((x, y), text, font=font, fill=fill)


# ── Sign Definitions ─────────────────────────────────────────────────

def gen_overhead_i495(index: int):
    """Overhead gantry: I-495 EAST / Long Island Expwy"""
    w, h = 1024, 256
    img, draw = make_sign(w, h, GREEN_BG)

    title_font = load_font(72)
    sub_font   = load_font(48)

    # Interstate shield text (simplified — just text, no shield graphic)
    center_text(draw, 30, "I-495  EAST", title_font, WHITE, w)
    center_text(draw, 130, "Long Island Expwy", sub_font, WHITE, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — overhead: I-495 EAST / Long Island Expwy")


def gen_exit_guide(index: int, exit_num: str, street: str, distance: str):
    """Roadside guide sign: EXIT XX / Street Name / Distance"""
    w, h = 512, 256
    img, draw = make_sign(w, h, GREEN_BG)

    exit_font    = load_font(40)
    street_font  = load_font(52)
    dist_font    = load_font(36)

    center_text(draw, 20,  f"EXIT {exit_num}", exit_font, WHITE, w)
    center_text(draw, 80,  street, street_font, WHITE, w)
    center_text(draw, 160, distance, dist_font, WHITE, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — guide: EXIT {exit_num} / {street} / {distance}")


def gen_overhead_exit(index: int, exit_num: str, street: str, distance: str):
    """Overhead gantry exit sign with distance."""
    w, h = 1024, 256
    img, draw = make_sign(w, h, GREEN_BG)

    exit_font   = load_font(48)
    street_font = load_font(64)
    dist_font   = load_font(40)

    center_text(draw, 15,  f"EXIT {exit_num}", exit_font, WHITE, w)
    center_text(draw, 80,  street, street_font, WHITE, w)
    center_text(draw, 170, distance, dist_font, YELLOW, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — overhead exit: {exit_num} / {street} / {distance}")


def gen_service_sign(index: int):
    """Blue service sign: GAS / FOOD / LODGING"""
    w, h = 512, 256
    img, draw = make_sign(w, h, BLUE_BG)

    title_font = load_font(44)
    icon_font  = load_font(36)

    center_text(draw, 20,  "SERVICES", title_font, WHITE, w)
    center_text(draw, 90,  "GAS", icon_font, WHITE, w)
    center_text(draw, 140, "FOOD", icon_font, WHITE, w)
    center_text(draw, 190, "LODGING", icon_font, WHITE, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — service: GAS / FOOD / LODGING")


def gen_speed_limit(index: int, speed: int):
    """Speed limit sign: white background, black text."""
    w, h = 256, 384
    img, draw = make_sign(w, h, WHITE, border_width=8)

    # Override border to black
    draw.rectangle([8, 8, w - 9, h - 9], outline=BLACK, width=4)

    title_font = load_font(28)
    speed_font = load_font(120)

    center_text(draw, 20,  "SPEED", title_font, BLACK, w)
    center_text(draw, 55,  "LIMIT", title_font, BLACK, w)
    center_text(draw, 110, str(speed), speed_font, BLACK, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — speed limit: {speed}")


def gen_mile_marker(index: int, mile: int):
    """Small green mile marker."""
    w, h = 128, 192
    img, draw = make_sign(w, h, GREEN_BG, border_width=4)

    label_font = load_font(24)
    num_font   = load_font(48)

    center_text(draw, 20,  "MILE", label_font, WHITE, w)
    center_text(draw, 70,  str(mile), num_font, WHITE, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — mile marker: {mile}")


def gen_pavement_text(index: int, text: str):
    """White text on transparent background for pavement markings."""
    w, h = 256, 512
    img  = Image.new("RGBA", (w, h), (0, 0, 0, 0))  # transparent background
    draw = ImageDraw.Draw(img)

    font = load_font(72)
    # Draw each character vertically stacked (pavement style)
    chars = list(text)
    total_h = len(chars) * 85
    start_y = (h - total_h) // 2

    for i, ch in enumerate(chars):
        center_text(draw, start_y + i * 85, ch, font, WHITE, w)

    img.save(os.path.join(OUTPUT_DIR, f"sign_{index:02d}.png"))
    print(f"  sign_{index:02d}.png  — pavement text: {text}")


# ── Main ──────────────────────────────────────────────────────────────

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("Generating LIE highway sign textures...")

    gen_overhead_i495(0)
    gen_exit_guide(1, "41B", "Jericho Tpke", "1 MILE")
    gen_overhead_exit(2, "41B", "Jericho Tpke", "1/2 MILE")
    gen_service_sign(3)
    gen_speed_limit(4, 55)
    gen_mile_marker(5, 36)
    gen_pavement_text(6, "I-495")

    print(f"\nDone — {7} sign textures written to {OUTPUT_DIR}/")


if __name__ == "__main__":
    main()
