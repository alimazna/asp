#!/usr/bin/env python3
"""AYZR brand-asset generator — deterministic, offline, Pillow-only.

This is a *programmatic* raster generator, not a generative model. It draws the
brand geometry (a chevron "A" with a gold crossbar and arc over a navy field)
from explicit coordinates, so every asset is reproducible byte-for-byte from
this file. It exists because the environment has no generative image model and
no ImageMagick; see docs/ayzr/VISUAL_DESIGN.md for the full capability
disclosure.

Usage:
    python3 tools/ayzr_assets/generate_assets.py [--out DIR]

Assets emitted (PNG):
    logo-mark.png       256x256  app/logo mark, transparent background
    favicon.png          64x64   browser favicon, transparent background
    app-icon.png        512x512  Android adaptive-icon foreground, transparent
    og-card.png        1200x630  social/OG card with the wordmark
    wordmark-ar.png    1200x260  wordmark lockup, transparent background

The palette matches assets/design-system.css exactly.
"""
from __future__ import annotations

import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: python3 -m pip install Pillow")

# Palette — identical to design-system.css.
NAVY_ABYSS = (5, 10, 20)
NAVY_SURFACE = (15, 33, 53)
GOLD = (201, 162, 39)
GOLD_SOFT = (227, 197, 102)
TEXT_HI = (237, 243, 250)
TEXT_MID = (143, 165, 192)
TEXT_LOW = (94, 118, 153)
LINE = (27, 51, 80)


def _font(size: int):
    """Best available font, else Pillow's bitmap default (never fails)."""
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    ):
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def _linear_gradient(size, top, bottom):
    """Vertical linear gradient, one row at a time (no numpy dependency)."""
    w, h = size
    img = Image.new("RGB", size)
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        row = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = row
    return img


def draw_mark(size, bg):
    """The AYZR mark: a chevron 'A' with a gold crossbar and arc."""
    ss = 4  # supersample for smooth edges
    S = size * ss
    if bg is None:
        img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    else:
        img = _linear_gradient((S, S), NAVY_SURFACE, NAVY_ABYSS).convert("RGBA")
    d = ImageDraw.Draw(img)

    # The 'A' — two strokes meeting at the apex, drawn as a filled polygon.
    cx, top = S / 2, S * 0.22
    half_w, base_y, stroke = S * 0.30, S * 0.80, S * 0.075
    d.polygon(
        [(cx - stroke * 0.55, top), (cx + stroke * 0.55, top),
         (cx + half_w + stroke * 0.5, base_y), (cx + half_w - stroke * 0.5, base_y),
         (cx + stroke * 0.75, base_y - (base_y - top) * 0.28),
         (cx - stroke * 0.75, base_y - (base_y - top) * 0.28),
         (cx - half_w + stroke * 0.5, base_y), (cx - half_w - stroke * 0.5, base_y)],
        fill=TEXT_HI,
    )
    bar_y = S * 0.60
    d.rounded_rectangle([cx - half_w * 0.62, bar_y, cx + half_w * 0.62, bar_y + stroke * 0.9],
                        radius=stroke * 0.45, fill=GOLD)
    r = S * 0.34
    d.arc([cx - r, S * 0.42 - r, cx + r, S * 0.42 + r],
          start=200, end=340, fill=GOLD_SOFT, width=max(2, int(S * 0.022)))
    return img.resize((size, size), Image.LANCZOS)


def draw_og_card(w=1200, h=630):
    img = _linear_gradient((w, h), NAVY_SURFACE, NAVY_ABYSS).convert("RGBA")
    d = ImageDraw.Draw(img)
    for x in range(0, w, 60):
        d.line([(x, 0), (x, h)], fill=LINE, width=1)
    for y in range(0, h, 60):
        d.line([(0, y), (w, y)], fill=LINE, width=1)

    mark = draw_mark(150, None)
    img.paste(mark, (w - 150 - 80, 90), mark)
    d = ImageDraw.Draw(img)

    f_big, f_small, f_tag = _font(64), _font(28), _font(24)
    d.text((w - 80, 280), "AYZR", font=f_big, fill=TEXT_HI, anchor="ra")
    d.text((w - 80, 360), "منصة ذكاء سوق الذهب", font=f_small, fill=GOLD_SOFT, anchor="ra")
    d.text((w - 80, 410), "XAUUSD · تحليل آلي · وضع الظل فقط",
           font=f_tag, fill=TEXT_MID, anchor="ra")
    d.text((80, 300), "SHADOW ONLY", font=f_tag, fill=TEXT_LOW)
    d.text((80, 340), "لا تنفيذ لعمليات تداول", font=f_small, fill=(198, 212, 229))
    return img.convert("RGB")


def draw_wordmark(w=1200, h=260):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    mark = draw_mark(180, None)
    img.paste(mark, (0, (h - 180) // 2), mark)
    d = ImageDraw.Draw(img)
    d.text((210, 60), "AYZR", font=_font(84), fill=TEXT_HI)
    d.text((214, 160), "ذكاء سوق الذهب", font=_font(34), fill=GOLD_SOFT)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..",
                    "ayzr_observatory", "assets"))
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    jobs = [
        ("logo-mark.png", lambda: draw_mark(256, None)),
        ("favicon.png", lambda: draw_mark(64, None)),
        ("app-icon.png", lambda: draw_mark(512, None)),
        ("og-card.png", draw_og_card),
        ("wordmark-ar.png", draw_wordmark),
    ]
    for name, fn in jobs:
        p = os.path.join(out, name)
        fn().save(p)
        print("wrote", p, os.path.getsize(p), "bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
